#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include <stdint.h>

#define WIFI_SCANNER_MAX_APS 48

typedef struct WifiApInfo {
  char     ssid[33];
  uint8_t  bssid[6];
  char     bssid_str[18];
  int8_t   rssi_dbm;
  uint8_t  channel;
  uint8_t  security;   /* 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3 */
  uint8_t  wps;
  uint64_t last_seen_us;
  uint8_t  is_new;
} WifiApInfo;

typedef struct WifiScannerMetrics {
  uint8_t  scanning;
  uint8_t  scan_done;
  uint32_t ap_count;
  uint32_t scan_count;
  WifiApInfo aps[WIFI_SCANNER_MAX_APS];
  int      last_error;
} WifiScannerMetrics;

typedef struct WifiScanner {
  WifiScannerMetrics metrics;
} WifiScanner;

void wifi_scanner_init(WifiScanner *s);
int  wifi_scanner_start_scan(WifiScanner *s);
void wifi_scanner_tick(WifiScanner *s, uint64_t now_us);
void wifi_scanner_get_metrics(const WifiScanner *s, WifiScannerMetrics *out);

#endif
