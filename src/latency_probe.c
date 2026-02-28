#include "latency_probe.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum {
  PROBE_THREAD_PRIORITY = 0x10000120,
  PROBE_THREAD_STACK_SIZE = 0x10000
};

static void push_history(LatencyProbe *probe, int success, int rtt_ms) {
  probe->recent_ok[probe->history_head] = success ? 1U : 0U;
  probe->recent_rtt_ms[probe->history_head] = success ? rtt_ms : -1;
  probe->history_head = (probe->history_head + 1U) % LATENCY_PROBE_HISTORY_CAPACITY;
  if (probe->history_count < LATENCY_PROBE_HISTORY_CAPACITY) {
    probe->history_count++;
  }
}

static int sort_int_asc(const void *a, const void *b) {
  const int ia = *(const int *)a;
  const int ib = *(const int *)b;
  if (ia < ib) return -1;
  if (ia > ib) return 1;
  return 0;
}

static int percentile_from_sorted(const int *values, int count, float p) {
  if (count <= 0) {
    return -1;
  }
  if (count == 1) {
    return values[0];
  }
  const float pos = p * (float)(count - 1);
  int idx = (int)(pos + 0.5f);
  if (idx < 0) idx = 0;
  if (idx >= count) idx = count - 1;
  return values[idx];
}

static int perform_tcp_probe(const LatencyProbeConfig *cfg, int *rtt_ms_out, int *err_out) {
  int result = -1;
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

  const uint64_t t0 = (uint64_t)sceKernelGetProcessTimeWide();

  sock = sceNetSocket("lat_tcp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
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
      *err_out = -err;
      goto cleanup;
    }
  }

  ep = sceNetEpollCreate("lat_ep", 0);
  if (ep < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  SceNetEpollEvent add_ev;
  memset(&add_ev, 0, sizeof(add_ev));
  add_ev.events = SCE_NET_EPOLLOUT | SCE_NET_EPOLLERR | SCE_NET_EPOLLHUP;
  add_ev.data.fd = sock;

  if (sceNetEpollControl(ep, SCE_NET_EPOLL_CTL_ADD, sock, &add_ev) < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  SceNetEpollEvent events[1];
  memset(events, 0, sizeof(events));
  rc = sceNetEpollWait(ep, events, 1, (int)cfg->timeout_ms);
  if (rc <= 0) {
    *err_out = -SCE_NET_ETIMEDOUT;
    goto cleanup;
  }

  int so_error = 0;
  unsigned int so_len = sizeof(so_error);
  if (sceNetGetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &so_error, &so_len) < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }
  if (so_error != 0) {
    *err_out = -so_error;
    goto cleanup;
  }

  {
    const uint64_t t1 = (uint64_t)sceKernelGetProcessTimeWide();
    *rtt_ms_out = (int)((t1 - t0) / 1000ULL);
  }
  *err_out = 0;
  result = 0;

cleanup:
  if (ep >= 0) {
    sceNetEpollDestroy(ep);
  }
  if (sock >= 0) {
    sceNetSocketClose(sock);
  }
  return result;
}

