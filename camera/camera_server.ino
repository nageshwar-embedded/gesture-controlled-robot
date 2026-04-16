#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include <lwip/sockets.h>

const char* ssid     = "Nageshwar_Singh_Car";
const char* password = "12345678";

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
#define FLASH_LED_PIN      4

#define MIN_HEAP_BYTES    20000
#define MAX_STREAM_FAILS  8
#define STREAM_TIMEOUT_MS 6000

bool cameraEnabled               = false;
bool flashEnabled                = false;
bool cameraReady                 = false;
bool needRestart                 = false;
bool flashJustChanged            = false;
volatile bool snapshotInProgress = false;
volatile bool streamRestart      = false; // ── quality switch signal ──

// ── 0=FAST: QVGA q15 | 1=QUALITY: VGA q12 ──
// q12 instead of q8 — smaller frames, less power, less FB-OVF
int qualityMode = 0;

framesize_t getFrameSize()   { return qualityMode==0 ? FRAMESIZE_QVGA : FRAMESIZE_VGA; }
int         getJpegQuality() { return qualityMode==0 ? 15 : 12; }
const char* getModeName()    { return qualityMode==0 ? "FAST"    : "QUALITY"; }
const char* getModeRes()     { return qualityMode==0 ? "320x240" : "640x480"; }

httpd_handle_t stream_httpd  = NULL;
httpd_handle_t control_httpd = NULL;

// ════════════════════════════════════════════════════════════
//  CAMERA INIT
// ════════════════════════════════════════════════════════════
bool initCamera() {
  esp_camera_deinit();
  delay(150);

  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sscb_sda  = SIOD_GPIO_NUM;
  config.pin_sscb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.fb_count      = 1;
  config.grab_mode     = CAMERA_GRAB_LATEST;
  config.frame_size    = getFrameSize();
  config.jpeg_quality  = getJpegQuality();
  config.fb_location   = CAMERA_FB_IN_DRAM;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    Serial.println("PSRAM found");
  }

  esp_err_t err = ESP_FAIL;
  for (int i=1; i<=3; i++) {
    Serial.printf("Camera init %d/3 [%s]...\n", i, getModeName());
    err = esp_camera_init(&config);
    if (err==ESP_OK) break;
    esp_camera_deinit();
    delay(500);
  }
  if (err!=ESP_OK) { Serial.printf("Camera FAILED: 0x%x\n", err); return false; }

  sensor_t *s = esp_camera_sensor_get();
  if (!s) return false;

  s->set_framesize(s,     getFrameSize());
  s->set_quality(s,       getJpegQuality());
  s->set_brightness(s,    1);
  s->set_contrast(s,      1);
  s->set_saturation(s,    0);
  s->set_sharpness(s,     1);
  s->set_denoise(s,       1);
  s->set_gainceiling(s,   GAINCEILING_4X);
  s->set_whitebal(s,      1);
  s->set_awb_gain(s,      1);
  s->set_exposure_ctrl(s, 1);
  s->set_aec2(s,          1);
  s->set_lenc(s,          1);
  s->set_hmirror(s,       0);
  s->set_vflip(s,         0);
  s->set_dcw(s,           1);

  // warmup — flush stale frames
  for (int i=0; i<10; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(50);
  }

  Serial.printf("Camera ready [%s | %s] Heap: %d\n",
    getModeName(), getModeRes(), ESP.getFreeHeap());
  return true;
}

// ── Flush buffer + skip overflow frames ──
void flushCameraBuffer() {
  for (int i=0; i<10; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(30);
  }
}

void stopServers() {
  if (stream_httpd)  { httpd_stop(stream_httpd);  stream_httpd  = NULL; }
  if (control_httpd) { httpd_stop(control_httpd); control_httpd = NULL; }
  delay(300);
}

