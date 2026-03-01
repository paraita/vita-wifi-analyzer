#ifndef ALERT_RULES_H
#define ALERT_RULES_H

#include <stdint.h>

#define ALERT_RULES_MAX_WHITELIST 32

typedef struct WhitelistEntry {
  char ip[16];
  uint8_t enabled;
} WhitelistEntry;

typedef struct AlertRuleConfig {
  uint8_t unknown_only;
  uint8_t sensitive_ports_only;
  uint8_t quiet_hours_enabled;
  uint8_t quiet_start_h;
  uint8_t quiet_end_h;
  WhitelistEntry whitelist[ALERT_RULES_MAX_WHITELIST];
  uint32_t whitelist_count;
} AlertRuleConfig;

void alert_rules_init(AlertRuleConfig *cfg);
int alert_rules_is_whitelisted(const AlertRuleConfig *cfg, const char *ip);
void alert_rules_toggle_whitelist(AlertRuleConfig *cfg, const char *ip);
int alert_rules_allow(const AlertRuleConfig *cfg, const char *ip, int unknown, int sensitive, uint64_t now_us);

#endif
