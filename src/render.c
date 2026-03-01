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
    case APP_SCREEN_RADAR: return "RADAR";
    case APP_SCREEN_STATS: return "STATS";
    case APP_SCREEN_SCAN: return "SCAN";
    case APP_SCREEN_HOST_DETAIL: return "DETAIL";
    case APP_SCREEN_HOST_TOOLS: return "TOOLS";
    case APP_SCREEN_TIMELINE: return "TIME";
    case APP_SCREEN_MAP: return "MAP";
    case APP_SCREEN_ALERTS: return "ALERT";
    case APP_SCREEN_RULES: return "RULES";
    case APP_SCREEN_SETTINGS: return "SET";
    case APP_SCREEN_BT: return "BT";
    case APP_SCREEN_EXPORTS: return "EXPORT";
    default: return "UNKNOWN";
  }
}

static const char *profile_name(ScanProfile p) {
  switch (p) {
    case SCAN_PROFILE_QUICK: return "BATTERY";
    case SCAN_PROFILE_NORMAL: return "BALANCED";
    case SCAN_PROFILE_DEEP: return "AUDIT";
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

static void ellipsize_text(vita2d_pgf *font, float scale, const char *src, int max_width, char *dst, size_t dst_len) {
  if (dst_len == 0) return;
  if (src == NULL) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_len, "%s", src);
  if (vita2d_pgf_text_width(font, scale, dst) <= max_width) {
    return;
  }

  const char *dots = "...";
  size_t n = strlen(dst);
  while (n > 0) {
    dst[n - 1] = '\0';
    char trial[256];
    snprintf(trial, sizeof(trial), "%s%s", dst, dots);
    if (vita2d_pgf_text_width(font, scale, trial) <= max_width) {
      snprintf(dst, dst_len, "%s", trial);
      return;
    }
    n--;
  }
  snprintf(dst, dst_len, "%s", dots);
}

static void draw_text_clamped(vita2d_pgf *font, float x, float y, unsigned int color, float scale,
                              int max_width, const char *text) {
  char clipped[256];
  ellipsize_text(font, scale, text, max_width, clipped, sizeof(clipped));
  vita2d_pgf_draw_text(font, x, y, color, scale, clipped);
}

static void draw_text_centered(vita2d_pgf *font, float y, unsigned int color, float scale, const char *text) {
  char clipped[256];
  ellipsize_text(font, scale, text, SCREEN_W - 16, clipped, sizeof(clipped));
  const int width = vita2d_pgf_text_width(font, scale, clipped);
  float x = ((float)SCREEN_W - (float)width) * 0.5f;
  if (x < 8.0f) {
    x = 8.0f;
  }
  vita2d_pgf_draw_text(font, x, y, color, scale, clipped);
}

static void draw_fx_background(const UiFxState *fx) {
  if (fx == NULL) {
    return;
  }
  const float phase = fx->scanline_phase;
  for (int y = 60; y < SCREEN_H - 30; y += 6) {
    const float shift = fmodf((float)y * 0.07f + phase * 20.0f, 1.0f);
    const unsigned int c = (shift > 0.5f) ? RGBA8(14, 24, 26, 60) : RGBA8(8, 14, 16, 48);
    vita2d_draw_rectangle(0.0f, (float)y, (float)SCREEN_W, 1.0f, c);
  }
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
  if (screen == APP_SCREEN_HOST_TOOLS) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "CROSS:DNS  SQUARE:HTTP  TRIANGLE:HTTPS  SELECT:RTT  CIRCLE:back");
    return;
  }
  if (screen == APP_SCREEN_TIMELINE) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  UP/DOWN:select round  START:quit");
    return;
  }
  if (screen == APP_SCREEN_MAP) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  UP/DOWN:node select  START:quit");
    return;
  }
  if (screen == APP_SCREEN_RULES) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  UP/DOWN:rule  CROSS:toggle  START:quit");
    return;
  }
  if (screen == APP_SCREEN_BT) {
    draw_text_centered(font, 525.0f, C_GRID, 0.75f,
                       "L/R:view  TRIANGLE:inquiry start/stop  SELECT:refresh  START:quit");
    return;
  }

  const char *text = (screen == APP_SCREEN_HOST_DETAIL)
                       ? "L/R:view  CROSS:host tools  TRIANGLE:white-list toggle  CIRCLE:back"
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
  const float tab_scale = 0.56f;

  vita2d_draw_rectangle(nav_x, nav_y, nav_w, nav_h, RGBA8(14, 24, 27, 255));
  for (int i = 0; i < APP_SCREEN_COUNT; i++) {
    const char *name = screen_tab_name((AppScreen)i);
    const float slot_x = nav_x + (float)i * slot_w;
    const int text_w = vita2d_pgf_text_width(font, tab_scale, name);
    const float x = slot_x + (slot_w - (float)text_w) * 0.5f;
    const unsigned int color = (i == (int)screen) ? C_ACCENT : C_GRID;
    draw_textf(font, x, 37.0f, color, tab_scale, "%s", name);
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

  if (selected_host_index >= 0 && selected_host_index < view_count) {
    const int host_idx = view_indices[selected_host_index];
    if (host_idx >= 0 && host_idx < (int)raw_host_count) {
      const LanHostResult *h = &hosts[host_idx];
      char pbuf[96];
      format_ports(h, pbuf, sizeof(pbuf));
      vita2d_draw_rectangle(42.0f, 482.0f, 876.0f, 18.0f, RGBA8(22, 40, 41, 255));
      draw_textf(font, 48.0f, 496.0f, C_ACCENT, 0.66f, "Focus %s | %s | ports %s | hint %s",
                 h->ip, h->hostname[0] ? h->hostname : "-", pbuf, h->service_hint[0] ? h->service_hint : "-");
    }
  }
}

