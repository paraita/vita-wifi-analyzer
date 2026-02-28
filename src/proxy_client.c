#include "proxy_client.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  PROXY_THREAD_PRIORITY = 0x10000120,
  PROXY_THREAD_STACK_SIZE = 0x10000,
  PROXY_RX_BUF_SIZE = 1024
};

static void proxy_sleep_interruptible(const ProxyClient *client, uint32_t ms) {
  uint32_t left = ms;
  while (!client->stop_requested && left > 0U) {
    const uint32_t step = (left > 20U) ? 20U : left;
    sceKernelDelayThread(step * 1000U);
    left -= step;
  }
}

static int connect_with_timeout(const ProxyClientConfig *cfg, int *err_out) {
  int sock = -1;
  int ep = -1;

  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(cfg->target_port);
  if (sceNetInetPton(SCE_NET_AF_INET, cfg->target_ip, &addr.sin_addr) != 1) {
    *err_out = SCE_NET_EINVAL;
    return -1;
  }

  sock = sceNetSocket("proxy_client", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
  if (sock < 0) {
    *err_out = -*sceNetErrnoLoc();
    return -1;
  }

  int nbio = 1;
  if (sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio)) < 0) {
    *err_out = -*sceNetErrnoLoc();
    sceNetSocketClose(sock);
    return -1;
  }

  int rc = sceNetConnect(sock, (const SceNetSockaddr *)&addr, sizeof(addr));
  if (rc < 0) {
    const int err = *sceNetErrnoLoc();
    if (err != SCE_NET_EINPROGRESS) {
      *err_out = -err;
      sceNetSocketClose(sock);
      return -1;
    }
  }

  ep = sceNetEpollCreate("proxy_ep", 0);
  if (ep < 0) {
    *err_out = -*sceNetErrnoLoc();
    sceNetSocketClose(sock);
    return -1;
  }

  SceNetEpollEvent add_ev;
  memset(&add_ev, 0, sizeof(add_ev));
  add_ev.events = SCE_NET_EPOLLOUT | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
  add_ev.data.fd = sock;
  if (sceNetEpollControl(ep, SCE_NET_EPOLL_CTL_ADD, sock, &add_ev) < 0) {
    *err_out = -*sceNetErrnoLoc();
    sceNetEpollDestroy(ep);
    sceNetSocketClose(sock);
    return -1;
  }

  SceNetEpollEvent event;
  memset(&event, 0, sizeof(event));
  rc = sceNetEpollWait(ep, &event, 1, (int)cfg->socket_timeout_ms);
  if (rc <= 0) {
    *err_out = -SCE_NET_ETIMEDOUT;
    sceNetEpollDestroy(ep);
    sceNetSocketClose(sock);
    return -1;
  }

  int so_error = 0;
  unsigned int so_len = sizeof(so_error);
  if (sceNetGetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &so_error, &so_len) < 0) {
    *err_out = -*sceNetErrnoLoc();
    sceNetEpollDestroy(ep);
    sceNetSocketClose(sock);
    return -1;
  }

  sceNetEpollDestroy(ep);
  if (so_error != 0) {
    *err_out = -so_error;
    sceNetSocketClose(sock);
    return -1;
  }

  *err_out = 0;
  return sock;
}

static int parse_ports(const char *ports_value, uint16_t *ports_out, uint8_t *count_out) {
  char work[96];
  snprintf(work, sizeof(work), "%s", ports_value);

  uint8_t count = 0;
  char *token = strtok(work, ",");
  while (token != NULL && count < LAN_SCANNER_PORT_COUNT) {
    unsigned int port = 0;
    if (sscanf(token, "%u", &port) == 1 && port <= 65535U) {
      ports_out[count++] = (uint16_t)port;
    }
    token = strtok(NULL, ",");
  }

  *count_out = count;
  return 0;
}

