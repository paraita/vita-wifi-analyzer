#include "scan_history.h"

#include <string.h>

static int has_ip(char arr[][16], uint32_t count, const char *ip) {
  for (uint32_t i = 0; i < count; i++) {
    if (strcmp(arr[i], ip) == 0) return 1;
  }
  return 0;
}

static int has_ip_hosts(const LanHostResult *hosts, uint32_t count, const char *ip) {
  for (uint32_t i = 0; i < count; i++) {
    if (strcmp(hosts[i].ip, ip) == 0) return 1;
  }
  return 0;
}

static void push_event(ScanHistory *h, const char *ip, HostTimelineKind kind, uint64_t now_us) {
  if (h->event_count >= SCAN_HISTORY_MAX_EVENTS) {
    memmove(&h->events[0], &h->events[1], sizeof(h->events[0]) * (SCAN_HISTORY_MAX_EVENTS - 1));
    h->event_count = SCAN_HISTORY_MAX_EVENTS - 1;
  }
  HostTimelineEvent *e = &h->events[h->event_count++];
  memset(e, 0, sizeof(*e));
  strncpy(e->ip, ip, sizeof(e->ip) - 1);
  e->kind = kind;
  e->timestamp_us = now_us;
}

static void push_round(ScanHistory *h, uint32_t round, uint64_t now_us, uint32_t host_count,
                       uint32_t added, uint32_t removed, uint32_t changed) {
  if (h->round_count >= SCAN_HISTORY_MAX_ROUNDS) {
    memmove(&h->rounds[0], &h->rounds[1], sizeof(h->rounds[0]) * (SCAN_HISTORY_MAX_ROUNDS - 1));
    h->round_count = SCAN_HISTORY_MAX_ROUNDS - 1;
  }
  ScanRoundEntry *r = &h->rounds[h->round_count++];
  memset(r, 0, sizeof(*r));
  r->round = round;
  r->timestamp_us = now_us;
  r->host_count = host_count;
  r->added = added;
  r->removed = removed;
  r->changed = changed;

  int flap_penalty = (int)((added + removed + changed) * 4U);
  int score = 100 - flap_penalty;
  if (score < 5) score = 5;
  if (score > 100) score = 100;
  r->stability_score = score;
}

void scan_history_init(ScanHistory *h) {
  memset(h, 0, sizeof(*h));
}

void scan_history_update(ScanHistory *h, const LanScannerMetrics *m, uint64_t now_us) {
  if (m->scan_round == 0 || m->scan_round == h->prev_round) {
    return;
  }

  uint32_t added = 0;
  uint32_t removed = 0;
  uint32_t changed = 0;

  for (uint32_t i = 0; i < m->host_count; i++) {
    const char *ip = m->hosts[i].ip;
    if (!has_ip(h->prev_hosts, h->prev_host_count, ip)) {
      added++;
      push_event(h, ip, HOST_EVT_JOIN, now_us);
    } else if (m->hosts[i].open_port_count > 0 || m->hosts[i].source_flags != 0) {
      changed++;
      push_event(h, ip, HOST_EVT_CHANGE, now_us);
    }
  }
  for (uint32_t i = 0; i < h->prev_host_count; i++) {
    if (!has_ip_hosts(m->hosts, m->host_count, h->prev_hosts[i])) {
      removed++;
      push_event(h, h->prev_hosts[i], HOST_EVT_LEAVE, now_us);
    }
  }

  push_round(h, m->scan_round, now_us, m->host_count, added, removed, changed);

  h->prev_host_count = m->host_count;
  if (h->prev_host_count > LAN_SCANNER_MAX_HOSTS) h->prev_host_count = LAN_SCANNER_MAX_HOSTS;
  for (uint32_t i = 0; i < h->prev_host_count; i++) {
    strncpy(h->prev_hosts[i], m->hosts[i].ip, sizeof(h->prev_hosts[i]) - 1);
    h->prev_hosts[i][sizeof(h->prev_hosts[i]) - 1] = '\0';
  }
  h->prev_round = m->scan_round;
}

int scan_history_score(const ScanHistory *h) {
  if (h->round_count == 0) return 100;
  const uint32_t n = (h->round_count < 8U) ? h->round_count : 8U;
  int sum = 0;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t idx = h->round_count - 1U - i;
    sum += h->rounds[idx].stability_score;
  }
  return sum / (int)n;
}
