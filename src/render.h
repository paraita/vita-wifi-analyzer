#ifndef RENDER_H
#define RENDER_H

#include "net_monitor.h"
#include "latency_probe.h"
#include "lan_scanner.h"
#include "proxy_client.h"
#include "alerts.h"
#include "export_viewer.h"
#include "bt_monitor.h"
#include "nfc_scanner.h"
#include "wifi_scanner.h"
#include <vita2d.h>

typedef enum AppScreen {
  APP_SCREEN_RADAR       = 0,
  APP_SCREEN_STATS       = 1,
  APP_SCREEN_SCAN        = 2,
  APP_SCREEN_HOST_DETAIL = 3,
  APP_SCREEN_ALERTS      = 4,
  APP_SCREEN_SETTINGS    = 5,
  APP_SCREEN_BT          = 6,
  APP_SCREEN_NFC         = 7,
  APP_SCREEN_WIFI_AP     = 8,
  APP_SCREEN_EXPORTS     = 9,
  APP_SCREEN_COUNT       = 10
} AppScreen;

typedef enum ScanDataSource {
  SCAN_SOURCE_LOCAL = 0,
  SCAN_SOURCE_PROXY = 1
} ScanDataSource;

typedef enum ScanProfile {
  SCAN_PROFILE_QUICK  = 0,
  SCAN_PROFILE_NORMAL = 1,
  SCAN_PROFILE_DEEP   = 2
} ScanProfile;

typedef enum ScanFilterMode {
  SCAN_FILTER_ALL     = 0,
  SCAN_FILTER_GATEWAY = 1,
  SCAN_FILTER_NAMED   = 2,
  SCAN_FILTER_PORTS   = 3
} ScanFilterMode;

typedef enum ScanSortMode {
  SCAN_SORT_IP     = 0,
  SCAN_SORT_NAME   = 1,
  SCAN_SORT_PORTS  = 2,
  SCAN_SORT_SOURCE = 3
} ScanSortMode;

void render_frame(const NetMonitor *monitor,
                  const LatencyProbeMetrics *latency,
                  const LanScannerMetrics *scanner,
                  const ProxyClientMetrics *proxy,
                  const AlertManager *alerts,
                  const BtMonitorMetrics *bt,
                  ScanDataSource scan_source,
                  const int *scan_view_indices,
                  int scan_view_count,
                  ScanFilterMode scan_filter,
                  ScanSortMode scan_sort,
                  int scan_scroll,
                  int selected_host_index,
                  int host_detail_index,
                  int alerts_scroll,
                  int settings_index,
                  ScanProfile scan_profile,
                  int audio_enabled,
                  const ExportViewer *export_viewer,
                  int exports_scroll,
                  int exports_selected,
                  int exports_compare_index,
                  const NfcScannerMetrics *nfc,
                  int nfc_scroll,
                  const WifiScannerMetrics *wifi_ap,
                  int wifi_scroll,
                  AppScreen screen,
                  vita2d_pgf *font,
                  uint64_t now_us);

#endif
