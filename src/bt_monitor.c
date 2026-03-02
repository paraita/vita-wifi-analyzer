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

static const char *decode_bt_class(uint32_t cls) {
  const uint32_t major = (cls >> 8) & 0x1FU;
  switch (major) {
    case 0x01: return "Computer";
    case 0x02: return "Phone";
    case 0x04: return "LAN AP";
    case 0x05: return "Audio/HID";
    case 0x06: return "Imaging";
    case 0x07: return "Wearable";
    case 0x08: return "Toy";
    case 0x09: return "Health";
    case 0x1F: return "Peripheral";
    default:   return "Unknown";
  }
}

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
    snprintf(d->device_type, sizeof(d->device_type), "%s", decode_bt_class(info.bt_class));
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

  /* Also track in discovered list (ring, dedup by MAC) */
  int found = 0;
  for (uint32_t i = 0; i < monitor->metrics.inquiry_count; i++) {
    if (strcmp(monitor->metrics.discovered[i].mac, mac) == 0) {
      found = 1;
      break;
    }
  }
  if (!found) {
    uint32_t slot;
    if (monitor->metrics.inquiry_count < BT_MONITOR_MAX_EVENTS) {
      slot = monitor->metrics.inquiry_count++;
    } else {
      /* Ring: overwrite oldest entry (index 0 after a shift) */
      memmove(&monitor->metrics.discovered[0], &monitor->metrics.discovered[1],
              sizeof(BtInquiryResult) * (BT_MONITOR_MAX_EVENTS - 1));
      slot = BT_MONITOR_MAX_EVENTS - 1;
    }
    BtInquiryResult *d = &monitor->metrics.discovered[slot];
    memset(d, 0, sizeof(*d));
    snprintf(d->mac, sizeof(d->mac), "%s", mac);
    d->first_seen_us = now_us;
    /* Try to resolve device_type from paired list */
    const char *dtype = "Unknown";
    for (uint32_t i = 0; i < monitor->metrics.paired_count; i++) {
      if (strcmp(monitor->metrics.paired[i].mac, mac) == 0) {
        dtype = monitor->metrics.paired[i].device_type;
        break;
      }
    }
    snprintf(d->device_type, sizeof(d->device_type), "%s", dtype);
  }
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
