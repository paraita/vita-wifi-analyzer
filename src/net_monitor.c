#include "net_monitor.h"

#include <psp2/net/netctl.h>
#include <stdio.h>
#include <string.h>

enum {
  RSSI_FALLBACK_DBM = -100,
  RSSI_BEST_DBM = -30,
  RSSI_WORST_DBM = -95
};

static int normalize_rssi_dbm(int raw_rssi) {
  if (raw_rssi > 0) {
    raw_rssi = -raw_rssi;
  }
  if (raw_rssi > RSSI_BEST_DBM) {
    raw_rssi = RSSI_BEST_DBM;
  }
  if (raw_rssi < RSSI_FALLBACK_DBM) {
    raw_rssi = RSSI_FALLBACK_DBM;
  }
  return raw_rssi;
}

void net_monitor_init(NetMonitor *monitor) {
  memset(monitor, 0, sizeof(*monitor));
  snprintf(monitor->ssid, sizeof(monitor->ssid), "N/A");
  snprintf(monitor->ip_address, sizeof(monitor->ip_address), "N/A");
  snprintf(monitor->netmask, sizeof(monitor->netmask), "N/A");
  snprintf(monitor->default_route, sizeof(monitor->default_route), "N/A");
  snprintf(monitor->primary_dns, sizeof(monitor->primary_dns), "N/A");
  snprintf(monitor->secondary_dns, sizeof(monitor->secondary_dns), "N/A");
  monitor->channel = 0;
  monitor->rssi_dbm = RSSI_FALLBACK_DBM;
  monitor->rssi_ema = (float)RSSI_FALLBACK_DBM;
  monitor->rssi_min = 0;
  monitor->rssi_max = 0;
  monitor->rssi_avg = 0.0f;
}

int net_monitor_history_at(const NetMonitor *monitor, size_t chronological_index) {
  if (monitor->history_count == 0 || chronological_index >= monitor->history_count) {
    return RSSI_FALLBACK_DBM;
  }

  const size_t oldest = (monitor->history_head + NETMON_HISTORY_CAPACITY - monitor->history_count) %
                        NETMON_HISTORY_CAPACITY;
  const size_t idx = (oldest + chronological_index) % NETMON_HISTORY_CAPACITY;
  return monitor->history[idx];
}

float net_monitor_signal_norm(const NetMonitor *monitor) {
  const float clamped = (float)monitor->rssi_ema;
  const float range = (float)(RSSI_BEST_DBM - RSSI_WORST_DBM);
  float norm = (clamped - (float)RSSI_WORST_DBM) / range;
  if (norm < 0.0f) {
    norm = 0.0f;
  } else if (norm > 1.0f) {
    norm = 1.0f;
  }
  return norm;
}

uint32_t net_monitor_connected_uptime_s(const NetMonitor *monitor, uint64_t now_us) {
  if (!monitor->connected || monitor->connected_since_us == 0) {
    return 0;
  }
  return (uint32_t)((now_us - monitor->connected_since_us) / 1000000ULL);
}

void net_monitor_poll(NetMonitor *monitor, uint64_t now_us) {
  const bool was_connected = monitor->connected;

  int state = 0;
  monitor->connected = (sceNetCtlInetGetState(&state) >= 0 && state == SCE_NETCTL_STATE_CONNECTED);
  if (!monitor->connected) {
    snprintf(monitor->ssid, sizeof(monitor->ssid), "Disconnected");
    snprintf(monitor->ip_address, sizeof(monitor->ip_address), "N/A");
    snprintf(monitor->netmask, sizeof(monitor->netmask), "N/A");
    snprintf(monitor->default_route, sizeof(monitor->default_route), "N/A");
    snprintf(monitor->primary_dns, sizeof(monitor->primary_dns), "N/A");
    snprintf(monitor->secondary_dns, sizeof(monitor->secondary_dns), "N/A");
    monitor->channel = 0;
    monitor->rssi_dbm = RSSI_FALLBACK_DBM;
  } else {
    SceNetCtlInfo info;

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SSID, &info) >= 0) {
      snprintf(monitor->ssid, sizeof(monitor->ssid), "%s", info.ssid);
    }

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_CHANNEL, &info) >= 0) {
      monitor->channel = (int)info.channel;
    }

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0) {
      snprintf(monitor->ip_address, sizeof(monitor->ip_address), "%s", info.ip_address);
    }

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK, &info) >= 0) {
      snprintf(monitor->netmask, sizeof(monitor->netmask), "%s", info.netmask);
    }

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_DEFAULT_ROUTE, &info) >= 0) {
      snprintf(monitor->default_route, sizeof(monitor->default_route), "%s", info.default_route);
    }

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_PRIMARY_DNS, &info) >= 0) {
      snprintf(monitor->primary_dns, sizeof(monitor->primary_dns), "%s", info.primary_dns);
    }

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SECONDARY_DNS, &info) >= 0) {
      snprintf(monitor->secondary_dns, sizeof(monitor->secondary_dns), "%s", info.secondary_dns);
    }

    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_DBM, &info) >= 0) {
      monitor->rssi_dbm = normalize_rssi_dbm((int)info.rssi_dbm);
    } else {
      monitor->rssi_dbm = RSSI_FALLBACK_DBM;
    }
  }

  if (monitor->connected && !was_connected) {
    monitor->connected_since_us = now_us;
  } else if (!monitor->connected && was_connected) {
    if (monitor->connected_since_us > 0) {
      monitor->total_connected_us += (now_us - monitor->connected_since_us);
      monitor->connected_since_us = 0;
    }
  }

  if (monitor->sample_count == 0) {
    monitor->rssi_ema = (float)monitor->rssi_dbm;
    monitor->rssi_min = monitor->rssi_dbm;
    monitor->rssi_max = monitor->rssi_dbm;
    monitor->rssi_avg = (float)monitor->rssi_dbm;
  } else {
    const float alpha = 0.25f;
    monitor->rssi_ema = alpha * (float)monitor->rssi_dbm + (1.0f - alpha) * monitor->rssi_ema;
    if (monitor->rssi_dbm < monitor->rssi_min) {
      monitor->rssi_min = monitor->rssi_dbm;
    }
    if (monitor->rssi_dbm > monitor->rssi_max) {
      monitor->rssi_max = monitor->rssi_dbm;
    }
    monitor->rssi_avg = ((monitor->rssi_avg * (float)monitor->sample_count) + (float)monitor->rssi_dbm) /
                        (float)(monitor->sample_count + 1);
  }

  monitor->history[monitor->history_head] = monitor->rssi_dbm;
  monitor->history_head = (monitor->history_head + 1) % NETMON_HISTORY_CAPACITY;
  if (monitor->history_count < NETMON_HISTORY_CAPACITY) {
    monitor->history_count++;
  }

  monitor->sample_count++;
}
