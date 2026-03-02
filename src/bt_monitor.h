#ifndef BT_MONITOR_H
#define BT_MONITOR_H

#include <stdint.h>

#define BT_MONITOR_MAX_PAIRED 8
#define BT_MONITOR_MAX_EVENTS 24

typedef struct BtPairedDevice {
  char     mac[18];
  char     name[48];
  char     device_type[24];
  uint32_t bt_class;
} BtPairedDevice;

typedef struct BtEventEntry {
  uint8_t  event_id;
  char     mac[18];
  uint64_t timestamp_us;
} BtEventEntry;

typedef struct BtInquiryResult {
  char     mac[18];
  char     device_type[24];
  uint64_t first_seen_us;
} BtInquiryResult;

typedef struct BtMonitorMetrics {
  uint8_t  api_available;
  uint8_t  bt_enabled;
  uint8_t  inquiry_running;
  int      last_error;
  uint32_t paired_count;
  BtPairedDevice paired[BT_MONITOR_MAX_PAIRED];
  uint32_t event_count;
  BtEventEntry events[BT_MONITOR_MAX_EVENTS];
  uint32_t inquiry_count;
  BtInquiryResult discovered[BT_MONITOR_MAX_EVENTS];
} BtMonitorMetrics;

typedef struct BtMonitor {
  BtMonitorMetrics metrics;
  uint64_t next_refresh_us;
} BtMonitor;

void bt_monitor_init(BtMonitor *monitor);
void bt_monitor_poll(BtMonitor *monitor, uint64_t now_us);
int  bt_monitor_set_inquiry(BtMonitor *monitor, int enabled);
void bt_monitor_get_metrics(const BtMonitor *monitor, BtMonitorMetrics *out);

#endif
