#include "lan_scanner.h"

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  STEP_INTERVAL_US = 20000,
  UI_PROBE_BUDGET_MS = 14
};

typedef struct IcmpEchoPacket {
  SceNetIcmpHeader header;
  uint8_t payload[8];
} IcmpEchoPacket;

typedef enum ProbeResult {
  PROBE_NO_RESPONSE = 0,
  PROBE_HOST_RESPONDED = 1,
  PROBE_OPEN = 2
} ProbeResult;

typedef enum ScanPhase {
  SCAN_PHASE_RESOLVE = 0,
  SCAN_PHASE_ALIVE = 1,
  SCAN_PHASE_PORTS = 2
} ScanPhase;

static int resolve_local_subnet(char *prefix_out, size_t prefix_len, char *cidr_out, size_t cidr_len) {
  SceNetCtlInfo info;
  memset(&info, 0, sizeof(info));
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0) {
    return -1;
  }

  unsigned int a = 0;
  unsigned int b = 0;
  unsigned int c = 0;
  unsigned int d = 0;
  if (sscanf(info.ip_address, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
    return -2;
  }
  if (a > 255U || b > 255U || c > 255U || d > 255U || (a == 0U && b == 0U && c == 0U)) {
    return -3;
  }

  snprintf(prefix_out, prefix_len, "%u.%u.%u.", a, b, c);
  snprintf(cidr_out, cidr_len, "%u.%u.%u.0/24", a, b, c);
  return 0;
}

static ProbeResult probe_tcp_port(const char *ip, uint16_t port, uint32_t timeout_ms, int *err_out) {
  int sock = -1;
  ProbeResult result = PROBE_NO_RESPONSE;

  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(port);
  if (sceNetInetPton(SCE_NET_AF_INET, ip, &addr.sin_addr) != 1) {
    *err_out = SCE_NET_EINVAL;
    return PROBE_NO_RESPONSE;
  }

  sock = sceNetSocket("lan_scan", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
  if (sock < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  int nbio = 1;
  if (sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio)) < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  int rc = sceNetConnect(sock, (const SceNetSockaddr *)&addr, sizeof(addr));
  if (rc < 0) {
    const int err = *sceNetErrnoLoc();
    if (err != SCE_NET_EINPROGRESS) {
      if (err == SCE_NET_ECONNREFUSED) {
        *err_out = -err;
        result = PROBE_HOST_RESPONDED;
      } else {
        *err_out = -err;
      }
      goto cleanup;
    }
  }

  const uint64_t deadline_us = sceKernelGetProcessTimeWide() + ((uint64_t)timeout_ms * 1000ULL);
  for (;;) {
    int so_error = 0;
    unsigned int so_len = sizeof(so_error);
    if (sceNetGetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &so_error, &so_len) < 0) {
      *err_out = -*sceNetErrnoLoc();
      goto cleanup;
    }

    if (so_error == 0) {
      SceNetSockaddrIn peer;
      unsigned int peer_len = sizeof(peer);
      const int peer_rc = sceNetGetpeername(sock, (SceNetSockaddr *)&peer, &peer_len);
      if (peer_rc == 0) {
        *err_out = 0;
        result = PROBE_OPEN;
        break;
      }
      const int peer_err = *sceNetErrnoLoc();
      if (peer_err != SCE_NET_ENOTCONN && peer_err != SCE_NET_EINPROGRESS) {
        *err_out = -peer_err;
        break;
      }
    } else if (so_error == SCE_NET_ECONNREFUSED) {
      *err_out = -so_error;
      result = PROBE_HOST_RESPONDED;
      break;
    } else if (so_error != SCE_NET_EINPROGRESS && so_error != SCE_NET_EALREADY) {
      *err_out = -so_error;
      break;
    }

    if (sceKernelGetProcessTimeWide() >= deadline_us) {
      *err_out = -SCE_NET_ETIMEDOUT;
      break;
    }
    sceKernelDelayThread(1000);
  }

cleanup:
  if (sock >= 0) {
    sceNetSocketClose(sock);
  }
  return result;
}

static uint16_t icmp_checksum(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t sum = 0;
  while (len > 1U) {
    sum += ((uint32_t)bytes[0] << 8) | (uint32_t)bytes[1];
    bytes += 2;
    len -= 2;
  }
  if (len == 1U) {
    sum += ((uint32_t)bytes[0] << 8);
  }
  while ((sum >> 16) != 0U) {
    sum = (sum & 0xFFFFU) + (sum >> 16);
  }
  return (uint16_t)(~sum & 0xFFFFU);
}