static void draw_host_detail_screen(const LanScannerMetrics *scanner,
                                    int selected_host_index,
                                    const AlertRuleConfig *alert_rules,
                                    const HostToolsCache *host_tools,
                                    vita2d_pgf *font) {
  vita2d_draw_rectangle(32.0f, 80.0f, 896.0f, 420.0f, C_PANEL);
  draw_textf(font, 50.0f, 112.0f, C_TEXT, 1.0f, "HOST DETAIL");
  if (scanner->host_count == 0) {
    draw_textf(font, 50.0f, 152.0f, C_WARN, 0.9f, "No host available. Use SCAN first.");
    return;
  }

  if (selected_host_index < 0) selected_host_index = 0;
  if ((uint32_t)selected_host_index >= scanner->host_count) selected_host_index = (int)scanner->host_count - 1;

  const LanHostResult *h = &scanner->hosts[selected_host_index];
  char ports[96];
  char sources[96];
  format_ports(h, ports, sizeof(ports));
  format_sources(h->source_flags, sources, sizeof(sources));
  draw_textf(font, 50.0f, 152.0f, C_TEXT, 0.9f, "IP: %s", h->ip);
  char name_line[128];
  snprintf(name_line, sizeof(name_line), "Name: %s", h->hostname[0] ? h->hostname : "-");
  draw_text_clamped(font, 50.0f, 182.0f, C_TEXT, 0.9f, 820, name_line);
  draw_textf(font, 50.0f, 212.0f, C_TEXT, 0.9f, "Sources: %s", sources);
  char hint_line[128];
  snprintf(hint_line, sizeof(hint_line), "Service hint: %s", h->service_hint[0] ? h->service_hint : "-");
  draw_text_clamped(font, 50.0f, 242.0f, C_TEXT, 0.9f, 820, hint_line);
  draw_textf(font, 50.0f, 272.0f, C_TEXT, 0.9f, "Open ports: %s", ports);
  draw_textf(font, 50.0f, 302.0f, h->is_gateway ? C_ACCENT : C_TEXT, 0.9f, "Role: %s",
             h->is_gateway ? "Gateway" : "Host");
  draw_textf(font, 50.0f, 332.0f, C_GRID, 0.84f, "Last error: %d (0x%08X)", h->last_error, (unsigned int)h->last_error);
  if (alert_rules != NULL) {
    draw_textf(font, 50.0f, 360.0f, C_GRID, 0.82f, "Whitelisted: %s",
               alert_rules_is_whitelisted(alert_rules, h->ip) ? "YES" : "NO");
  }
  if (host_tools != NULL) {
    const HostToolResult *t = NULL;
    for (uint32_t i = 0; i < host_tools->count; i++) {
      if (strcmp(host_tools->entries[i].ip, h->ip) == 0) {
        t = &host_tools->entries[i];
        break;
      }
    }
    if (t != NULL) {
      draw_textf(font, 50.0f, 388.0f, C_TEXT, 0.80f, "Tools: DNS:%s HTTP:%s HTTPS:%s RTT:%dms",
                 (t->dns_ok > 0) ? "OK" : (t->dns_ok == 0) ? "NO" : "-",
                 (t->http_ok > 0) ? "OK" : (t->http_ok == 0) ? "NO" : "-",
                 (t->https_ok > 0) ? "OK" : (t->https_ok == 0) ? "NO" : "-",
                 t->rtt_ms);
    }
  }
}