// ════════════════════════════════════════════════════════════
//  ROOT PAGE
// ════════════════════════════════════════════════════════════
esp_err_t root_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const char* html = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM</title>
<style>
body{background:#111;color:#eee;font-family:sans-serif;
     display:flex;flex-direction:column;align-items:center;
     padding:20px;margin:0;}
h2{margin-bottom:10px;}
#stream{width:100%;max-width:640px;border:2px solid #444;
        border-radius:8px;background:#000;min-height:180px;display:block;}
#status{margin:8px 0;font-size:14px;color:#aaa;min-height:20px;text-align:center;}
.btns{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin:8px 0;}
.btn{padding:10px 20px;font-size:15px;border:none;border-radius:6px;
     cursor:pointer;font-weight:bold;transition:all 0.15s;}
#btnCam  {background:#2196F3;color:#fff;}
#btnFlash{background:#FF9800;color:#fff;}
#btnSnap {background:#4CAF50;color:#fff;}
#btnFast {background:#444;color:#fff;}
#btnQual {background:#444;color:#fff;}
.btn:disabled{opacity:0.4;cursor:default;}
.modeActive{outline:3px solid #fff;outline-offset:2px;filter:brightness(1.3);}
#modeInfo{font-size:12px;color:#aaa;margin:4px 0 8px;
          background:#222;padding:5px 14px;border-radius:6px;text-align:center;}
#snapMsg{font-size:13px;color:#4CAF50;min-height:18px;margin-top:4px;}
#snapPreview{display:none;margin-top:12px;max-width:640px;
             width:100%;border:2px solid #4CAF50;border-radius:8px;}
</style>
</head><body>
<h2>ESP32-CAM</h2>
<img id="stream" src="" alt="Stream">
<div id="status">Connecting...</div>

<div class="btns">
  <button class="btn" id="btnCam"   onclick="toggleCam()">Camera ON/OFF</button>
  <button class="btn" id="btnFlash" onclick="toggleFlash()">Flash ON/OFF</button>
  <button class="btn" id="btnSnap"  onclick="takeSnapshot()">&#128247; Snapshot</button>
</div>

<div class="btns">
  <button class="btn" id="btnFast" onclick="setMode(0)">
    &#9889; FAST<br><small>320x240 &middot; Zero lag</small>
  </button>
  <button class="btn" id="btnQual" onclick="setMode(1)">
    &#128247; QUALITY<br><small>640x480 &middot; Better image</small>
  </button>
</div>
<div id="modeInfo">Mode: loading...</div>
<div id="snapMsg"></div>
<img id="snapPreview" src="" alt="Snapshot">

<script>
const BASE       = window.location.hostname;
const STREAM_URL = `http://${BASE}:81/stream`;
let streamImg    = document.getElementById('stream');
let statusEl     = document.getElementById('status');
let snapMsg      = document.getElementById('snapMsg');
let reconnTimer  = null;
let streamActive = false;
let lastFrame    = Date.now();
let currentMode  = -1;
let switching    = false;

function startStream() {
  clearTimeout(reconnTimer);
  streamImg.src = '';
  streamActive  = false;
  lastFrame     = Date.now();
  statusEl.textContent = 'Connecting stream...';
  reconnTimer = setTimeout(() => {
    streamImg.src = STREAM_URL + '?t=' + Date.now();
  }, 500);
}

streamImg.onload = () => {
  lastFrame = Date.now();
  if (!streamActive) {
    streamActive = true;
    statusEl.textContent = 'Stream live';
  }
};
streamImg.onerror = () => {
  if (switching) return; // ignore errors during switch
  streamActive = false;
  statusEl.textContent = 'Stream lost — reconnecting...';
  streamImg.src = '';
  clearTimeout(reconnTimer);
  reconnTimer = setTimeout(startStream, 2000);
};

// Frozen frame watchdog
setInterval(() => {
  if (!streamActive || switching) return;
  if (Date.now() - lastFrame > 5000) {
    statusEl.textContent = 'Stream frozen — reconnecting...';
    streamActive = false;
    streamImg.src = '';
    clearTimeout(reconnTimer);
    reconnTimer = setTimeout(startStream, 500);
  }
}, 1500);

function updateModeUI(d) {
  document.getElementById('btnFast').className = 'btn' + (d.mode===0?' modeActive':'');
  document.getElementById('btnQual').className = 'btn' + (d.mode===1?' modeActive':'');
  document.getElementById('modeInfo').textContent =
    `Mode: ${d.modeName} | Res: ${d.res} | Quality: ${d.quality}`;
  currentMode = d.mode;
}

// ── Quality switch — stop stream, switch, restart ──
async function setMode(mode) {
  if (currentMode===mode || switching) return;
  switching = true;
  document.getElementById('btnFast').disabled = true;
  document.getElementById('btnQual').disabled = true;
  statusEl.textContent = `Switching to ${mode===0?'FAST':'QUALITY'}...`;

  // 1. Stop stream completely
  clearTimeout(reconnTimer);
  streamImg.src = '';
  streamActive  = false;

  // 2. Wait for stream to close on server side
  await new Promise(r => setTimeout(r, 800));

  try {
    const r = await fetch(`http://${BASE}/setmode?mode=${mode}`);
    const d = await r.json();
    updateModeUI(d);
    statusEl.textContent = `${d.modeName} active — restarting stream...`;

    // 3. Extra wait for sensor to settle + buffer flush
    await new Promise(r => setTimeout(r, 1000));
    startStream();
  } catch(e) {
    statusEl.textContent = 'Mode switch failed — try again';
  }

  document.getElementById('btnFast').disabled = false;
  document.getElementById('btnQual').disabled = false;
  switching = false;
}

async function toggleCam() {
  document.getElementById('btnCam').disabled = true;
  try {
    const r = await fetch(`http://${BASE}/camtoggle`);
    const d = await r.json();
    updateModeUI(d);
    if (d.camera==='on' && d.ready==='yes') {
      statusEl.textContent = 'Camera ON — starting stream...';
      setTimeout(startStream, 800);
    } else {
      clearTimeout(reconnTimer);
      streamImg.src = '';
      streamActive  = false;
      statusEl.textContent = 'Camera OFF';
    }
  } catch(e) { statusEl.textContent = 'Control error: '+e; }
  document.getElementById('btnCam').disabled = false;
}

async function toggleFlash() {
  document.getElementById('btnFlash').disabled = true;
  try {
    const r = await fetch(`http://${BASE}/flash`);
    const d = await r.json();
    if (d.error) { statusEl.textContent = d.error; }
    else {
      statusEl.textContent = `Flash: ${d.flash}`;
      streamImg.src = ''; streamActive = false;
      clearTimeout(reconnTimer);
      reconnTimer = setTimeout(startStream, 800);
    }
  } catch(e) { statusEl.textContent = 'Flash error: '+e; }
  document.getElementById('btnFlash').disabled = false;
}

async function takeSnapshot() {
  const btn = document.getElementById('btnSnap');
  btn.disabled = true; btn.textContent = 'Capturing...';
  snapMsg.textContent = '';
  clearTimeout(reconnTimer);
  streamImg.src = ''; streamActive = false;
  statusEl.textContent = 'Capturing snapshot...';
  await new Promise(r => setTimeout(r, 400));
  try {
    const r = await fetch(`http://${BASE}/snapshot`, {cache:'no-cache'});
    if (!r.ok) throw new Error('failed');
    const blob = await r.blob();
    const url  = URL.createObjectURL(blob);
    const prev = document.getElementById('snapPreview');
    prev.src = url; prev.style.display = 'block';
    const a  = document.createElement('a');
    const ts = new Date().toISOString().replace(/[:.]/g,'-').slice(0,19);
    a.href = url; a.download = `snapshot_${ts}.jpg`; a.click();
    snapMsg.textContent = 'Snapshot saved!';
  } catch(e) { snapMsg.textContent = 'Snapshot failed'; }
  btn.disabled = false; btn.textContent = '📷 Snapshot';
  statusEl.textContent = 'Restarting stream...';
  reconnTimer = setTimeout(startStream, 600);
  setTimeout(() => { snapMsg.textContent=''; }, 3000);
}

async function checkStatus() {
  try {
    const r = await fetch(`http://${BASE}/status`);
    const d = await r.json();
    updateModeUI(d);
    if (d.camera==='on' && d.ready==='yes') startStream();
    else statusEl.textContent = 'Camera OFF — press Camera ON/OFF';
  } catch(e) {
    statusEl.textContent = 'ESP32 not reachable — retrying...';
    setTimeout(checkStatus, 3000);
  }
}
checkStatus();
</script>
</body></html>
)rawhtml";
  httpd_resp_sendstr(req, html);
  return ESP_OK;
}

