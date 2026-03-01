#ifndef SCAN_HISTORY_H
#define SCAN_HISTORY_H

#include "lan_scanner.h"
#include <stdint.h>

#define SCAN_HISTORY_MAX_ROUNDS 48
#define SCAN_HISTORY_MAX_EVENTS 128

typedef enum HostTimelineKind {
  HOST_EVT_JOIN = 0,
  HOST_EVT_LEAVE = 1,
  HOST_EVT_CHANGE = 2
} HostTimelineKind;

typedef struct HostTimelineEvent {
  char ip[16];
  HostTimelineKind kind;
  uint64_t timestamp_us;
} HostTimelineEvent;

typedef struct ScanRoundEntry {
  uint32_t round;
  uint64_t timestamp_us;
  uint32_t host_count;
  uint32_t added;
  uint32_t removed;
  uint32_t changed;
  int stability_score;
} ScanRoundEntry;

typedef struct ScanHistory {
  ScanRoundEntry rounds[SCAN_HISTORY_MAX_ROUNDS];
  uint32_t round_count;
  HostTimelineEvent events[SCAN_HISTORY_MAX_EVENTS];
  uint32_t event_count;

  uint32_t prev_round;
  uint32_t prev_host_count;
  char prev_hosts[LAN_SCANNER_MAX_HOSTS][16];
} ScanHistory;

void scan_history_init(ScanHistory *h);
void scan_history_update(ScanHistory *h, const LanScannerMetrics *m, uint64_t now_us);
int scan_history_score(const ScanHistory *h);

#endif