static void draw_host_tools_screen(const LanScannerMetrics *scanner,
                                   int selected_host_index,
                                   const HostToolsCache *host_tools,
                                   int host_tools_selected,
                                   vita2d_pgf *font) {
  (void)host_tools_selected;
  vita2d_draw_rectangle(32.0f, 80.0f, 896.0f, 420.0f, C_PANEL);
  draw_textf(font, 50.0f, 112.0f, C_TEXT, 1.0f, "HOST TOOLS");
  if (scanner->host_count == 0) {
    draw_textf(font, 50.0f, 152.0f, C_WARN, 0.9f, "No host selected.");
    return;
  }
  if (selected_host_index < 0) selected_host_index = 0;
  if ((uint32_t)selected_host_index >= scanner->host_count) selected_host_index = (int)scanner->host_count - 1;
  const LanHostResult *h = &scanner->hosts[selected_host_index];
  draw_textf(font, 50.0f, 154.0f, C_TEXT, 0.9f, "Host: %s (%s)", h->ip, h->hostname[0] ? h->hostname : "-");
  draw_textf(font, 50.0f, 182.0f, C_GRID, 0.82f, "Manual tests: CROSS DNS | SQUARE HTTP | TRIANGLE HTTPS | SELECT RTT");

  const HostToolResult *t = NULL;
  for (uint32_t i = 0; i < host_tools->count; i++) {
    if (strcmp(host_tools->entries[i].ip, h->ip) == 0) {
      t = &host_tools->entries[i];
      break;
    }
  }
  if (t == NULL) {
    draw_textf(font, 50.0f, 236.0f, C_GRID, 0.9f, "No probe result yet.");
    return;
  }
  draw_textf(font, 50.0f, 236.0f, (t->dns_ok > 0) ? C_PRIMARY : C_WARN, 0.88f, "DNS 53: %s",
             (t->dns_ok > 0) ? "reachable" : (t->dns_ok == 0) ? "no response" : "unknown");
  draw_textf(font, 50.0f, 268.0f, (t->http_ok > 0) ? C_PRIMARY : C_WARN, 0.88f, "HTTP 80: %s",
             (t->http_ok > 0) ? "reachable" : (t->http_ok == 0) ? "no response" : "unknown");
  draw_textf(font, 50.0f, 300.0f, (t->https_ok > 0) ? C_PRIMARY : C_WARN, 0.88f, "HTTPS 443: %s",
             (t->https_ok > 0) ? "reachable" : (t->https_ok == 0) ? "no response" : "unknown");
  draw_textf(font, 50.0f, 332.0f, (t->rtt_ms >= 0) ? C_ACCENT : C_WARN, 0.88f, "RTT probe: %d ms", t->rtt_ms);
  draw_textf(font, 50.0f, 364.0f, C_GRID, 0.82f, "Last error: %d (0x%08X)", t->last_error, (unsigned int)t->last_error);
}

