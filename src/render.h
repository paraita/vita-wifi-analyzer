#ifndef RENDER_H
#define RENDER_H

#include "net_monitor.h"
#include "latency_probe.h"
#include "lan_scanner.h"
#include "proxy_client.h"
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

void render_frame(const NetMonitor *monitor,
                  const LatencyProbeMetrics *latency,
                  const LanScannerMetrics *scanner,
                  const ProxyClientMetrics *proxy,
                  ScanDataSource scan_source,
                  int scan_scroll,
                  int selected_host_index,
                  AppScreen screen,
                  vita2d_pgf *font,
                  uint64_t now_us);

#endif
