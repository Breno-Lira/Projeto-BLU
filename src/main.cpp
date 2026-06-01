#include "Arduino.h"
#include "WiFi.h"
#include "esp_camera.h"
#include "camera_pins.h"
#include <AsyncMqttClient.h> // Reintroduzindo a biblioteca MQTT Assíncrona
#include "secrets.h"

// ==============================================================
//  CONFIGURAÇÃO — Wi-Fi e MQTT
// ==============================================================
#define WIFI_SSID SECRET_SSID
#define WIFI_PASSWORD SECRET_PASS

#define MQTT_SERVER SECRET_MQTT_SERVER
#define MQTT_PORT 1883

// ==============================================================
//  Variáveis Globais e Instâncias
// ==============================================================
AsyncMqttClient mqttClient;
unsigned long lastMqttRetry = 0;

#define LED_FLASH_GPIO 4

// Declaração do servidor web (definido em app_httpd.cpp)
void startCameraServer();

// Chamadas de funções do MQTT
void conectarMQTT();
void vMqttCameraTask(void *pvParameters);

void onMqttConnect(bool sessionPresent)
{
    Serial.println("[MQTT] Conectado com sucesso ao Broker Mosquitto!");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    Serial.printf("[MQTT] Desconectado! Motivo: %d\n", (int)reason);
}

// ==============================================================
//  Flash LED
// ==============================================================
void setupLedFlash()
{
    ledcSetup(7, 5000, 8);
    ledcAttachPin(LED_FLASH_GPIO, 7);
    ledcWrite(7, 0);
}

// ==============================================================
//  Inicialização da câmera
// ==============================================================
bool initCamera()
{
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
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_SVGA; // 800x600
        config.jpeg_quality = 10;
        config.fb_count = 2; // Crucial para permitir web server + MQTT juntos
        Serial.println("[CAM] PSRAM OK — SVGA (800x600)");
    }
    else
    {
        config.frame_size = FRAMESIZE_QVGA; // 320x240
        config.jpeg_quality = 12;
        config.fb_count = 1;
        Serial.println("[CAM] Sem PSRAM — QVGA (320x240)");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("[CAM] ERRO: 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_vflip(s, 1); // 1 = Ativa a inversão vertical (cabeça para baixo)
    s->set_hmirror(s, 1);

    Serial.println("[CAM] Inicializada com sucesso!");
    return true;
}

// ==============================================================
//  SETUP
// ==============================================================
void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println("\n\n=============================");
    Serial.println("   ESP32-CAM: WebServer + MQTT");
    Serial.println("=============================");

    setupLedFlash();

    if (!initCamera())
    {
        Serial.println("[ERRO FATAL] Câmera não inicializou!");
        while (true)
        {
            ledcWrite(7, 50);
            delay(100);
            ledcWrite(7, 0);
            delay(100);
        }
    }

    // Conexão Wi-Fi
    Serial.printf("[WIFI] Conectando a: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);

    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        if (++tentativas > 40)
        {
            Serial.println("\n[WIFI] Falha! Reiniciando...");
            ESP.restart();
        }
    }

    Serial.println("\n[WIFI] Conectado!");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    // Configurando o Cliente MQTT Assíncrono
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setClientId("ESP32-CAM-Estacionamento");
    mqttClient.setKeepAlive(60);

    conectarMQTT();

    // Inicializa o servidor Web de vídeo (vindo do app_httpd.cpp)
    startCameraServer();

    Serial.println("\n=============================");
    Serial.printf(" Acesse o Vídeo: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("=============================\n");

    // Cria a Task em segundo plano para enviar fotos ao MQTT a cada 10 segundos
    xTaskCreatePinnedToCore(vMqttCameraTask, "MqttCamTask", 8192, NULL, 1, NULL, 1);

    // Flash 2x = Inicialização Concluída
    for (int i = 0; i < 2; i++)
    {
        ledcWrite(7, 100);
        delay(200);
        ledcWrite(7, 0);
        delay(200);
    }
}

// ==============================================================
//  LOOP
// ==============================================================
void loop()
{
    // Gerencia reconexão do MQTT de forma limpa e sem travar o chip
    if (WiFi.status() == WL_CONNECTED && !mqttClient.connected())
    {
        unsigned long now = millis();
        if (now - lastMqttRetry > 5000)
        {
            lastMqttRetry = now;
            conectarMQTT();
        }
    }
    delay(100);
}

void conectarMQTT()
{
    if (!mqttClient.connected())
    {
        Serial.println("[MQTT] Tentando conectar ao Broker Mosquitto...");
        mqttClient.connect();
    }
}

// ==============================================================
//  Task FreeRTOS: Captura e Envio Fatiado via MQTT
// ==============================================================
void vMqttCameraTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        // Só tenta tirar foto se a rede e o broker estiverem firmes
        if (WiFi.status() == WL_CONNECTED && mqttClient.connected())
        {
            Serial.println("[MQTT-TASK] Tirando foto para o Broker...");

            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb)
            {
                Serial.println("[MQTT-TASK] Erro ao capturar imagem.");
            }
            else
            {
                Serial.printf("[MQTT-TASK] Enviando foto fatiada. Tamanho: %d bytes\n", fb->len);

                // Envia primeiro o tamanho da imagem
                char bufferTamanho[16];
                itoa(fb->len, bufferTamanho, 10);
                mqttClient.publish("estacionamento/camera/tamanho", 0, false, bufferTamanho);

                // Fatiamento dinâmico de 1KB por vez para não estourar o timeout
                const size_t chunkSize = 1024;
                size_t bytesEnviados = 0;

                while (bytesEnviados < fb->len)
                {
                    size_t tamPedaco = fb->len - bytesEnviados;
                    if (tamPedaco > chunkSize)
                    {
                        tamPedaco = chunkSize;
                    }

                    mqttClient.publish("estacionamento/camera/stream", 0, false, (const char *)(fb->buf + bytesEnviados), tamPedaco);
                    bytesEnviados += tamPedaco;

                    // Alivia o processador para a pilha de rede respirar
                    vTaskDelay(pdMS_TO_TICKS(8));
                }

                Serial.println("[MQTT-TASK] Foto enviada com sucesso!");
                esp_camera_fb_return(fb);
            }
        }
        // Intervalo de 10 segundos entre envios de fotos no MQTT
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}