static void draw_timeline_screen(const ScanHistory *history, int scroll, int selected, vita2d_pgf *font) {
  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  draw_textf(font, 42.0f, 104.0f, C_TEXT, 1.0f, "SCAN TIMELINE");
  const int score = scan_history_score(history);
  draw_textf(font, 42.0f, 130.0f, (score >= 75) ? C_PRIMARY : (score >= 55) ? C_ACCENT : C_WARN,
             0.86f, "Stability score: %d/100", score);

  const float lx = 42.0f;
  const float ly = 148.0f;
  const float lw = 430.0f;
  const float lh = 336.0f;
  vita2d_draw_rectangle(lx, ly, lw, lh, RGBA8(16, 27, 31, 255));
  draw_textf(font, lx + 10.0f, ly + 22.0f, C_TEXT, 0.80f, "Rounds (%u)", history->round_count);

  const int rows = 10;
  for (int i = 0; i < rows; i++) {
    const int idx = scroll + i;
    if (idx < 0 || idx >= (int)history->round_count) break;
    const ScanRoundEntry *r = &history->rounds[idx];
    const float y = ly + 34.0f + 30.0f * (float)i;
    const int is_sel = (idx == selected);
    vita2d_draw_rectangle(lx + 6.0f, y, lw - 12.0f, 28.0f, is_sel ? RGBA8(54, 76, 72, 255) : RGBA8(21, 36, 40, 255));
    draw_textf(font, lx + 12.0f, y + 20.0f, is_sel ? C_ACCENT : C_TEXT, 0.72f,
               "#%u hosts:%u +%u -%u ~%u score:%d",
               r->round, r->host_count, r->added, r->removed, r->changed, r->stability_score);
  }

  const float rx = 488.0f;
  const float ry = 148.0f;
  const float rw = 430.0f;
  const float rh = 336.0f;
  vita2d_draw_rectangle(rx, ry, rw, rh, RGBA8(16, 27, 31, 255));
  draw_textf(font, rx + 10.0f, ry + 22.0f, C_TEXT, 0.80f, "Host activity events (%u)", history->event_count);
  for (int i = 0; i < 10; i++) {
    const int idx = (int)history->event_count - 1 - i;
    if (idx < 0) break;
    const HostTimelineEvent *e = &history->events[idx];
    const char *kind = (e->kind == HOST_EVT_JOIN) ? "join" : (e->kind == HOST_EVT_LEAVE) ? "leave" : "change";
    const unsigned int c = (e->kind == HOST_EVT_JOIN) ? C_PRIMARY : (e->kind == HOST_EVT_LEAVE) ? C_WARN : C_ACCENT;
    draw_textf(font, rx + 12.0f, ry + 52.0f + 28.0f * (float)i, c, 0.74f, "%s  %s", e->ip, kind);
  }
}

static void draw_map_screen(const NetMonitor *monitor,
                            const LanScannerMetrics *scanner,
                            int map_selected,
                            vita2d_pgf *font) {
  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  draw_textf(font, 42.0f, 104.0f, C_TEXT, 1.0f, "NETWORK MAP");
  draw_textf(font, 42.0f, 130.0f, C_GRID, 0.8f, "Gateway, Vita and known hosts in local topology view");

  const float cx = 310.0f;
  const float cy = 300.0f;
  const float ring = 160.0f;
  vita2d_draw_fill_circle(cx, cy, 16.0f, C_ACCENT);
  draw_textf(font, cx - 42.0f, cy - 22.0f, C_TEXT, 0.72f, "VITA");
  draw_textf(font, cx - 90.0f, cy + 34.0f, C_GRID, 0.68f, "%s", monitor->ip_address);

  float gwx = cx;
  float gwy = cy - 120.0f;
  vita2d_draw_line(cx, cy, gwx, gwy, C_GRID);
  vita2d_draw_fill_circle(gwx, gwy, 12.0f, C_PRIMARY);
  draw_textf(font, gwx - 34.0f, gwy - 18.0f, C_TEXT, 0.68f, "GW");
  draw_textf(font, gwx - 78.0f, gwy + 26.0f, C_GRID, 0.66f, "%s", monitor->default_route);

  const int count = (scanner->host_count > 20U) ? 20 : (int)scanner->host_count;
  for (int i = 0; i < count; i++) {
    const float a = (6.283185f * (float)i) / (float)((count > 0) ? count : 1);
    const float hx = cx + cosf(a) * ring;
    const float hy = cy + sinf(a) * (ring * 0.78f);
    const int selected = (i == map_selected);
    const unsigned int hc = selected ? C_ACCENT : (scanner->hosts[i].is_gateway ? C_PRIMARY : C_TEXT);
    vita2d_draw_line(cx, cy, hx, hy, RGBA8(28, 52, 54, 180));
    vita2d_draw_fill_circle(hx, hy, selected ? 10.0f : 7.0f, hc);
  }

  if (count > 0 && map_selected >= 0 && map_selected < count) {
    const LanHostResult *h = &scanner->hosts[map_selected];
    draw_textf(font, 560.0f, 186.0f, C_TEXT, 0.9f, "Selected host");
    draw_textf(font, 560.0f, 214.0f, C_TEXT, 0.82f, "IP: %s", h->ip);
      char map_name[128];
      snprintf(map_name, sizeof(map_name), "Name: %s", h->hostname[0] ? h->hostname : "-");
      draw_text_clamped(font, 560.0f, 240.0f, C_TEXT, 0.82f, 340, map_name);
    draw_textf(font, 560.0f, 266.0f, C_TEXT, 0.82f, "Role: %s", h->is_gateway ? "Gateway" : "Host");
    draw_textf(font, 560.0f, 292.0f, C_TEXT, 0.82f, "Ports: %u", h->open_port_count);
  }
}

