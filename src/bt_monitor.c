#include "bt_monitor.h"

#include <stdio.h>
#include <string.h>

typedef struct SceBtRegisteredInfo {
  unsigned char  mac[6];
  unsigned short unk0;
  unsigned int   bt_class;
  unsigned int   unk1;
  unsigned int   unk2;
  unsigned short vid;
  unsigned short pid;
  unsigned int   unk3;
  unsigned int   unk4;
  char           name[128];
  unsigned char  unk5[0x60];
} SceBtRegisteredInfo;

typedef struct SceBtEvent {
  union {
    unsigned char data[0x10];
    struct {
      unsigned char  id;
      unsigned char  unk1;
      unsigned short unk2;
      unsigned int   unk3;
      unsigned int   mac0;
      unsigned int   mac1;
    };
  };
} SceBtEvent;

int sceBtGetConfiguration(void);
int sceBtGetLastError(void);
int sceBtGetRegisteredInfo(int device, int unk, SceBtRegisteredInfo *info, unsigned int info_size);
int sceBtReadEvent(SceBtEvent *events, int num_events);
int sceBtStartInquiry(void);
int sceBtStopInquiry(void);

static void format_mac_from_bytes(char out[18], const unsigned char mac[6]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void format_mac_from_event(char out[18], unsigned int mac0, unsigned int mac1) {
  const unsigned char b0 = (unsigned char)((mac0 >> 0) & 0xFFU);
  const unsigned char b1 = (unsigned char)((mac0 >> 8) & 0xFFU);
  const unsigned char b2 = (unsigned char)((mac0 >> 16) & 0xFFU);
  const unsigned char b3 = (unsigned char)((mac0 >> 24) & 0xFFU);
  const unsigned char b4 = (unsigned char)((mac1 >> 0) & 0xFFU);
  const unsigned char b5 = (unsigned char)((mac1 >> 8) & 0xFFU);
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b0, b1, b2, b3, b4, b5);
}

static void refresh_paired(BtMonitor *monitor) {
  monitor->metrics.paired_count = 0;
  for (int i = 0; i < BT_MONITOR_MAX_PAIRED; i++) {
    SceBtRegisteredInfo info;
    memset(&info, 0, sizeof(info));
    const int rc = sceBtGetRegisteredInfo(i, 0, &info, (unsigned int)sizeof(info));
    if (rc < 0) {
      if (i == 0) {
        monitor->metrics.last_error = rc;
      }
      break;
    }

    BtPairedDevice *d = &monitor->metrics.paired[monitor->metrics.paired_count++];
    memset(d, 0, sizeof(*d));
    format_mac_from_bytes(d->mac, info.mac);
    snprintf(d->name, sizeof(d->name), "%s", info.name[0] ? info.name : "(unknown)");
    d->bt_class = info.bt_class;
  }
}

static void push_event(BtMonitor *monitor, uint8_t event_id, const char *mac, uint64_t now_us) {
  if (monitor->metrics.event_count >= BT_MONITOR_MAX_EVENTS) {
    memmove(&monitor->metrics.events[0], &monitor->metrics.events[1],
            sizeof(BtEventEntry) * (BT_MONITOR_MAX_EVENTS - 1));
    monitor->metrics.event_count = BT_MONITOR_MAX_EVENTS - 1;
  }

  BtEventEntry *dst = &monitor->metrics.events[monitor->metrics.event_count++];
  memset(dst, 0, sizeof(*dst));
  dst->event_id = event_id;
  dst->timestamp_us = now_us;
  snprintf(dst->mac, sizeof(dst->mac), "%s", mac);
}

void bt_monitor_init(BtMonitor *monitor) {
  memset(monitor, 0, sizeof(*monitor));
}

void bt_monitor_poll(BtMonitor *monitor, uint64_t now_us) {
  const int cfg = sceBtGetConfiguration();
  if (cfg < 0) {
    monitor->metrics.api_available = 0;
    monitor->metrics.bt_enabled = 0;
    monitor->metrics.last_error = cfg;
    return;
  }

  monitor->metrics.api_available = 1;
  monitor->metrics.bt_enabled = (cfg != 0) ? 1U : 0U;

  if (now_us >= monitor->next_refresh_us) {
    refresh_paired(monitor);
    monitor->next_refresh_us = now_us + 2000000ULL;
  }

  SceBtEvent events[4];
  memset(events, 0, sizeof(events));
  const int n = sceBtReadEvent(events, 4);
  if (n < 0) {
    monitor->metrics.last_error = n;
    return;
  }

  for (int i = 0; i < n; i++) {
    char mac[18];
    format_mac_from_event(mac, events[i].mac0, events[i].mac1);
    push_event(monitor, events[i].id, mac, now_us);
  }

  monitor->metrics.last_error = sceBtGetLastError();
}

int bt_monitor_set_inquiry(BtMonitor *monitor, int enabled) {
  int rc = enabled ? sceBtStartInquiry() : sceBtStopInquiry();
  if (rc < 0) {
    monitor->metrics.last_error = rc;
    return rc;
  }
  monitor->metrics.inquiry_running = enabled ? 1U : 0U;
  return 0;
}

void bt_monitor_get_metrics(const BtMonitor *monitor, BtMonitorMetrics *out) {
  memcpy(out, &monitor->metrics, sizeof(*out));
}
