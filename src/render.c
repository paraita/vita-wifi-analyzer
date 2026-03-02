#include "render.h"

#include <math.h>
#include <psp2/display.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define PI_F 3.14159265f

enum {
  SCREEN_W = 960,
  SCREEN_H = 544
};

static const unsigned int C_BG = RGBA8(10, 16, 18, 255);
static const unsigned int C_PANEL = RGBA8(18, 30, 33, 255);
static const unsigned int C_GRID = RGBA8(35, 67, 66, 255);
static const unsigned int C_PRIMARY = RGBA8(98, 234, 151, 255);
static const unsigned int C_ACCENT = RGBA8(255, 199, 106, 255);
static const unsigned int C_WARN = RGBA8(255, 115, 101, 255);
static const unsigned int C_TEXT = RGBA8(210, 244, 233, 255);

static const char *screen_tab_name(AppScreen screen) {
  switch (screen) {
    case APP_SCREEN_RADAR:       return "RADAR";
    case APP_SCREEN_STATS:       return "STATS";
    case APP_SCREEN_SCAN:        return "SCAN";
    case APP_SCREEN_HOST_DETAIL: return "DETAIL";
    case APP_SCREEN_ALERTS:      return "ALERTS";
    case APP_SCREEN_SETTINGS:    return "SET";
    case APP_SCREEN_BT:          return "BT";
    case APP_SCREEN_NFC:         return "NFC";
    case APP_SCREEN_WIFI_AP:     return "WIFI";
    case APP_SCREEN_EXPORTS:     return "EXPORT";
    default:                     return "UNKNOWN";
  }
}

static const char *profile_name(ScanProfile p) {
  switch (p) {
    case SCAN_PROFILE_QUICK: return "QUICK";
    case SCAN_PROFILE_NORMAL: return "NORMAL";
    case SCAN_PROFILE_DEEP: return "DEEP";
    default: return "UNKNOWN";
  }
}

static const char *scan_filter_name(ScanFilterMode mode) {
  switch (mode) {
    case SCAN_FILTER_ALL: return "ALL";
    case SCAN_FILTER_GATEWAY: return "GATEWAY";
    case SCAN_FILTER_NAMED: return "NAMED";
    case SCAN_FILTER_PORTS: return "OPEN PORTS";
    default: return "UNKNOWN";
  }
}

static const char *scan_sort_name(ScanSortMode mode) {
  switch (mode) {
    case SCAN_SORT_IP: return "IP";
    case SCAN_SORT_NAME: return "NAME";
    case SCAN_SORT_PORTS: return "PORTS";
    case SCAN_SORT_SOURCE: return "SOURCE";
    default: return "UNKNOWN";
  }
}

static void draw_textf(vita2d_pgf *font, float x, float y, unsigned int color, float scale,
                       const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  vita2d_pgf_draw_text(font, x, y, color, scale, buffer);
}

static void draw_text_centered(vita2d_pgf *font, float y, unsigned int color, float scale, const char *text) {
  const int width = vita2d_pgf_text_width(font, scale, text);
  float x = ((float)SCREEN_W - (float)width) * 0.5f;
  if (x < 8.0f) {
    x = 8.0f;
  }
  vita2d_pgf_draw_text(font, x, y, color, scale, text);
}

static void draw_circle_outline(float cx, float cy, float radius, int segments, unsigned int color) {
  const float step = (2.0f * PI_F) / (float)segments;
  for (int i = 0; i < segments; i++) {
    const float a0 = step * (float)i;
    const float a1 = step * (float)(i + 1);
    const float x0 = cx + cosf(a0) * radius;
    const float y0 = cy + sinf(a0) * radius;
    const float x1 = cx + cosf(a1) * radius;
    const float y1 = cy + sinf(a1) * radius;
    vita2d_draw_line(x0, y0, x1, y1, color);
  }
}

static void draw_radar(const NetMonitor *monitor, uint64_t now_us) {
  const float cx = 260.0f;
  const float cy = 265.0f;
  const float max_r = 150.0f;

  vita2d_draw_fill_circle(cx, cy, max_r + 10.0f, C_PANEL);

  for (int ring = 1; ring <= 4; ring++) {
    draw_circle_outline(cx, cy, (max_r / 4.0f) * (float)ring, 56, C_GRID);
  }
  vita2d_draw_line(cx - max_r, cy, cx + max_r, cy, C_GRID);
  vita2d_draw_line(cx, cy - max_r, cx, cy + max_r, C_GRID);

  const float t = (float)(now_us % 1500000ULL) / 1500000.0f;
  const float signal = net_monitor_signal_norm(monitor);
  const float pulse_r = 24.0f + t * max_r;
  const unsigned int pulse_color = (signal > 0.35f) ? C_PRIMARY : C_WARN;
  draw_circle_outline(cx, cy, pulse_r, 64, pulse_color);

  const float sweep = (float)(now_us % 5000000ULL) / 5000000.0f;
  const float sweep_a = sweep * 2.0f * PI_F;
  const float sweep_r = 20.0f + signal * (max_r - 8.0f);
  const float sx = cx + cosf(sweep_a) * sweep_r;
  const float sy = cy + sinf(sweep_a) * sweep_r;
  vita2d_draw_line(cx, cy, sx, sy, C_ACCENT);

  const float core = 6.0f + signal * 20.0f;
  vita2d_draw_fill_circle(cx, cy, core, C_PRIMARY);
}

static void draw_oscilloscope(const NetMonitor *monitor) {
  const float x = 465.0f;
  const float y = 130.0f;
  const float w = 450.0f;
  const float h = 300.0f;

  vita2d_draw_rectangle(x, y, w, h, C_PANEL);
  draw_circle_outline(x + 8.0f, y + 8.0f, 6.0f, 16, C_PANEL);
  vita2d_draw_line(x, y, x + w, y, C_GRID);
  vita2d_draw_line(x, y + h, x + w, y + h, C_GRID);
  vita2d_draw_line(x, y, x, y + h, C_GRID);
  vita2d_draw_line(x + w, y, x + w, y + h, C_GRID);

  for (int i = 1; i < 6; i++) {
    const float gy = y + (h / 6.0f) * (float)i;
    vita2d_draw_line(x, gy, x + w, gy, RGBA8(28, 48, 50, 255));
  }

  if (monitor->history_count < 2) {
    return;
  }

  const size_t points = monitor->history_count;
  const float dx = w / (float)(points - 1);
  const int best = -30;
  const int worst = -95;
  const float span = (float)(best - worst);

  float px = x;
  float py = y + h;
  for (size_t i = 0; i < points; i++) {
    int sample = net_monitor_history_at(monitor, i);
    if (sample > best) sample = best;
    if (sample < worst) sample = worst;
    const float norm = ((float)sample - (float)worst) / span;
    const float nx = x + dx * (float)i;
    const float ny = y + h - norm * h;
    if (i > 0) {
      vita2d_draw_line(px, py, nx, ny, C_PRIMARY);
    }
    px = nx;
    py = ny;
  }
}

