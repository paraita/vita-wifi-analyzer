#ifndef LATENCY_PROBE_H
#define LATENCY_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#define LATENCY_PROBE_HISTORY_CAPACITY 120

typedef enum LatencyProtocol {
  LATENCY_PROTOCOL_TCP = 0,
  LATENCY_PROTOCOL_UDP = 1
} LatencyProtocol;

typedef struct LatencyProbeConfig {
  char target_ip[16];
  uint16_t target_port;
  LatencyProtocol protocol;
  uint32_t interval_ms;
  uint32_t timeout_ms;
} LatencyProbeConfig;

typedef struct LatencyProbeMetrics {
  bool enabled;
  bool running;
  char target_ip[16];
  uint16_t target_port;
  LatencyProtocol protocol;

  int last_rtt_ms;
  float ema_rtt_ms;
  int min_rtt_ms;
  int max_rtt_ms;
  float avg_rtt_ms;
  uint32_t probes_sent;
  uint32_t probes_ok;
  float loss_percent;
  int rtt_p50_ms;
  int rtt_p95_ms;
  float recent_loss_percent;
  uint32_t recent_count;
  uint8_t recent_ok[LATENCY_PROBE_HISTORY_CAPACITY];
  int last_error;
} LatencyProbeMetrics;

typedef struct LatencyProbe {
  LatencyProbeConfig config;
  volatile int stop_requested;
  int thread_id;

  volatile int running;
  volatile int last_rtt_ms;
  volatile float ema_rtt_ms;
  volatile int min_rtt_ms;
  volatile int max_rtt_ms;
  volatile float avg_rtt_ms;
  volatile uint32_t probes_sent;
  volatile uint32_t probes_ok;
  volatile int last_error;

  uint8_t recent_ok[LATENCY_PROBE_HISTORY_CAPACITY];
  int recent_rtt_ms[LATENCY_PROBE_HISTORY_CAPACITY];
  uint32_t history_head;
  uint32_t history_count;
} LatencyProbe;

void latency_probe_default_config(LatencyProbeConfig *config);
void latency_probe_init(LatencyProbe *probe);
int latency_probe_start(LatencyProbe *probe, const LatencyProbeConfig *config);
void latency_probe_stop(LatencyProbe *probe);
void latency_probe_get_metrics(const LatencyProbe *probe, LatencyProbeMetrics *out);

#endif
