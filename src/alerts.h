#ifndef ALERTS_H
#define ALERTS_H

#include <stdint.h>

#define ALERTS_CAPACITY 64
#define ALERT_TEXT_MAX 96

typedef enum AlertSeverity {
  ALERT_INFO = 0,
  ALERT_WARN = 1,
  ALERT_ERROR = 2
} AlertSeverity;

typedef struct AlertEntry {
  uint64_t timestamp_us;
  AlertSeverity severity;
  char text[ALERT_TEXT_MAX];
} AlertEntry;

typedef struct AlertManager {
  AlertEntry items[ALERTS_CAPACITY];
  uint32_t head;
  uint32_t count;
} AlertManager;

void alerts_init(AlertManager *mgr);
void alerts_push(AlertManager *mgr, uint64_t timestamp_us, AlertSeverity severity, const char *fmt, ...);
void alerts_clear(AlertManager *mgr);
uint32_t alerts_count(const AlertManager *mgr);
void alerts_get(const AlertManager *mgr, uint32_t index_from_newest, AlertEntry *out);

#endif
