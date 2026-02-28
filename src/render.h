#ifndef RENDER_H
#define RENDER_H

#include "net_monitor.h"
#include "latency_probe.h"
#include "lan_scanner.h"
#include "proxy_client.h"
#include "alerts.h"
#include "export_viewer.h"
#include <vita2d.h>

typedef enum AppScreen {
  APP_SCREEN_RADAR = 0,
  APP_SCREEN_STATS = 1,
  APP_SCREEN_SCAN = 2,
  APP_SCREEN_HOST_DETAIL = 3,
  APP_SCREEN_ALERTS = 4,
  APP_SCREEN_SETTINGS = 5,
  APP_SCREEN_EXPORTS = 6,
  APP_SCREEN_COUNT = 7
} AppScreen;

typedef enum ScanDataSource {
  SCAN_SOURCE_LOCAL = 0,
  SCAN_SOURCE_PROXY = 1
} ScanDataSource;

typedef enum ScanProfile {
  SCAN_PROFILE_QUICK = 0,
  SCAN_PROFILE_NORMAL = 1,
  SCAN_PROFILE_DEEP = 2
} ScanProfile;

void render_frame(const NetMonitor *monitor,
                  const LatencyProbeMetrics *latency,
                  const LanScannerMetrics *scanner,
                  const ProxyClientMetrics *proxy,
                  const AlertManager *alerts,
                  ScanDataSource scan_source,
                  int scan_scroll,
                  int selected_host_index,
                  int alerts_scroll,
                  int settings_index,
                  ScanProfile scan_profile,
                  int audio_enabled,
                  const ExportViewer *export_viewer,
                  int exports_scroll,
                  int exports_selected,
                  AppScreen screen,
                  vita2d_pgf *font,
                  uint64_t now_us);

#endif
