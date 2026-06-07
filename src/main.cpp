#include "Arduino.h"
#include "WiFi.h"
#include "esp_camera.h"
#include "camera_pins.h"
#include <AsyncMqttClient.h>
#include "secrets.h"

// ==============================================================
//  FLAG DE CONTROLE: SISTEMAS EMBARCADOS vs ANÁLISE DE ALGORITMOS
// ==============================================================
// Mude para 'false' para enviar as fotos normalmente (Resolução original).
// Mude para 'true' para rodar o teste de estresse de memória (AA).
#define MODO_TESTE_AA true

// ==============================================================
//  CONFIGURAÇÃO — Wi-Fi e MQTT
// ==============================================================
#define WIFI_SSID SECRET_SSID
#define WIFI_PASSWORD SECRET_PASS

#define MQTT_SERVER SECRET_MQTT_SERVER
#define MQTT_PORT 1883

// ==============================================================
//  [ENTREGÁVEL 1 - AA] Estrutura do Buffer Circular O(1)
// ==============================================================
class BufferCircular
{
private:
    uint8_t *buffer;
    int head;
    int tail;
    int max_size;

public:
    BufferCircular(int size)
    {
        max_size = size;
        buffer = new (std::nothrow) uint8_t[size];
        head = 0;
        tail = 0;
    }
    ~BufferCircular()
    {
        if (buffer != nullptr)
            delete[] buffer;
    }
    void push(uint8_t data)
    {
        if (buffer == nullptr)
            return;
        buffer[head] = data;
        head = (head + 1) % max_size;
        if (head == tail)
        {
            tail = (tail + 1) % max_size;
        }
    }
};

AsyncMqttClient mqttClient;
unsigned long lastMqttRetry = 0;
#define LED_FLASH_GPIO 4

void startCameraServer();
void conectarMQTT();
void vMqttCameraTask(void *pvParameters);

void onMqttConnect(bool sessionPresent) { Serial.println("[MQTT] Conectado com sucesso ao Broker Mosquitto!"); }
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) { Serial.printf("[MQTT] Desconectado! Motivo: %d\n", (int)reason); }

void setupLedFlash()
{
    ledcSetup(7, 5000, 8);
    ledcAttachPin(LED_FLASH_GPIO, 7);
    ledcWrite(7, 0);
}

// ==============================================================
//  INICIALIZAÇÃO DA CÂMERA (CORRIGIDA E PROTEGIDA)
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
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
        Serial.println("[CAM] PSRAM OK — Buffer maximo reservado para testes.");
    }
    else
    {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        Serial.println("[CAM] Sem PSRAM — Forçando QVGA.");
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
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);

    
    if (!MODO_TESTE_AA)
    {
        if (psramFound())
        {
            s->set_framesize(s, FRAMESIZE_SVGA); 
            s->set_quality(s, 10);
            Serial.println("[CAM] MODO NORMAL ATIVO: Sensor reconfigurado para SVGA (800x600)");
        }
        else
        {
            s->set_framesize(s, FRAMESIZE_QVGA);
            s->set_quality(s, 12);
        }
    }

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
            ESP.restart();
        }
    }

    Serial.println("\n[WIFI] Conectado!");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    startCameraServer();

    Serial.println("\n=============================================");
    Serial.printf(" ACESSE O VÍDEO AQUI: http://%s/\n", WiFi.localIP().toString().c_str());
    if (MODO_TESTE_AA)
        Serial.println(" MODO: TESTE DE ANÁLISE DE ALGORITMOS (AA) ATIVADO");
    else
        Serial.println(" MODO: NORMAL (CAPTURA E ENVIO DE FOTOS)");
    Serial.println("=============================================\n");

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setClientId("ESP32-CAM-Estacionamento");
    mqttClient.setKeepAlive(60);

    conectarMQTT();

    xTaskCreatePinnedToCore(vMqttCameraTask, "MqttCamTask", 8192, NULL, 1, NULL, 1);

    for (int i = 0; i < 2; i++)
    {
        ledcWrite(7, 100);
        delay(200);
        ledcWrite(7, 0);
        delay(200);
    }
}

void loop()
{
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
        mqttClient.connect();
}

