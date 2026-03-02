# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PS Vita homebrew application (C11, VitaSDK + vita2d) that monitors Wi-Fi and scans the LAN from userland. Title ID: `VWSC00001`.

## Build Commands

**Prerequisites:** VitaSDK configured (`$VITASDK` env var), `cmake`, `make`, `vita2d` in SDK.

```bash
# Standard build
mkdir -p build && cd build && cmake .. && make -j

# Build with version bump (increments VITA_VERSION in CMakeLists.txt)
./scripts/build_and_bump.sh
```

Build artifacts: `build/vita_wifi_scope.vpk`, `build/vita_wifi_scope.self`

```bash
# Deploy to Vita over FTP (requires FTP server running on Vita, e.g. VitaShell)
FTP_HOST=<vita-ip> ./scripts/deploy_ftp.sh
# Optional flags: FTP_PORT (default 1337), FTP_TARGET_DIR (default ux0:/downloads)
# --diag flag probes the FTP endpoint before uploading
```

There is no linting or test suite; the build itself (with `-Wall -Wextra -Wpedantic`) serves as the primary validation step.

## Architecture

The app is single-threaded, frame-loop based. `src/main.c` owns the main loop: it polls all subsystems, handles controller input, and calls `render_frame()` each iteration.

### Module Responsibilities

| Module | Role |
|--------|------|
| `net_monitor.*` | Polls Wi-Fi state via `SceNetCtl`: SSID, RSSI, IP/mask/gateway/DNS, RSSI ring-buffer history (8 Hz, 60s) |
| `latency_probe.*` | Background TCP/UDP RTT measurement; exposes RTT last/min/max/avg/EMA, p50/p95, loss stats |
| `lan_scanner.*` | Cooperative (tick-based, non-blocking) /24 LAN scan using TCP connect + ICMP; integrates `DiscoveryEngine` |
| `discovery.*` | Passive mDNS/SSDP/NBNS listener that enriches `LanHostResult` with hostnames and service hints |
| `alerts.*` | Ring buffer of timestamped alert events (INFO/WARN/ERROR); consumed by ALERTS screen |
| `export_json.*` | Writes scan snapshots as JSON to `ux0:` filesystem; maintains an index file |
| `export_viewer.*` | Reads and parses the snapshot index; supports baseline comparison |
| `proxy_client.*` | Optional TCP channel to an external proxy (e.g. Raspberry Pi) for receiving remote scan data |
| `bt_monitor.*` | Reads Bluetooth state via `SceBt`; supports inquiry start/stop |
| `ui_audio.*` | Thin wrapper around `SceAudio` for UI sound feedback events |
| `render.*` | All vita2d drawing; receives read-only metrics structs and current UI state, no side-effects |

### Data Flow

```
net_monitor_poll() → LanScanner (ip/gateway hints) → lan_scanner_tick()
                                                     └─ DiscoveryEngine (passive UDP listeners)

All metrics structs → render_frame() (read-only)
Controller input   → main.c state machine → subsystem calls
```

### Screens (`AppScreen` enum in `render.h`)

`RADAR` → `STATS` → `SCAN` → `HOST_DETAIL` → `ALERTS` → `SETTINGS` → `BT` → `EXPORTS`

Navigate with L/R triggers (cycle). SQUARE toggles between SCAN and EXPORTS directly.

### Key Design Constraints

- **No dynamic allocation in the frame loop.** All subsystem structs use fixed-size arrays (`LAN_SCANNER_MAX_HOSTS = 128`, `NETMON_HISTORY_CAPACITY = 480`).
- **Cooperative scanning:** `lan_scanner_tick()` advances one probe step per frame call, capped per step to avoid UI stalls. Scan does **not** auto-start; it requires explicit user input (`TRIANGLE` on SCAN screen) or `local_scan_armed = true`.
- **Metrics are copied out:** Callers call `*_get_metrics()` to get a snapshot struct; render only sees those snapshots.
- **VITA_VERSION** format is `MM.mm` (two-digit major, two-digit minor), managed by `build_and_bump.sh`.