static void draw_text_block(const NetMonitor *monitor, vita2d_pgf *font) {
  const unsigned int quality = (monitor->rssi_ema > -65.0f) ? C_PRIMARY :
                               (monitor->rssi_ema > -75.0f) ? C_ACCENT : C_WARN;

  draw_textf(font, 32.0f, 92.0f, C_TEXT, 1.0f, "VITA WIFI SCOPE");
  draw_textf(font, 32.0f, 116.0f, C_GRID, 0.8f, "Userland mode | %d Hz sample | 60 fps render", NETMON_SAMPLE_HZ);

  draw_textf(font, 32.0f, 454.0f, C_TEXT, 0.95f, "SSID: %s", monitor->ssid);
  draw_textf(font, 32.0f, 478.0f, C_TEXT, 0.95f, "Channel: %d", monitor->channel);
  draw_textf(font, 32.0f, 502.0f, quality, 1.0f, "RSSI: %d dBm (EMA %.1f)", monitor->rssi_dbm, monitor->rssi_ema);
  draw_textf(font, 280.0f, 502.0f, C_GRID, 0.78f, "IP %s", monitor->ip_address);

  draw_textf(font, 465.0f, 462.0f, C_TEXT, 0.85f, "Min: %d dBm", monitor->rssi_min);
  draw_textf(font, 610.0f, 462.0f, C_TEXT, 0.85f, "Max: %d dBm", monitor->rssi_max);
  draw_textf(font, 760.0f, 462.0f, C_TEXT, 0.85f, "Avg: %.1f dBm", monitor->rssi_avg);
}

static void draw_latency_strip(const LatencyProbeMetrics *latency, vita2d_pgf *font) {
  const char *proto = (latency->protocol == LATENCY_PROTOCOL_UDP) ? "UDP" : "TCP";
  const unsigned int color = (latency->loss_percent < 20.0f) ? C_PRIMARY : C_WARN;

  vita2d_draw_rectangle(465.0f, 82.0f, 450.0f, 38.0f, C_PANEL);
  draw_textf(font, 476.0f, 106.0f, C_TEXT, 0.76f, "%s %s:%d", proto, latency->target_ip, latency->target_port);

  if (latency->probes_ok > 0) {
    draw_textf(font, 700.0f, 106.0f, color, 0.76f, "RTT %dms | Loss %.1f%%", latency->last_rtt_ms, latency->loss_percent);
  } else {
    draw_textf(font, 700.0f, 106.0f, C_WARN, 0.76f, "RTT N/A | Loss %.1f%%", latency->loss_percent);
  }
}

static void draw_nav_hint(vita2d_pgf *font, AppScreen screen) {
  if (screen == APP_SCREEN_SCAN) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  UP/DOWN:select  LEFT/RIGHT:sort  CIRCLE:filter  CROSS:detail  TRIANGLE:start/stop");
    return;
  }
  if (screen == APP_SCREEN_EXPORTS) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  UP/DOWN:select  LEFT/RIGHT:scroll  CROSS:pin base  CIRCLE:clear  TRIANGLE:export");
    return;
  }
  if (screen == APP_SCREEN_SETTINGS) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  UP/DOWN:option  CROSS:apply/open  START:quit");
    return;
  }
  if (screen == APP_SCREEN_BT) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  TRIANGLE:inquiry start/stop  SELECT:refresh  START:quit");
    return;
  }
  if (screen == APP_SCREEN_NFC) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  TRIANGLE:detect on/off  SELECT:clear tags  UP/DOWN:scroll  START:quit");
    return;
  }
  if (screen == APP_SCREEN_WIFI_AP) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  TRIANGLE:scan  UP/DOWN:scroll  START:quit");
    return;
  }

  const char *text = (screen == APP_SCREEN_HOST_DETAIL)
                       ? "L/R:view  CIRCLE:back to scan  START:quit"
                       : "L/R:switch view  START:quit";
  draw_text_centered(font, 525.0f, C_GRID, 0.8f, text);
}

static void draw_top_nav(vita2d_pgf *font, AppScreen screen) {
  const float nav_x = 20.0f;
  const float nav_y = 10.0f;
  const float nav_w = 920.0f;
  const float nav_h = 46.0f;
  const float index_w = 82.0f;
  const float tabs_w = nav_w - index_w;
  const float slot_w = tabs_w / (float)APP_SCREEN_COUNT;

  vita2d_draw_rectangle(nav_x, nav_y, nav_w, nav_h, RGBA8(14, 24, 27, 255));
  for (int i = 0; i < APP_SCREEN_COUNT; i++) {
    const char *name = screen_tab_name((AppScreen)i);
    const float slot_x = nav_x + (float)i * slot_w;
    const int text_w = vita2d_pgf_text_width(font, 0.68f, name);
    const float x = slot_x + (slot_w - (float)text_w) * 0.5f;
    const unsigned int color = (i == (int)screen) ? C_ACCENT : C_GRID;
    draw_textf(font, x, 39.0f, color, 0.68f, "%s", name);
    if (i > 0) {
      const float sep_x = slot_x;
      vita2d_draw_line(sep_x, nav_y + 8.0f, sep_x, nav_y + nav_h - 8.0f, RGBA8(24, 40, 44, 255));
    }
  }
  draw_textf(font, nav_x + tabs_w + 12.0f, 39.0f, C_TEXT, 0.74f, "%d/%d", (int)screen + 1, APP_SCREEN_COUNT);
}

