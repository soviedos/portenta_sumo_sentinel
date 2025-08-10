#include "camera.h"
#include "himax.h"
#include <WiFi.h>
#include <SPI.h>
#include <PortentaEthernet.h>
#include <Ethernet.h>
#include <mbed.h>
#include <cstring> // Para memcpy
#include "config.h" // Archivo de configuración con credenciales WiFi
using namespace rtos;

// ==== CONFIGURACIÓN DE RED ====
// Las credenciales WiFi ahora están en config.h (no se sube a GitHub)
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
IPAddress localIp(ETHERNET_IP_1, ETHERNET_IP_2, ETHERNET_IP_3, ETHERNET_IP_4);
bool usingEthernet = false;

WiFiServer wifiServer(81);
EthernetServer ethServer(81);

// ==== CONFIGURACIÓN DE CÁMARA ====
#define IMAGE_MODE CAMERA_GRAYSCALE
#define FRAME_WIDTH 160
#define FRAME_HEIGHT 120
#define FRAME_SIZE 2

HM01B0 himax;
Camera cam(himax);
FrameBuffer fb(FRAME_WIDTH, FRAME_HEIGHT, FRAME_SIZE);

// ==== MÉTRICAS DEL SISTEMA ====
extern "C" char* sbrk(int incr);
size_t freeMemory() {
  char top;
  return &top - reinterpret_cast<char*>(sbrk(0));
}

unsigned long getUptimeMillis() { return millis(); }
unsigned long getUptimeSeconds() { return millis() / 1000; }

volatile unsigned long idleCount = 0;
Thread idleThread;
void idleLoop() {
  while (1) idleCount++;
}

void printNetworkStatus() {
  if (usingEthernet) {
    //Serial.print("Conectado por Ethernet. IP: ");
    //Serial.println(Ethernet.localIP());
  } else if (WiFi.status() == WL_CONNECTED) {
    //Serial.print("Conectado por WiFi. IP: ");
    //Serial.println(WiFi.localIP());
    //Serial.print("RSSI WiFi: ");
    //Serial.print(WiFi.RSSI());
    //Serial.println(" dBm");
  } else {
    //Serial.println("No conectado a red.");
  }
}

// ==== CONEXIÓN DE RED ====
void connectToNetwork() {
  //Serial.println("Intentando conexión por Ethernet...");
  Ethernet.begin(localIp);

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    //Serial.println("❌ No se detectó hardware Ethernet.");
  } else if (Ethernet.linkStatus() == LinkOFF) {
    //Serial.println("❌ Cable Ethernet no conectado.");
  } else {
    usingEthernet = true;
    //Serial.print("✅ Conectado por Ethernet. IP: ");
    //Serial.println(Ethernet.localIP());
    ethServer.begin();
    return;
  }

  // Si falla Ethernet, intenta WiFi
  //Serial.println("Intentando conexión por WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    //Serial.print(".");
  }

  //Serial.println("\n✅ Conectado a WiFi.");
  //Serial.print("IP: ");
  //Serial.println(WiFi.localIP());
  wifiServer.begin();
}

// ==== PROCESAMIENTO DE IMAGEN ====
void processImageToBinaryWhite(uint8_t* raw, size_t w, size_t h, uint8_t threshold = 200) {
  // Convertir imagen a binario: blanco si pixel > threshold, negro si no
  for (size_t i = 0; i < w * h; i++) {
    if (raw[i] > threshold) {
      raw[i] = 255; // Blanco
    } else {
      raw[i] = 0;   // Negro
    }
  }
}

void enhanceWhiteDetection(uint8_t* raw, size_t w, size_t h) {
  // Crear una copia temporal para el procesamiento
  uint8_t* temp = new uint8_t[w * h];
  memcpy(temp, raw, w * h);
  
  // Aplicar filtro de mediana 3x3 para reducir ruido
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      uint8_t values[9];
      int idx = 0;
      
      // Recoger valores de la ventana 3x3
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          values[idx++] = temp[(y + dy) * w + (x + dx)];
        }
      }
      
      // Ordenamiento burbuja simple para encontrar la mediana
      for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8 - i; j++) {
          if (values[j] > values[j + 1]) {
            uint8_t temp_val = values[j];
            values[j] = values[j + 1];
            values[j + 1] = temp_val;
          }
        }
      }
      
      raw[y * w + x] = values[4]; // Mediana (elemento central)
    }
  }
  
  delete[] temp;
  
  // Aplicar umbralización adaptativa para detección de blancos
  processImageToBinaryWhite(raw, w, h, 180);
}

