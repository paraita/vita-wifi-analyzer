#include "discovery.h"

#include <psp2/net/net.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  DISCOVERY_QUERY_INTERVAL_US  = 3000000ULL,
  UPNP_CONNECT_TIMEOUT_US      = 1000000ULL,
  UPNP_RECV_TIMEOUT_US         = 1000000ULL
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

/* ------------------------------------------------------------------ */
/* UPnP helpers                                                         */
/* ------------------------------------------------------------------ */

/* Parse "http://IP[:PORT]/PATH" into ip/port/path components. */
static int parse_location_url(const char *url,
                              char ip[16], uint16_t *port, char path[128]) {
  const char *p = url;
  if (strncmp(p, "http://", 7) != 0) {
    return -1;
  }
  p += 7;
  /* extract host (IP or hostname — we only handle dotted-decimal) */
  const char *host_end = strpbrk(p, ":/");
  if (host_end == NULL) {
    return -1;
  }
  size_t host_len = (size_t)(host_end - p);
  if (host_len == 0 || host_len >= 16U) {
    return -1;
  }
  memcpy(ip, p, host_len);
  ip[host_len] = '\0';

  *port = 80;
  if (*host_end == ':') {
    const char *port_start = host_end + 1;
    *port = 0;
    while (*port_start >= '0' && *port_start <= '9') {
      *port = (uint16_t)(*port * 10U + (uint16_t)(*port_start - '0'));
      port_start++;
    }
    host_end = port_start;
  }

  const char *slash = strchr(host_end, '/');
  if (slash != NULL) {
    snprintf(path, 128, "%s", slash);
  } else {
    snprintf(path, 128, "/");
  }
  return 0;
}

/* Extract a tag value "<tag>value</tag>" from XML buf. */
static void xml_extract(const char *buf, const char *tag, char *out, size_t out_len) {
  char open_tag[64];
  char close_tag[64];
  snprintf(open_tag,  sizeof(open_tag),  "<%s>",  tag);
  snprintf(close_tag, sizeof(close_tag), "</%s>", tag);
  const char *start = strstr(buf, open_tag);
  if (start == NULL) {
    return;
  }
  start += strlen(open_tag);
  const char *end = strstr(start, close_tag);
  if (end == NULL) {
    return;
  }
  size_t n = (size_t)(end - start);
  if (n >= out_len) {
    n = out_len - 1U;
  }
  memcpy(out, start, n);
  out[n] = '\0';
}

static void upnp_finish_fetch(DiscoveryEngine *engine) {
  if (engine->upnp_fetch_sock >= 0) {
    sceNetSocketClose(engine->upnp_fetch_sock);
    engine->upnp_fetch_sock = -1;
  }
  engine->upnp_fetch_state = UPNP_FETCH_IDLE;
}

static void upnp_parse_and_store(DiscoveryEngine *engine) {
  const uint32_t idx = engine->upnp_fetch_target_idx;
  if (idx >= DISCOVERY_MAX_UPNP) {
    return;
  }
  engine->upnp_fetch_buf[engine->upnp_fetch_len] = '\0';

  /* Skip past HTTP headers to XML body */
  const char *body = strstr(engine->upnp_fetch_buf, "\r\n\r\n");
  if (body != NULL) {
    body += 4;
  } else {
    body = strstr(engine->upnp_fetch_buf, "\n\n");
    if (body != NULL) {
      body += 2;
    } else {
      body = engine->upnp_fetch_buf;
    }
  }

  UPnPDevice *d = &engine->upnp_devices[idx];
  xml_extract(body, "modelName",    d->model,        sizeof(d->model));
  xml_extract(body, "manufacturer", d->manufacturer, sizeof(d->manufacturer));

  /* Friendly name (just overwrite model if empty) */
  if (d->model[0] == '\0') {
    xml_extract(body, "friendlyName", d->model, sizeof(d->model));
  }
}