static void draw_stats_screen(const NetMonitor *monitor,
                              const LatencyProbeMetrics *latency,
                              vita2d_pgf *font,
                              uint64_t now_us) {
  const unsigned int signal_color = (monitor->rssi_ema > -65.0f) ? C_PRIMARY :
                                    (monitor->rssi_ema > -75.0f) ? C_ACCENT : C_WARN;
  const char *proto = (latency->protocol == LATENCY_PROTOCOL_UDP) ? "UDP" : "TCP";
  const uint32_t uptime_s = net_monitor_connected_uptime_s(monitor, now_us);
  const unsigned int loss_color = (latency->loss_percent < 20.0f) ? C_PRIMARY : C_WARN;

  vita2d_draw_rectangle(36.0f, 88.0f, 888.0f, 404.0f, C_PANEL);
  vita2d_draw_line(36.0f, 138.0f, 924.0f, 138.0f, C_GRID);
  draw_textf(font, 54.0f, 122.0f, C_TEXT, 1.0f, "WIFI + LATENCY STATISTICS");

  draw_textf(font, 60.0f, 176.0f, C_TEXT, 0.9f, "SSID: %s", monitor->ssid);
  draw_textf(font, 60.0f, 204.0f, C_TEXT, 0.9f, "Channel: %d", monitor->channel);
  draw_textf(font, 60.0f, 232.0f, signal_color, 0.95f, "RSSI now: %d dBm | EMA %.1f dBm", monitor->rssi_dbm, monitor->rssi_ema);
  draw_textf(font, 60.0f, 260.0f, C_TEXT, 0.9f, "IP: %s | Mask: %s", monitor->ip_address, monitor->netmask);
  draw_textf(font, 60.0f, 288.0f, C_TEXT, 0.9f, "Gateway: %s | DNS1: %s",
             monitor->default_route, monitor->primary_dns);
  draw_textf(font, 60.0f, 316.0f, C_TEXT, 0.9f, "DNS2: %s", monitor->secondary_dns);
  draw_textf(font, 60.0f, 344.0f, C_TEXT, 0.9f, "RSSI min/max/avg: %d / %d / %.1f dBm",
             monitor->rssi_min, monitor->rssi_max, monitor->rssi_avg);
  draw_textf(font, 60.0f, 372.0f, C_TEXT, 0.9f, "Samples: %u", (unsigned int)monitor->sample_count);
  draw_textf(font, 420.0f, 372.0f, C_TEXT, 0.9f, "Connected uptime: %02u:%02u",
             (unsigned int)(uptime_s / 60U), (unsigned int)(uptime_s % 60U));

  draw_textf(font, 60.0f, 408.0f, C_ACCENT, 0.95f, "Active probe: %s %s:%d", proto, latency->target_ip, latency->target_port);
  if (latency->probes_ok > 0) {
    draw_textf(font, 60.0f, 434.0f, C_TEXT, 0.9f, "RTT last/ema: %d / %.1f ms", latency->last_rtt_ms, latency->ema_rtt_ms);
    draw_textf(font, 60.0f, 458.0f, C_TEXT, 0.9f, "RTT min/max/avg: %d / %d / %.1f ms",
               latency->min_rtt_ms, latency->max_rtt_ms, latency->avg_rtt_ms);
    draw_textf(font, 60.0f, 482.0f, C_TEXT, 0.9f, "RTT p50/p95: %d / %d ms", latency->rtt_p50_ms, latency->rtt_p95_ms);
  } else {
    draw_textf(font, 60.0f, 434.0f, C_WARN, 0.9f, "RTT not available yet");
  }
  draw_textf(font, 500.0f, 434.0f, C_TEXT, 0.9f, "Probes ok/sent: %u / %u", latency->probes_ok, latency->probes_sent);
  draw_textf(font, 500.0f, 458.0f, loss_color,
             0.95f, "Loss: %.1f%%", latency->loss_percent);
  draw_textf(font, 500.0f, 482.0f, C_TEXT, 0.9f, "Recent loss: %.1f%%", latency->recent_loss_percent);
  draw_textf(font, 720.0f, 482.0f, C_GRID, 0.8f, "last error: %d", latency->last_error);
}

static void format_ports(const LanHostResult *host, char *out, size_t out_len) {
  if (host->open_port_count == 0) {
    snprintf(out, out_len, "-");
    return;
  }

  out[0] = '\0';
  for (uint8_t i = 0; i < host->open_port_count; i++) {
    char part[12];
    snprintf(part, sizeof(part), "%s%u", (i == 0) ? "" : ",", host->open_ports[i]);
    strncat(out, part, out_len - strlen(out) - 1);
  }
}

static void format_sources(uint32_t flags, char *out, size_t out_len) {
  out[0] = '\0';
  if (flags & DISCOVERY_SRC_MDNS) strncat(out, "mDNS ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_SSDP) strncat(out, "SSDP ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_NBNS) strncat(out, "NBNS ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_ICMP) strncat(out, "ICMP ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_TCP) strncat(out, "TCP ", out_len - strlen(out) - 1);
  if (out[0] == '\0') {
    snprintf(out, out_len, "-");
  }
}

static void truncate_copy(const char *src, char *dst, size_t dst_len) {
  if (dst_len == 0) {
    return;
  }
  if (src == NULL || src[0] == '\0') {
    snprintf(dst, dst_len, "-");
    return;
  }
  const size_t n = strlen(src);
  if (n < dst_len) {
    snprintf(dst, dst_len, "%s", src);
    return;
  }
  if (dst_len <= 4) {
    memset(dst, '.', dst_len - 1);
    dst[dst_len - 1] = '\0';
    return;
  }
  const size_t keep = dst_len - 4;
  memcpy(dst, src, keep);
  dst[keep] = '.';
  dst[keep + 1] = '.';
  dst[keep + 2] = '.';
  dst[keep + 3] = '\0';
}

