// parking-monitor / ESP32-CAM bench test firmware
// Connects to WiFi and starts a camera web server for live streaming.
// Uses esp_http_server (async, runs on its own task) instead of Arduino WebServer.
// Flash with: make controller-flash

#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>
#include <ArduinoOTA.h>

// ---------------------------------------------------------------------------
// AI-Thinker ESP32-CAM pin definitions
// ---------------------------------------------------------------------------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Built-in flash LED
#define FLASH_GPIO_NUM     4

// ---------------------------------------------------------------------------
// WiFi credentials (injected via build flags from .env)
// Stringification avoids quoting hell across PlatformIO -> SCons -> shell -> GCC
// ---------------------------------------------------------------------------
#define _STR(x) #x
#define STR(x) _STR(x)

#ifndef WIFI_SSID
#error "WIFI_SSID not defined — copy .env.example to .env and fill in your WiFi credentials"
#endif
#ifndef WIFI_PASS
#error "WIFI_PASS not defined — copy .env.example to .env and fill in your WiFi credentials"
#endif

const char *ssid = STR(WIFI_SSID);
const char *password = STR(WIFI_PASS);

// MJPEG stream disabled until external antenna mod — XCLK kills WiFi via PCB antenna

// ---------------------------------------------------------------------------
// Camera init
// ---------------------------------------------------------------------------
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 10000000;   // 10MHz — 20MHz harmonics cripple WiFi
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;  // capture on demand, not continuous DMA

    if (psramFound()) {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 20;
        config.fb_count = 1;               // single buffer — no background DMA
        config.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 20;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }

    // Start at VGA for streaming — good balance of quality vs speed
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_vflip(s, 0);
    s->set_hmirror(s, 0);
    s->set_brightness(s, 1);      // -2 to 2
    s->set_exposure_ctrl(s, 1);   // auto exposure on
    s->set_aec2(s, 1);            // auto exposure DSP on
    s->set_gain_ctrl(s, 1);       // auto gain on
    s->set_awb_gain(s, 1);        // auto white balance gain on

    return true;
}

// ---------------------------------------------------------------------------
// HTTP handlers (esp_http_server — async, runs on its own RTOS task)
// ---------------------------------------------------------------------------

