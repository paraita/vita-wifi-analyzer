#include "alerts.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void alerts_init(AlertManager *mgr) {
  memset(mgr, 0, sizeof(*mgr));
}

void alerts_push(AlertManager *mgr, uint64_t timestamp_us, AlertSeverity severity, const char *fmt, ...) {
  AlertEntry *slot = &mgr->items[mgr->head];
  memset(slot, 0, sizeof(*slot));
  slot->timestamp_us = timestamp_us;
  slot->severity = severity;

  va_list args;
  va_start(args, fmt);
  vsnprintf(slot->text, sizeof(slot->text), fmt, args);
  va_end(args);

  mgr->head = (mgr->head + 1U) % ALERTS_CAPACITY;
  if (mgr->count < ALERTS_CAPACITY) {
    mgr->count++;
  }
}

void alerts_clear(AlertManager *mgr) {
  memset(mgr, 0, sizeof(*mgr));
}

uint32_t alerts_count(const AlertManager *mgr) {
  return mgr->count;
}

void alerts_get(const AlertManager *mgr, uint32_t index_from_newest, AlertEntry *out) {
  memset(out, 0, sizeof(*out));
  if (index_from_newest >= mgr->count) {
    return;
  }
  uint32_t newest = (mgr->head + ALERTS_CAPACITY - 1U) % ALERTS_CAPACITY;
  uint32_t idx = (newest + ALERTS_CAPACITY - (index_from_newest % ALERTS_CAPACITY)) % ALERTS_CAPACITY;
  *out = mgr->items[idx];
}