// ════════════════════════════════════════════════════════════
//  STREAM HANDLER
// ════════════════════════════════════════════════════════════
esp_err_t stream_handler(httpd_req_t *req) {
  if (!cameraEnabled || !cameraReady) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "CAMERA_OFF");
    return ESP_OK;
  }

  int sock = httpd_req_to_sockfd(req);
  if (sock >= 0) {
    struct timeval tv; tv.tv_sec=3; tv.tv_usec=0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }

  camera_fb_t  *fb           = NULL;
  char          part_buf[64];
  int           failCount     = 0;
  unsigned long lastFrameTime = millis();

  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");
  httpd_resp_set_hdr(req, "Connection", "close");

  Serial.println("Stream started");

  while (true) {

    // ── Quality switch signal — break cleanly ──
    if (streamRestart) {
      streamRestart = false;
      Serial.println("Stream break — quality switch");
      break;
    }

    if (snapshotInProgress) {
      delay(30); lastFrameTime = millis(); continue;
    }
    if (millis()-lastFrameTime > STREAM_TIMEOUT_MS) { Serial.println("Stream timeout"); break; }
    if (ESP.getFreeHeap() < MIN_HEAP_BYTES)          { needRestart=true; httpd_resp_send_chunk(req,NULL,0); break; }
    if (!cameraEnabled)                              { httpd_resp_send_chunk(req,NULL,0); break; }
    if (flashJustChanged) { flashJustChanged=false; flushCameraBuffer(); lastFrameTime=millis(); }

    fb = esp_camera_fb_get();
    if (!fb) {
      failCount++;
      if (failCount >= MAX_STREAM_FAILS) {
        Serial.println("Too many frame fails");
        cameraReady = initCamera();
        failCount   = 0;
        if (!cameraReady) { cameraEnabled=false; needRestart=true; httpd_resp_send_chunk(req,NULL,0); break; }
      }
      delay(50); continue;
    }

    // ── FB-OVF fix: skip corrupt/tiny frames ──
    if (fb->len < 200) {
      esp_camera_fb_return(fb); fb=NULL;
      delay(20); continue;
    }

    failCount = 0;

    size_t hlen = snprintf(part_buf, sizeof(part_buf),
      "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);

    esp_err_t res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res==ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    if (res==ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);

    esp_camera_fb_return(fb); fb=NULL;

    if (res!=ESP_OK) { Serial.println("Client disconnected"); break; }
    lastFrameTime = millis();
  }

  if (fb) { esp_camera_fb_return(fb); fb=NULL; }
  httpd_resp_send_chunk(req, NULL, 0);
  Serial.printf("Stream closed. Heap: %d\n", ESP.getFreeHeap());
  return ESP_OK;
}

