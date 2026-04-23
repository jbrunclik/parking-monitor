# parking-monitor

Solar-powered ESP32-CAM that watches a street parking spot and sends notifications when it's free.

## Components

| Directory    | What                                              |
|--------------|---------------------------------------------------|
| `controller/`| ESP32-CAM firmware (PlatformIO / Arduino)         |
| `server/`    | API receiver — accepts images, runs as systemd unit |
| `processor/` | Image analysis — systemd timer, detects parked cars |

## Quick start

```bash
# 1. Set up WiFi credentials
cp .env.example .env
# edit .env with your SSID and password

# 2. Build and flash ESP32-CAM (plug in via USB-C programmer)
make controller-flash

# 3. Open serial monitor to get the IP address
make controller-monitor

# 4. Open the IP in your browser — live camera feed
#    Stream is on port 81 (http://<ip>:81/stream)
```

## OTA updates

After the first USB flash, note the ESP32's IP from the serial monitor and add it to `.env`:

```bash
echo "ESP32_IP=10.0.0.176" >> .env
```

Then flash over WiFi (no need to open the enclosure):

```bash
make controller-flash-ota
```

## Requirements

- [PlatformIO](https://platformio.org/) — ESP32 toolchain
- [uv](https://docs.astral.sh/uv/) — Python package manager
- ESP32-CAM (AI-Thinker) + ESP32-CAM-MB programmer

## Make targets

```
make help                 # list all targets
make controller-build     # build firmware
make controller-flash     # flash via USB
make controller-flash-ota # flash over WiFi (OTA)
make controller-monitor   # serial monitor
make lint                 # ruff lint
make format               # ruff format
make sync                 # uv sync
```