/* Advance the UPnP XML fetch state machine by one step. */
static void upnp_fetch_tick(DiscoveryEngine *engine, uint64_t now_us) {
  switch (engine->upnp_fetch_state) {

    case UPNP_FETCH_IDLE:
      break;

    case UPNP_FETCH_CONNECTING: {
      if (now_us >= engine->upnp_fetch_deadline_us) {
        upnp_finish_fetch(engine);
        break;
      }
      int so_error = 0;
      unsigned int so_len = sizeof(so_error);
      if (sceNetGetsockopt(engine->upnp_fetch_sock, SCE_NET_SOL_SOCKET,
                           SCE_NET_SO_ERROR, &so_error, &so_len) < 0) {
        upnp_finish_fetch(engine);
        break;
      }
      if (so_error == SCE_NET_EINPROGRESS || so_error == SCE_NET_EALREADY) {
        break; /* still connecting */
      }
      if (so_error != 0) {
        upnp_finish_fetch(engine);
        break;
      }
      /* Verify connected */
      SceNetSockaddrIn peer;
      unsigned int peer_len = sizeof(peer);
      if (sceNetGetpeername(engine->upnp_fetch_sock,
                            (SceNetSockaddr *)&peer, &peer_len) != 0) {
        const int err = *sceNetErrnoLoc();
        if (err == SCE_NET_ENOTCONN || err == SCE_NET_EINPROGRESS) {
          break; /* still connecting */
        }
        upnp_finish_fetch(engine);
        break;
      }
      /* Connected — send HTTP GET */
      char req[256];
      const int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
        engine->upnp_fetch_path, engine->upnp_fetch_ip);
      (void)sceNetSend(engine->upnp_fetch_sock, req, (unsigned int)req_len, 0);
      engine->upnp_fetch_state    = UPNP_FETCH_RECEIVING;
      engine->upnp_fetch_deadline_us = now_us + UPNP_RECV_TIMEOUT_US;
      engine->upnp_fetch_len = 0;
      break;
    }

    case UPNP_FETCH_RECEIVING: {
      if (now_us >= engine->upnp_fetch_deadline_us) {
        upnp_parse_and_store(engine);
        upnp_finish_fetch(engine);
        break;
      }
      const uint32_t space =
        (uint32_t)(sizeof(engine->upnp_fetch_buf) - 1U) - engine->upnp_fetch_len;
      if (space == 0U) {
        upnp_parse_and_store(engine);
        upnp_finish_fetch(engine);
        break;
      }
      const int rc = sceNetRecv(engine->upnp_fetch_sock,
                                engine->upnp_fetch_buf + engine->upnp_fetch_len,
                                space, 0);
      if (rc > 0) {
        engine->upnp_fetch_len += (uint32_t)rc;
        engine->upnp_fetch_buf[engine->upnp_fetch_len] = '\0';
        /* Done when we see the closing tag of the root device */
        if (strstr(engine->upnp_fetch_buf, "</device>") != NULL ||
            strstr(engine->upnp_fetch_buf, "</root>") != NULL) {
          upnp_parse_and_store(engine);
          upnp_finish_fetch(engine);
        }
      } else if (rc == 0) {
        upnp_parse_and_store(engine);
        upnp_finish_fetch(engine);
      } else {
        const int err = *sceNetErrnoLoc();
        if (err != SCE_NET_EAGAIN && err != SCE_NET_EWOULDBLOCK) {
          upnp_finish_fetch(engine);
        }
      }
      break;
    }

    default:
      break;
  }
}