static void draw_scan_screen(const LanScannerMetrics *scanner,
                             const ProxyClientMetrics *proxy,
                             ScanDataSource source,
                             const int *view_indices,
                             int view_count,
                             ScanFilterMode filter_mode,
                             ScanSortMode sort_mode,
                             int scroll,
                             int selected_host_index,
                             vita2d_pgf *font) {
  const int showing_proxy = (source == SCAN_SOURCE_PROXY);

  const char *mode_name = showing_proxy ? "PROXY" : "LOCAL";
  const unsigned int mode_color = showing_proxy ? C_ACCENT : C_PRIMARY;

  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  vita2d_draw_line(24.0f, 118.0f, 936.0f, 118.0f, C_GRID);
  draw_textf(font, 42.0f, 105.0f, C_TEXT, 1.0f, "LAN SCANNER");
  draw_textf(font, 240.0f, 105.0f, mode_color, 0.9f, "[%s]", mode_name);

  uint32_t raw_host_count = 0;
  const LanHostResult *hosts = NULL;
  uint32_t scanned = 0;
  uint32_t total = 0;
  uint32_t alive = 0;
  uint32_t round = 0;
  int running = 0;
  int connected = 1;
  int subnet_valid = 1;
  int icmp_supported = -1;
  int last_error = 0;
  char subnet[24];
  subnet[0] = '\0';

  if (showing_proxy) {
    hosts = proxy->hosts;
    raw_host_count = proxy->host_count;
    scanned = proxy->host_count;
    total = proxy->host_count;
    alive = proxy->host_count;
    round = proxy->scan_round;
    running = proxy->running;
    connected = proxy->connected;
    subnet_valid = 1;
    last_error = proxy->last_error;
    snprintf(subnet, sizeof(subnet), "%s", proxy->subnet_label);
    draw_textf(font, 42.0f, 140.0f, C_TEXT, 0.82f, "Proxy: %s:%u %s",
               proxy->target_ip, proxy->target_port, proxy->connected ? "(connected)" : "(offline)");
  } else {
    hosts = scanner->hosts;
    raw_host_count = scanner->host_count;
    scanned = scanner->hosts_scanned;
    total = scanner->hosts_total;
    alive = scanner->hosts_alive;
    round = scanner->scan_round;
    running = scanner->running;
    connected = scanner->subnet_valid;
    subnet_valid = scanner->subnet_valid;
    icmp_supported = scanner->icmp_supported;
    last_error = scanner->last_error;
    snprintf(subnet, sizeof(subnet), "%s", scanner->subnet_cidr);
    draw_textf(font, 42.0f, 140.0f, C_TEXT, 0.82f, "Subnet: %s", scanner->subnet_valid ? scanner->subnet_cidr : "N/A");
  }

  if (!showing_proxy && !scanner->enabled) {
    draw_textf(font, 42.0f, 172.0f, C_WARN, 0.9f, "Local scanner stopped. Press TRIANGLE to start.");
  } else if (showing_proxy && !connected) {
    draw_textf(font, 42.0f, 172.0f, C_WARN, 0.9f, "Proxy offline. Press SELECT to use local scanner.");
  } else if (!showing_proxy && (!connected || !subnet_valid)) {
    draw_textf(font, 42.0f, 172.0f, C_WARN, 0.9f, "Local subnet unavailable. Check Wi-Fi connection.");
  } else {
    draw_textf(font, 42.0f, 172.0f, C_TEXT, 0.9f, "Round #%u | Progress %u/%u | Alive %u | State %s",
               round, scanned, total, alive, running ? "SCANNING" : "IDLE");
  }
  draw_textf(font, 42.0f, 196.0f, C_GRID, 0.75f, "Last error: %d (0x%08X)", last_error, (unsigned int)last_error);
  if (!showing_proxy) {
    const char *icmp_state = (icmp_supported > 0) ? "yes" : (icmp_supported == 0) ? "no" : "unknown";
    draw_textf(font, 320.0f, 196.0f, C_GRID, 0.75f,
               "ICMP:%s mDNS:%s SSDP:%s NBNS:%s",
               icmp_state,
               scanner->mdns_running ? "on" : "off",
               scanner->ssdp_running ? "on" : "off",
               scanner->nbns_running ? "on" : "off");
  }
  draw_textf(font, 42.0f, 216.0f, C_GRID, 0.72f, "Hits M:%u S:%u N:%u I:%u T:%u",
             scanner->mdns_hits, scanner->ssdp_hits, scanner->nbns_hits, scanner->icmp_hits, scanner->tcp_hits);
  draw_textf(font, 620.0f, 216.0f, C_GRID, 0.72f, "Filter:%s  Sort:%s  View:%d/%u",
             scan_filter_name(filter_mode), scan_sort_name(sort_mode), view_count, raw_host_count);
  (void)subnet;

  const float table_x = 42.0f;
  const float table_y = 238.0f;
  const float table_w = 876.0f;
  const float row_h = 30.0f;
  const int rows_visible = 8;

  vita2d_draw_rectangle(table_x, table_y, table_w, row_h, RGBA8(26, 44, 47, 255));
  draw_textf(font, table_x + 10.0f, table_y + 21.0f, C_TEXT, 0.78f, "IP");
  draw_textf(font, table_x + 140.0f, table_y + 21.0f, C_TEXT, 0.78f, "Name");
  draw_textf(font, table_x + 425.0f, table_y + 21.0f, C_TEXT, 0.78f, "Source");
  draw_textf(font, table_x + 610.0f, table_y + 21.0f, C_TEXT, 0.78f, "Ports");
  draw_textf(font, table_x + 775.0f, table_y + 21.0f, C_TEXT, 0.78f, "Status");

  for (int r = 0; r < rows_visible; r++) {
    const uint32_t idx = (uint32_t)(scroll + r);
    const float y = table_y + row_h + row_h * (float)r;
    const int selected = ((int)idx == selected_host_index);
    vita2d_draw_rectangle(table_x, y, table_w, row_h - 1.0f,
                          selected ? RGBA8(40, 58, 56, 255) : RGBA8(19, 33, 36, 255));

    if (idx < (uint32_t)view_count) {
      const int host_idx = view_indices[idx];
      if (host_idx < 0 || host_idx >= (int)raw_host_count) {
        draw_textf(font, table_x + 12.0f, y + 21.0f, C_WARN, 0.72f, "Index error");
        continue;
      }
      const LanHostResult *host = &hosts[host_idx];
      char ports[96];
      char sources[96];
      char hostname_short[30];
      format_ports(host, ports, sizeof(ports));
      format_sources(host->source_flags, sources, sizeof(sources));
      truncate_copy(host->hostname, hostname_short, sizeof(hostname_short));
      draw_textf(font, table_x + 10.0f, y + 21.0f, C_TEXT, 0.72f, "%s", host->ip);
      draw_textf(font, table_x + 140.0f, y + 21.0f, C_TEXT, 0.72f, "%s", hostname_short);
      draw_textf(font, table_x + 425.0f, y + 21.0f, C_TEXT, 0.72f, "%s", sources);
      draw_textf(font, table_x + 610.0f, y + 21.0f, C_TEXT, 0.72f, "%s", ports);
      draw_textf(font, table_x + 775.0f, y + 21.0f, host->is_gateway ? C_ACCENT : C_PRIMARY,
                 0.72f, host->is_gateway ? "GW" : "OK");
    } else {
      draw_textf(font, table_x + 12.0f, y + 21.0f, C_GRID, 0.72f, "-");
    }
  }
}

