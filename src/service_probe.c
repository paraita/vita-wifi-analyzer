#include "service_probe.h"

#include <psp2/net/net.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  SP_CONNECT_TIMEOUT_US = 1500000ULL,  /* 1.5 s to establish TCP */
  SP_RECV_TIMEOUT_US    = 1500000ULL   /* 1.5 s to receive headers */
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void parse_http_response(const char *buf, uint32_t len,
                                ServiceProbeResult *out) {
  (void)len; /* buf is NUL-terminated */

  /* Status line: "HTTP/1.x CODE reason" */
  if (strncmp(buf, "HTTP/", 5) == 0) {
    const char *sp = strchr(buf, ' ');
    if (sp != NULL) {
      out->status_code = (int)strtol(sp + 1, NULL, 10);
    }
  }

  /* Walk header lines */
  const char *p = buf;
  while (p != NULL && *p != '\0') {
    const char *eol = strstr(p, "\r\n");
    if (eol == NULL) {
      eol = strchr(p, '\n');
    }
    if (eol == NULL) {
      break;
    }
    /* Server: */
    if (strncasecmp(p, "Server:", 7) == 0) {
      const char *v = p + 7;
      while (*v == ' ') { v++; }
      size_t n = (size_t)(eol - v);
      if (n >= sizeof(out->server)) { n = sizeof(out->server) - 1U; }
      memcpy(out->server, v, n);
      out->server[n] = '\0';
    }
    /* X-Powered-By: */
    if (strncasecmp(p, "X-Powered-By:", 13) == 0) {
      const char *v = p + 13;
      while (*v == ' ') { v++; }
      size_t n = (size_t)(eol - v);
      if (n >= sizeof(out->powered_by)) { n = sizeof(out->powered_by) - 1U; }
      memcpy(out->powered_by, v, n);
      out->powered_by[n] = '\0';
    }
    /* WWW-Authenticate: */
    if (strncasecmp(p, "WWW-Authenticate:", 17) == 0) {
      const char *v = p + 17;
      while (*v == ' ') { v++; }
      /* Only the scheme (first word) */
      size_t n = 0;
      while (v[n] != ' ' && v[n] != '\r' && v[n] != '\n' && v[n] != '\0' &&
             n < sizeof(out->auth_scheme) - 1U) {
        n++;
      }
      memcpy(out->auth_scheme, v, n);
      out->auth_scheme[n] = '\0';
    }
    /* Advance past CRLF */
    p = (*eol == '\r') ? eol + 2 : eol + 1;
    /* Blank line = end of headers */
    if (*p == '\r' || *p == '\n') {
      break;
    }
  }
}

static void begin_next_probe(ServiceProbe *p, uint64_t now_us) {
  if (p->q_head >= p->q_count) {
    p->state = SP_IDLE;
    p->metrics.pending_count = 0;
    return;
  }

  const char   *ip   = p->q_ips[p->q_head];
  const uint16_t port = p->q_ports[p->q_head];
  p->q_head++;
  p->metrics.pending_count = p->q_count - p->q_head;

  /* Find or create result slot for this IP */
  uint32_t slot = SERVICE_PROBE_MAX_RESULTS; /* sentinel: not found */
  for (uint32_t i = 0; i < p->metrics.probed_count; i++) {
    if (strcmp(p->metrics.results[i].ip, ip) == 0) {
      slot = i;
      break;
    }
  }
  if (slot == SERVICE_PROBE_MAX_RESULTS) {
    if (p->metrics.probed_count < SERVICE_PROBE_MAX_RESULTS) {
      slot = p->metrics.probed_count++;
    } else {
      slot = 0; /* overwrite oldest */
    }
  }
  ServiceProbeResult *r = &p->metrics.results[slot];
  memset(r, 0, sizeof(*r));
  snprintf(r->ip, sizeof(r->ip), "%s", ip);
  r->port = port;

  p->cur_slot = slot;
  snprintf(p->cur_ip, sizeof(p->cur_ip), "%s", ip);
  p->cur_port = port;
  p->recv_len = 0;

  /* Create non-blocking TCP socket */
  p->sock = sceNetSocket("svc_probe", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
  if (p->sock < 0) {
    p->metrics.last_error = p->sock;
    begin_next_probe(p, now_us); /* skip, try next */
    return;
  }

  int nbio = 1;
  if (sceNetSetsockopt(p->sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio)) < 0) {
    sceNetSocketClose(p->sock);
    p->sock = -1;
    begin_next_probe(p, now_us);
    return;
  }

  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len    = sizeof(addr);
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port   = sceNetHtons(port);
  if (sceNetInetPton(SCE_NET_AF_INET, ip, &addr.sin_addr) != 1) {
    sceNetSocketClose(p->sock);
    p->sock = -1;
    begin_next_probe(p, now_us);
    return;
  }

  const int rc = sceNetConnect(p->sock, (const SceNetSockaddr *)&addr, sizeof(addr));
  if (rc < 0) {
    const int err = *sceNetErrnoLoc();
    if (err != SCE_NET_EINPROGRESS) {
      p->metrics.last_error = -err;
      sceNetSocketClose(p->sock);
      p->sock = -1;
      begin_next_probe(p, now_us);
      return;
    }
  }

  p->state        = SP_CONNECTING;
  p->deadline_us  = now_us + SP_CONNECT_TIMEOUT_US;
}