// ════════════════════════════════════════════════════════════
//  SNAPSHOT HANDLER
// ════════════════════════════════════════════════════════════
esp_err_t snapshot_handler(httpd_req_t *req) {
  if (!cameraEnabled || !cameraReady) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "CAMERA_OFF");
    return ESP_OK;
  }
  snapshotInProgress = true;
  delay(300);
  for (int i=0; i<3; i++) {
    camera_fb_t *tmp = esp_camera_fb_get();
    if (tmp) esp_camera_fb_return(tmp);
    delay(30);
  }
  camera_fb_t *fb = esp_camera_fb_get();
  snapshotInProgress = false;
  if (!fb) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "CAPTURE_FAILED");
    return ESP_OK;
  }
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=snapshot.jpg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_send(req, (const char*)fb->buf, fb->len);
  Serial.printf("Snapshot: %d bytes\n", fb->len);
  esp_camera_fb_return(fb);
  return ESP_OK;
}

// ════════════════════════════════════════════════════════════
//  JSON STATE
// ════════════════════════════════════════════════════════════
void sendStateJSON(httpd_req_t *req) {
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  char resp[220];
  snprintf(resp, sizeof(resp),
    "{\"camera\":\"%s\",\"flash\":\"%s\",\"ready\":\"%s\","
    "\"mode\":%d,\"modeName\":\"%s\",\"res\":\"%s\","
    "\"quality\":%d,\"heap\":%d}",
    cameraEnabled?"on":"off",
    flashEnabled ?"on":"off",
    cameraReady  ?"yes":"no",
    qualityMode, getModeName(), getModeRes(),
    getJpegQuality(), ESP.getFreeHeap());
  httpd_resp_sendstr(req, resp);
}

