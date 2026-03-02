#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stdint.h>

enum {
  DISCOVERY_SRC_MDNS = 1u << 0,
  DISCOVERY_SRC_SSDP = 1u << 1,
  DISCOVERY_SRC_NBNS = 1u << 2,
  DISCOVERY_SRC_ICMP = 1u << 3,
  DISCOVERY_SRC_TCP  = 1u << 4
};

typedef struct DiscoveryEvent {
  char     ip[16];
  uint32_t source_flags;
  char     hostname[64];
  char     service_hint[64];
} DiscoveryEvent;

#define DISCOVERY_MAX_UPNP 16

typedef struct UPnPDevice {
  char    ip[16];
  char    usn[128];          /* Unique Service Name from SSDP */
  char    server[64];        /* SERVER: header from SSDP */
  char    model[64];         /* from XML: modelName */
  char    manufacturer[48];  /* from XML: manufacturer */
  uint8_t is_igd;            /* 1 if WANIPConnection detected */
} UPnPDevice;

typedef enum UpnpFetchState {
  UPNP_FETCH_IDLE       = 0,
  UPNP_FETCH_CONNECTING = 1,
  UPNP_FETCH_RECEIVING  = 2
} UpnpFetchState;

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

  /* UPnP device table */
  UPnPDevice upnp_devices[DISCOVERY_MAX_UPNP];
  uint32_t   upnp_count;

  /* UPnP XML fetch state machine (one in-flight fetch at a time) */
  UpnpFetchState upnp_fetch_state;
  int            upnp_fetch_sock;
  char           upnp_fetch_ip[16];
  uint16_t       upnp_fetch_port;
  char           upnp_fetch_path[128];
  uint32_t       upnp_fetch_target_idx; /* index into upnp_devices[] */
  uint64_t       upnp_fetch_deadline_us;
  char           upnp_fetch_buf[2048];
  uint32_t       upnp_fetch_len;
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
