#include "wifi_scanner.h"

#include <stdio.h>
#include <string.h>

/* SceWlanDrv types.  No AP-scan stub is available in the baseline VitaSDK,
   so we provide local fallback stubs that return -1 (hardware unavailable).
   Link against SceWlanDrv_stub and remove the statics if the real library
   becomes available. */
typedef struct SceWlanDrvScanResult {
  char    ssid[33];
  uint8_t bssid[6];
  int8_t  rssi;
  uint8_t channel;
  uint8_t security_mode;
  uint8_t wps_enable;
  uint8_t _pad[10];
} SceWlanDrvScanResult;

static int sceWlanDrvScan(int count, SceWlanDrvScanResult *buf)            { (void)count; (void)buf; return -1; }
static int sceWlanDrvGetScanResult(int *count, SceWlanDrvScanResult *buf)  { (void)count; (void)buf; return -1; }

/* Temporary scan buffer (fixed, no heap) */
static SceWlanDrvScanResult s_scan_buf[WIFI_SCANNER_MAX_APS];

static void format_bssid(const uint8_t bssid[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static int bssid_eq(const uint8_t a[6], const uint8_t b[6]) {
  for (int i = 0; i < 6; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

void wifi_scanner_init(WifiScanner *s) {
  memset(s, 0, sizeof(*s));
}

int wifi_scanner_start_scan(WifiScanner *s) {
  if (s->metrics.scanning) {
    return 0; /* already in progress */
  }
  /* Clear 'is_new' flags from previous scan */
  for (uint32_t i = 0; i < s->metrics.ap_count; i++) {
    s->metrics.aps[i].is_new = 0;
  }
  memset(s_scan_buf, 0, sizeof(s_scan_buf));
  const int rc = sceWlanDrvScan(WIFI_SCANNER_MAX_APS, s_scan_buf);
  if (rc < 0) {
    s->metrics.last_error = rc;
    return rc;
  }
  s->metrics.scanning  = 1;
  s->metrics.scan_done = 0;
  return 0;
}

void wifi_scanner_tick(WifiScanner *s, uint64_t now_us) {
  (void)now_us;
  if (!s->metrics.scanning) {
    return;
  }
  int count = WIFI_SCANNER_MAX_APS;
  const int rc = sceWlanDrvGetScanResult(&count, s_scan_buf);
  if (rc < 0) {
    /* Results not ready yet — try next tick */
    if (rc != -1) { /* -1 == EAGAIN-like; don't overwrite last_error constantly */
      s->metrics.last_error = rc;
    }
    return;
  }

  /* Results ready — parse into aps[] */
  if (count < 0 || count > WIFI_SCANNER_MAX_APS) {
    count = WIFI_SCANNER_MAX_APS;
  }

  /* Save old BSSIDs to detect new/evil-twin entries */
  char old_ssids[WIFI_SCANNER_MAX_APS][33];
  uint8_t old_bssids[WIFI_SCANNER_MAX_APS][6];
  const uint32_t old_count = s->metrics.ap_count;
  for (uint32_t i = 0; i < old_count; i++) {
    snprintf(old_ssids[i], sizeof(old_ssids[i]), "%s", s->metrics.aps[i].ssid);
    memcpy(old_bssids[i], s->metrics.aps[i].bssid, 6);
  }

  s->metrics.ap_count = 0;
  for (int i = 0; i < count; i++) {
    const SceWlanDrvScanResult *r = &s_scan_buf[i];
    if (r->ssid[0] == '\0' && r->bssid[0] == 0) {
      continue; /* empty slot */
    }
    if (s->metrics.ap_count >= WIFI_SCANNER_MAX_APS) {
      break;
    }
    WifiApInfo *ap = &s->metrics.aps[s->metrics.ap_count++];
    memset(ap, 0, sizeof(*ap));
    memcpy(ap->ssid, r->ssid, sizeof(ap->ssid));
    ap->ssid[sizeof(ap->ssid) - 1U] = '\0';
    memcpy(ap->bssid, r->bssid, 6);
    format_bssid(ap->bssid, ap->bssid_str);
    ap->rssi_dbm = r->rssi;
    ap->channel  = r->channel;
    ap->security = r->security_mode;
    ap->wps      = r->wps_enable;
    ap->last_seen_us = 0; /* set below */

    /* Determine if new (BSSID not seen before) */
    ap->is_new = 1;
    for (uint32_t j = 0; j < old_count; j++) {
      if (bssid_eq(ap->bssid, old_bssids[j])) {
        ap->is_new = 0;
        break;
      }
    }
    /* Detect evil twin: same SSID but different BSSID that's new */
    if (ap->is_new && ap->ssid[0] != '\0') {
      for (uint32_t j = 0; j < old_count; j++) {
        if (strcmp(ap->ssid, old_ssids[j]) == 0 &&
            !bssid_eq(ap->bssid, old_bssids[j])) {
          /* SSID collision with different BSSID — keep is_new=1 for alert */
          break;
        }
      }
    }
  }

  s->metrics.scanning  = 0;
  s->metrics.scan_done = 1;
  s->metrics.scan_count++;
}

void wifi_scanner_get_metrics(const WifiScanner *s, WifiScannerMetrics *out) {
  memcpy(out, &s->metrics, sizeof(*out));
}