static void draw_rules_screen(const AlertRuleConfig *cfg, int rules_index, vita2d_pgf *font) {
  if (rules_index < 0) rules_index = 0;
  if (rules_index > 2) rules_index = 2;
  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  draw_textf(font, 42.0f, 104.0f, C_TEXT, 1.0f, "ALERT RULES");
  draw_textf(font, 42.0f, 130.0f, C_GRID, 0.8f, "Smart filtering for noisy networks");

  const unsigned int c0 = (rules_index == 0) ? C_ACCENT : C_TEXT;
  const unsigned int c1 = (rules_index == 1) ? C_ACCENT : C_TEXT;
  const unsigned int c2 = (rules_index == 2) ? C_ACCENT : C_TEXT;
  draw_textf(font, 56.0f, 186.0f, c0, 0.9f, "Unknown-only alerts: %s", cfg->unknown_only ? "ON" : "OFF");
  draw_textf(font, 56.0f, 218.0f, c1, 0.9f, "Sensitive ports only: %s", cfg->sensitive_ports_only ? "ON" : "OFF");
  draw_textf(font, 56.0f, 250.0f, c2, 0.9f, "Quiet hours: %s (%02u:00-%02u:00)",
             cfg->quiet_hours_enabled ? "ON" : "OFF", cfg->quiet_start_h, cfg->quiet_end_h);
  draw_textf(font, 56.0f, 294.0f, C_GRID, 0.8f, "Whitelist count: %u", cfg->whitelist_count);
  for (uint32_t i = 0; i < cfg->whitelist_count && i < 8U; i++) {
    draw_textf(font, 80.0f, 322.0f + 22.0f * (float)i, C_TEXT, 0.74f, "- %s %s",
               cfg->whitelist[i].ip, cfg->whitelist[i].enabled ? "(on)" : "(off)");
  }
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
             "Battery: low-power | Balanced: default | Audit: slower/full coverage");
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
      draw_text_clamped(font, left_x + 12.0f, y + 20.0f, selected ? C_ACCENT : C_TEXT, 0.72f, (int)left_w - 30, viewer->exports[idx].label);
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
  char file_line[128];
  snprintf(file_line, sizeof(file_line), "File: %s", e->label);
  draw_text_clamped(font, right_x + 10.0f, right_y + 54.0f, C_TEXT, 0.82f, (int)right_w - 20, file_line);
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

  char base_line[128];
  snprintf(base_line, sizeof(base_line), "Delta vs baseline: %s", base->label);
  draw_text_clamped(font, right_x + 10.0f, right_y + 274.0f, C_ACCENT, 0.78f, (int)right_w - 20, base_line);
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
    char line[256];
    snprintf(line, sizeof(line), "[%06us] %s", sec, e.text);
    draw_text_clamped(font, 50.0f, 176.0f + i * 30.0f, color, 0.78f, 860, line);
  }
}

static void draw_bt_screen(const BtMonitorMetrics *bt, const BtStore *bt_store, vita2d_pgf *font, uint64_t now_us) {
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
    char name_line[96];
    snprintf(name_line, sizeof(name_line), "%u) %s", i + 1U, d->name);
    draw_text_clamped(font, left_x + 12.0f, y, C_TEXT, 0.74f, (int)left_w - 24, name_line);
    draw_textf(font, left_x + 34.0f, y + 16.0f, C_GRID, 0.70f, "%s  class:0x%06X", d->mac, d->bt_class);
  }
  if (bt->paired_count == 0) {
    draw_textf(font, left_x + 12.0f, left_y + 58.0f, C_GRID, 0.76f, "No paired device reported.");
  }

  const float right_x = 478.0f;
  const float right_y = 154.0f;
  const float right_w = 440.0f;
  vita2d_draw_rectangle(right_x, right_y, right_w, box_h, RGBA8(16, 27, 31, 255));
  draw_textf(font, right_x + 10.0f, right_y + 22.0f, C_TEXT, 0.82f, "Seen devices (%u)", bt_store->count);
  if (bt_store->count == 0) {
    draw_textf(font, right_x + 10.0f, right_y + 58.0f, C_GRID, 0.76f, "No seen device yet. Start inquiry with TRIANGLE.");
  } else {
    for (int i = 0; i < 10; i++) {
      const int idx = (int)bt_store->count - 1 - i;
      if (idx < 0) break;
      const BtSeenDevice *d = &bt_store->devices[idx];
      const uint64_t age_s = (now_us > d->last_seen_us) ? ((now_us - d->last_seen_us) / 1000000ULL) : 0ULL;
      const float y = right_y + 52.0f + 28.0f * (float)i;
      char seen_line[160];
      snprintf(seen_line, sizeof(seen_line), "%s %s (%s) -%llus",
               d->mac, d->name[0] ? d->name : "-", d->type, (unsigned long long)age_s);
      draw_text_clamped(font, right_x + 10.0f, y, C_TEXT, 0.72f, (int)right_w - 20, seen_line);
    }
  }
}