static void finish_probe(ServiceProbe *p) {
  if (p->recv_len > 0) {
    p->recv_buf[p->recv_len] = '\0';
    ServiceProbeResult *r = &p->metrics.results[p->cur_slot];
    r->valid = 1;
    parse_http_response(p->recv_buf, p->recv_len, r);
  }
  if (p->sock >= 0) {
    sceNetSocketClose(p->sock);
    p->sock = -1;
  }
  p->state = SP_IDLE;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void service_probe_init(ServiceProbe *p) {
  memset(p, 0, sizeof(*p));
  p->sock = -1;
}

void service_probe_arm(ServiceProbe *p, const LanScannerMetrics *scanner) {
  /* Abort any current probe */
  if (p->sock >= 0) {
    sceNetSocketClose(p->sock);
    p->sock = -1;
  }
  p->state   = SP_IDLE;
  p->q_count = 0;
  p->q_head  = 0;

  /* Enqueue alive hosts with port 80 or 8080 open */
  for (uint32_t i = 0; i < scanner->host_count && p->q_count < LAN_SCANNER_MAX_HOSTS; i++) {
    const LanHostResult *h = &scanner->hosts[i];
    if (!h->alive) {
      continue;
    }
    uint16_t port = 0;
    for (uint8_t j = 0; j < h->open_port_count; j++) {
      if (h->open_ports[j] == 80 || h->open_ports[j] == 8080) {
        port = h->open_ports[j];
        break;
      }
    }
    if (port == 0) {
      continue;
    }
    snprintf(p->q_ips[p->q_count], sizeof(p->q_ips[p->q_count]), "%s", h->ip);
    p->q_ports[p->q_count] = port;
    p->q_count++;
  }
  p->metrics.pending_count = p->q_count;
}

void service_probe_tick(ServiceProbe *p, uint64_t now_us) {
  switch (p->state) {

    case SP_IDLE:
      if (p->q_head < p->q_count) {
        begin_next_probe(p, now_us);
      }
      break;

    case SP_CONNECTING: {
      /* Check connection result via SO_ERROR */
      if (now_us >= p->deadline_us) {
        p->metrics.last_error = -SCE_NET_ETIMEDOUT;
        finish_probe(p);
        break;
      }
      int so_error = 0;
      unsigned int so_len = sizeof(so_error);
      if (sceNetGetsockopt(p->sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR,
                           &so_error, &so_len) < 0) {
        finish_probe(p);
        break;
      }
      if (so_error == SCE_NET_EINPROGRESS || so_error == SCE_NET_EALREADY) {
        break; /* still connecting */
      }
      if (so_error != 0) {
        p->metrics.last_error = -so_error;
        finish_probe(p);
        break;
      }
      /* Verify connected via getpeername */
      SceNetSockaddrIn peer;
      unsigned int peer_len = sizeof(peer);
      if (sceNetGetpeername(p->sock, (SceNetSockaddr *)&peer, &peer_len) != 0) {
        const int err = *sceNetErrnoLoc();
        if (err == SCE_NET_ENOTCONN || err == SCE_NET_EINPROGRESS) {
          break; /* still connecting */
        }
        p->metrics.last_error = -err;
        finish_probe(p);
        break;
      }
      /* Connected — send HTTP GET */
      char req[256];
      const int req_len = snprintf(req, sizeof(req),
        "GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
        p->cur_ip);
      (void)sceNetSend(p->sock, req, (unsigned int)req_len, 0);
      p->state       = SP_RECEIVING;
      p->deadline_us = now_us + SP_RECV_TIMEOUT_US;
      p->recv_len    = 0;
      break;
    }

    case SP_RECEIVING: {
      if (now_us >= p->deadline_us) {
        finish_probe(p);
        break;
      }
      const uint32_t space = (uint32_t)(sizeof(p->recv_buf) - 1U) - p->recv_len;
      if (space == 0U) {
        finish_probe(p);
        break;
      }
      const int rc = sceNetRecv(p->sock, p->recv_buf + p->recv_len, space, 0);
      if (rc > 0) {
        p->recv_len += (uint32_t)rc;
        /* Done as soon as we have the end of headers */
        p->recv_buf[p->recv_len] = '\0';
        if (strstr(p->recv_buf, "\r\n\r\n") != NULL ||
            strstr(p->recv_buf, "\n\n") != NULL) {
          finish_probe(p);
        }
      } else if (rc == 0) {
        finish_probe(p); /* connection closed */
      } else {
        const int err = *sceNetErrnoLoc();
        if (err != SCE_NET_EAGAIN && err != SCE_NET_EWOULDBLOCK) {
          p->metrics.last_error = -err;
          finish_probe(p);
        }
        /* EAGAIN: wait next tick */
      }
      break;
    }

    default:
      break;
  }
}

void service_probe_get_metrics(const ServiceProbe *p, ServiceProbeMetrics *out) {
  memcpy(out, &p->metrics, sizeof(*out));
}
