#ifndef RENDER_H
#define RENDER_H

#include "net_monitor.h"
#include "latency_probe.h"
#include "lan_scanner.h"
#include "proxy_client.h"
#include "alerts.h"
#include "export_viewer.h"
#include "bt_monitor.h"
#include "host_tools.h"
#include "scan_history.h"
#include "alert_rules.h"
#include "bt_store.h"
#include "ui_fx.h"
#include <vita2d.h>

typedef enum AppScreen {
  APP_SCREEN_RADAR = 0,
  APP_SCREEN_STATS = 1,
  APP_SCREEN_SCAN = 2,
  APP_SCREEN_HOST_DETAIL = 3,
  APP_SCREEN_HOST_TOOLS = 4,
  APP_SCREEN_TIMELINE = 5,
  APP_SCREEN_MAP = 6,
  APP_SCREEN_ALERTS = 7,
  APP_SCREEN_RULES = 8,
  APP_SCREEN_SETTINGS = 9,
  APP_SCREEN_BT = 10,
  APP_SCREEN_EXPORTS = 11,
  APP_SCREEN_COUNT = 12
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

typedef enum ScanFilterMode {
  SCAN_FILTER_ALL = 0,
  SCAN_FILTER_GATEWAY = 1,
  SCAN_FILTER_NAMED = 2,
  SCAN_FILTER_PORTS = 3
} ScanFilterMode;

typedef enum ScanSortMode {
  SCAN_SORT_IP = 0,
  SCAN_SORT_NAME = 1,
  SCAN_SORT_PORTS = 2,
  SCAN_SORT_SOURCE = 3
} ScanSortMode;

void render_frame(const NetMonitor *monitor,
                  const LatencyProbeMetrics *latency,
                  const LanScannerMetrics *scanner,
                  const ProxyClientMetrics *proxy,
                  const AlertManager *alerts,
                  const BtMonitorMetrics *bt,
                  const BtStore *bt_store,
                  const ScanHistory *history,
                  const HostToolsCache *host_tools,
                  const AlertRuleConfig *alert_rules,
                  const UiFxState *ui_fx,
                  ScanDataSource scan_source,
                  const int *scan_view_indices,
                  int scan_view_count,
                  ScanFilterMode scan_filter,
                  ScanSortMode scan_sort,
                  int scan_scroll,
                  int selected_host_index,
                  int host_detail_index,
                  int timeline_scroll,
                  int timeline_selected,
                  int map_selected,
                  int host_tools_selected,
                  int alerts_scroll,
                  int settings_index,
                  int rules_index,
                  ScanProfile scan_profile,
                  int audio_enabled,
                  const ExportViewer *export_viewer,
                  int exports_scroll,
                  int exports_selected,
                  int exports_compare_index,
                  AppScreen screen,
                  vita2d_pgf *font,
                  uint64_t now_us);

#endif
