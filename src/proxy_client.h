#ifndef PROXY_CLIENT_H
#define PROXY_CLIENT_H

#include "lan_scanner.h"
#include <stdint.h>

typedef struct ProxyClientConfig {
  char target_ip[16];
  uint16_t target_port;
  uint32_t reconnect_ms;
  uint32_t socket_timeout_ms;
} ProxyClientConfig;

typedef struct ProxyClientMetrics {
  uint8_t enabled;
  uint8_t running;
  uint8_t connected;

  char target_ip[16];
  uint16_t target_port;

  uint32_t scan_round;
  uint32_t lines_received;
  uint32_t host_count;
  char subnet_label[24];
  LanHostResult hosts[LAN_SCANNER_MAX_HOSTS];
  int last_error;
} ProxyClientMetrics;

typedef struct ProxyClient {
  ProxyClientConfig config;
  volatile int stop_requested;
  volatile int request_scan;
  int thread_id;

  volatile int running;
  volatile int connected;
  volatile int last_error;
  volatile uint32_t scan_round;
  volatile uint32_t lines_received;

  char subnet_label[24];
  uint32_t host_count;
  LanHostResult hosts[LAN_SCANNER_MAX_HOSTS];
} ProxyClient;

void proxy_client_default_config(ProxyClientConfig *cfg);
void proxy_client_init(ProxyClient *client);
int proxy_client_start(ProxyClient *client, const ProxyClientConfig *cfg);
void proxy_client_stop(ProxyClient *client);
void proxy_client_request_scan(ProxyClient *client);
void proxy_client_get_metrics(const ProxyClient *client, ProxyClientMetrics *out);

#endif