static void parse_host_line(ProxyClient *client, const char *line) {
  if (client->host_count >= LAN_SCANNER_MAX_HOSTS) {
    return;
  }

  LanHostResult host;
  memset(&host, 0, sizeof(host));

  char ip[16] = "";
  int alive = 0;
  char ports[96] = "";

  const int matched = sscanf(line, "HOST ip=%15s alive=%d ports=%95s", ip, &alive, ports);
  if (matched < 2) {
    return;
  }

  snprintf(host.ip, sizeof(host.ip), "%s", ip);
  host.alive = (alive != 0) ? 1U : 0U;
  if (matched >= 3 && strcmp(ports, "-") != 0) {
    parse_ports(ports, host.open_ports, &host.open_port_count);
  }

  client->hosts[client->host_count++] = host;
}

static void parse_proxy_line(ProxyClient *client, const char *line) {
  client->lines_received++;

  if (strncmp(line, "BEGIN", 5) == 0) {
    client->running = 1;
    client->host_count = 0;

    const char *subnet = strstr(line, "subnet=");
    if (subnet != NULL) {
      subnet += 7;
      snprintf(client->subnet_label, sizeof(client->subnet_label), "%s", subnet);
    } else {
      snprintf(client->subnet_label, sizeof(client->subnet_label), "remote");
    }
    return;
  }

  if (strncmp(line, "HOST ", 5) == 0) {
    parse_host_line(client, line);
    return;
  }

  if (strncmp(line, "ERROR", 5) == 0) {
    int code = 0;
    if (sscanf(line, "ERROR code=%d", &code) == 1) {
      client->last_error = code;
    }
    return;
  }

  if (strncmp(line, "END", 3) == 0) {
    client->running = 0;
    client->scan_round++;
  }
}

static int proxy_thread_main(SceSize args, void *argp) {
  (void)args;
  ProxyClient *client = (ProxyClient *)argp;

  char rx_buf[PROXY_RX_BUF_SIZE];
  int rx_len = 0;

  while (!client->stop_requested) {
    int conn_err = 0;
    int sock = connect_with_timeout(&client->config, &conn_err);
    if (sock < 0) {
      client->connected = 0;
      client->last_error = conn_err;
      proxy_sleep_interruptible(client, client->config.reconnect_ms);
      continue;
    }

    client->connected = 1;
    client->last_error = 0;
    client->request_scan = 1;

    int ep = sceNetEpollCreate("proxy_rx_ep", 0);
    if (ep < 0) {
      client->last_error = -*sceNetErrnoLoc();
      client->connected = 0;
      sceNetSocketClose(sock);
      proxy_sleep_interruptible(client, client->config.reconnect_ms);
      continue;
    }

    SceNetEpollEvent add_ev;
    memset(&add_ev, 0, sizeof(add_ev));
    add_ev.events = SCE_NET_EPOLLIN | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
    add_ev.data.fd = sock;
    if (sceNetEpollControl(ep, SCE_NET_EPOLL_CTL_ADD, sock, &add_ev) < 0) {
      client->last_error = -*sceNetErrnoLoc();
      client->connected = 0;
      sceNetEpollDestroy(ep);
      sceNetSocketClose(sock);
      proxy_sleep_interruptible(client, client->config.reconnect_ms);
      continue;
    }

    while (!client->stop_requested) {
      if (client->request_scan) {
        const char *request_line = "SCAN_REQUEST quick\n";
        if (sceNetSend(sock, request_line, (int)strlen(request_line), 0) < 0) {
          client->last_error = -*sceNetErrnoLoc();
          break;
        }
        client->request_scan = 0;
        client->running = 1;
        client->host_count = 0;
      }

      SceNetEpollEvent event;
      memset(&event, 0, sizeof(event));
      int rc = sceNetEpollWait(ep, &event, 1, 250);
      if (rc < 0) {
        client->last_error = -*sceNetErrnoLoc();
        break;
      }
      if (rc == 0) {
        continue;
      }
      if (event.events & (SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP)) {
        client->last_error = -SCE_NET_ECONNRESET;
        break;
      }

      const int to_read = (int)sizeof(rx_buf) - rx_len - 1;
      if (to_read <= 0) {
        rx_len = 0;
      }
      int r = sceNetRecv(sock, rx_buf + rx_len, (int)sizeof(rx_buf) - rx_len - 1, 0);
      if (r <= 0) {
        client->last_error = (r == 0) ? -SCE_NET_ECONNRESET : -*sceNetErrnoLoc();
        break;
      }

      rx_len += r;
      rx_buf[rx_len] = '\0';

      char *line_start = rx_buf;
      char *newline = strchr(line_start, '\n');
      while (newline != NULL) {
        *newline = '\0';
        if (*line_start != '\0') {
          parse_proxy_line(client, line_start);
        }
        line_start = newline + 1;
        newline = strchr(line_start, '\n');
      }

      const int remaining = (int)strlen(line_start);
      if (remaining > 0 && line_start != rx_buf) {
        memmove(rx_buf, line_start, (size_t)remaining);
      }
      rx_len = remaining;
      rx_buf[rx_len] = '\0';
    }

    client->connected = 0;
    client->running = 0;
    sceNetEpollDestroy(ep);
    sceNetSocketClose(sock);
    proxy_sleep_interruptible(client, client->config.reconnect_ms);
  }

  client->connected = 0;
  client->running = 0;
  return sceKernelExitDeleteThread(0);
}

