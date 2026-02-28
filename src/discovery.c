#include "discovery.h"

#include <psp2/net/net.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  DISCOVERY_QUERY_INTERVAL_US = 3000000ULL
};

static int set_nonblocking(int sock) {
  int nbio = 1;
  if (sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio)) < 0) {
    return -1;
  }
  return 0;
}

static int open_udp_socket_ephemeral(int allow_broadcast) {
  int sock = sceNetSocket("discover_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
  if (sock < 0) {
    return -1;
  }
  if (set_nonblocking(sock) < 0) {
    sceNetSocketClose(sock);
    return -1;
  }
  if (allow_broadcast) {
    int yes = 1;
    (void)sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &yes, sizeof(yes));
  }

  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(0);
  addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
  if (sceNetBind(sock, (const SceNetSockaddr *)&addr, sizeof(addr)) < 0) {
    sceNetSocketClose(sock);
    return -1;
  }
  return sock;
}

static int append_event(DiscoveryEvent *events,
                        uint32_t max_events,
                        uint32_t *event_count,
                        const char *ip,
                        uint32_t source_flag,
                        const char *hostname,
                        const char *service_hint) {
  if (*event_count >= max_events) {
    return -1;
  }
  DiscoveryEvent *ev = &events[*event_count];
  memset(ev, 0, sizeof(*ev));
  snprintf(ev->ip, sizeof(ev->ip), "%s", ip);
  ev->source_flags = source_flag;
  if (hostname != NULL && hostname[0] != '\0') {
    snprintf(ev->hostname, sizeof(ev->hostname), "%s", hostname);
  }
  if (service_hint != NULL && service_hint[0] != '\0') {
    snprintf(ev->service_hint, sizeof(ev->service_hint), "%s", service_hint);
  }
  (*event_count)++;
  return 0;
}

static void recv_loop(int sock,
                      uint32_t source_flag,
                      DiscoveryEvent *events,
                      uint32_t max_events,
                      uint32_t *event_count,
                      uint32_t *hits_out) {
  if (sock < 0) {
    return;
  }

  for (int i = 0; i < 16; i++) {
    char buf[1024];
    SceNetSockaddrIn src;
    unsigned int src_len = sizeof(src);
    const int rc = sceNetRecvfrom(sock, buf, sizeof(buf) - 1, 0, (SceNetSockaddr *)&src, &src_len);
    if (rc <= 0) {
      break;
    }
    buf[rc] = '\0';

    char ip[16];
    if (sceNetInetNtop(SCE_NET_AF_INET, &src.sin_addr, ip, sizeof(ip)) == NULL) {
      continue;
    }

    char hostname[64];
    hostname[0] = '\0';
    char service_hint[64];
    service_hint[0] = '\0';
    if (source_flag == DISCOVERY_SRC_SSDP) {
      const char *server = strstr(buf, "\nSERVER:");
      if (server == NULL) {
        server = strstr(buf, "\nServer:");
      }
      if (server != NULL) {
        server += 8;
        while (*server == ' ') server++;
        const char *end = strstr(server, "\r\n");
        if (end == NULL) end = strchr(server, '\n');
        if (end != NULL) {
          size_t n = (size_t)(end - server);
          if (n >= sizeof(hostname)) n = sizeof(hostname) - 1;
          memcpy(hostname, server, n);
          hostname[n] = '\0';
        }
      }
      const char *st = strstr(buf, "\nST:");
      if (st == NULL) {
        st = strstr(buf, "\nSt:");
      }
      if (st != NULL) {
        st += 4;
        while (*st == ' ') st++;
        const char *end = strstr(st, "\r\n");
        if (end == NULL) end = strchr(st, '\n');
        if (end != NULL) {
          size_t n = (size_t)(end - st);
          if (n >= sizeof(service_hint)) n = sizeof(service_hint) - 1;
          memcpy(service_hint, st, n);
          service_hint[n] = '\0';
        }
      }
    }

    if (append_event(events, max_events, event_count, ip, source_flag, hostname, service_hint) == 0) {
      (*hits_out)++;
    }
  }
}

static void send_ssdp_query(int sock) {
  if (sock < 0) {
    return;
  }
  static const char req[] =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST:239.255.255.250:1900\r\n"
    "MAN:\"ssdp:discover\"\r\n"
    "MX:1\r\n"
    "ST:ssdp:all\r\n\r\n";

  SceNetSockaddrIn dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_len = sizeof(dst);
  dst.sin_family = SCE_NET_AF_INET;
  dst.sin_port = sceNetHtons(1900);
  (void)sceNetInetPton(SCE_NET_AF_INET, "239.255.255.250", &dst.sin_addr);
  (void)sceNetSendto(sock, req, (unsigned int)(sizeof(req) - 1), 0, (const SceNetSockaddr *)&dst, sizeof(dst));
}

