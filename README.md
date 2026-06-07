# Projeto BLU - ESP32-CAM com MQTT e Dashboard

Este projeto utiliza uma ESP32-CAM para captura de imagens, transmissão de vídeo ao vivo, envio de fotos via MQTT e integração com Node-RED para coleta de informações e construção de dashboard.

O firmware foi desenvolvido com PlatformIO e Arduino framework, com um servidor web embarcado na ESP32-CAM e publicação de telemetria em um broker Mosquitto.

## Visão geral

O sistema foi pensado para demonstrar um fluxo completo de IoT com câmera embarcada:

- Captura de imagens pela ESP32-CAM.
- Stream de vídeo MJPEG pelas portas HTTP do firmware.
- Controle remoto de parâmetros da câmera pela interface web.
- Publicação de dados no MQTT para consumo por Node-RED.
- Apoio à análise de desempenho com teste de buffer e comparação entre duas vertentes de implementação.

## Análise de Algoritmos

Esta parte do projeto foi criada para comparar duas abordagens de manipulação de dados em memória usando um mesmo conjunto de frames capturados pela câmera.

### Vertente 1 - Implementação ineficiente

Nesta vertente, os bytes da imagem são inseridos em um array com deslocamento dos elementos a cada nova inserção. Isso faz com que o custo cresça rapidamente conforme o tamanho de entrada aumenta.

- Complexidade de tempo: O(n²)
- Objetivo: mostrar o impacto de uma abordagem baseada em cópias repetidas
- Métricas coletadas: latência e uso de heap livre

### Vertente 2 - Buffer circular

Nesta vertente, o projeto utiliza um buffer circular para armazenar os dados da imagem de forma contínua, com inserção em tempo constante.

- Complexidade de tempo: O(1) por inserção
- Objetivo: mostrar uma alternativa mais eficiente para o mesmo fluxo de dados
- Métricas coletadas: latência e uso de heap livre

### O que é testado

O teste executa cenários com diferentes tamanhos de frame:

- QVGA
- SVGA
- UXGA

Em cada cenário, o firmware mede o tempo de processamento nas duas vertentes e publica os resultados via MQTT para posterior análise.

## ESP32-CAM

O projeto foi desenvolvido para a placa ESP32-CAM, que combina microcontrolador, câmera e conectividade Wi-Fi em um único módulo.

O firmware expõe:

- Página web principal na porta 80.
- Captura de imagem em `/capture`.
- Controle de parâmetros em `/control`.
- Stream de vídeo em `/stream` na porta 81.

## Variáveis de ambiente e configuração

As credenciais e o endereço do broker são configurados no arquivo `src/secrets.h`.

Defina os seguintes valores antes de compilar e enviar o firmware:

- `SECRET_SSID` - nome da rede Wi-Fi.
- `SECRET_PASS` - senha da rede Wi-Fi.
- `SECRET_MQTT_SERVER` - IP ou host do broker MQTT.

## Tecnologias usadas

- ESP32-CAM
- Arduino framework
- PlatformIO
- MQTT
- Mosquitto como broker
- Node-RED para coleta de informações e criação do dashboard
- Servidor HTTP embarcado
- AsyncMqttClient
- AsyncTCP

## Como executar

1. Abra o projeto no VS Code com a extensão do PlatformIO instalada.
2. Verifique o arquivo `src/secrets.h` e configure sua rede Wi-Fi e o broker MQTT.
3. No arquivo `platformio.ini`, confirme a placa `esp32cam` e as dependências do projeto.
4. Compile o firmware usando o comando de build do PlatformIO.
5. Envie o firmware para a ESP32-CAM.
6. Abra o Monitor Serial em `115200` para acompanhar o IP da placa e os logs de conexão.
7. Acesse o endereço informado no monitor serial para abrir a interface web da câmera.
8. No Node-RED, assine os tópicos MQTT do projeto para coletar os dados e montar o dashboard.

## Tópicos MQTT

Os principais tópicos publicados pelo firmware são:

- `estacionamento/telemetria/aa` - resultados do teste de análise de algoritmos.
- `estacionamento/camera/tamanho` - tamanho do frame capturado no modo normal.
- `estacionamento/camera/stream` - fragmentos da imagem enviada por MQTT.

## Equipe

Espaço reservado para o nome dos integrantes:

- Integrante 1:
- Integrante 2:
- Integrante 3:
- Integrante 4:

## Observações

- O modo de teste de análise de algoritmos é controlado pela flag `MODO_TESTE_AA` em `src/main.cpp`.
- Quando a flag está ativa, o firmware executa os testes de buffer e publica as métricas no MQTT.
- Quando a flag está desativada, o firmware envia a foto original em partes via MQTT.