void proxy_client_default_config(ProxyClientConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  snprintf(cfg->target_ip, sizeof(cfg->target_ip), "192.168.1.2");
  cfg->target_port = 9000;
  cfg->reconnect_ms = 1500;
  cfg->socket_timeout_ms = 800;
}

void proxy_client_init(ProxyClient *client) {
  memset(client, 0, sizeof(*client));
  client->thread_id = -1;
  proxy_client_default_config(&client->config);
  snprintf(client->subnet_label, sizeof(client->subnet_label), "remote");
}

int proxy_client_start(ProxyClient *client, const ProxyClientConfig *cfg) {
  if (client->thread_id >= 0) {
    return 0;
  }

  if (cfg != NULL) {
    client->config = *cfg;
  }

  client->stop_requested = 0;
  client->request_scan = 1;

  const int priorities[] = {PROXY_THREAD_PRIORITY, 0x10000100, 0x10000140};
  client->thread_id = -1;
  for (size_t i = 0; i < sizeof(priorities) / sizeof(priorities[0]); i++) {
    client->thread_id = sceKernelCreateThread("proxy_client", proxy_thread_main,
                                               priorities[i], PROXY_THREAD_STACK_SIZE,
                                               0, 0, NULL);
    if (client->thread_id >= 0) {
      break;
    }
  }
  if (client->thread_id < 0) {
    return client->thread_id;
  }

  const int rc = sceKernelStartThread(client->thread_id, sizeof(client), client);
  if (rc < 0) {
    sceKernelDeleteThread(client->thread_id);
    client->thread_id = -1;
    return rc;
  }

  return 0;
}

void proxy_client_stop(ProxyClient *client) {
  if (client->thread_id < 0) {
    return;
  }

  client->stop_requested = 1;
  sceKernelWaitThreadEnd(client->thread_id, NULL, NULL);
  sceKernelDeleteThread(client->thread_id);
  client->thread_id = -1;
}

void proxy_client_request_scan(ProxyClient *client) {
  client->request_scan = 1;
}

void proxy_client_get_metrics(const ProxyClient *client, ProxyClientMetrics *out) {
  memset(out, 0, sizeof(*out));
  out->enabled = (client->thread_id >= 0);
  out->running = (client->running != 0);
  out->connected = (client->connected != 0);
  out->target_port = client->config.target_port;
  out->scan_round = client->scan_round;
  out->lines_received = client->lines_received;
  out->last_error = client->last_error;
  out->host_count = client->host_count;
  snprintf(out->target_ip, sizeof(out->target_ip), "%s", client->config.target_ip);
  snprintf(out->subnet_label, sizeof(out->subnet_label), "%s", client->subnet_label);

  if (out->host_count > LAN_SCANNER_MAX_HOSTS) {
    out->host_count = LAN_SCANNER_MAX_HOSTS;
  }
  for (uint32_t i = 0; i < out->host_count; i++) {
    out->hosts[i] = client->hosts[i];
  }
}