static void draw_host_detail_screen(const LanScannerMetrics *scanner, int selected_host_index, vita2d_pgf *font) {
  vita2d_draw_rectangle(32.0f, 80.0f, 896.0f, 420.0f, C_PANEL);
  draw_textf(font, 50.0f, 112.0f, C_TEXT, 1.0f, "HOST DETAIL");
  if (scanner->host_count == 0) {
    draw_textf(font, 50.0f, 152.0f, C_WARN, 0.9f, "No host available. Use SCAN first.");
    return;
  }

  if (selected_host_index < 0) { selected_host_index = 0; }
  if ((uint32_t)selected_host_index >= scanner->host_count) {
    selected_host_index = (int)scanner->host_count - 1;
  }

  const LanHostResult *h = &scanner->hosts[selected_host_index];
  char ports[96];
  char sources[96];
  format_ports(h, ports, sizeof(ports));
  format_sources(h->source_flags, sources, sizeof(sources));
  draw_textf(font, 50.0f, 152.0f, C_TEXT, 0.9f, "IP: %s", h->ip);
  draw_textf(font, 50.0f, 182.0f, C_TEXT, 0.9f, "Name: %s", h->hostname[0] ? h->hostname : "-");
  draw_textf(font, 50.0f, 212.0f, C_TEXT, 0.9f, "Sources: %s", sources);
  draw_textf(font, 50.0f, 242.0f, C_TEXT, 0.9f, "Service hint: %s", h->service_hint[0] ? h->service_hint : "-");
  draw_textf(font, 50.0f, 272.0f, C_TEXT, 0.9f, "Open ports: %s", ports);
  draw_textf(font, 50.0f, 302.0f, h->is_gateway ? C_ACCENT : C_TEXT, 0.9f, "Role: %s",
             h->is_gateway ? "Gateway" : "Host");

  /* HTTP Banner (filled by service_probe via main.c) */
  if (h->banner[0] != '\0') {
    draw_textf(font, 50.0f, 332.0f, C_ACCENT, 0.84f, "HTTP Banner: %s", h->banner);
  } else {
    draw_textf(font, 50.0f, 332.0f, C_GRID, 0.84f, "HTTP Banner: -");
  }

  /* UPnP info (if this host is in the UPnP table) */
  for (uint32_t u = 0; u < scanner->upnp_count; u++) {
    const UPnPDevice *ud = &scanner->upnp_devices[u];
    if (strcmp(ud->ip, h->ip) != 0) {
      continue;
    }
    const unsigned int igd_color = ud->is_igd ? C_WARN : C_PRIMARY;
    draw_textf(font, 50.0f, 362.0f, igd_color, 0.84f,
               "UPnP%s  %s / %s",
               ud->is_igd ? " [IGD]" : "",
               ud->manufacturer[0] ? ud->manufacturer : "-",
               ud->model[0]        ? ud->model        : "-");
    draw_textf(font, 50.0f, 384.0f, C_GRID, 0.76f, "USN: %s", ud->usn[0] ? ud->usn : "-");
    break;
  }

  draw_textf(font, 50.0f, 410.0f, C_GRID, 0.84f, "Last error: %d (0x%08X)",
             h->last_error, (unsigned int)h->last_error);
}

static void draw_settings_screen(vita2d_pgf *font, int settings_index, ScanProfile scan_profile, int audio_enabled) {
  if (settings_index < 0) settings_index = 0;
  if (settings_index > 4) settings_index = 4;
  vita2d_draw_rectangle(32.0f, 80.0f, 896.0f, 420.0f, C_PANEL);
  draw_textf(font, 50.0f, 112.0f, C_TEXT, 1.0f, "SETTINGS");
  draw_textf(font, 50.0f, 136.0f, C_GRID, 0.8f, "UP/DOWN: select  CROSS: change/apply");

  const unsigned int c0 = (settings_index == 0) ? C_ACCENT : C_TEXT;
  const unsigned int c1 = (settings_index == 1) ? C_ACCENT : C_TEXT;
  const unsigned int c2 = (settings_index == 2) ? C_ACCENT : C_TEXT;
  const unsigned int c3 = (settings_index == 3) ? C_ACCENT : C_TEXT;
  const unsigned int c4 = (settings_index == 4) ? C_ACCENT : C_TEXT;
  draw_textf(font, 60.0f, 182.0f, c0, 0.9f, "Scan profile: %s", profile_name(scan_profile));
  draw_textf(font, 60.0f, 212.0f, c1, 0.9f, "Audio feedback: %s", audio_enabled ? "ON" : "OFF");
  draw_textf(font, 60.0f, 242.0f, c2, 0.9f, "Apply profile to scanner now");
  draw_textf(font, 60.0f, 272.0f, c3, 0.9f, "Export snapshot now");
  draw_textf(font, 60.0f, 302.0f, c4, 0.9f, "Open exports viewer");
  draw_textf(font, 60.0f, 320.0f, C_GRID, 0.84f,
             "Quick: fast/low coverage | Normal: balanced | Deep: slower/max coverage");
}

static int export_has_host(const ExportSummary *e, const char *ip) {
  for (uint32_t i = 0; i < e->all_host_count; i++) {
    if (strcmp(e->all_hosts[i], ip) == 0) {
      return 1;
    }
  }
  return 0;
}

