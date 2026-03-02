#ifndef SERVICE_PROBE_H
#define SERVICE_PROBE_H

#include "lan_scanner.h"
#include <stdint.h>

#define SERVICE_PROBE_MAX_RESULTS LAN_SCANNER_MAX_HOSTS

typedef struct ServiceProbeResult {
  char     ip[16];
  uint16_t port;
  char     server[64];       /* HTTP Server: header */
  char     powered_by[32];   /* X-Powered-By: header */
  char     auth_scheme[16];  /* WWW-Authenticate: scheme word */
  int      status_code;      /* HTTP status code (200, 401, 403 …) */
  int      last_error;
  uint8_t  valid;            /* 1 if probe completed */
} ServiceProbeResult;

typedef struct ServiceProbeMetrics {
  uint32_t probed_count;
  uint32_t pending_count;
  ServiceProbeResult results[SERVICE_PROBE_MAX_RESULTS];
  int      last_error;
} ServiceProbeMetrics;

typedef enum SpState {
  SP_IDLE       = 0,
  SP_CONNECTING = 1,
  SP_RECEIVING  = 2
} SpState;

typedef struct ServiceProbe {
  ServiceProbeMetrics metrics;

  /* Pending queue (filled by arm()) */
  char     q_ips[LAN_SCANNER_MAX_HOSTS][16];
  uint16_t q_ports[LAN_SCANNER_MAX_HOSTS];
  uint32_t q_count;
  uint32_t q_head;

  /* Current probe state machine */
  SpState  state;
  int      sock;
  char     cur_ip[16];
  uint16_t cur_port;
  uint32_t cur_slot;         /* index into metrics.results[] */
  uint64_t deadline_us;

  /* Receive accumulation buffer */
  char     recv_buf[2048];
  uint32_t recv_len;
} ServiceProbe;

void service_probe_init(ServiceProbe *p);
void service_probe_arm(ServiceProbe *p, const LanScannerMetrics *scanner);
void service_probe_tick(ServiceProbe *p, uint64_t now_us);
void service_probe_get_metrics(const ServiceProbe *p, ServiceProbeMetrics *out);

#endif