// ==============================================================
//  TASK DE GERENCIAMENTO
// ==============================================================
void vMqttCameraTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        if (WiFi.status() == WL_CONNECTED && mqttClient.connected())
        {
            // ============================================================
            // ROTA 1: MODO TESTE DE ALGORITMOS (AA)
            // ============================================================
            if (MODO_TESTE_AA)
            {
                Serial.println("\n[AA] --- INICIANDO PERFILAMENTO MULTI-ESCALA ---");
                sensor_t *s = esp_camera_sensor_get();

                framesize_t resolucoes[3] = {FRAMESIZE_QVGA, FRAMESIZE_SVGA, FRAMESIZE_UXGA};
                int qualidades[3] = {63, 20, 10};
                const char *nomes_cenario[3] = {"BAIXA (QVGA)", "MEDIA (SVGA)", "ALTA (UXGA)"};

                for (int cenario = 0; cenario < 3; cenario++)
                {
                    s->set_framesize(s, resolucoes[cenario]);
                    s->set_quality(s, qualidades[cenario]);

                    vTaskDelay(pdMS_TO_TICKS(1500)); 

                    
                    for (int i = 0; i < 2; i++)
                    {
                        camera_fb_t *flush_fb = esp_camera_fb_get();
                        if (flush_fb)
                            esp_camera_fb_return(flush_fb);
                    }

                    camera_fb_t *fb = esp_camera_fb_get();
                    if (!fb)
                    {
                        Serial.printf("[AA] Erro ao capturar na qualidade %s\n", nomes_cenario[cenario]);
                        continue;
                    }

                    int tamanho_real_foto = fb->len;

                    
                    int N = tamanho_real_foto;
                    if (N > 18000)
                    {
                        N = 18000;
                    }

                    Serial.printf("\n---> FOTO %d (%s) | TAMANHO REAL: %d bytes | PROCESSO NO TESTE (N): %d bytes\n",
                                  cenario + 1, nomes_cenario[cenario], tamanho_real_foto, N);

                    // 1. VERTENTE INEFICIENTE O(N^2)
                    uint8_t *array_ineficiente = (uint8_t *)malloc(N);
                    unsigned long durationV1 = 0;
                    uint32_t heapV1 = ESP.getFreeHeap();

                    if (array_ineficiente != NULL)
                    {
                        int tamanho_atual = 0;
                        unsigned long startV1 = micros();
                        for (int i = 0; i < N; i++)
                        {
                            for (int j = tamanho_atual; j > 0; j--)
                            {
                                array_ineficiente[j] = array_ineficiente[j - 1];
                            }
                            array_ineficiente[0] = fb->buf[i];
                            tamanho_atual++;
                        }
                        durationV1 = micros() - startV1;
                        free(array_ineficiente);
                    }
                    Serial.printf("[Vertente 1 - O(n^2)] Latencia : %lu us | Heap Livre : %u bytes \n", durationV1, heapV1);

                    // 2. VERTENTE EFICIENTE O(1)
                    uint32_t heapV2 = ESP.getFreeHeap();
                    BufferCircular buffer_circular(N);

                    unsigned long startV2 = micros();
                    for (int i = 0; i < N; i++)
                    {
                        buffer_circular.push(fb->buf[i]);
                    }
                    unsigned long durationV2 = micros() - startV2;
                    Serial.printf("[Vertente 2 - O(1)]   Latencia : %lu us | Heap Livre : %u bytes \n", durationV2, heapV2);

                    char jsonPayload[256];
                    snprintf(jsonPayload, sizeof(jsonPayload),
                             "{\"Cenario\":\"%s\",\"N\":%d, \"latencia_v1\":%lu, \"latencia_v2\":%lu, \"heap_v1\":%u, \"heap_v2\":%u}",
                             nomes_cenario[cenario], N, durationV1, durationV2, heapV1, heapV2);

                    mqttClient.publish("estacionamento/telemetria/aa", 0, false, jsonPayload);

                    esp_camera_fb_return(fb);
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                Serial.println("[AA] Fim do Teste! Aguardando 60 segundos...\n");
                vTaskDelay(pdMS_TO_TICKS(60000));
            }
            // ============================================================
            // ROTA 2: MODO NORMAL (FOTO ORIGINAL FATIADA VIA MQTT)
            // ============================================================
            else
            {
                camera_fb_t *fb = esp_camera_fb_get();
                if (!fb)
                {
                    Serial.println("[MQTT-TASK] Erro ao capturar imagem.");
                }
                else
                {
                    Serial.printf("[MQTT-TASK] Enviando foto fatiada. Tamanho: %d bytes\n", fb->len);

                    char bufferTamanho[16];
                    itoa(fb->len, bufferTamanho, 10);
                    mqttClient.publish("estacionamento/camera/tamanho", 0, false, bufferTamanho);

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

                        vTaskDelay(pdMS_TO_TICKS(8));
                    }
                    Serial.println("[MQTT-TASK] Foto enviada com sucesso!");
                    esp_camera_fb_return(fb);
                }
                vTaskDelay(pdMS_TO_TICKS(50000));
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}