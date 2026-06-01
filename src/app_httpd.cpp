#include "esp_http_server.h"
#include "esp_camera.h"
#include "Arduino.h"

#define PART_BOUNDARY "123456789000000000000987654321"

static const char* _STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// ----------------------------------------------------------
//  Página HTML principal
// ----------------------------------------------------------
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #0d0d0d;
      color: #e0e0e0;
      font-family: 'Courier New', monospace;
      display: flex;
      flex-direction: column;
      align-items: center;
      min-height: 100vh;
      padding: 20px;
    }
    h1 {
      font-size: 1.1rem;
      letter-spacing: 4px;
      color: #00ff88;
      margin: 20px 0 16px;
      text-transform: uppercase;
    }
    #stream-container {
      border: 2px solid #00ff88;
      border-radius: 6px;
      overflow: hidden;
      max-width: 640px;
      width: 100%;
    }
    #stream {
      width: 100%;
      display: block;
    }
    .controls {
      margin-top: 16px;
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      justify-content: center;
      max-width: 640px;
      width: 100%;
    }
    .ctrl-group {
      background: #1a1a1a;
      border: 1px solid #333;
      border-radius: 6px;
      padding: 12px;
      flex: 1;
      min-width: 200px;
    }
    .ctrl-group h3 {
      font-size: 0.7rem;
      color: #00ff88;
      letter-spacing: 2px;
      margin-bottom: 10px;
      text-transform: uppercase;
    }
    label {
      font-size: 0.75rem;
      color: #aaa;
      display: flex;
      justify-content: space-between;
      margin-bottom: 6px;
    }
    input[type=range] {
      width: 100%;
      accent-color: #00ff88;
    }
    select {
      width: 100%;
      background: #222;
      color: #e0e0e0;
      border: 1px solid #444;
      padding: 4px;
      border-radius: 4px;
      font-size: 0.8rem;
    }
    .btn {
      background: #00ff88;
      color: #000;
      border: none;
      padding: 8px 18px;
      border-radius: 4px;
      font-weight: bold;
      font-family: monospace;
      cursor: pointer;
      font-size: 0.8rem;
      letter-spacing: 1px;
    }
    .btn:hover { background: #00cc6a; }
    .btn.danger { background: #ff4444; color: #fff; }
    .btn.danger:hover { background: #cc0000; }
    #status {
      font-size: 0.7rem;
      color: #555;
      margin-top: 12px;
    }
  </style>
</head>
<body>
  <h1>&#128247; ESP32-CAM Stream</h1>

  <div id="stream-container">
    <img id="stream" src="" />
  </div>

  <div class="controls">
    <div class="ctrl-group">
      <h3>Resolucao</h3>
      <label>Framesize
        <select id="framesize" onchange="updateControl('framesize', this.value)">
          <option value="10">UXGA (1600x1200)</option>
          <option value="9">SXGA (1280x1024)</option>
          <option value="8">XGA (1024x768)</option>
          <option value="7" selected>SVGA (800x600)</option>
          <option value="6">VGA (640x480)</option>
          <option value="5">CIF (400x296)</option>
          <option value="4">QVGA (320x240)</option>
        </select>
      </label>
      <label>Qualidade JPEG: <span id="quality-val">12</span>
        <input type="range" id="quality" min="4" max="63" value="12"
          oninput="document.getElementById('quality-val').innerText=this.value"
          onchange="updateControl('quality', this.value)">
      </label>
    </div>

    <div class="ctrl-group">
      <h3>Imagem</h3>
      <label>Brilho: <span id="brightness-val">0</span>
        <input type="range" id="brightness" min="-2" max="2" value="0"
          oninput="document.getElementById('brightness-val').innerText=this.value"
          onchange="updateControl('brightness', this.value)">
      </label>
      <label>Contraste: <span id="contrast-val">0</span>
        <input type="range" id="contrast" min="-2" max="2" value="0"
          oninput="document.getElementById('contrast-val').innerText=this.value"
          onchange="updateControl('contrast', this.value)">
      </label>
      <label>Saturacao: <span id="saturation-val">0</span>
        <input type="range" id="saturation" min="-2" max="2" value="0"
          oninput="document.getElementById('saturation-val').innerText=this.value"
          onchange="updateControl('saturation', this.value)">
      </label>
    </div>

    <div class="ctrl-group">
      <h3>Flash / Foto</h3>
      <label>Flash LED: <span id="flash-val">0</span>
        <input type="range" id="flash" min="0" max="255" value="0"
          oninput="document.getElementById('flash-val').innerText=this.value"
          onchange="updateControl('flash', this.value)">
      </label>
      <br>
      <button class="btn" onclick="capturePhoto()">&#128247; Capturar Foto</button>
    </div>
  </div>

  <p id="status">Conectando ao stream...</p>

  <script>
    // Porta do stream é 81
    var streamUrl = window.location.protocol + '//' + window.location.hostname + ':81/stream';
    var img = document.getElementById('stream');
    img.src = streamUrl;
    img.onload = function() {
      document.getElementById('status').innerText = 'Stream ativo ✓';
    };
    img.onerror = function() {
      document.getElementById('status').innerText = 'Erro no stream — verifique a porta 81';
    };

    function updateControl(name, value) {
      fetch('/control?var=' + name + '&val=' + value)
        .catch(err => console.error(err));
    }

    function capturePhoto() {
      var url = window.location.protocol + '//' + window.location.hostname + '/capture';
      window.open(url, '_blank');
    }
  </script>
</body>
</html>
)rawliteral";

// ----------------------------------------------------------
//  Handler: página index
// ----------------------------------------------------------
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

// ----------------------------------------------------------
//  Handler: captura foto única (JPEG)
// ----------------------------------------------------------
static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

// ----------------------------------------------------------
//  Handler: controle de parâmetros da câmera
// ----------------------------------------------------------
static esp_err_t cmd_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    if (ret != ESP_OK) { httpd_resp_send_404(req); return ESP_FAIL; }

    char var[32], val[32];
    if (httpd_query_key_value(buf, "var", var, sizeof(var)) != ESP_OK ||
        httpd_query_key_value(buf, "val", val, sizeof(val)) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int value = atoi(val);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if      (!strcmp(var, "framesize"))   res = s->set_framesize(s, (framesize_t)value);
    else if (!strcmp(var, "quality"))     res = s->set_quality(s, value);
    else if (!strcmp(var, "brightness"))  res = s->set_brightness(s, value);
    else if (!strcmp(var, "contrast"))    res = s->set_contrast(s, value);
    else if (!strcmp(var, "saturation"))  res = s->set_saturation(s, value);
    else if (!strcmp(var, "flash")) {
        ledcWrite(7, value);  // Canal 7 = LED flash (GPIO 4)
    }

    if (res < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "OK");
}

// ----------------------------------------------------------
//  Handler: MJPEG stream (porta 81)
// ----------------------------------------------------------
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    Serial.println("[STREAM] Cliente conectado");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("[STREAM] Falha no frame");
            res = ESP_FAIL;
            break;
        }

        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res != ESP_OK) { esp_camera_fb_return(fb); break; }

        size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) { esp_camera_fb_return(fb); break; }

        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;
    }

    Serial.println("[STREAM] Cliente desconectado");
    return res;
}

// ----------------------------------------------------------
//  Inicialização dos dois servidores HTTP
// ----------------------------------------------------------
void startCameraServer() {
    // --- Servidor principal: porta 80 (index, capture, control)
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri  = { .uri="/",        .method=HTTP_GET, .handler=index_handler,   .user_ctx=NULL };
    httpd_uri_t capture_uri= { .uri="/capture", .method=HTTP_GET, .handler=capture_handler, .user_ctx=NULL };
    httpd_uri_t cmd_uri    = { .uri="/control", .method=HTTP_GET, .handler=cmd_handler,     .user_ctx=NULL };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        Serial.println("[HTTP] Servidor porta 80 OK");
    }

    // --- Servidor de stream: porta 81
    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = 81;
    stream_config.ctrl_port   = 32769;  // porta de controle diferente

    httpd_uri_t stream_uri = { .uri="/stream", .method=HTTP_GET, .handler=stream_handler, .user_ctx=NULL };

    if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        Serial.println("[HTTP] Servidor stream porta 81 OK");
    }
}