static int perform_udp_probe(const LatencyProbeConfig *cfg, int *rtt_ms_out, int *err_out) {
  int result = -1;
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

  const uint64_t t0 = (uint64_t)sceKernelGetProcessTimeWide();

  sock = sceNetSocket("lat_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP);
  if (sock < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  if (sceNetConnect(sock, (const SceNetSockaddr *)&addr, sizeof(addr)) < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  ep = sceNetEpollCreate("lat_ep", 0);
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

  unsigned int payload = 0xA55A1234;
  if (sceNetSend(sock, &payload, sizeof(payload), 0) < 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  SceNetEpollEvent events[1];
  memset(events, 0, sizeof(events));
  int rc = sceNetEpollWait(ep, events, 1, (int)cfg->timeout_ms);
  if (rc <= 0) {
    *err_out = -SCE_NET_ETIMEDOUT;
    goto cleanup;
  }

  unsigned int response = 0;
  rc = sceNetRecv(sock, &response, sizeof(response), 0);
  if (rc <= 0) {
    *err_out = -*sceNetErrnoLoc();
    goto cleanup;
  }

  {
    const uint64_t t1 = (uint64_t)sceKernelGetProcessTimeWide();
    *rtt_ms_out = (int)((t1 - t0) / 1000ULL);
  }
  *err_out = 0;
  result = 0;

cleanup:
  if (ep >= 0) {
    sceNetEpollDestroy(ep);
  }
  if (sock >= 0) {
    sceNetSocketClose(sock);
  }
  return result;
}

static int probe_thread_main(SceSize args, void *argp) {
  (void)args;
  LatencyProbe *probe = (LatencyProbe *)argp;

  probe->running = 1;
  while (!probe->stop_requested) {
    int rtt_ms = -1;
    int err = 0;
    int rc = -1;

    if (probe->config.protocol == LATENCY_PROTOCOL_UDP) {
      rc = perform_udp_probe(&probe->config, &rtt_ms, &err);
    } else {
      rc = perform_tcp_probe(&probe->config, &rtt_ms, &err);
    }

    probe->probes_sent++;
    if (rc == 0) {
      probe->probes_ok++;
      push_history(probe, 1, rtt_ms);
      probe->last_rtt_ms = rtt_ms;
      if (probe->probes_ok == 1) {
        probe->min_rtt_ms = rtt_ms;
        probe->max_rtt_ms = rtt_ms;
        probe->avg_rtt_ms = (float)rtt_ms;
        probe->ema_rtt_ms = (float)rtt_ms;
      } else {
        if (rtt_ms < probe->min_rtt_ms) {
          probe->min_rtt_ms = rtt_ms;
        }
        if (rtt_ms > probe->max_rtt_ms) {
          probe->max_rtt_ms = rtt_ms;
        }
        probe->avg_rtt_ms = ((probe->avg_rtt_ms * (float)(probe->probes_ok - 1)) + (float)rtt_ms) /
                            (float)probe->probes_ok;
        probe->ema_rtt_ms = 0.3f * (float)rtt_ms + 0.7f * probe->ema_rtt_ms;
      }
      probe->last_error = 0;
    } else {
      push_history(probe, 0, -1);
      probe->last_error = err;
    }

    uint32_t sleep_left = probe->config.interval_ms;
    while (!probe->stop_requested && sleep_left > 0) {
      const uint32_t step = (sleep_left > 25U) ? 25U : sleep_left;
      sceKernelDelayThread(step * 1000U);
      sleep_left -= step;
    }
  }

  probe->running = 0;
  return sceKernelExitDeleteThread(0);
}

void latency_probe_default_config(LatencyProbeConfig *config) {
  memset(config, 0, sizeof(*config));
  snprintf(config->target_ip, sizeof(config->target_ip), "192.168.1.1");
  config->target_port = 53;
  config->protocol = LATENCY_PROTOCOL_TCP;
  config->interval_ms = 1000;
  config->timeout_ms = 250;
}

void latency_probe_init(LatencyProbe *probe) {
  memset(probe, 0, sizeof(*probe));
  probe->thread_id = -1;
  probe->last_rtt_ms = -1;
  probe->min_rtt_ms = -1;
  probe->max_rtt_ms = -1;
  for (uint32_t i = 0; i < LATENCY_PROBE_HISTORY_CAPACITY; i++) {
    probe->recent_rtt_ms[i] = -1;
  }
  latency_probe_default_config(&probe->config);
}

int latency_probe_start(LatencyProbe *probe, const LatencyProbeConfig *config) {
  if (probe->thread_id >= 0) {
    return 0;
  }
  if (config != NULL) {
    probe->config = *config;
  }
  probe->stop_requested = 0;

  probe->thread_id = sceKernelCreateThread("latency_probe", probe_thread_main, PROBE_THREAD_PRIORITY,
                                           PROBE_THREAD_STACK_SIZE, 0, 0, NULL);
  if (probe->thread_id < 0) {
    return probe->thread_id;
  }

  const int rc = sceKernelStartThread(probe->thread_id, sizeof(probe), probe);
  if (rc < 0) {
    sceKernelDeleteThread(probe->thread_id);
    probe->thread_id = -1;
    return rc;
  }
  return 0;
}

void latency_probe_stop(LatencyProbe *probe) {
  if (probe->thread_id < 0) {
    return;
  }
  probe->stop_requested = 1;
  sceKernelWaitThreadEnd(probe->thread_id, NULL, NULL);
  sceKernelDeleteThread(probe->thread_id);
  probe->thread_id = -1;
}

void latency_probe_get_metrics(const LatencyProbe *probe, LatencyProbeMetrics *out) {
  memset(out, 0, sizeof(*out));
  out->enabled = (probe->thread_id >= 0);
  out->running = (probe->running != 0);
  out->protocol = probe->config.protocol;
  out->target_port = probe->config.target_port;
  snprintf(out->target_ip, sizeof(out->target_ip), "%s", probe->config.target_ip);

  out->last_rtt_ms = probe->last_rtt_ms;
  out->ema_rtt_ms = probe->ema_rtt_ms;
  out->min_rtt_ms = probe->min_rtt_ms;
  out->max_rtt_ms = probe->max_rtt_ms;
  out->avg_rtt_ms = probe->avg_rtt_ms;
  out->probes_sent = probe->probes_sent;
  out->probes_ok = probe->probes_ok;
  out->last_error = probe->last_error;
  out->recent_count = probe->history_count;

  if (out->probes_sent > 0) {
    const float lost = (float)(out->probes_sent - out->probes_ok);
    out->loss_percent = (lost * 100.0f) / (float)out->probes_sent;
  }

  if (probe->history_count > 0) {
    const uint32_t oldest =
      (probe->history_head + LATENCY_PROBE_HISTORY_CAPACITY - probe->history_count) %
      LATENCY_PROBE_HISTORY_CAPACITY;
    uint32_t recent_ok_count = 0;
    int successful_rtt[LATENCY_PROBE_HISTORY_CAPACITY];
    int successful_count = 0;
    for (uint32_t i = 0; i < probe->history_count; i++) {
      const uint32_t idx = (oldest + i) % LATENCY_PROBE_HISTORY_CAPACITY;
      const uint8_t ok = probe->recent_ok[idx];
      out->recent_ok[i] = ok;
      if (ok) {
        recent_ok_count++;
        if (successful_count < LATENCY_PROBE_HISTORY_CAPACITY) {
          successful_rtt[successful_count++] = probe->recent_rtt_ms[idx];
        }
      }
    }
    out->recent_loss_percent = 100.0f - ((float)recent_ok_count * 100.0f / (float)probe->history_count);

    if (successful_count > 0) {
      qsort(successful_rtt, (size_t)successful_count, sizeof(successful_rtt[0]), sort_int_asc);
      out->rtt_p50_ms = percentile_from_sorted(successful_rtt, successful_count, 0.50f);
      out->rtt_p95_ms = percentile_from_sorted(successful_rtt, successful_count, 0.95f);
    } else {
      out->rtt_p50_ms = -1;
      out->rtt_p95_ms = -1;
    }
  } else {
    out->rtt_p50_ms = -1;
    out->rtt_p95_ms = -1;
    out->recent_loss_percent = 0.0f;
  }
}
