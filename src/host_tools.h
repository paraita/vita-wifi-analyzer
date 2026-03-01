#ifndef HOST_TOOLS_H
#define HOST_TOOLS_H

#include <stdint.h>

#define HOST_TOOLS_MAX_ENTRIES 64

typedef enum HostToolTest {
  HOST_TOOL_DNS = 0,
  HOST_TOOL_HTTP = 1,
  HOST_TOOL_HTTPS = 2,
  HOST_TOOL_RTT = 3
} HostToolTest;

typedef struct HostToolResult {
  char ip[16];
  int dns_ok;
  int http_ok;
  int https_ok;
  int rtt_ms;
  int last_error;
  uint64_t updated_at_us;
} HostToolResult;

typedef struct HostToolsCache {
  HostToolResult entries[HOST_TOOLS_MAX_ENTRIES];
  uint32_t count;
} HostToolsCache;

void host_tools_init(HostToolsCache *cache);
HostToolResult *host_tools_get(HostToolsCache *cache, const char *ip);
int host_tools_run_test(HostToolsCache *cache, const char *ip, HostToolTest test, uint32_t timeout_ms, uint64_t now_us);

#endif
