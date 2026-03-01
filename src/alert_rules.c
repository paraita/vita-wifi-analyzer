#include "alert_rules.h"

#include <string.h>

void alert_rules_init(AlertRuleConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->unknown_only = 1;
  cfg->sensitive_ports_only = 0;
  cfg->quiet_hours_enabled = 0;
  cfg->quiet_start_h = 22;
  cfg->quiet_end_h = 7;
}

int alert_rules_is_whitelisted(const AlertRuleConfig *cfg, const char *ip) {
  for (uint32_t i = 0; i < cfg->whitelist_count; i++) {
    if (cfg->whitelist[i].enabled && strcmp(cfg->whitelist[i].ip, ip) == 0) return 1;
  }
  return 0;
}

void alert_rules_toggle_whitelist(AlertRuleConfig *cfg, const char *ip) {
  for (uint32_t i = 0; i < cfg->whitelist_count; i++) {
    if (strcmp(cfg->whitelist[i].ip, ip) == 0) {
      cfg->whitelist[i].enabled = (uint8_t)!cfg->whitelist[i].enabled;
      return;
    }
  }
  if (cfg->whitelist_count >= ALERT_RULES_MAX_WHITELIST) return;
  strncpy(cfg->whitelist[cfg->whitelist_count].ip, ip, sizeof(cfg->whitelist[cfg->whitelist_count].ip) - 1);
  cfg->whitelist[cfg->whitelist_count].enabled = 1;
  cfg->whitelist_count++;
}

int alert_rules_allow(const AlertRuleConfig *cfg, const char *ip, int unknown, int sensitive, uint64_t now_us) {
  if (alert_rules_is_whitelisted(cfg, ip)) return 0;
  if (cfg->unknown_only && !unknown) return 0;
  if (cfg->sensitive_ports_only && !sensitive) return 0;

  if (cfg->quiet_hours_enabled) {
    const uint32_t sec = (uint32_t)(now_us / 1000000ULL);
    const uint8_t h = (uint8_t)((sec / 3600U) % 24U);
    if (cfg->quiet_start_h > cfg->quiet_end_h) {
      if (h >= cfg->quiet_start_h || h < cfg->quiet_end_h) return 0;
    } else {
      if (h >= cfg->quiet_start_h && h < cfg->quiet_end_h) return 0;
    }
  }

  return 1;
}
