#include "net_monitor.h"
#include "render.h"
#include "latency_probe.h"
#include "lan_scanner.h"
#include "proxy_client.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdbool.h>
#include <stdlib.h>
#include <vita2d.h>

enum {
  NET_MEMPOOL_SIZE = 1 * 1024 * 1024
};

static int init_network(void **mempool_out) {
  int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  if (ret < 0) {
    return ret;
  }

  *mempool_out = malloc(NET_MEMPOOL_SIZE);
  if (*mempool_out == NULL) {
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    return -1;
  }

  SceNetInitParam net_param;
  net_param.memory = *mempool_out;
  net_param.size = NET_MEMPOOL_SIZE;
  net_param.flags = 0;

  ret = sceNetInit(&net_param);
  if (ret < 0) {
    free(*mempool_out);
    *mempool_out = NULL;
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    return ret;
  }

  ret = sceNetCtlInit();
  if (ret < 0) {
    sceNetTerm();
    free(*mempool_out);
    *mempool_out = NULL;
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    return ret;
  }

  return 0;
}

static void term_network(void *mempool) {
  sceNetCtlTerm();
  sceNetTerm();
  free(mempool);
  sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
}

int main(void) {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

  void *net_mempool = NULL;
  if (init_network(&net_mempool) < 0) {
    sceKernelExitProcess(1);
    return 1;
  }

  vita2d_init();
  vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
  vita2d_pgf *font = vita2d_load_default_pgf();
  if (font == NULL) {
    vita2d_fini();
    term_network(net_mempool);
    sceKernelExitProcess(1);
    return 1;
  }

  NetMonitor monitor;
  net_monitor_init(&monitor);

  LatencyProbe probe;
  latency_probe_init(&probe);
  LatencyProbeConfig probe_cfg;
  latency_probe_default_config(&probe_cfg);
  latency_probe_start(&probe, &probe_cfg);

  LanScanner scanner;
  lan_scanner_init(&scanner);
  LanScannerConfig scanner_cfg;
  lan_scanner_default_config(&scanner_cfg);

  ProxyClient proxy;
  proxy_client_init(&proxy);

  const uint64_t poll_interval_us = 1000000ULL / NETMON_SAMPLE_HZ;
  uint64_t next_poll = 0;
  AppScreen screen = APP_SCREEN_RADAR;
  ScanDataSource scan_source = SCAN_SOURCE_LOCAL;
  int scan_scroll = 0;
  int selected_host_index = 0;
  bool local_scan_armed = true;
  unsigned int prev_buttons = 0;
  bool running = true;

  while (running) {
    const uint64_t now = sceKernelGetProcessTimeWide();
    if (now >= next_poll) {
      net_monitor_poll(&monitor, now);
      lan_scanner_set_ip_hint(&scanner, monitor.ip_address);
      lan_scanner_set_gateway_hint(&scanner, monitor.default_route);
      next_poll = now + poll_interval_us;
    }

    lan_scanner_tick(&scanner, now);

    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    const unsigned int pressed = pad.buttons & ~prev_buttons;
    prev_buttons = pad.buttons;

    if (pressed & SCE_CTRL_LTRIGGER) {
      screen = (AppScreen)((screen + APP_SCREEN_COUNT - 1) % APP_SCREEN_COUNT);
    }
    if (pressed & SCE_CTRL_RTRIGGER) {
      screen = (AppScreen)((screen + 1) % APP_SCREEN_COUNT);
    }
    if (pressed & SCE_CTRL_SQUARE) {
      screen = APP_SCREEN_SCAN;
    }
    if (pad.buttons & SCE_CTRL_START) {
      running = false;
    }

    LatencyProbeMetrics latency_metrics;
    latency_probe_get_metrics(&probe, &latency_metrics);

    LanScannerMetrics scanner_metrics;
    lan_scanner_get_metrics(&scanner, &scanner_metrics);

    ProxyClientMetrics proxy_metrics;
    proxy_client_get_metrics(&proxy, &proxy_metrics);

    if (screen == APP_SCREEN_SCAN) {
      if (local_scan_armed && !scanner.running) {
        (void)lan_scanner_start(&scanner, &scanner_cfg);
        lan_scanner_set_ip_hint(&scanner, monitor.ip_address);
        local_scan_armed = false;
      }

      scan_source = SCAN_SOURCE_LOCAL;

      const uint32_t host_count = (scan_source == SCAN_SOURCE_PROXY) ? proxy_metrics.host_count
                                                                      : scanner_metrics.host_count;
      const int max_scroll = (host_count > 10U) ? (int)(host_count - 10U) : 0;

      if (pressed & SCE_CTRL_UP) {
        scan_scroll--;
        selected_host_index = scan_scroll;
      }
      if (pressed & SCE_CTRL_DOWN) {
        scan_scroll++;
        selected_host_index = scan_scroll;
      }
      if (pressed & SCE_CTRL_SELECT) {
        if (scanner.running) {
          lan_scanner_request_rescan(&scanner);
        } else {
          local_scan_armed = true;
        }
        scan_scroll = 0;
      }
      if (pressed & SCE_CTRL_TRIANGLE) {
        if (scanner.running) {
          local_scan_armed = false;
          lan_scanner_stop(&scanner);
        } else {
          local_scan_armed = true;
        }
        scan_scroll = 0;
      }

      if (scan_scroll < 0) {
        scan_scroll = 0;
      } else if (scan_scroll > max_scroll) {
        scan_scroll = max_scroll;
      }
      if (selected_host_index < scan_scroll) {
        selected_host_index = scan_scroll;
      }
      if (selected_host_index > scan_scroll + 7) {
        selected_host_index = scan_scroll + 7;
      }
      if (pressed & SCE_CTRL_CROSS) {
        if (scanner_metrics.host_count > 0) {
          if ((uint32_t)selected_host_index >= scanner_metrics.host_count) {
            selected_host_index = (int)scanner_metrics.host_count - 1;
          }
          screen = APP_SCREEN_HOST_DETAIL;
        }
      }
    } else if (screen == APP_SCREEN_HOST_DETAIL) {
      if (pressed & SCE_CTRL_CIRCLE) {
        screen = APP_SCREEN_SCAN;
      }
    } else {
      if (scanner.running) {
        lan_scanner_stop(&scanner);
      }
    }

    render_frame(&monitor, &latency_metrics, &scanner_metrics, &proxy_metrics,
                 scan_source, scan_scroll, selected_host_index, screen, font, now);
  }

  proxy_client_stop(&proxy);
  lan_scanner_stop(&scanner);
  latency_probe_stop(&probe);
  vita2d_free_pgf(font);
  vita2d_fini();
  term_network(net_mempool);
  sceKernelExitProcess(0);
  return 0;
}
