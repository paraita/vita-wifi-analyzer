#ifndef NET_MONITOR_H
#define NET_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETMON_SAMPLE_HZ 8
#define NETMON_HISTORY_SECONDS 60
#define NETMON_HISTORY_CAPACITY (NETMON_SAMPLE_HZ * NETMON_HISTORY_SECONDS)

typedef struct NetMonitor {
  bool connected;
  char ssid[33];
  char ip_address[16];
  char netmask[16];
  char default_route[16];
  char primary_dns[16];
  char secondary_dns[16];
  int channel;
  int rssi_dbm;
  float rssi_ema;

  int history[NETMON_HISTORY_CAPACITY];
  size_t history_head;
  size_t history_count;

  int rssi_min;
  int rssi_max;
  float rssi_avg;
  uint64_t sample_count;
  uint64_t connected_since_us;
  uint64_t total_connected_us;
} NetMonitor;

void net_monitor_init(NetMonitor *monitor);
void net_monitor_poll(NetMonitor *monitor, uint64_t now_us);
int net_monitor_history_at(const NetMonitor *monitor, size_t chronological_index);
float net_monitor_signal_norm(const NetMonitor *monitor);
uint32_t net_monitor_connected_uptime_s(const NetMonitor *monitor, uint64_t now_us);

#endif
