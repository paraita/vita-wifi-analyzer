#ifndef LAN_SCANNER_H
#define LAN_SCANNER_H

#include "discovery.h"
#include <stdint.h>

#define LAN_SCANNER_PORT_COUNT 8
#define LAN_SCANNER_MAX_HOSTS 128

typedef struct LanScannerConfig {
  uint32_t interval_ms_per_host;
  uint32_t connect_timeout_ms;
  uint16_t ports[LAN_SCANNER_PORT_COUNT];
  uint32_t max_hosts;
} LanScannerConfig;

typedef struct LanHostResult {
  char ip[16];
  char hostname[64];
  uint8_t alive;
  uint8_t is_gateway;
  uint8_t open_port_count;
  uint16_t open_ports[LAN_SCANNER_PORT_COUNT];
  uint32_t source_flags;
  int last_error;
} LanHostResult;

typedef struct LanScannerMetrics {
  uint8_t enabled;
  uint8_t running;
  uint8_t subnet_valid;
  int8_t icmp_supported;
  uint8_t mdns_running;
  uint8_t ssdp_running;
  uint8_t nbns_running;
  char subnet_cidr[20];

  uint32_t hosts_total;
  uint32_t hosts_scanned;
  uint32_t hosts_alive;
  uint32_t scan_round;
  uint32_t mdns_hits;
  uint32_t ssdp_hits;
  uint32_t nbns_hits;
  uint32_t icmp_hits;
  uint32_t tcp_hits;

  uint32_t host_count;
  LanHostResult hosts[LAN_SCANNER_MAX_HOSTS];
  int last_error;
} LanScannerMetrics;

typedef struct LanScanner {
  LanScannerConfig config;
  int running;
  int subnet_valid;
  char subnet_cidr[20];
  char subnet_prefix[16];
  uint32_t hinted_a;
  uint32_t hinted_b;
  uint32_t hinted_c;
  int hinted_prefix_valid;
  uint32_t gateway_a;
  uint32_t gateway_b;
  uint32_t gateway_c;
  uint32_t gateway_d;
  int gateway_hint_valid;

  uint32_t hosts_scanned;
  uint32_t hosts_alive;
  uint32_t scan_round;
  uint32_t mdns_hits;
  uint32_t ssdp_hits;
  uint32_t nbns_hits;
  uint32_t icmp_hits;
  uint32_t tcp_hits;
  int last_error;
  DiscoveryEngine discovery;

  uint32_t host_count;
  LanHostResult hosts[LAN_SCANNER_MAX_HOSTS];

  uint64_t next_step_us;
  uint32_t next_host;
  uint32_t current_host;
  uint32_t preferred_host;
  uint8_t preferred_host_valid;
  uint8_t preferred_host_done;
  int8_t icmp_supported;
  uint8_t phase;
  uint8_t alive_probe_index;
  uint8_t port_probe_index;
  uint8_t current_host_alive;
  uint32_t current_source_flags;
  uint8_t current_open_port_count;
  uint16_t current_open_ports[LAN_SCANNER_PORT_COUNT];
  char current_ip[16];
} LanScanner;

void lan_scanner_default_config(LanScannerConfig *cfg);
void lan_scanner_init(LanScanner *scanner);
int lan_scanner_start(LanScanner *scanner, const LanScannerConfig *cfg);
void lan_scanner_stop(LanScanner *scanner);
void lan_scanner_request_rescan(LanScanner *scanner);
void lan_scanner_get_metrics(const LanScanner *scanner, LanScannerMetrics *out);
void lan_scanner_set_ip_hint(LanScanner *scanner, const char *ip_address);
void lan_scanner_set_gateway_hint(LanScanner *scanner, const char *gateway_ip);
void lan_scanner_tick(LanScanner *scanner, uint64_t now_us);

#endif
