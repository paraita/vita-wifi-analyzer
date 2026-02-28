#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stdint.h>

enum {
  DISCOVERY_SRC_MDNS = 1u << 0,
  DISCOVERY_SRC_SSDP = 1u << 1,
  DISCOVERY_SRC_NBNS = 1u << 2,
  DISCOVERY_SRC_ICMP = 1u << 3,
  DISCOVERY_SRC_TCP = 1u << 4
};

typedef struct DiscoveryEvent {
  char ip[16];
  uint32_t source_flags;
  char hostname[64];
} DiscoveryEvent;

typedef struct DiscoveryEngine {
  int mdns_sock;
  int ssdp_sock;
  int nbns_sock;
  int mdns_running;
  int ssdp_running;
  int nbns_running;
  uint32_t mdns_hits;
  uint32_t ssdp_hits;
  uint32_t nbns_hits;
  uint64_t next_query_us;
} DiscoveryEngine;

void discovery_init(DiscoveryEngine *engine);
void discovery_term(DiscoveryEngine *engine);
void discovery_tick(DiscoveryEngine *engine,
                    uint64_t now_us,
                    const char *subnet_prefix,
                    DiscoveryEvent *events,
                    uint32_t max_events,
                    uint32_t *event_count);

#endif