static void draw_exports_screen(const ExportViewer *viewer,
                                int exports_scroll,
                                int exports_selected,
                                int exports_compare_index,
                                vita2d_pgf *font) {
  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  draw_textf(font, 42.0f, 104.0f, C_TEXT, 1.0f, "EXPORTS");
  draw_textf(font, 42.0f, 128.0f, C_GRID, 0.78f, "UP/DOWN:select  LEFT/RIGHT:scroll  CROSS:view detail");

  const float left_x = 42.0f;
  const float left_y = 146.0f;
  const float left_w = 360.0f;
  const float left_h = 340.0f;
  vita2d_draw_rectangle(left_x, left_y, left_w, left_h, RGBA8(16, 27, 31, 255));
  draw_textf(font, left_x + 10.0f, left_y + 20.0f, C_TEXT, 0.8f, "Snapshots (%u)", viewer->count);

  const int rows = 10;
  const float row_h = 30.0f;
  for (int i = 0; i < rows; i++) {
    const uint32_t idx = (uint32_t)(exports_scroll + i);
    const float y = left_y + 28.0f + row_h * (float)i;
    const int selected = ((int)idx == exports_selected);
    vita2d_draw_rectangle(left_x + 6.0f, y, left_w - 12.0f, row_h - 2.0f,
                          selected ? RGBA8(54, 76, 72, 255) : RGBA8(21, 36, 40, 255));
    if (idx < viewer->count) {
      draw_textf(font, left_x + 12.0f, y + 20.0f, selected ? C_ACCENT : C_TEXT, 0.72f, "%s", viewer->exports[idx].label);
    }
  }

  const float right_x = 418.0f;
  const float right_y = 146.0f;
  const float right_w = 500.0f;
  const float right_h = 340.0f;
  vita2d_draw_rectangle(right_x, right_y, right_w, right_h, RGBA8(16, 27, 31, 255));
  draw_textf(font, right_x + 10.0f, right_y + 20.0f, C_TEXT, 0.8f, "Interpreted detail");
  if (viewer->count == 0 || exports_selected < 0 || (uint32_t)exports_selected >= viewer->count) {
    draw_textf(font, right_x + 10.0f, right_y + 54.0f, C_GRID, 0.84f, "No export snapshot found.");
    return;
  }

  const ExportSummary *e = &viewer->exports[exports_selected];
  draw_textf(font, right_x + 10.0f, right_y + 54.0f, C_TEXT, 0.82f, "File: %s", e->label);
  draw_textf(font, right_x + 10.0f, right_y + 82.0f, C_TEXT, 0.82f, "Subnet: %s", e->subnet[0] ? e->subnet : "-");
  draw_textf(font, right_x + 10.0f, right_y + 110.0f, C_TEXT, 0.82f, "Host count: %u", e->host_count);
  draw_textf(font, right_x + 10.0f, right_y + 138.0f, C_GRID, 0.8f, "Preview hosts:");
  for (uint32_t i = 0; i < e->preview_count && i < EXPORT_VIEWER_HOST_PREVIEW; i++) {
    draw_textf(font, right_x + 24.0f, right_y + 166.0f + 22.0f * (float)i, C_TEXT, 0.76f, "- %s", e->preview_hosts[i]);
  }

  const int baseline_ok = (exports_compare_index >= 0 &&
                           exports_compare_index < (int)viewer->count &&
                           exports_compare_index != exports_selected);
  if (!baseline_ok) {
    draw_textf(font, right_x + 10.0f, right_y + 274.0f, C_GRID, 0.76f,
               "Baseline: none (CROSS to pin selected as baseline)");
    return;
  }

  const ExportSummary *base = &viewer->exports[exports_compare_index];
  int added = 0;
  int removed = 0;
  char added_ips[4][16];
  char removed_ips[4][16];
  int added_preview = 0;
  int removed_preview = 0;

  for (uint32_t i = 0; i < e->all_host_count; i++) {
    if (!export_has_host(base, e->all_hosts[i])) {
      added++;
      if (added_preview < 4) {
        snprintf(added_ips[added_preview], sizeof(added_ips[added_preview]), "%s", e->all_hosts[i]);
        added_preview++;
      }
    }
  }
  for (uint32_t i = 0; i < base->all_host_count; i++) {
    if (!export_has_host(e, base->all_hosts[i])) {
      removed++;
      if (removed_preview < 4) {
        snprintf(removed_ips[removed_preview], sizeof(removed_ips[removed_preview]), "%s", base->all_hosts[i]);
        removed_preview++;
      }
    }
  }

  draw_textf(font, right_x + 10.0f, right_y + 274.0f, C_ACCENT, 0.78f, "Delta vs baseline: %s", base->label);
  draw_textf(font, right_x + 10.0f, right_y + 296.0f, C_PRIMARY, 0.78f, "Added: %d", added);
  draw_textf(font, right_x + 140.0f, right_y + 296.0f, C_WARN, 0.78f, "Removed: %d", removed);
  for (int i = 0; i < added_preview; i++) {
    draw_textf(font, right_x + 10.0f + 116.0f * (float)i, right_y + 318.0f, C_PRIMARY, 0.72f, "+%s", added_ips[i]);
  }
  for (int i = 0; i < removed_preview; i++) {
    draw_textf(font, right_x + 10.0f + 116.0f * (float)i, right_y + 338.0f, C_WARN, 0.72f, "-%s", removed_ips[i]);
  }
}

static void draw_alerts_screen(const AlertManager *alerts, int scroll, vita2d_pgf *font, uint64_t now_us) {
  (void)now_us;
  vita2d_draw_rectangle(32.0f, 80.0f, 896.0f, 420.0f, C_PANEL);
  draw_textf(font, 50.0f, 112.0f, C_TEXT, 1.0f, "ALERTS");
  draw_textf(font, 50.0f, 136.0f, C_GRID, 0.8f, "UP/DOWN: scroll  TRIANGLE: clear");

  const uint32_t total = alerts_count(alerts);
  if (total == 0) {
    draw_textf(font, 50.0f, 176.0f, C_GRID, 0.9f, "No alerts.");
    return;
  }

  const int rows_visible = 10;
  if (scroll < 0) scroll = 0;
  if (scroll > (int)total - 1) scroll = (int)total - 1;

  for (int i = 0; i < rows_visible; i++) {
    const uint32_t idx = (uint32_t)(scroll + i);
    if (idx >= total) break;
    AlertEntry e;
    alerts_get(alerts, idx, &e);
    const unsigned int color = (e.severity == ALERT_ERROR) ? C_WARN :
                               (e.severity == ALERT_WARN) ? C_ACCENT : C_TEXT;
    const uint32_t sec = (uint32_t)(e.timestamp_us / 1000000ULL);
    draw_textf(font, 50.0f, 176.0f + i * 30.0f, color, 0.78f, "[%06us] %s", sec, e.text);
  }
}

static const char *nfc_type_name(uint8_t t) {
  switch (t) {
    case 1: return "NFC-A";
    case 2: return "NFC-B";
    case 3: return "NFC-F";
    default: return "?";
  }
}

