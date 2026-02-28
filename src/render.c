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

static void draw_textf(vita2d_pgf *font, float x, float y, unsigned int color, float scale,
                       const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  vita2d_pgf_draw_text(font, x, y, color, scale, buffer);
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

  draw_textf(font, 32.0f, 45.0f, C_TEXT, 1.0f, "VITA WIFI SCOPE");
  draw_textf(font, 32.0f, 72.0f, C_GRID, 0.8f, "Userland mode | %d Hz sample | 60 fps render", NETMON_SAMPLE_HZ);

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

  vita2d_draw_rectangle(465.0f, 74.0f, 450.0f, 42.0f, C_PANEL);
  draw_textf(font, 476.0f, 100.0f, C_TEXT, 0.8f, "%s %s:%d", proto, latency->target_ip, latency->target_port);

  if (latency->probes_ok > 0) {
    draw_textf(font, 700.0f, 100.0f, color, 0.8f, "RTT %dms | Loss %.1f%%", latency->last_rtt_ms, latency->loss_percent);
  } else {
    draw_textf(font, 700.0f, 100.0f, C_WARN, 0.8f, "RTT N/A | Loss %.1f%%", latency->loss_percent);
  }
}

static void draw_nav_hint(vita2d_pgf *font, AppScreen screen) {
  if (screen == APP_SCREEN_SCAN) {
    draw_textf(font, 345.0f, 525.0f, C_GRID, 0.8f,
               "L/R: switch | TRIANGLE: start/stop scan | SELECT: rescan | UP/DOWN: scroll | START: quit");
    return;
  }

  const char *name = (screen == APP_SCREEN_RADAR) ? "RADAR" : "STATS";
  draw_textf(font, 688.0f, 525.0f, C_GRID, 0.8f, "L/R: switch view | START: quit | view: %s", name);
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

static void draw_scan_screen(const LanScannerMetrics *scanner,
                             const ProxyClientMetrics *proxy,
                             ScanDataSource source,
                             int scroll,
                             vita2d_pgf *font) {
  const int showing_proxy = (source == SCAN_SOURCE_PROXY);

  const char *mode_name = showing_proxy ? "PROXY" : "LOCAL";
  const unsigned int mode_color = showing_proxy ? C_ACCENT : C_PRIMARY;

  vita2d_draw_rectangle(24.0f, 72.0f, 912.0f, 430.0f, C_PANEL);
  vita2d_draw_line(24.0f, 118.0f, 936.0f, 118.0f, C_GRID);
  draw_textf(font, 42.0f, 105.0f, C_TEXT, 1.0f, "LAN SCANNER");
  draw_textf(font, 240.0f, 105.0f, mode_color, 0.9f, "[%s]", mode_name);

  uint32_t host_count = 0;
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
    host_count = proxy->host_count;
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
    host_count = scanner->host_count;
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
  draw_textf(font, 42.0f, 196.0f, C_GRID, 0.78f, "Last error: %d (0x%08X)", last_error, (unsigned int)last_error);
  if (!showing_proxy) {
    const char *icmp_state = (icmp_supported > 0) ? "yes" : (icmp_supported == 0) ? "no" : "unknown";
    draw_textf(font, 360.0f, 196.0f, C_GRID, 0.78f,
               "ICMP:%s mDNS:%s SSDP:%s NBNS:%s",
               icmp_state,
               scanner->mdns_running ? "on" : "off",
               scanner->ssdp_running ? "on" : "off",
               scanner->nbns_running ? "on" : "off");
  }
  draw_textf(font, 650.0f, 216.0f, C_GRID, 0.72f, "Hits M:%u S:%u N:%u I:%u T:%u",
             scanner->mdns_hits, scanner->ssdp_hits, scanner->nbns_hits, scanner->icmp_hits, scanner->tcp_hits);
  draw_textf(font, 790.0f, 196.0f, C_GRID, 0.78f, "Rows: %u", host_count);
  (void)subnet;

  const float table_x = 42.0f;
  const float table_y = 238.0f;
  const float table_w = 876.0f;
  const float row_h = 24.0f;
  const int rows_visible = 10;

  vita2d_draw_rectangle(table_x, table_y, table_w, row_h, RGBA8(26, 44, 47, 255));
  draw_textf(font, table_x + 10.0f, table_y + 18.0f, C_TEXT, 0.75f, "IP");
  draw_textf(font, table_x + 140.0f, table_y + 18.0f, C_TEXT, 0.75f, "Name");
  draw_textf(font, table_x + 430.0f, table_y + 18.0f, C_TEXT, 0.75f, "Source");
  draw_textf(font, table_x + 620.0f, table_y + 18.0f, C_TEXT, 0.75f, "Ports");
  draw_textf(font, table_x + 780.0f, table_y + 18.0f, C_TEXT, 0.75f, "Status");

  for (int r = 0; r < rows_visible; r++) {
    const uint32_t idx = (uint32_t)(scroll + r);
    const float y = table_y + row_h + row_h * (float)r;
    vita2d_draw_rectangle(table_x, y, table_w, row_h - 1.0f, RGBA8(19, 33, 36, 255));

    if (idx < host_count) {
      const LanHostResult *host = &hosts[idx];
      char ports[96];
      char sources[96];
      format_ports(host, ports, sizeof(ports));
      format_sources(host->source_flags, sources, sizeof(sources));
      draw_textf(font, table_x + 10.0f, y + 18.0f, C_TEXT, 0.74f, "%s", host->ip);
      draw_textf(font, table_x + 140.0f, y + 18.0f, C_TEXT, 0.74f, "%s",
                 host->hostname[0] != '\0' ? host->hostname : "-");
      draw_textf(font, table_x + 430.0f, y + 18.0f, C_TEXT, 0.74f, "%s", sources);
      draw_textf(font, table_x + 620.0f, y + 18.0f, C_TEXT, 0.74f, "%s", ports);
      draw_textf(font, table_x + 780.0f, y + 18.0f, host->is_gateway ? C_ACCENT : C_PRIMARY,
                 0.74f, host->is_gateway ? "gateway" : "alive");
    } else {
      draw_textf(font, table_x + 12.0f, y + 18.0f, C_GRID, 0.78f, "-");
    }
  }
}

void render_frame(const NetMonitor *monitor,
                  const LatencyProbeMetrics *latency,
                  const LanScannerMetrics *scanner,
                  const ProxyClientMetrics *proxy,
                  ScanDataSource scan_source,
                  int scan_scroll,
                  AppScreen screen,
                  vita2d_pgf *font,
                  uint64_t now_us) {
  vita2d_start_drawing();
  vita2d_clear_screen();

  vita2d_draw_rectangle(0.0f, 0.0f, (float)SCREEN_W, (float)SCREEN_H, C_BG);
  if (screen == APP_SCREEN_STATS) {
    draw_stats_screen(monitor, latency, font, now_us);
  } else if (screen == APP_SCREEN_SCAN) {
    draw_scan_screen(scanner, proxy, scan_source, scan_scroll, font);
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
