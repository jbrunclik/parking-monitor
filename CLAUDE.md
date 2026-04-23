# Parking Monitor

Solar-powered ESP32-CAM that watches a street parking spot ("pole position") and sends notifications when it's free.

## Architecture

```
controller/    ESP32-CAM firmware (PlatformIO / Arduino)
server/        API receiver — accepts images from ESP32, runs as --user systemd unit
processor/     Image analysis — runs as --user systemd timer, detects parked cars
```

## Stack

| Component   | Stack                                  |
|-------------|----------------------------------------|
| Controller  | PlatformIO, Arduino framework, ESP32-CAM |
| Server      | Python, uv, ruff                       |
| Processor   | Python, uv, ruff                       |

## Conventions

- **Python**: managed with `uv`. Lint/format with `ruff`. Follow latest standards.
- **ESP32**: PlatformIO project in `controller/`. WiFi credentials via `.env` → build flags (never committed).
- **Build**: all targets go through `Makefile` at repo root.
- **Secrets**: `.env` at repo root, `.gitignore`'d. Copy `.env.example` to `.env` and fill in.

## Make targets

```
make controller-build      # build ESP32 firmware
make controller-flash      # flash via USB programmer
make controller-flash-ota  # flash over WiFi (OTA)
make controller-monitor    # serial monitor (115200 baud)
make lint                  # ruff check + format check
make format                # ruff format
make sync                  # uv sync for Python components
```

## ESP32-CAM hardware

- Board: AI-Thinker ESP32-CAM with OV2640 camera
- Programmer: ESP32-CAM-MB (CH340C USB-C)
- External antenna mod: 0Ω resistor moved to IPEX connector
- Camera swap: 120° wide-angle OV2640 on 75mm flex cable
- Power: 2x 18650 → TP4056 → HT7333-A LDO → 3.3V pin
- Solar: 6V/3.5W panel → TP4056 charging input
- Battery target: 2x 18650 must last 3+ months (deep sleep in production)

## ESP32-CAM gotchas (learned the hard way)

- **XCLK must be 10MHz, not 20MHz.** The 20MHz camera clock on GPIO0 generates harmonics that cripple WiFi throughput. Symptom: even tiny HTTP responses take 10-15s.
- **`WiFi.setSleep(false)` hurts on ESP32-CAM.** Causes DMA bus contention with the camera. Let WiFi manage its own power.
- **`esp_http_server` stack size defaults to 4096 — too small.** Use 8192 for UI/API, 16384 for MJPEG stream. Symptom: `Stack canary watchpoint triggered (httpd)` crash.
- **Large string literals in handlers must be `static const`.** A 2KB `const char html[]` on a 4KB stack = instant stack overflow.
- **Run MJPEG stream on a separate server (port 81).** The stream handler's infinite loop blocks the HTTP task. Port 80 for UI/API, port 81 for stream.
- **MJPEG part header buffer must be ≥128 bytes.** The boundary + Content-Type + Content-Length header is ~80 chars. A 64-byte buffer silently truncates → browser gets malformed MIME → no video.
- **PlatformIO env vars: use `${sysenv.VAR}`.** Bare `${VAR}` in platformio.ini resolves PlatformIO internal vars, not OS environment variables. Silently returns empty.
- **C stringify macro for build flags.** Quoting SSID/password through PlatformIO → SCons → shell → GCC is fragile. Use `-DWIFI_SSID=${sysenv.WIFI_SSID}` + `#define STR(x) _STR(x)` / `#define _STR(x) #x` in C.
- **OV2640 auto-exposure DSP (`aec2`) is off by default.** Enable it for outdoor use or images will be dark.
- **XCLK cripples WiFi via PCB antenna coupling.** Even at 10MHz the camera clock interferes with 2.4GHz. Workaround: `esp_camera_deinit()` after capture, copy frame to PSRAM, send over WiFi with clock stopped. Permanent fix: external antenna mod (IPEX connector).
- **Auto-exposure needs warm-up frames after init.** Discard 4 frames with 100ms delay before the real capture, otherwise the image is dark.
- **`WiFi.setSleep(false)` is fine with on-demand capture.** The earlier concern about DMA bus contention only applies to continuous camera streaming. With init/deinit per capture, WiFi sleep disable is safe and eliminates modem-sleep latency.

## Firmware architecture

- **Bench test firmware** (`controller/src/main.cpp`): capture-on-demand web server. Camera init/deinit per capture to avoid XCLK→WiFi interference. Not for production — too power-hungry.
- **Production firmware** (TODO): deep sleep cycle — wake → capture → POST JPEG to API → sleep. OTA updates via ArduinoOTA.
- **OTA**: ArduinoOTA enabled, hostname `parking-monitor`. Set `ESP32_IP` in `.env` for `make controller-flash-ota`.

## Current goal

Phase 1 bench testing — verify WiFi + camera stream from terrace, confirm parking spot is visible in frame.