static void draw_nfc_screen(const NfcScannerMetrics *nfc, int scroll,
                             vita2d_pgf *font, uint64_t now_us) {
  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  draw_textf(font, 42.0f, 104.0f, C_TEXT, 1.0f, "NFC SCANNER");

  const unsigned int hw_color  = nfc->hw_available ? C_PRIMARY : C_WARN;
  const unsigned int det_color = nfc->detecting    ? C_PRIMARY : C_GRID;
  draw_textf(font, 42.0f, 132.0f, hw_color,  0.8f, "HW: %s",
             nfc->hw_available ? "AVAILABLE" : "UNAVAILABLE");
  draw_textf(font, 200.0f, 132.0f, det_color, 0.8f, "Detect: %s",
             nfc->detecting ? "ON" : "OFF");
  draw_textf(font, 360.0f, 132.0f, C_TEXT, 0.8f, "Tags: %u  Total seen: %u",
             nfc->tag_count, nfc->total_seen);
  draw_textf(font, 720.0f, 132.0f, C_GRID, 0.76f, "Err: %d", nfc->last_error);

  /* Table header */
  const float tx = 42.0f;
  const float ty = 158.0f;
  const float tw = 876.0f;
  const float rh = 30.0f;
  const int   rv = 10;

  vita2d_draw_rectangle(tx, ty, tw, rh, RGBA8(26, 44, 47, 255));
  draw_textf(font, tx + 10.0f,  ty + 21.0f, C_TEXT, 0.76f, "UID");
  draw_textf(font, tx + 310.0f, ty + 21.0f, C_TEXT, 0.76f, "Type");
  draw_textf(font, tx + 400.0f, ty + 21.0f, C_TEXT, 0.76f, "ATQA");
  draw_textf(font, tx + 490.0f, ty + 21.0f, C_TEXT, 0.76f, "SAK");
  draw_textf(font, tx + 570.0f, ty + 21.0f, C_TEXT, 0.76f, "Seen");
  draw_textf(font, tx + 660.0f, ty + 21.0f, C_TEXT, 0.76f, "Age(s)");

  int scroll_off = scroll;
  if (scroll_off < 0) { scroll_off = 0; }
  for (int r = 0; r < rv; r++) {
    const int idx = scroll_off + r;
    const float y = ty + rh + rh * (float)r;
    vita2d_draw_rectangle(tx, y, tw, rh - 1.0f, RGBA8(19, 33, 36, 255));
    if (idx < 0 || (uint32_t)idx >= nfc->tag_count) {
      draw_textf(font, tx + 12.0f, y + 21.0f, C_GRID, 0.72f, "-");
      continue;
    }
    const NfcTagRecord *tr = &nfc->tags[(uint32_t)idx];
    const uint64_t age_s = (now_us > tr->last_seen_us)
                             ? (now_us - tr->last_seen_us) / 1000000ULL : 0ULL;
    draw_textf(font, tx + 10.0f,  y + 21.0f, C_PRIMARY, 0.72f, "%s", tr->uid_str);
    draw_textf(font, tx + 310.0f, y + 21.0f, C_TEXT,    0.72f, "%s", nfc_type_name(tr->tag_type));
    draw_textf(font, tx + 400.0f, y + 21.0f, C_GRID,    0.70f, "%04X", (unsigned int)tr->atqa);
    draw_textf(font, tx + 490.0f, y + 21.0f, C_GRID,    0.70f, "%02X", (unsigned int)tr->sak);
    draw_textf(font, tx + 570.0f, y + 21.0f, C_TEXT,    0.72f, "%u", tr->seen_count);
    draw_textf(font, tx + 660.0f, y + 21.0f, C_GRID,    0.70f, "%llu", (unsigned long long)age_s);
  }
}

static const char *security_name(uint8_t s) {
  switch (s) {
    case 0: return "Open";
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA3";
    default: return "?";
  }
}

static void draw_wifi_ap_screen(const WifiScannerMetrics *wifi, int scroll,
                                vita2d_pgf *font, uint64_t now_us) {
  (void)now_us;
  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  draw_textf(font, 42.0f, 104.0f, C_TEXT, 1.0f, "WI-FI AP SCANNER");

  const unsigned int scan_color = wifi->scanning ? C_ACCENT : C_GRID;
  draw_textf(font, 42.0f, 132.0f, scan_color, 0.8f, "Status: %s",
             wifi->scanning ? "SCANNING..." : (wifi->scan_done ? "DONE" : "IDLE"));
  draw_textf(font, 280.0f, 132.0f, C_TEXT, 0.8f, "APs: %u  Scans: %u",
             wifi->ap_count, wifi->scan_count);
  draw_textf(font, 600.0f, 132.0f, C_GRID, 0.76f, "Err: %d", wifi->last_error);

  /* Table */
  const float tx = 42.0f;
  const float ty = 158.0f;
  const float tw = 876.0f;
  const float rh = 30.0f;
  const int   rv = 10;

  vita2d_draw_rectangle(tx, ty, tw, rh, RGBA8(26, 44, 47, 255));
  draw_textf(font, tx + 10.0f,  ty + 21.0f, C_TEXT, 0.76f, "SSID");
  draw_textf(font, tx + 290.0f, ty + 21.0f, C_TEXT, 0.76f, "BSSID");
  draw_textf(font, tx + 470.0f, ty + 21.0f, C_TEXT, 0.76f, "RSSI");
  draw_textf(font, tx + 580.0f, ty + 21.0f, C_TEXT, 0.76f, "CH");
  draw_textf(font, tx + 640.0f, ty + 21.0f, C_TEXT, 0.76f, "Security");
  draw_textf(font, tx + 790.0f, ty + 21.0f, C_TEXT, 0.76f, "WPS");

  int scroll_off = scroll;
  if (scroll_off < 0) { scroll_off = 0; }
  for (int r = 0; r < rv; r++) {
    const int idx = scroll_off + r;
    const float y = ty + rh + rh * (float)r;
    vita2d_draw_rectangle(tx, y, tw, rh - 1.0f, RGBA8(19, 33, 36, 255));
    if (idx < 0 || (uint32_t)idx >= wifi->ap_count) {
      draw_textf(font, tx + 12.0f, y + 21.0f, C_GRID, 0.72f, "-");
      continue;
    }
    const WifiApInfo *ap = &wifi->aps[(uint32_t)idx];

    /* RSSI bar color */
    const unsigned int rssi_color = (ap->rssi_dbm > -60) ? C_PRIMARY :
                                    (ap->rssi_dbm > -75) ? C_ACCENT  : C_WARN;

    char ssid_trunc[22];
    truncate_copy(ap->ssid, ssid_trunc, sizeof(ssid_trunc));

    draw_textf(font, tx + 10.0f,  y + 21.0f, ap->is_new ? C_PRIMARY : C_TEXT,
               0.72f, "%s%s", ap->is_new ? "[NEW] " : "", ssid_trunc);
    draw_textf(font, tx + 290.0f, y + 21.0f, C_GRID,    0.70f, "%s", ap->bssid_str);
    draw_textf(font, tx + 470.0f, y + 21.0f, rssi_color, 0.72f, "%d", (int)ap->rssi_dbm);
    draw_textf(font, tx + 580.0f, y + 21.0f, C_TEXT,    0.72f, "%u", (unsigned int)ap->channel);
    draw_textf(font, tx + 640.0f, y + 21.0f, C_TEXT,    0.72f, "%s", security_name(ap->security));
    draw_textf(font, tx + 790.0f, y + 21.0f, ap->wps ? C_ACCENT : C_GRID,
               0.72f, "%s", ap->wps ? "YES" : "-");
  }
}