// ════════════════════════════════════════════════════════════
//  CONTROL HANDLERS
// ════════════════════════════════════════════════════════════
esp_err_t cam_toggle_handler(httpd_req_t *req) {
  cameraEnabled = !cameraEnabled;
  if (cameraEnabled) {
    cameraReady = initCamera();
    if (!cameraReady) cameraEnabled = false;
  } else {
    flashEnabled = false;
    digitalWrite(FLASH_LED_PIN, LOW);
    cameraReady = false;
    esp_camera_deinit();
    delay(100);
  }
  sendStateJSON(req);
  return ESP_OK;
}

// ── FIXED: Live sensor update + stream restart signal ──
esp_err_t setmode_handler(httpd_req_t *req) {
  char buf[32];
  int  len = httpd_req_get_url_query_len(req)+1;
  if (len>1 && len<=(int)sizeof(buf)) {
    httpd_req_get_url_query_str(req, buf, len);
    char val[4];
    if (httpd_query_key_value(buf,"mode",val,sizeof(val))==ESP_OK) {
      int newMode = atoi(val);
      if ((newMode==0||newMode==1) && newMode!=qualityMode) {
        qualityMode = newMode;
        Serial.printf("Mode -> %s\n", getModeName());

        if (cameraEnabled && cameraReady) {
          // Signal stream loop to break cleanly
          streamRestart = true;
          delay(200); // give stream loop time to see flag

          sensor_t *s = esp_camera_sensor_get();
          if (s) {
            s->set_framesize(s, getFrameSize());
            s->set_quality(s,   getJpegQuality());
            Serial.printf("Sensor updated: %s q=%d\n",
              getModeRes(), getJpegQuality());
          }
          // Flush old frames
          flushCameraBuffer();
          streamRestart = false;
        }
      }
    }
  }
  sendStateJSON(req);
  return ESP_OK;
}

esp_err_t flash_toggle_handler(httpd_req_t *req) {
  if (!cameraEnabled) {
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"error\":\"Enable camera first\"}");
    return ESP_OK;
  }
  flashEnabled = !flashEnabled;
  digitalWrite(FLASH_LED_PIN, flashEnabled ? HIGH : LOW);
  delay(80);
  flashJustChanged = true;
  sendStateJSON(req);
  return ESP_OK;
}

esp_err_t status_handler(httpd_req_t *req) {
  sendStateJSON(req);
  return ESP_OK;
}

