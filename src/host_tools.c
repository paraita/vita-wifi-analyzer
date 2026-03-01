#include "host_tools.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <stdio.h>
#include <string.h>

static HostToolResult *find_or_add(HostToolsCache *cache, const char *ip) {
  for (uint32_t i = 0; i < cache->count; i++) {
    if (strcmp(cache->entries[i].ip, ip) == 0) {
      return &cache->entries[i];
    }
  }
  if (cache->count >= HOST_TOOLS_MAX_ENTRIES) {
    memmove(&cache->entries[0], &cache->entries[1], sizeof(cache->entries[0]) * (HOST_TOOLS_MAX_ENTRIES - 1));
    cache->count = HOST_TOOLS_MAX_ENTRIES - 1;
  }
  HostToolResult *e = &cache->entries[cache->count++];
  memset(e, 0, sizeof(*e));
  snprintf(e->ip, sizeof(e->ip), "%s", ip);
  e->dns_ok = -1;
  e->http_ok = -1;
  e->https_ok = -1;
  e->rtt_ms = -1;
  return e;
}

static int tcp_probe(const char *ip, uint16_t port, uint32_t timeout_ms, int *rtt_ms, int *err_out) {
  int sock = -1;
  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len = sizeof(addr);
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(port);
  if (sceNetInetPton(SCE_NET_AF_INET, ip, &addr.sin_addr) != 1) {
    *err_out = -SCE_NET_EINVAL;
    return 0;
  }

  sock = sceNetSocket("host_tools", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
  if (sock < 0) {
    *err_out = -*sceNetErrnoLoc();
    return 0;
  }

  int nbio = 1;
  if (sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio)) < 0) {
    *err_out = -*sceNetErrnoLoc();
    sceNetSocketClose(sock);
    return 0;
  }

  const uint64_t t0 = sceKernelGetProcessTimeWide();
  int rc = sceNetConnect(sock, (const SceNetSockaddr *)&addr, sizeof(addr));
  if (rc == 0) {
    const uint64_t dt = sceKernelGetProcessTimeWide() - t0;
    *rtt_ms = (int)(dt / 1000ULL);
    *err_out = 0;
    sceNetSocketClose(sock);
    return 1;
  }
  const int conn_err = *sceNetErrnoLoc();
  if (conn_err != SCE_NET_EINPROGRESS) {
    *err_out = -conn_err;
    sceNetSocketClose(sock);
    return (conn_err == SCE_NET_ECONNREFUSED) ? 1 : 0;
  }

  const uint64_t deadline = t0 + (uint64_t)timeout_ms * 1000ULL;
  for (;;) {
    int so_error = 0;
    unsigned int len = sizeof(so_error);
    if (sceNetGetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &so_error, &len) < 0) {
      *err_out = -*sceNetErrnoLoc();
      break;
    }
    if (so_error == 0) {
      SceNetSockaddrIn peer;
      unsigned int plen = sizeof(peer);
      if (sceNetGetpeername(sock, (SceNetSockaddr *)&peer, &plen) == 0) {
        const uint64_t dt = sceKernelGetProcessTimeWide() - t0;
        *rtt_ms = (int)(dt / 1000ULL);
        *err_out = 0;
        sceNetSocketClose(sock);
        return 1;
      }
    } else if (so_error == SCE_NET_ECONNREFUSED) {
      const uint64_t dt = sceKernelGetProcessTimeWide() - t0;
      *rtt_ms = (int)(dt / 1000ULL);
      *err_out = -so_error;
      sceNetSocketClose(sock);
      return 1;
    } else if (so_error != SCE_NET_EINPROGRESS && so_error != SCE_NET_EALREADY) {
      *err_out = -so_error;
      break;
    }

    if (sceKernelGetProcessTimeWide() >= deadline) {
      *err_out = -SCE_NET_ETIMEDOUT;
      break;
    }
    sceKernelDelayThread(1000);
  }

  sceNetSocketClose(sock);
  return 0;
}

void host_tools_init(HostToolsCache *cache) {
  memset(cache, 0, sizeof(*cache));
}

HostToolResult *host_tools_get(HostToolsCache *cache, const char *ip) {
  for (uint32_t i = 0; i < cache->count; i++) {
    if (strcmp(cache->entries[i].ip, ip) == 0) return &cache->entries[i];
  }
  return NULL;
}

int host_tools_run_test(HostToolsCache *cache, const char *ip, HostToolTest test, uint32_t timeout_ms, uint64_t now_us) {
  HostToolResult *r = find_or_add(cache, ip);
  int err = 0;
  int rtt = -1;
  int ok = 0;

  if (test == HOST_TOOL_DNS) {
    ok = tcp_probe(ip, 53, timeout_ms, &rtt, &err);
    r->dns_ok = ok;
  } else if (test == HOST_TOOL_HTTP) {
    ok = tcp_probe(ip, 80, timeout_ms, &rtt, &err);
    r->http_ok = ok;
  } else if (test == HOST_TOOL_HTTPS) {
    ok = tcp_probe(ip, 443, timeout_ms, &rtt, &err);
    r->https_ok = ok;
  } else {
    ok = tcp_probe(ip, 80, timeout_ms, &rtt, &err);
    if (!ok) {
      ok = tcp_probe(ip, 443, timeout_ms, &rtt, &err);
    }
    if (!ok) {
      ok = tcp_probe(ip, 53, timeout_ms, &rtt, &err);
    }
    r->rtt_ms = ok ? rtt : -1;
  }

  r->last_error = err;
  r->updated_at_us = now_us;
  return ok ? 0 : -1;
}