static esp_err_t index_handler(httpd_req_t *req) {
    static const char html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>parking-monitor // bench test</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{
  background:#1e1e2e;color:#cdd6f4;
  font-family:'SF Mono','Cascadia Code','Fira Code','JetBrains Mono',
    'Menlo','Consolas','Liberation Mono',monospace;
  display:flex;flex-direction:column;align-items:center;
  min-height:100vh;padding:20px 16px;
}
h1{
  font-size:18px;font-weight:600;letter-spacing:.5px;
  color:#b4befe;margin-bottom:16px;
}
h1 span{color:#6c7086;font-weight:400}
.stats{
  display:flex;flex-wrap:wrap;justify-content:center;gap:6px 14px;
  padding:10px 16px;margin-bottom:16px;
  background:#181825;border:1px solid #313244;border-radius:6px;
  font-size:13px;max-width:660px;width:100%;
}
.stat{color:#a6adc8}
.stat b{color:#a6e3a1}
.stat.warn b{color:#f9e2af}
.cam-wrap{
  max-width:660px;width:100%;
  background:#181825;border:1px solid #313244;border-radius:6px;
  overflow:hidden;line-height:0;
}
.cam-wrap img{width:100%;display:block}
.controls{
  display:flex;gap:8px;margin-top:14px;
}
.btn{
  background:#313244;color:#cdd6f4;
  border:1px solid #6c7086;border-radius:4px;
  padding:8px 20px;font-family:inherit;font-size:13px;
  cursor:pointer;transition:background .15s,border-color .15s;
}
.btn:hover{background:#45475a;border-color:#89b4fa}
.btn:active{background:#585b70}
</style>
</head>
<body>
<h1>parking-monitor <span>//</span> bench test</h1>
<div class="stats" id="stats">
  <span class="stat">connecting...</span>
</div>
<div class="cam-wrap">
  <img id="cam" alt="camera feed">
</div>
<div class="controls">
  <button class="btn" id="btnSnap">Capture</button>
  <button class="btn" id="btnAuto">Auto: OFF</button>
</div>
<script>
(function(){
  var cam=document.getElementById('cam');
  var autoBtn=document.getElementById('btnAuto');
  var autoMode=false, capturing=false;

  function capture(){
    if(capturing)return;
    capturing=true;
    cam.src='/capture?'+Date.now();
  }
  cam.onload=function(){capturing=false;};
  cam.onerror=function(){capturing=false;};

  document.getElementById('btnSnap').onclick=capture;
  autoBtn.onclick=function(){
    autoMode=!autoMode;
    autoBtn.textContent='Auto: '+(autoMode?'ON':'OFF');
  };
  // Auto-capture every 5s when enabled
  setInterval(function(){if(autoMode)capture();},5000);
  capture();

  function fmt(b){
    return b>1048576?(b/1048576).toFixed(1)+'MB'
      :b>1024?(b/1024).toFixed(0)+'KB':b+'B';
  }
  function uptime(s){
    if(s<60)return s+'s';
    if(s<3600)return Math.floor(s/60)+'m '+s%60+'s';
    var h=Math.floor(s/3600),m=Math.floor(s%3600/60);
    return h+'h '+m+'m';
  }
  var pending=false;
  function update(){
    if(pending)return;
    pending=true;
    fetch('/status').then(function(r){return r.json()}).then(function(d){
      var w=d.heap_min<20000?' warn':'';
      document.getElementById('stats').innerHTML=
        '<span class="stat">RSSI <b>'+d.rssi+' dBm</b></span>'+
        '<span class="stat'+w+'">Heap <b>'+fmt(d.heap)+'</b> (min '+fmt(d.heap_min)+')</span>'+
        '<span class="stat">PSRAM <b>'+fmt(d.psram)+'</b> (min '+fmt(d.psram_min)+')</span>'+
        '<span class="stat">Uptime <b>'+uptime(d.uptime)+'</b></span>';
    }).catch(function(){}).then(function(){pending=false;});
  }
  update();
  setInterval(update,5000);
})();
</script>
</body>
</html>
)rawliteral";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, sizeof(html) - 1);
}

static esp_err_t capture_handler(httpd_req_t *req) {
    // Init camera, grab frame, deinit — stops XCLK interference with WiFi
    if (!initCamera()) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Discard warm-up frames — auto-exposure needs time to converge after init
    for (int i = 0; i < 4; i++) {
        camera_fb_t *discard = esp_camera_fb_get();
        if (discard) esp_camera_fb_return(discard);
        delay(100);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        esp_camera_deinit();
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    // Copy frame to PSRAM before deinit (fb is invalid after deinit)
    size_t len = fb->len;
    uint8_t *buf = (uint8_t *)ps_malloc(len);
    if (!buf) {
        esp_camera_fb_return(fb);
        esp_camera_deinit();
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    memcpy(buf, fb->buf, len);
    esp_camera_fb_return(fb);
    esp_camera_deinit();

    // Send with XCLK stopped — fast WiFi
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(req, (const char *)buf, len);
    free(buf);
    return res;
}

// NOTE: MJPEG stream disabled — XCLK interference with PCB antenna makes
// continuous streaming impossible. Using capture-on-demand instead.
// Re-enable after external antenna mod (Step 2 in assembly guide).

static esp_err_t status_handler(httpd_req_t *req) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"rssi\":%d,\"heap\":%u,\"heap_min\":%u,\"psram\":%u,\"psram_min\":%u,"
             "\"uptime\":%lu,\"ip\":\"%s\"}",
             WiFi.RSSI(),
             ESP.getFreeHeap(), ESP.getMinFreeHeap(),
             ESP.getFreePsram(), ESP.getMinFreePsram(),
             millis() / 1000,
             WiFi.localIP().toString().c_str());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, strlen(buf));
}

httpd_handle_t startWebServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 4;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        Serial.println("Failed to start HTTP server");
        return NULL;
    }

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler };
    httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &capture_uri);
    httpd_register_uri_handler(server, &status_uri);

    return server;
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("=== parking-monitor bench test ===");

    // Flash LED off
    pinMode(FLASH_GPIO_NUM, OUTPUT);
    digitalWrite(FLASH_GPIO_NUM, LOW);

    // Camera — init once to verify it works, then deinit to stop XCLK interference.
    // XCLK on GPIO0 cripples WiFi via PCB antenna coupling.
    // Each capture does init → capture → deinit. Antenna mod eliminates this need.
    if (!initCamera()) {
        Serial.println("FATAL: Camera init failed. Check flex cable.");
        while (true) delay(1000);
    }
    Serial.println("Camera OK (deinit to free WiFi)");
    esp_camera_deinit();

    // WiFi
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);  // disable modem sleep — safe now that camera DMA is on-demand
    Serial.print("Connecting to WiFi");
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFATAL: WiFi connection failed. Check SSID/password.");
        while (true) delay(1000);
    }
    Serial.println();
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    // OTA updates
    ArduinoOTA.setHostname("parking-monitor");
    ArduinoOTA.onStart([]() {
        esp_camera_deinit();
        Serial.println("OTA update starting...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA done. Rebooting.");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA: %u%%\r", progress * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA error [%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("auth failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("begin failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("connect failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("receive failed");
        else if (error == OTA_END_ERROR) Serial.println("end failed");
    });
    ArduinoOTA.begin();
    Serial.println("OTA ready (hostname: parking-monitor)");

    // HTTP servers (each runs on its own RTOS task)
    // Port 80: UI + API + capture.  Port 81: MJPEG stream (separate so it doesn't block API)
    startWebServer();
    Serial.println("HTTP server on port 80 (capture-on-demand)");
    Serial.println("Open http://" + WiFi.localIP().toString() + " in your browser");
}

unsigned long lastStatusLog = 0;

void loop() {
    ArduinoOTA.handle();

    // Log status every 30s for serial monitoring
    if (millis() - lastStatusLog > 30000) {
        lastStatusLog = millis();
        Serial.printf("[%lus] heap=%u (min=%u) psram=%u rssi=%d\n",
                      millis() / 1000, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      ESP.getFreePsram(), WiFi.RSSI());
    }

    delay(1);
}