static void draw_bt_screen(const BtMonitorMetrics *bt, vita2d_pgf *font, uint64_t now_us) {
  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  draw_textf(font, 42.0f, 104.0f, C_TEXT, 1.0f, "BLUETOOTH TOOLS");

  const unsigned int api_color = bt->api_available ? C_PRIMARY : C_WARN;
  const unsigned int bt_color = bt->bt_enabled ? C_PRIMARY : C_WARN;
  const unsigned int inq_color = bt->inquiry_running ? C_ACCENT : C_GRID;
  draw_textf(font, 42.0f, 132.0f, api_color, 0.8f, "API: %s", bt->api_available ? "AVAILABLE" : "UNAVAILABLE");
  draw_textf(font, 210.0f, 132.0f, bt_color, 0.8f, "BT: %s", bt->bt_enabled ? "ENABLED" : "DISABLED");
  draw_textf(font, 340.0f, 132.0f, inq_color, 0.8f, "Inquiry: %s", bt->inquiry_running ? "RUNNING" : "IDLE");
  draw_textf(font, 530.0f, 132.0f, C_GRID, 0.76f, "Last error: %d (0x%08X)", bt->last_error, (unsigned int)bt->last_error);

  const float left_x = 42.0f;
  const float left_y = 154.0f;
  const float left_w = 420.0f;
  const float box_h = 336.0f;
  vita2d_draw_rectangle(left_x, left_y, left_w, box_h, RGBA8(16, 27, 31, 255));
  draw_textf(font, left_x + 10.0f, left_y + 22.0f, C_TEXT, 0.82f, "Paired devices (%u)", bt->paired_count);

  for (uint32_t i = 0; i < bt->paired_count && i < BT_MONITOR_MAX_PAIRED; i++) {
    const BtPairedDevice *d = &bt->paired[i];
    const float y = left_y + 48.0f + 34.0f * (float)i;
    draw_textf(font, left_x + 12.0f, y, C_TEXT, 0.74f, "%u) %s (%s)", i + 1U, d->name, d->device_type);
    draw_textf(font, left_x + 34.0f, y + 16.0f, C_GRID, 0.70f, "%s  class:0x%06X", d->mac, d->bt_class);
  }
  if (bt->paired_count == 0) {
    draw_textf(font, left_x + 12.0f, left_y + 58.0f, C_GRID, 0.76f, "No paired device reported.");
  }

  const float right_x = 478.0f;
  const float right_y = 154.0f;
  const float right_w = 440.0f;
  vita2d_draw_rectangle(right_x, right_y, right_w, box_h, RGBA8(16, 27, 31, 255));

  const int rows = 10;
  if (bt->inquiry_count > 0) {
    /* Show discovered devices (inquiry results with device type) */
    draw_textf(font, right_x + 10.0f, right_y + 22.0f, C_TEXT, 0.82f,
               "Discovered (%u)", bt->inquiry_count);
    for (int i = 0; i < rows; i++) {
      const uint32_t idx = (uint32_t)i;
      if (idx >= bt->inquiry_count) { break; }
      const BtInquiryResult *d = &bt->discovered[idx];
      const uint64_t age_s = (now_us > d->first_seen_us)
                               ? ((now_us - d->first_seen_us) / 1000000ULL) : 0ULL;
      const float y = right_y + 52.0f + 28.0f * (float)i;
      draw_textf(font, right_x + 10.0f, y, C_TEXT, 0.72f, "%s  [%s]  -%llus",
                 d->mac, d->device_type, (unsigned long long)age_s);
    }
  } else {
    /* Fall back to raw inquiry events */
    draw_textf(font, right_x + 10.0f, right_y + 22.0f, C_TEXT, 0.82f,
               "Inquiry events (%u)", bt->event_count);
    if (bt->event_count == 0) {
      draw_textf(font, right_x + 10.0f, right_y + 58.0f, C_GRID, 0.76f,
                 "No event yet. Start inquiry with TRIANGLE.");
      return;
    }
    for (int i = 0; i < rows; i++) {
      const int idx = (int)bt->event_count - 1 - i;
      if (idx < 0) { break; }
      const BtEventEntry *e = &bt->events[idx];
      const uint64_t age_s = (now_us > e->timestamp_us)
                               ? ((now_us - e->timestamp_us) / 1000000ULL) : 0ULL;
      const float y = right_y + 52.0f + 28.0f * (float)i;
      draw_textf(font, right_x + 10.0f, y, C_TEXT, 0.72f, "id:%02u  %s  -%llus",
                 (unsigned int)e->event_id, e->mac, (unsigned long long)age_s);
    }
  }
}

void render_frame(const NetMonitor *monitor,
                  const LatencyProbeMetrics *latency,
                  const LanScannerMetrics *scanner,
                  const ProxyClientMetrics *proxy,
                  const AlertManager *alerts,
                  const BtMonitorMetrics *bt,
                  ScanDataSource scan_source,
                  const int *scan_view_indices,
                  int scan_view_count,
                  ScanFilterMode scan_filter,
                  ScanSortMode scan_sort,
                  int scan_scroll,
                  int selected_host_index,
                  int host_detail_index,
                  int alerts_scroll,
                  int settings_index,
                  ScanProfile scan_profile,
                  int audio_enabled,
                  const ExportViewer *export_viewer,
                  int exports_scroll,
                  int exports_selected,
                  int exports_compare_index,
                  const NfcScannerMetrics *nfc,
                  int nfc_scroll,
                  const WifiScannerMetrics *wifi_ap,
                  int wifi_scroll,
                  AppScreen screen,
                  vita2d_pgf *font,
                  uint64_t now_us) {
  (void)exports_compare_index;
  vita2d_start_drawing();
  vita2d_clear_screen();

  vita2d_draw_rectangle(0.0f, 0.0f, (float)SCREEN_W, (float)SCREEN_H, C_BG);
  draw_top_nav(font, screen);
  if (screen == APP_SCREEN_STATS) {
    draw_stats_screen(monitor, latency, font, now_us);
  } else if (screen == APP_SCREEN_SCAN) {
    draw_scan_screen(scanner, proxy, scan_source, scan_view_indices, scan_view_count,
                     scan_filter, scan_sort, scan_scroll, selected_host_index, font);
  } else if (screen == APP_SCREEN_HOST_DETAIL) {
    draw_host_detail_screen(scanner, host_detail_index, font);
  } else if (screen == APP_SCREEN_ALERTS) {
    draw_alerts_screen(alerts, alerts_scroll, font, now_us);
  } else if (screen == APP_SCREEN_SETTINGS) {
    draw_settings_screen(font, settings_index, scan_profile, audio_enabled);
  } else if (screen == APP_SCREEN_BT) {
    draw_bt_screen(bt, font, now_us);
  } else if (screen == APP_SCREEN_NFC) {
    draw_nfc_screen(nfc, nfc_scroll, font, now_us);
  } else if (screen == APP_SCREEN_WIFI_AP) {
    draw_wifi_ap_screen(wifi_ap, wifi_scroll, font, now_us);
  } else if (screen == APP_SCREEN_EXPORTS) {
    draw_exports_screen(export_viewer, exports_scroll, exports_selected, exports_compare_index, font);
  } else {
    draw_radar(monitor, now_us);
    draw_oscilloscope(monitor);
    draw_text_block(monitor, font);
    draw_latency_strip(latency, font);
  }
  draw_nav_hint(font, screen);

  vita2d_end_drawing();
  vita2d_swap_buffers();
  sceDisplayWaitVblankStart();
}