// ════════════════════════════════════════════════════════════
//  SERVERS
// ════════════════════════════════════════════════════════════
void startServers() {
  httpd_config_t ctrl_cfg    = HTTPD_DEFAULT_CONFIG();
  ctrl_cfg.server_port       = 80;
  ctrl_cfg.ctrl_port         = 32768;
  ctrl_cfg.max_open_sockets  = 5;
  ctrl_cfg.task_priority     = tskIDLE_PRIORITY+5;
  ctrl_cfg.stack_size        = 8192;
  ctrl_cfg.recv_wait_timeout = 5;
  ctrl_cfg.send_wait_timeout = 5;

  httpd_uri_t uris[] = {
    {"/",          HTTP_GET, root_handler,         NULL},
    {"/camtoggle", HTTP_GET, cam_toggle_handler,   NULL},
    {"/flash",     HTTP_GET, flash_toggle_handler, NULL},
    {"/status",    HTTP_GET, status_handler,       NULL},
    {"/snapshot",  HTTP_GET, snapshot_handler,     NULL},
    {"/setmode",   HTTP_GET, setmode_handler,      NULL},
  };
  if (httpd_start(&control_httpd,&ctrl_cfg)==ESP_OK) {
    for (auto &u:uris) httpd_register_uri_handler(control_httpd,&u);
    Serial.println("Control server — port 80");
  }

  httpd_config_t strm_cfg    = HTTPD_DEFAULT_CONFIG();
  strm_cfg.server_port       = 81;
  strm_cfg.ctrl_port         = 32769;
  strm_cfg.max_open_sockets  = 2;
  strm_cfg.task_priority     = tskIDLE_PRIORITY+4;
  strm_cfg.stack_size        = 8192;
  strm_cfg.recv_wait_timeout = 5;
  strm_cfg.send_wait_timeout = 5;

  httpd_uri_t stream_uri = {"/stream", HTTP_GET, stream_handler, NULL};
  if (httpd_start(&stream_httpd,&strm_cfg)==ESP_OK) {
    httpd_register_uri_handler(stream_httpd,&stream_uri);
    Serial.println("Stream server — port 81");
  }
  Serial.printf("Heap: %d\n", ESP.getFreeHeap());
}

void restartServers() {
  Serial.println("=== SERVER RESTART ===");
  if (cameraEnabled) {
    flashEnabled=false; cameraEnabled=false; cameraReady=false;
    digitalWrite(FLASH_LED_PIN,LOW);
    esp_camera_deinit(); delay(200);
  }
  stopServers(); delay(500);
  startServers();
  needRestart=false;
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32-CAM Boot ---");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // Boot blink
  for (int i=0; i<2; i++) {
    digitalWrite(FLASH_LED_PIN,HIGH); delay(100);
    digitalWrite(FLASH_LED_PIN,LOW);  delay(100);
  }

  // ── WDT removed — was causing "task not found" error ──

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  int tries=0;
  while (WiFi.status()!=WL_CONNECTED && tries<20) {
    delay(500); Serial.print("."); tries++;
  }
  if (WiFi.status()==WL_CONNECTED)
    Serial.println("\nIP: " + WiFi.localIP().toString());
  else
    Serial.println("\nWiFi FAILED");

  startServers();
  Serial.println("--- Boot Complete ---");
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  if (needRestart) restartServers();

  if (WiFi.status()!=WL_CONNECTED) {
    WiFi.reconnect();
    int t=0;
    while (WiFi.status()!=WL_CONNECTED && t<20) { delay(500); t++; }
  }

  static unsigned long lastLog=0;
  if (millis()-lastLog > 30000) {
    lastLog=millis();
    Serial.printf("[HEAP] %d | Cam:%s | Mode:%s\n",
      ESP.getFreeHeap(), cameraEnabled?"ON":"OFF", getModeName());
  }

  delay(100);
}
