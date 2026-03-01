#ifndef BT_STORE_H
#define BT_STORE_H

#include "bt_monitor.h"
#include <stdint.h>

#define BT_STORE_MAX_DEVICES 64

typedef struct BtSeenDevice {
  char mac[18];
  char name[48];
  char type[16];
  uint32_t class_code;
  uint64_t first_seen_us;
  uint64_t last_seen_us;
  uint32_t seen_count;
} BtSeenDevice;

typedef struct BtStore {
  BtSeenDevice devices[BT_STORE_MAX_DEVICES];
  uint32_t count;
  uint64_t last_save_us;
} BtStore;

void bt_store_init(BtStore *s);
void bt_store_update(BtStore *s, const BtMonitorMetrics *m, uint64_t now_us);
const char *bt_store_guess_type(uint32_t class_code);
int bt_store_load(BtStore *s);
int bt_store_save(BtStore *s, uint64_t now_us);

#endif