/* Start a UPnP XML fetch for the device at upnp_devices[idx]. */
static void upnp_start_fetch(DiscoveryEngine *engine, uint32_t idx, uint64_t now_us) {
  if (engine->upnp_fetch_state != UPNP_FETCH_IDLE) {
    return; /* already fetching */
  }

  const UPnPDevice *d = &engine->upnp_devices[idx];
  if (d->ip[0] == '\0') {
    return;
  }

  /* Re-derive the path from the USN / use upnp_fetch_path if set */
  /* (We stored the path in upnp_fetch_path when the device was added) */

  const int sock = sceNetSocket("upnp_fetch", SCE_NET_AF_INET,
                                SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
  if (sock < 0) {
    return;
  }
  if (set_nonblocking(sock) < 0) {
    sceNetSocketClose(sock);
    return;
  }

  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len    = sizeof(addr);
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port   = sceNetHtons(engine->upnp_fetch_port);
  if (sceNetInetPton(SCE_NET_AF_INET, d->ip, &addr.sin_addr) != 1) {
    sceNetSocketClose(sock);
    return;
  }

  const int rc = sceNetConnect(sock, (const SceNetSockaddr *)&addr, sizeof(addr));
  if (rc < 0) {
    const int err = *sceNetErrnoLoc();
    if (err != SCE_NET_EINPROGRESS) {
      sceNetSocketClose(sock);
      return;
    }
  }

  engine->upnp_fetch_sock        = sock;
  engine->upnp_fetch_target_idx  = idx;
  engine->upnp_fetch_state       = UPNP_FETCH_CONNECTING;
  engine->upnp_fetch_deadline_us = now_us + UPNP_CONNECT_TIMEOUT_US;
  engine->upnp_fetch_len         = 0;
}

/* Add or update a UPnP device entry from a SSDP response. */
static void upnp_upsert(DiscoveryEngine *engine,
                        const char *ip,
                        const char *usn,
                        const char *server,
                        const char *location,
                        int is_igd,
                        uint64_t now_us) {
  /* Check for existing entry */
  for (uint32_t i = 0; i < engine->upnp_count; i++) {
    if (strcmp(engine->upnp_devices[i].ip, ip) == 0) {
      /* Update IGD flag if newly determined */
      if (is_igd) {
        engine->upnp_devices[i].is_igd = 1;
      }
      return; /* already known */
    }
  }

  if (engine->upnp_count >= DISCOVERY_MAX_UPNP) {
    return; /* table full */
  }

  UPnPDevice *d = &engine->upnp_devices[engine->upnp_count];
  memset(d, 0, sizeof(*d));
  snprintf(d->ip,     sizeof(d->ip),     "%s", ip);
  snprintf(d->usn,    sizeof(d->usn),    "%s", usn[0]    ? usn    : "-");
  snprintf(d->server, sizeof(d->server), "%s", server[0] ? server : "-");
  d->is_igd = (uint8_t)is_igd;

  const uint32_t idx = engine->upnp_count++;

  /* Parse LOCATION URL and schedule XML fetch */
  if (location != NULL && location[0] != '\0') {
    char loc_ip[16];
    uint16_t loc_port = 80;
    char loc_path[128];
    if (parse_location_url(location, loc_ip, &loc_port, loc_path) == 0) {
      snprintf(engine->upnp_fetch_ip,   sizeof(engine->upnp_fetch_ip),   "%s", loc_ip);
      engine->upnp_fetch_port = loc_port;
      snprintf(engine->upnp_fetch_path, sizeof(engine->upnp_fetch_path), "%s", loc_path);
      upnp_start_fetch(engine, idx, now_us);
    }
  }
}

/* ------------------------------------------------------------------ */
/* SSDP receive loop (extended to parse UPnP LOCATION)                 */
/* ------------------------------------------------------------------ */

static void recv_loop(int sock,
                      uint32_t source_flag,
                      DiscoveryEvent *events,
                      uint32_t max_events,
                      uint32_t *event_count,
                      uint32_t *hits_out,
                      DiscoveryEngine *engine,
                      uint64_t now_us) {
  if (sock < 0) {
    return;
  }

  for (int i = 0; i < 16; i++) {
    char buf[1024];
    SceNetSockaddrIn src;
    unsigned int src_len = sizeof(src);
    const int rc = sceNetRecvfrom(sock, buf, sizeof(buf) - 1, 0,
                                  (SceNetSockaddr *)&src, &src_len);
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
      /* SERVER: */
      const char *server_hdr = strstr(buf, "\nSERVER:");
      if (server_hdr == NULL) server_hdr = strstr(buf, "\nServer:");
      if (server_hdr != NULL) {
        server_hdr += 8;
        while (*server_hdr == ' ') server_hdr++;
        const char *end = strstr(server_hdr, "\r\n");
        if (end == NULL) end = strchr(server_hdr, '\n');
        if (end != NULL) {
          size_t n = (size_t)(end - server_hdr);
          if (n >= sizeof(hostname)) n = sizeof(hostname) - 1U;
          memcpy(hostname, server_hdr, n);
          hostname[n] = '\0';
        }
      }
      /* ST: */
      const char *st = strstr(buf, "\nST:");
      if (st == NULL) st = strstr(buf, "\nSt:");
      if (st != NULL) {
        st += 4;
        while (*st == ' ') st++;
        const char *end = strstr(st, "\r\n");
        if (end == NULL) end = strchr(st, '\n');
        if (end != NULL) {
          size_t n = (size_t)(end - st);
          if (n >= sizeof(service_hint)) n = sizeof(service_hint) - 1U;
          memcpy(service_hint, st, n);
          service_hint[n] = '\0';
        }
      }

      /* UPnP: parse LOCATION, USN for UPnP device table */
      if (engine != NULL) {
        char location[256];
        location[0] = '\0';
        const char *loc = strstr(buf, "\nLOCATION:");
        if (loc == NULL) loc = strstr(buf, "\nLocation:");
        if (loc != NULL) {
          loc += 10;
          while (*loc == ' ') loc++;
          const char *end = strstr(loc, "\r\n");
          if (end == NULL) end = strchr(loc, '\n');
          if (end != NULL) {
            size_t n = (size_t)(end - loc);
            if (n >= sizeof(location)) n = sizeof(location) - 1U;
            memcpy(location, loc, n);
            location[n] = '\0';
          }
        }

        char usn[128];
        usn[0] = '\0';
        const char *usn_hdr = strstr(buf, "\nUSN:");
        if (usn_hdr == NULL) usn_hdr = strstr(buf, "\nUsn:");
        if (usn_hdr != NULL) {
          usn_hdr += 5;
          while (*usn_hdr == ' ') usn_hdr++;
          const char *end = strstr(usn_hdr, "\r\n");
          if (end == NULL) end = strchr(usn_hdr, '\n');
          if (end != NULL) {
            size_t n = (size_t)(end - usn_hdr);
            if (n >= sizeof(usn)) n = sizeof(usn) - 1U;
            memcpy(usn, usn_hdr, n);
            usn[n] = '\0';
          }
        }

        if (location[0] != '\0') {
          const int is_igd = (strstr(service_hint, "WANIPConnection") != NULL) ? 1 : 0;
          upnp_upsert(engine, ip, usn, hostname, location, is_igd, now_us);
        }
      }
    }

    if (append_event(events, max_events, event_count, ip, source_flag,
                     hostname, service_hint) == 0) {
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
  (void)sceNetSendto(sock, req, (unsigned int)(sizeof(req) - 1), 0,
                     (const SceNetSockaddr *)&dst, sizeof(dst));
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

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void discovery_init(DiscoveryEngine *engine) {
  memset(engine, 0, sizeof(*engine));
  engine->mdns_sock = -1;
  engine->ssdp_sock = -1;
  engine->nbns_sock = -1;
  engine->upnp_fetch_sock = -1;
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
  if (engine->upnp_fetch_sock >= 0) {
    sceNetSocketClose(engine->upnp_fetch_sock);
    engine->upnp_fetch_sock = -1;
  }
  engine->mdns_running  = 0;
  engine->ssdp_running  = 0;
  engine->nbns_running  = 0;
  engine->upnp_fetch_state = UPNP_FETCH_IDLE;
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

  recv_loop(engine->mdns_sock, DISCOVERY_SRC_MDNS,
            events, max_events, event_count, &engine->mdns_hits, NULL, now_us);
  recv_loop(engine->ssdp_sock, DISCOVERY_SRC_SSDP,
            events, max_events, event_count, &engine->ssdp_hits, engine, now_us);
  recv_loop(engine->nbns_sock, DISCOVERY_SRC_NBNS,
            events, max_events, event_count, &engine->nbns_hits, NULL, now_us);

  /* Advance UPnP XML fetch state machine */
  upnp_fetch_tick(engine, now_us);
}