void render_frame(const NetMonitor *monitor,
                  const LatencyProbeMetrics *latency,
                  const LanScannerMetrics *scanner,
                  const ProxyClientMetrics *proxy,
                  const AlertManager *alerts,
                  const BtMonitorMetrics *bt,
                  const BtStore *bt_store,
                  const ScanHistory *history,
                  const HostToolsCache *host_tools,
                  const AlertRuleConfig *alert_rules,
                  const UiFxState *ui_fx,
                  ScanDataSource scan_source,
                  const int *scan_view_indices,
                  int scan_view_count,
                  ScanFilterMode scan_filter,
                  ScanSortMode scan_sort,
                  int scan_scroll,
                  int selected_host_index,
                  int host_detail_index,
                  int timeline_scroll,
                  int timeline_selected,
                  int map_selected,
                  int host_tools_selected,
                  int alerts_scroll,
                  int settings_index,
                  int rules_index,
                  ScanProfile scan_profile,
                  int audio_enabled,
                  const ExportViewer *export_viewer,
                  int exports_scroll,
                  int exports_selected,
                  int exports_compare_index,
                  AppScreen screen,
                  vita2d_pgf *font,
                  uint64_t now_us) {
  (void)exports_compare_index;
  vita2d_start_drawing();
  vita2d_clear_screen();

  vita2d_draw_rectangle(0.0f, 0.0f, (float)SCREEN_W, (float)SCREEN_H, C_BG);
  draw_fx_background(ui_fx);
  draw_top_nav(font, screen);
  draw_textf(font, 722.0f, 64.0f, monitor->connected ? C_PRIMARY : C_WARN, 0.62f, "WIFI");
  draw_textf(font, 772.0f, 64.0f, scanner->running ? C_ACCENT : C_GRID, 0.62f, "SCAN");
  draw_textf(font, 832.0f, 64.0f, bt->bt_enabled ? C_PRIMARY : C_GRID, 0.62f, "BT");
  draw_textf(font, 870.0f, 64.0f, (export_viewer->count > 0) ? C_PRIMARY : C_GRID, 0.62f, "EXP");
  if (screen == APP_SCREEN_STATS) {
    draw_stats_screen(monitor, latency, font, now_us);
  } else if (screen == APP_SCREEN_SCAN) {
    draw_scan_screen(scanner, proxy, scan_source, scan_view_indices, scan_view_count,
                     scan_filter, scan_sort, scan_scroll, selected_host_index, font);
  } else if (screen == APP_SCREEN_HOST_DETAIL) {
    draw_host_detail_screen(scanner, host_detail_index, alert_rules, host_tools, font);
  } else if (screen == APP_SCREEN_HOST_TOOLS) {
    draw_host_tools_screen(scanner, host_detail_index, host_tools, host_tools_selected, font);
  } else if (screen == APP_SCREEN_TIMELINE) {
    draw_timeline_screen(history, timeline_scroll, timeline_selected, font);
  } else if (screen == APP_SCREEN_MAP) {
    draw_map_screen(monitor, scanner, map_selected, font);
  } else if (screen == APP_SCREEN_ALERTS) {
    draw_alerts_screen(alerts, alerts_scroll, font, now_us);
  } else if (screen == APP_SCREEN_RULES) {
    draw_rules_screen(alert_rules, rules_index, font);
  } else if (screen == APP_SCREEN_SETTINGS) {
    draw_settings_screen(font, settings_index, scan_profile, audio_enabled);
  } else if (screen == APP_SCREEN_BT) {
    draw_bt_screen(bt, bt_store, font, now_us);
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