// ==== ENDPOINT STREAMING ORIGINAL ====
void sendCameraFrameOriginal(Client& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/bmp");
  client.println("Connection: close");
  client.println();

  if (cam.grabFrame(fb, 3000) == 0) {
    uint8_t* raw = fb.getBuffer();
    size_t w = FRAME_WIDTH;
    size_t h = FRAME_HEIGHT;
    const int headerSize = 54;
    const int paletteSize = 1024;
    size_t bmpSize = headerSize + paletteSize + w * h;

    uint8_t bmpHeader[54] = {
      0x42, 0x4D, 0, 0, 0, 0, 0, 0, 0, 0, 0x36, 0x04, 0x00, 0x00, 0x28, 0x00,
      0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x00, 0x00, 0, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    bmpHeader[2] = (uint8_t)(bmpSize);
    bmpHeader[3] = (uint8_t)(bmpSize >> 8);
    bmpHeader[4] = (uint8_t)(bmpSize >> 16);
    bmpHeader[5] = (uint8_t)(bmpSize >> 24);
    bmpHeader[18] = (uint8_t)(w);
    bmpHeader[19] = (uint8_t)(w >> 8);
    bmpHeader[20] = (uint8_t)(w >> 16);
    bmpHeader[21] = (uint8_t)(w >> 24);
    int32_t negHeight = -h;
    bmpHeader[22] = (uint8_t)(negHeight);
    bmpHeader[23] = (uint8_t)(negHeight >> 8);
    bmpHeader[24] = (uint8_t)(negHeight >> 16);
    bmpHeader[25] = (uint8_t)(negHeight >> 24);

    client.write(bmpHeader, 54);

    for (int i = 0; i < 256; i++) {
      uint8_t entry[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, 0x00 };
      client.write(entry, 4);
    }
    client.write(raw, w * h);
  }
  client.stop();
}

// ==== ENDPOINT STREAMING PROCESADO (BLANCO Y NEGRO) ====
void sendCameraFrameProcessed(Client& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/bmp");
  client.println("Connection: close");
  client.println();

  if (cam.grabFrame(fb, 3000) == 0) {
    uint8_t* raw = fb.getBuffer();
    size_t w = FRAME_WIDTH;
    size_t h = FRAME_HEIGHT;
    
    // Crear una copia del buffer para procesamiento
    uint8_t* processedBuffer = new uint8_t[w * h];
    memcpy(processedBuffer, raw, w * h);
    
    // Procesar imagen para detección de blancos (círculo del dojo)
    enhanceWhiteDetection(processedBuffer, w, h);
    
    const int headerSize = 54;
    const int paletteSize = 1024;
    size_t bmpSize = headerSize + paletteSize + w * h;

    uint8_t bmpHeader[54] = {
      0x42, 0x4D, 0, 0, 0, 0, 0, 0, 0, 0, 0x36, 0x04, 0x00, 0x00, 0x28, 0x00,
      0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x00, 0x00, 0, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    bmpHeader[2] = (uint8_t)(bmpSize);
    bmpHeader[3] = (uint8_t)(bmpSize >> 8);
    bmpHeader[4] = (uint8_t)(bmpSize >> 16);
    bmpHeader[5] = (uint8_t)(bmpSize >> 24);
    bmpHeader[18] = (uint8_t)(w);
    bmpHeader[19] = (uint8_t)(w >> 8);
    bmpHeader[20] = (uint8_t)(w >> 16);
    bmpHeader[21] = (uint8_t)(w >> 24);
    int32_t negHeight = -h;
    bmpHeader[22] = (uint8_t)(negHeight);
    bmpHeader[23] = (uint8_t)(negHeight >> 8);
    bmpHeader[24] = (uint8_t)(negHeight >> 16);
    bmpHeader[25] = (uint8_t)(negHeight >> 24);

    client.write(bmpHeader, 54);

    // Paleta de colores para escala de grises
    for (int i = 0; i < 256; i++) {
      uint8_t entry[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, 0x00 };
      client.write(entry, 4);
    }
    
    // Enviar imagen procesada
    client.write(processedBuffer, w * h);
    
    // Liberar memoria
    delete[] processedBuffer;
  }
  client.stop();
}




// ==== MÉTRICAS ENDPOINT ====
void handleMetrics(Client& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Access-Control-Allow-Origin: *");   // <--- LÍNEA NECESARIA PARA CORS
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();

  client.print("{");
  client.print("\"uptime_ms\":"); client.print(getUptimeMillis()); client.print(",");
  client.print("\"free_memory\":"); client.print(freeMemory()); client.print(",");
  client.print("\"idle_count\":"); client.print(idleCount); client.print(",");
  if (usingEthernet) {
    client.print("\"network\":\"ethernet\",");
    client.print("\"ip\":\""); client.print(Ethernet.localIP()); client.print("\"");
  } else if (WiFi.status() == WL_CONNECTED) {
    client.print("\"network\":\"wifi\",");
    client.print("\"ip\":\""); client.print(WiFi.localIP()); client.print("\",");
    client.print("\"rssi\":"); client.print(WiFi.RSSI());
  }
  client.println("}");
  idleCount = 0;
}


// ==== PÁGINA PRINCIPAL ====
void serveMainPage(Client& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body>");
  client.println("<h1>Sumo Sentinel Vision - Streams de Cámara</h1>");
  client.println("<h3>Stream 1 - Imagen Original:</h3>");
  client.println("<img src='/stream1' style='border:2px solid #333; margin:5px;' /><br>");
  client.println("<h3>Stream 2 - Procesado Blanco/Negro (Detección Dojo):</h3>");
  client.println("<img src='/stream2' style='border:2px solid #333; margin:5px;' /><br>");
  client.println("<h3>Stream 3 - Imagen Original:</h3>");
  client.println("<img src='/stream3' style='border:2px solid #333; margin:5px;' /><br>");
  client.println("<h2>Métricas</h2>");
  client.println("<div id='metrics'></div>");
  client.println("<script>");
  client.println("setInterval(()=>{fetch('/metrics').then(r=>r.json()).then(d=>{");
  client.println("document.getElementById('metrics').innerHTML = "
    "'Uptime: '+d.uptime_ms+' ms, Memoria libre: '+d.free_memory+' bytes, CPU: '+d.idle_count + (d.rssi !== undefined ? ', RSSI: ' + d.rssi + ' dBm' : '');");  
  client.println("});},1000);");
  client.println("</script>");
  client.println("</body></html>");
}

// ==== SETUP Y LOOP ====
unsigned long lastMetrics = 0;

void setup() {
  //Serial.begin(115200, SERIAL_8N1);
  //while (!Serial);

  cam.begin(CAMERA_R160x120, IMAGE_MODE, 30);
  connectToNetwork();

  idleThread.start(idleLoop);
  //Serial.println("----- READY, endpoints: /stream1, /stream2, /stream3, /metrics -----");
}

void loop() {
  WiFiClient wifiClient;
  EthernetClient ethClient;
  Client* client = nullptr;

  if (usingEthernet) {
    ethClient = ethServer.available();
    if (ethClient) client = &ethClient;
  } else {
    wifiClient = wifiServer.available();
    if (wifiClient) client = &wifiClient;
  }

  if (client && client->connected()) {
    String request = client->readStringUntil('\r');
    client->read(); // Consumir '\n'
    //Serial.println(request);

    if (request.indexOf("GET /stream1") >= 0) {
      sendCameraFrameOriginal(*client);
    }
    else if (request.indexOf("GET /stream2") >= 0) {
      sendCameraFrameProcessed(*client); // Stream procesado en blanco y negro
    }
    else if (request.indexOf("GET /stream3") >= 0) {
      sendCameraFrameOriginal(*client); // Por ahora igual al stream1
    }

    else if (request.indexOf("GET /metrics") >= 0) {
      handleMetrics(*client);
      client->stop();
    }
    else {
      serveMainPage(*client);
      client->stop();
    }
  }

  // Opcional: muestra las métricas en Serial cada 5s para debug
  unsigned long now = millis();
  if (now - lastMetrics > 5000) {
    lastMetrics = now;
    //Serial.print("Uptime: ");
    //Serial.print(getUptimeMillis());
    //Serial.print(" ms | Mem libre: ");
    //Serial.print(freeMemory());
    //Serial.print(" bytes | CPU: ");
    //Serial.println(idleCount);
    idleCount = 0;
    printNetworkStatus();
  }
}