static void send_mdns_query(int sock) {
  if (sock < 0) {
    return;
  }
  static const uint8_t req[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x09, '_','s','e','r','v','i','c','e','s',
    0x07, '_','d','n','s','-','s','d',
    0x04, '_','u','d','p',
    0x05, 'l','o','c','a','l',
    0x00,
    0x00, 0x0c,
    0x00, 0x01
  };

  SceNetSockaddrIn dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_len = sizeof(dst);
  dst.sin_family = SCE_NET_AF_INET;
  dst.sin_port = sceNetHtons(5353);
  (void)sceNetInetPton(SCE_NET_AF_INET, "224.0.0.251", &dst.sin_addr);
  (void)sceNetSendto(sock, req, sizeof(req), 0, (const SceNetSockaddr *)&dst, sizeof(dst));
}

static void send_nbns_query(int sock, const char *subnet_prefix) {
  if (sock < 0 || subnet_prefix == NULL || subnet_prefix[0] == '\0') {
    return;
  }

  static const uint8_t req[] = {
    0x12, 0x34, 0x01, 0x10, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x20,
    0x43,0x4b,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x41,0x41,0x41,0x41,0x41,0x41,0x41,0x41,
    0x00,
    0x00, 0x21,
    0x00, 0x01
  };

  char bcast_ip[16];
  snprintf(bcast_ip, sizeof(bcast_ip), "%s255", subnet_prefix);

  SceNetSockaddrIn dst;
  memset(&dst, 0, sizeof(dst));
  dst.sin_len = sizeof(dst);
  dst.sin_family = SCE_NET_AF_INET;
  dst.sin_port = sceNetHtons(137);
  if (sceNetInetPton(SCE_NET_AF_INET, bcast_ip, &dst.sin_addr) != 1) {
    return;
  }
  (void)sceNetSendto(sock, req, sizeof(req), 0, (const SceNetSockaddr *)&dst, sizeof(dst));
}

void discovery_init(DiscoveryEngine *engine) {
  memset(engine, 0, sizeof(*engine));
  engine->mdns_sock = -1;
  engine->ssdp_sock = -1;
  engine->nbns_sock = -1;
}

void discovery_term(DiscoveryEngine *engine) {
  if (engine->mdns_sock >= 0) {
    sceNetSocketClose(engine->mdns_sock);
    engine->mdns_sock = -1;
  }
  if (engine->ssdp_sock >= 0) {
    sceNetSocketClose(engine->ssdp_sock);
    engine->ssdp_sock = -1;
  }
  if (engine->nbns_sock >= 0) {
    sceNetSocketClose(engine->nbns_sock);
    engine->nbns_sock = -1;
  }
  engine->mdns_running = 0;
  engine->ssdp_running = 0;
  engine->nbns_running = 0;
}

void discovery_tick(DiscoveryEngine *engine,
                    uint64_t now_us,
                    const char *subnet_prefix,
                    DiscoveryEvent *events,
                    uint32_t max_events,
                    uint32_t *event_count) {
  *event_count = 0;

  if (engine->mdns_sock < 0) {
    engine->mdns_sock = open_udp_socket_ephemeral(0);
    engine->mdns_running = (engine->mdns_sock >= 0);
  }
  if (engine->ssdp_sock < 0) {
    engine->ssdp_sock = open_udp_socket_ephemeral(0);
    engine->ssdp_running = (engine->ssdp_sock >= 0);
  }
  if (engine->nbns_sock < 0) {
    engine->nbns_sock = open_udp_socket_ephemeral(1);
    engine->nbns_running = (engine->nbns_sock >= 0);
  }

  if (now_us >= engine->next_query_us) {
    send_mdns_query(engine->mdns_sock);
    send_ssdp_query(engine->ssdp_sock);
    send_nbns_query(engine->nbns_sock, subnet_prefix);
    engine->next_query_us = now_us + DISCOVERY_QUERY_INTERVAL_US;
  }

  recv_loop(engine->mdns_sock, DISCOVERY_SRC_MDNS, events, max_events, event_count, &engine->mdns_hits);
  recv_loop(engine->ssdp_sock, DISCOVERY_SRC_SSDP, events, max_events, event_count, &engine->ssdp_hits);
  recv_loop(engine->nbns_sock, DISCOVERY_SRC_NBNS, events, max_events, event_count, &engine->nbns_hits);
}