static ProbeResult probe_icmp_echo(const char *ip, uint32_t timeout_ms, int *err_out) {
  int sock = -1;
  int ep = -1;
  ProbeResult result = PROBE_NO_RESPONSE;

  SceNetSockaddrIn dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_len = sizeof(dst);
  dst.sin_family = SCE_NET_AF_INET;
  if (sceNetInetPton(SCE_NET_AF_INET, ip, &dst.sin_addr) != 1) {
    *err_out = -SCE_NET_EINVAL;
    return PROBE_NO_RESPONSE;
  }

  sock = sceNetSocket("lan_icmp", SCE_NET_AF_INET, SCE_NET_SOCK_RAW, SCE_NET_IPPROTO_ICMP);
  if (sock < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  int nbio = 1;
  if (sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio)) < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  ep = sceNetEpollCreate("lan_icmp_ep", 0);
  if (ep < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  SceNetEpollEvent add_ev;
  memset(&add_ev, 0, sizeof(add_ev));
  add_ev.events = SCE_NET_EPOLLIN | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
  add_ev.data.fd = sock;
  if (sceNetEpollControl(ep, SCE_NET_EPOLL_CTL_ADD, sock, &add_ev) < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  const uint16_t req_id = 0x4256;
  for (uint16_t seq = 1; seq <= 3; seq++) {
    IcmpEchoPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.type = SCE_NET_ICMP_TYPE_ECHO_REQUEST;
    pkt.header.code = 0;
    pkt.header.un.echo.id = sceNetHtons(req_id);
    pkt.header.un.echo.sequence = sceNetHtons(seq);
    for (uint32_t i = 0; i < sizeof(pkt.payload); i++) {
      pkt.payload[i] = (uint8_t)(0xA0U + i + seq);
    }
    pkt.header.checksum = icmp_checksum(&pkt, sizeof(pkt));

    const int sent = sceNetSendto(sock, &pkt, sizeof(pkt), 0, (const SceNetSockaddr *)&dst, sizeof(dst));
    if (sent < 0) {
      *err_out = -*sceNetErrnoLoc();
      goto cleanup;
    }

    SceNetEpollEvent event;
    memset(&event, 0, sizeof(event));
    const int rc = sceNetEpollWait(ep, &event, 1, (int)timeout_ms);
    if (rc <= 0) {
      *err_out = -SCE_NET_ETIMEDOUT;
      continue;
    }

    if ((event.events & (SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP)) != 0U) {
      *err_out = -SCE_NET_ECONNRESET;
      continue;
    }

    uint8_t recv_buf[128];
    SceNetSockaddrIn src;
    unsigned int src_len = sizeof(src);
    const int recv_len = sceNetRecvfrom(sock, recv_buf, sizeof(recv_buf), 0, (SceNetSockaddr *)&src, &src_len);
    if (recv_len <= 0) {
      *err_out = (recv_len == 0) ? -SCE_NET_ECONNRESET : -*sceNetErrnoLoc();
      continue;
    }

    if (src.sin_addr.s_addr != dst.sin_addr.s_addr) {
      continue;
    }
    if (recv_len < (int)(sizeof(SceNetIpHeader) + sizeof(SceNetIcmpHeader))) {
      continue;
    }

    const SceNetIpHeader *ip_hdr = (const SceNetIpHeader *)recv_buf;
    const uint8_t ihl = (uint8_t)((ip_hdr->un.ver_hl & 0x0FU) * 4U);
    if (recv_len < (int)(ihl + sizeof(SceNetIcmpHeader))) {
      continue;
    }

    const SceNetIcmpHeader *icmp_hdr = (const SceNetIcmpHeader *)(recv_buf + ihl);
    if (icmp_hdr->type == SCE_NET_ICMP_TYPE_ECHO_REPLY &&
        sceNetNtohs(icmp_hdr->un.echo.id) == req_id) {
      *err_out = 0;
      result = PROBE_HOST_RESPONDED;
      goto cleanup;
    }
  }

cleanup:
  if (ep >= 0) {
    sceNetEpollDestroy(ep);
  }
  if (sock >= 0) {
    sceNetSocketClose(sock);
  }
  return result;
}

static void begin_new_round(LanScanner *scanner, uint64_t now_us) {
  scanner->hosts_scanned = 0;
  scanner->hosts_alive = 0;
  scanner->host_count = 0;
  scanner->icmp_hits = 0;
  scanner->tcp_hits = 0;
  scanner->mdns_hits = 0;
  scanner->ssdp_hits = 0;
  scanner->nbns_hits = 0;
  scanner->last_error = 0;
  scanner->next_host = 1;
  scanner->current_host = 0;
  scanner->preferred_host = 0;
  scanner->preferred_host_valid = 0;
  scanner->preferred_host_done = 0;
  scanner->phase = SCAN_PHASE_RESOLVE;
  scanner->alive_probe_index = 0;
  scanner->port_probe_index = 0;
  scanner->current_host_alive = 0;
  scanner->current_source_flags = 0;
  scanner->current_open_port_count = 0;
  scanner->next_step_us = now_us;
}

static int is_gateway_ip(const LanScanner *scanner, const char *ip) {
  if (!scanner->gateway_hint_valid) {
    return 0;
  }
  char gateway[16];
  snprintf(gateway, sizeof(gateway), "%u.%u.%u.%u",
           scanner->gateway_a, scanner->gateway_b, scanner->gateway_c, scanner->gateway_d);
  return strcmp(ip, gateway) == 0;
}

static LanHostResult *find_host(LanScanner *scanner, const char *ip) {
  for (uint32_t i = 0; i < scanner->host_count; i++) {
    if (strcmp(scanner->hosts[i].ip, ip) == 0) {
      return &scanner->hosts[i];
    }
  }
  return NULL;
}

static void mark_source_hit(LanScanner *scanner, uint32_t flag) {
  if (flag == DISCOVERY_SRC_MDNS) scanner->mdns_hits++;
  else if (flag == DISCOVERY_SRC_SSDP) scanner->ssdp_hits++;
  else if (flag == DISCOVERY_SRC_NBNS) scanner->nbns_hits++;
  else if (flag == DISCOVERY_SRC_ICMP) scanner->icmp_hits++;
  else if (flag == DISCOVERY_SRC_TCP) scanner->tcp_hits++;
}

static void merge_ports(LanHostResult *host, const uint16_t *ports, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    const uint16_t p = ports[i];
    int exists = 0;
    for (uint8_t j = 0; j < host->open_port_count; j++) {
      if (host->open_ports[j] == p) {
        exists = 1;
        break;
      }
    }
    if (!exists && host->open_port_count < LAN_SCANNER_PORT_COUNT) {
      host->open_ports[host->open_port_count++] = p;
    }
  }
}

static void upsert_host(LanScanner *scanner,
                        const char *ip,
                        uint32_t source_flags,
                        const char *hostname,
                        const char *service_hint,
                        const uint16_t *ports,
                        uint8_t port_count,
                        int last_error) {
  LanHostResult *host = find_host(scanner, ip);
  if (host == NULL) {
    if (scanner->host_count >= scanner->config.max_hosts || scanner->host_count >= LAN_SCANNER_MAX_HOSTS) {
      return;
    }
    host = &scanner->hosts[scanner->host_count++];
    memset(host, 0, sizeof(*host));
    snprintf(host->ip, sizeof(host->ip), "%s", ip);
    host->alive = 1;
  }

  const uint32_t old_flags = host->source_flags;
  host->source_flags |= source_flags;
  uint32_t delta = host->source_flags & ~old_flags;
  while (delta != 0U) {
    const uint32_t bit = delta & (~delta + 1U);
    mark_source_hit(scanner, bit);
    delta &= ~bit;
  }

  if (hostname != NULL && hostname[0] != '\0' && host->hostname[0] == '\0') {
    snprintf(host->hostname, sizeof(host->hostname), "%s", hostname);
  }
  if (service_hint != NULL && service_hint[0] != '\0' && host->service_hint[0] == '\0') {
    snprintf(host->service_hint, sizeof(host->service_hint), "%s", service_hint);
  }
  if (ports != NULL && port_count > 0U) {
    merge_ports(host, ports, port_count);
    for (uint8_t i = 0; i < port_count; i++) {
      const uint16_t p = ports[i];
      if ((p == 80 || p == 443 || p == 8080) && host->service_hint[0] == '\0') {
        snprintf(host->service_hint, sizeof(host->service_hint), "web-service");
        break;
      }
    }
  }
  if (last_error != 0) {
    host->last_error = last_error;
  }
  host->is_gateway = (uint8_t)is_gateway_ip(scanner, ip);
  host->alive = 1;
  scanner->hosts_alive = scanner->host_count;
}

static void finalize_host(LanScanner *scanner) {
  scanner->hosts_scanned++;

  if (scanner->current_host_alive) {
    upsert_host(scanner, scanner->current_ip, scanner->current_source_flags, NULL, NULL,
                scanner->current_open_ports, scanner->current_open_port_count, scanner->last_error);
  }

  scanner->current_host = 0;
  scanner->phase = SCAN_PHASE_ALIVE;
  scanner->alive_probe_index = 0;
  scanner->port_probe_index = 0;
  scanner->current_host_alive = 0;
  scanner->current_source_flags = 0;
  scanner->current_open_port_count = 0;
}

static int select_next_host(LanScanner *scanner) {
  if (!scanner->preferred_host_done && scanner->preferred_host_valid) {
    scanner->current_host = scanner->preferred_host;
    scanner->preferred_host_done = 1;
    return 1;
  }

  while (scanner->next_host <= 254U) {
    const uint32_t candidate = scanner->next_host++;
    if (scanner->preferred_host_valid && candidate == scanner->preferred_host) {
      continue;
    }
    scanner->current_host = candidate;
    return 1;
  }

  return 0;
}

void lan_scanner_default_config(LanScannerConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->interval_ms_per_host = 25;
  cfg->connect_timeout_ms = 350;
  cfg->ports[0] = 22;
  cfg->ports[1] = 23;
  cfg->ports[2] = 53;
  cfg->ports[3] = 80;
  cfg->ports[4] = 123;
  cfg->ports[5] = 139;
  cfg->ports[6] = 443;
  cfg->ports[7] = 445;
  cfg->max_hosts = LAN_SCANNER_MAX_HOSTS;
}

void lan_scanner_init(LanScanner *scanner) {
  memset(scanner, 0, sizeof(*scanner));
  lan_scanner_default_config(&scanner->config);
  discovery_init(&scanner->discovery);
  scanner->subnet_valid = 0;
  scanner->running = 0;
  scanner->phase = SCAN_PHASE_RESOLVE;
  scanner->icmp_supported = -1;
}

int lan_scanner_start(LanScanner *scanner, const LanScannerConfig *cfg) {
  if (cfg != NULL) {
    scanner->config = *cfg;
    if (scanner->config.max_hosts == 0 || scanner->config.max_hosts > LAN_SCANNER_MAX_HOSTS) {
      scanner->config.max_hosts = LAN_SCANNER_MAX_HOSTS;
    }
  }

  scanner->running = 1;
  begin_new_round(scanner, 0);
  return 0;
}

void lan_scanner_stop(LanScanner *scanner) {
  scanner->running = 0;
}

void lan_scanner_request_rescan(LanScanner *scanner) {
  scanner->running = 1;
  begin_new_round(scanner, 0);
}

void lan_scanner_get_metrics(const LanScanner *scanner, LanScannerMetrics *out) {
  memset(out, 0, sizeof(*out));
  out->enabled = (scanner->running != 0);
  out->running = (scanner->running != 0);
  out->subnet_valid = (scanner->subnet_valid != 0);
  out->icmp_supported = scanner->icmp_supported;
  out->mdns_running = (uint8_t)(scanner->discovery.mdns_running != 0);
  out->ssdp_running = (uint8_t)(scanner->discovery.ssdp_running != 0);
  out->nbns_running = (uint8_t)(scanner->discovery.nbns_running != 0);
  out->hosts_total = 254;
  out->hosts_scanned = scanner->hosts_scanned;
  out->hosts_alive = scanner->hosts_alive;
  out->scan_round = scanner->scan_round;
  out->mdns_hits = scanner->mdns_hits;
  out->ssdp_hits = scanner->ssdp_hits;
  out->nbns_hits = scanner->nbns_hits;
  out->icmp_hits = scanner->icmp_hits;
  out->tcp_hits = scanner->tcp_hits;
  out->last_error = scanner->last_error;
  snprintf(out->subnet_cidr, sizeof(out->subnet_cidr), "%s", scanner->subnet_cidr);

  out->host_count = scanner->host_count;
  if (out->host_count > LAN_SCANNER_MAX_HOSTS) {
    out->host_count = LAN_SCANNER_MAX_HOSTS;
  }
  for (uint32_t i = 0; i < out->host_count; i++) {
    out->hosts[i] = scanner->hosts[i];
  }
}

void lan_scanner_set_ip_hint(LanScanner *scanner, const char *ip_address) {
  if (ip_address == NULL || ip_address[0] == '\0' || strcmp(ip_address, "N/A") == 0) {
    scanner->hinted_prefix_valid = 0;
    return;
  }

  unsigned int a = 0;
  unsigned int b = 0;
  unsigned int c = 0;
  unsigned int d = 0;
  if (sscanf(ip_address, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
      a > 255U || b > 255U || c > 255U || d > 255U || (a == 0U && b == 0U && c == 0U)) {
    scanner->hinted_prefix_valid = 0;
    return;
  }

  scanner->hinted_a = a;
  scanner->hinted_b = b;
  scanner->hinted_c = c;
  scanner->hinted_prefix_valid = 1;
}

void lan_scanner_set_gateway_hint(LanScanner *scanner, const char *gateway_ip) {
  if (gateway_ip == NULL || gateway_ip[0] == '\0' || strcmp(gateway_ip, "N/A") == 0) {
    scanner->gateway_hint_valid = 0;
    return;
  }

  unsigned int a = 0;
  unsigned int b = 0;
  unsigned int c = 0;
  unsigned int d = 0;
  if (sscanf(gateway_ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
      a > 255U || b > 255U || c > 255U || d > 255U || d == 0U || d == 255U) {
    scanner->gateway_hint_valid = 0;
    return;
  }

  scanner->gateway_a = a;
  scanner->gateway_b = b;
  scanner->gateway_c = c;
  scanner->gateway_d = d;
  scanner->gateway_hint_valid = 1;
}

void lan_scanner_tick(LanScanner *scanner, uint64_t now_us) {
  static const uint16_t alive_ports[] = {53, 80, 443, 445, 139, 22, 8080, 62078, 554, 8009};
  const uint8_t alive_steps = (uint8_t)(1 + (sizeof(alive_ports) / sizeof(alive_ports[0])));
  const uint32_t step_timeout_ms = (scanner->config.connect_timeout_ms > UI_PROBE_BUDGET_MS)
                                     ? UI_PROBE_BUDGET_MS
                                     : scanner->config.connect_timeout_ms;

  if (!scanner->running) {
    return;
  }
  if (now_us < scanner->next_step_us) {
    return;
  }

  if (scanner->phase == SCAN_PHASE_RESOLVE) {
    int rc = -1;
    if (scanner->hinted_prefix_valid) {
      snprintf(scanner->subnet_prefix, sizeof(scanner->subnet_prefix), "%u.%u.%u.",
               scanner->hinted_a, scanner->hinted_b, scanner->hinted_c);
      snprintf(scanner->subnet_cidr, sizeof(scanner->subnet_cidr), "%u.%u.%u.0/24",
               scanner->hinted_a, scanner->hinted_b, scanner->hinted_c);
      rc = 0;
    }
    if (rc < 0) {
      rc = resolve_local_subnet(scanner->subnet_prefix, sizeof(scanner->subnet_prefix),
                                scanner->subnet_cidr, sizeof(scanner->subnet_cidr));
    }
    if (rc < 0) {
      scanner->subnet_valid = 0;
      scanner->last_error = rc;
      scanner->next_step_us = now_us + 250000ULL;
      return;
    }

    scanner->subnet_valid = 1;
    scanner->preferred_host_valid = 0;
    scanner->preferred_host_done = 0;
    unsigned int subnet_a = 0;
    unsigned int subnet_b = 0;
    unsigned int subnet_c = 0;
    if (sscanf(scanner->subnet_prefix, "%u.%u.%u.", &subnet_a, &subnet_b, &subnet_c) != 3) {
      subnet_a = subnet_b = subnet_c = 0;
    }
    if (scanner->gateway_hint_valid &&
        scanner->gateway_a == subnet_a &&
        scanner->gateway_b == subnet_b &&
        scanner->gateway_c == subnet_c) {
      scanner->preferred_host = scanner->gateway_d;
      if (scanner->preferred_host >= 1U && scanner->preferred_host <= 254U) {
        scanner->preferred_host_valid = 1;
      }
    }
    scanner->phase = SCAN_PHASE_ALIVE;
    scanner->alive_probe_index = 0;
    scanner->port_probe_index = 0;
    scanner->current_host_alive = 0;
    scanner->current_open_port_count = 0;
  }

  if (scanner->subnet_valid) {
    DiscoveryEvent events[16];
    uint32_t event_count = 0;
    discovery_tick(&scanner->discovery, now_us, scanner->subnet_prefix, events, 16, &event_count);
    for (uint32_t i = 0; i < event_count; i++) {
      upsert_host(scanner, events[i].ip, events[i].source_flags, events[i].hostname, events[i].service_hint, NULL, 0, 0);
    }
  }

  if (scanner->current_host == 0U) {
    if (!select_next_host(scanner)) {
      scanner->scan_round++;
      scanner->running = 0;
      scanner->next_step_us = now_us + 500000ULL;
      return;
    }
  }

  if (scanner->current_host > 254U) {
    scanner->scan_round++;
    scanner->running = 0;
    scanner->next_step_us = now_us + 500000ULL;
    return;
  }

  snprintf(scanner->current_ip, sizeof(scanner->current_ip), "%s%u", scanner->subnet_prefix, scanner->current_host);

  if (scanner->phase == SCAN_PHASE_ALIVE) {
    int probe_err = 0;
    ProbeResult pr = PROBE_NO_RESPONSE;

    if (scanner->alive_probe_index == 0U) {
      if (scanner->icmp_supported != 0) {
        pr = probe_icmp_echo(scanner->current_ip, step_timeout_ms, &probe_err);
        if (probe_err == -SCE_NET_EPROTONOSUPPORT || probe_err == -SCE_NET_EPERM) {
          scanner->icmp_supported = 0;
        } else if (probe_err == 0 || probe_err == -SCE_NET_ETIMEDOUT) {
          scanner->icmp_supported = 1;
        }
      }
    } else {
      const uint16_t port = alive_ports[scanner->alive_probe_index - 1U];
      pr = probe_tcp_port(scanner->current_ip, port, step_timeout_ms, &probe_err);
    }

    if (probe_err != -SCE_NET_ETIMEDOUT) {
      scanner->last_error = probe_err;
    }
    if (pr == PROBE_OPEN || pr == PROBE_HOST_RESPONDED) {
      scanner->current_host_alive = 1;
      if (scanner->alive_probe_index == 0U) {
        scanner->current_source_flags |= DISCOVERY_SRC_ICMP;
      } else {
        scanner->current_source_flags |= DISCOVERY_SRC_TCP;
      }
      scanner->phase = SCAN_PHASE_PORTS;
      scanner->port_probe_index = 0;
      scanner->next_step_us = now_us + STEP_INTERVAL_US;
      return;
    }

    scanner->alive_probe_index++;
    if (scanner->alive_probe_index >= alive_steps) {
      finalize_host(scanner);
    }
    scanner->next_step_us = now_us + STEP_INTERVAL_US;
    return;
  }

  if (scanner->phase == SCAN_PHASE_PORTS) {
    if (scanner->port_probe_index < LAN_SCANNER_PORT_COUNT) {
      int probe_err = 0;
      const uint16_t port = scanner->config.ports[scanner->port_probe_index];
      const ProbeResult pr = probe_tcp_port(scanner->current_ip, port, step_timeout_ms, &probe_err);
      if (probe_err != -SCE_NET_ETIMEDOUT) {
        scanner->last_error = probe_err;
      }
      if (pr == PROBE_OPEN && scanner->current_open_port_count < LAN_SCANNER_PORT_COUNT) {
        scanner->current_open_ports[scanner->current_open_port_count++] = port;
      }
      scanner->port_probe_index++;
      if (scanner->port_probe_index < LAN_SCANNER_PORT_COUNT) {
        scanner->next_step_us = now_us + STEP_INTERVAL_US;
        return;
      }
    }

    finalize_host(scanner);
    scanner->next_step_us = now_us + STEP_INTERVAL_US;
  }
}
