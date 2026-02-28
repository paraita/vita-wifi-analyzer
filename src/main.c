#include "net_monitor.h"
#include "render.h"
#include "latency_probe.h"
#include "lan_scanner.h"
#include "proxy_client.h"
#include "alerts.h"
#include "export_json.h"
#include "export_viewer.h"
#include "ui_audio.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vita2d.h>

enum {
  NET_MEMPOOL_SIZE = 1 * 1024 * 1024
};

static void apply_scan_profile(LanScannerConfig *cfg, ScanProfile profile) {
  lan_scanner_default_config(cfg);
  if (profile == SCAN_PROFILE_QUICK) {
    cfg->interval_ms_per_host = 12;
    cfg->connect_timeout_ms = 70;
    cfg->ports[0] = 53;
    cfg->ports[1] = 80;
    cfg->ports[2] = 443;
    cfg->ports[3] = 445;
  } else if (profile == SCAN_PROFILE_DEEP) {
    cfg->interval_ms_per_host = 30;
    cfg->connect_timeout_ms = 180;
    cfg->ports[0] = 22;
    cfg->ports[1] = 23;
    cfg->ports[2] = 53;
    cfg->ports[3] = 80;
    cfg->ports[4] = 139;
    cfg->ports[5] = 443;
    cfg->ports[6] = 445;
    cfg->ports[7] = 8080;
  }
}

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
  AlertManager alerts;
  alerts_init(&alerts);
  ExportViewer export_viewer;
  export_viewer_init(&export_viewer);
  UiAudio ui_audio;
  ui_audio_init(&ui_audio);

  const uint64_t poll_interval_us = 1000000ULL / NETMON_SAMPLE_HZ;
  uint64_t next_poll = 0;
  AppScreen screen = APP_SCREEN_RADAR;
  ScanDataSource scan_source = SCAN_SOURCE_LOCAL;
  int scan_scroll = 0;
  int selected_host_index = 0;
  int alerts_scroll = 0;
  int settings_index = 0;
  ScanProfile scan_profile = SCAN_PROFILE_NORMAL;
  int exports_scroll = 0;
  int exports_selected = 0;
  AppScreen prev_screen = screen;
  uint32_t prev_scan_round = 0;
  int prev_scan_error = 0;
  uint32_t prev_host_count = 0;
  char prev_hosts[LAN_SCANNER_MAX_HOSTS][16];
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
      if (screen == APP_SCREEN_SCAN) {
        screen = APP_SCREEN_EXPORTS;
      } else {
        screen = APP_SCREEN_SCAN;
      }
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

    if (screen != prev_screen && screen == APP_SCREEN_EXPORTS) {
      (void)export_json_rebuild_index();
      (void)export_viewer_reload(&export_viewer);
      exports_scroll = 0;
      exports_selected = 0;
    }
    if (screen != prev_screen) {
      ui_audio_event(&ui_audio, UI_AUDIO_NAV);
    }
    prev_screen = screen;

    if (scanner_metrics.scan_round != prev_scan_round) {
      int joined_count = 0;
      for (uint32_t i = 0; i < scanner_metrics.host_count; i++) {
        const char *ip = scanner_metrics.hosts[i].ip;
        int existed = 0;
        for (uint32_t j = 0; j < prev_host_count; j++) {
          if (strcmp(prev_hosts[j], ip) == 0) {
            existed = 1;
            break;
          }
        }
        if (!existed) {
          alerts_push(&alerts, now, ALERT_INFO, "Host joined: %s", ip);
          joined_count++;
        }
      }
      for (uint32_t j = 0; j < prev_host_count; j++) {
        int still_exists = 0;
        for (uint32_t i = 0; i < scanner_metrics.host_count; i++) {
          if (strcmp(prev_hosts[j], scanner_metrics.hosts[i].ip) == 0) {
            still_exists = 1;
            break;
          }
        }
        if (!still_exists) {
          alerts_push(&alerts, now, ALERT_WARN, "Host left: %s", prev_hosts[j]);
        }
      }
      prev_host_count = scanner_metrics.host_count;
      if (prev_host_count > LAN_SCANNER_MAX_HOSTS) prev_host_count = LAN_SCANNER_MAX_HOSTS;
      for (uint32_t i = 0; i < prev_host_count; i++) {
        snprintf(prev_hosts[i], sizeof(prev_hosts[i]), "%s", scanner_metrics.hosts[i].ip);
      }
      prev_scan_round = scanner_metrics.scan_round;
      if (joined_count > 0) {
        ui_audio_event(&ui_audio, UI_AUDIO_HOST_NEW);
      }
    }

    if (scanner_metrics.last_error != 0 && scanner_metrics.last_error != -60 && scanner_metrics.last_error != prev_scan_error) {
      alerts_push(&alerts, now, ALERT_ERROR, "Scanner error: %d (0x%08X)",
                  scanner_metrics.last_error, (unsigned int)scanner_metrics.last_error);
      ui_audio_event(&ui_audio, UI_AUDIO_ERROR);
    }
    prev_scan_error = scanner_metrics.last_error;

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
        ui_audio_event(&ui_audio, UI_AUDIO_SCAN_TOGGLE);
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
    } else if (screen == APP_SCREEN_ALERTS) {
      const int max_scroll = (alerts_count(&alerts) > 10U) ? (int)(alerts_count(&alerts) - 10U) : 0;
      if (pressed & SCE_CTRL_UP) {
        alerts_scroll--;
      }
      if (pressed & SCE_CTRL_DOWN) {
        alerts_scroll++;
      }
      if (pressed & SCE_CTRL_TRIANGLE) {
        alerts_clear(&alerts);
        alerts_scroll = 0;
      }
      if (alerts_scroll < 0) alerts_scroll = 0;
      if (alerts_scroll > max_scroll) alerts_scroll = max_scroll;
    } else if (screen == APP_SCREEN_SETTINGS) {
      if (pressed & SCE_CTRL_UP) {
        settings_index--;
      }
      if (pressed & SCE_CTRL_DOWN) {
        settings_index++;
      }
      if (settings_index < 0) settings_index = 0;
      if (settings_index > 3) settings_index = 3;

      if (pressed & SCE_CTRL_CROSS) {
        if (settings_index == 0) {
          scan_profile = (ScanProfile)((scan_profile + 1) % 3);
        } else if (settings_index == 1) {
          apply_scan_profile(&scanner_cfg, scan_profile);
          if (scanner.running) {
            lan_scanner_request_rescan(&scanner);
          }
          alerts_push(&alerts, now, ALERT_INFO, "Applied scan profile: %s",
                      (scan_profile == SCAN_PROFILE_QUICK) ? "Quick" :
                      (scan_profile == SCAN_PROFILE_DEEP) ? "Deep" : "Normal");
        } else if (settings_index == 2) {
          char out_path[128];
          const int rc = export_json_write_snapshot(&scanner_metrics, now, out_path, sizeof(out_path));
          if (rc == 0) {
            (void)export_json_rebuild_index();
            alerts_push(&alerts, now, ALERT_INFO, "Export saved: %s", out_path);
            ui_audio_event(&ui_audio, UI_AUDIO_EXPORT_OK);
          } else {
            alerts_push(&alerts, now, ALERT_ERROR, "Export failed: %d", rc);
            ui_audio_event(&ui_audio, UI_AUDIO_EXPORT_FAIL);
          }
        } else if (settings_index == 3) {
          screen = APP_SCREEN_EXPORTS;
        }
      }
    } else if (screen == APP_SCREEN_EXPORTS) {
      const int max_scroll = (export_viewer.count > 10U) ? (int)(export_viewer.count - 10U) : 0;
      if (pressed & SCE_CTRL_UP) {
        exports_selected--;
      }
      if (pressed & SCE_CTRL_DOWN) {
        exports_selected++;
      }
      if (pressed & SCE_CTRL_LEFT) {
        exports_scroll -= 3;
      }
      if (pressed & SCE_CTRL_RIGHT) {
        exports_scroll += 3;
      }
      if (pressed & SCE_CTRL_TRIANGLE) {
        char out_path[128];
        const int rc = export_json_write_snapshot(&scanner_metrics, now, out_path, sizeof(out_path));
        if (rc == 0) {
          (void)export_json_rebuild_index();
          (void)export_viewer_reload(&export_viewer);
          alerts_push(&alerts, now, ALERT_INFO, "Export saved: %s", out_path);
          ui_audio_event(&ui_audio, UI_AUDIO_EXPORT_OK);
        } else {
          alerts_push(&alerts, now, ALERT_ERROR, "Export failed: %d", rc);
          ui_audio_event(&ui_audio, UI_AUDIO_EXPORT_FAIL);
        }
      }

      if (exports_selected < 0) exports_selected = 0;
      if (exports_selected >= (int)export_viewer.count) exports_selected = (int)export_viewer.count - 1;
      if (exports_selected < 0) exports_selected = 0;
      if (exports_scroll < 0) exports_scroll = 0;
      if (exports_scroll > max_scroll) exports_scroll = max_scroll;
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
                 &alerts, scan_source, scan_scroll, selected_host_index, alerts_scroll, settings_index, scan_profile,
                 &export_viewer, exports_scroll, exports_selected, screen, font, now);
  }

  proxy_client_stop(&proxy);
  ui_audio_term(&ui_audio);
  lan_scanner_stop(&scanner);
  latency_probe_stop(&probe);
  vita2d_free_pgf(font);
  vita2d_fini();
  term_network(net_mempool);
  sceKernelExitProcess(0);
  return 0;
}
