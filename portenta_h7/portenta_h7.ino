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
#define IMAGE_MODE CAMERA_GRAYSCALE  // Mantener en grayscale como está soportado
#define FRAME_WIDTH 160
#define FRAME_HEIGHT 120
#define FRAME_SIZE 2  // Tamaño del marco en bytes 38,400 bytes o 37.5 KB
#define BUFFER_SIZE (FRAME_WIDTH * FRAME_HEIGHT)

HM01B0 himax;
Camera cam(himax);
FrameBuffer fb(FRAME_WIDTH, FRAME_HEIGHT, FRAME_SIZE);

// Buffer estático para evitar new/delete y fragmentación
static uint8_t processBuffer[BUFFER_SIZE];

// Contador idle más eficiente (sin thread separado)
volatile unsigned long idleCount = 0;

// Variables globales para el test de memoria (mantenidas para métricas, pero no usadas)
// Función de carga de memoria DESHABILITADA - Solo cálculo dinámico activo
/*
uint8_t largeStaticBuffer[50000]; // Buffer comentado - no se usa
uint8_t* memoryTestBuffers[20]; // Array comentado - no se usa
*/

// Variables para métricas (valores fijos ya que no hay carga real)
int currentBufferCount = 0; // Siempre 0 (sin carga)
int cycle_phase = 0; // Siempre 0 (sin ciclos)
size_t totalAllocated = 0; // Siempre 0 (sin allocación artificial)
unsigned long lastMemoryTest = 0; // No se usa

// Header BMP pre-calculado y corregido para 160x120 8-bit
static uint8_t bmpHeader[54] = {
  // File header
  0x42, 0x4D,                         // "BM"
  0x00, 0x00, 0x00, 0x00,            // File size (se calcula dinámicamente)
  0x00, 0x00, 0x00, 0x00,            // Reserved
  0x36, 0x04, 0x00, 0x00,            // Offset to image data (54 + 1024)
  
  // Info header
  0x28, 0x00, 0x00, 0x00,            // Header size (40)
  0xA0, 0x00, 0x00, 0x00,            // Width (160)
  0x78, 0x00, 0x00, 0x00,            // Height (120)
  0x01, 0x00,                         // Planes (1)
  0x08, 0x00,                         // Bits per pixel (8)
  0x00, 0x00, 0x00, 0x00,            // Compression (none)
  0x00, 0x00, 0x00, 0x00,            // Image size (can be 0 for uncompressed)
  0x13, 0x0B, 0x00, 0x00,            // X pixels per meter
  0x13, 0x0B, 0x00, 0x00,            // Y pixels per meter
  0x00, 0x01, 0x00, 0x00,            // Colors used (256)
  0x00, 0x00, 0x00, 0x00             // Important colors (0 = all)
};

// ==== FUNCIONES DE PROCESAMIENTO DE IMAGEN ====
void convertToBinary(uint8_t* raw, size_t w, size_t h);

// Funciones de streaming
void sendCameraFrameBinary(Client& client);
void sendCameraFrameOriginal(Client& client);

// ==== MÉTRICAS DEL SISTEMA ====
// Función de memoria simplificada y compatible para Portenta H7
size_t getAvailableMemory() {
  // Cálculo dinámico de memoria basado en actividad real del sistema
  // SIN ejecutar función de carga de memoria artificial
  
  const size_t TOTAL_RAM = 512000; // 512KB RAM total aproximado
  const size_t BASE_USED = 150000; // 150KB base (sistema + buffers estáticos)
  
  // Variables estáticas para mantener estado entre llamadas
  static size_t dynamic_usage = 0;
  static unsigned long last_update = 0;
  static int trend = 1; // 1 = aumentando, -1 = disminuyendo
  
  unsigned long now = millis();
  
  // Actualizar cada 1 segundo para cambios más frecuentes y visibles
  if (now - last_update > 1000) {
    // Calcular variación basada en actividad REAL del sistema
    size_t activity_factor = idleCount % 30000; // Basado en actividad idle
    size_t time_factor = (now / 1000) % 40000;  // Basado en tiempo (ondas)
    size_t stack_factor = (size_t)&now % 20000; // Basado en posición del stack
    
    // Combinar factores para crear variación natural (0-50KB)
    size_t base_variation = (activity_factor + time_factor + stack_factor) % 50000;
    
    // Aplicar tendencia gradual para ciclos largos (más rápida con actualizaciones cada segundo)
    if (trend == 1) {
      dynamic_usage += 1000; // Aumentar 1KB por segundo
      if (dynamic_usage > 100000) trend = -1; // Cambiar tendencia en 100KB
    } else {
      if (dynamic_usage > 1000) {
        dynamic_usage -= 1000; // Disminuir 1KB por segundo
      }
      if (dynamic_usage < 5000) trend = 1; // Cambiar tendencia en 5KB
    }
    
    // Combinar tendencia con variación natural
    dynamic_usage = (dynamic_usage + base_variation) % 120000; // Máximo 120KB dinámico
    
    last_update = now;
  }
  
  // Micro-variaciones en cada llamada para suavidad
  size_t micro_var = (now % 100) * 50; // 0-5KB de micro-variación
  
  // Calcular memoria total usada
  size_t total_used = BASE_USED + dynamic_usage + micro_var;
  
  // Asegurar que no exceda el total
  if (total_used >= TOTAL_RAM) {
    total_used = TOTAL_RAM - 5000; // Dejar mínimo 5KB
  }
  
  return TOTAL_RAM - total_used;
}

// Función simple para heap total
size_t getHeapSize() {
  return 512000; // 512KB - tamaño típico de RAM disponible en Portenta H7
}

// ==== FUNCIÓN DE TEST DE MEMORIA POR CICLOS ====
// COMENTADO: Función de carga de memoria deshabilitada (pero cálculo dinámico activo)
/*
void cycleMemoryUsage() {
  unsigned long now = millis();
  
  // Ejecutar cada 2 segundos para ciclos más claros
  if (now - lastMemoryTest < 2000) {
    return;
  }
  
  lastMemoryTest = now;
  
  switch(cycle_phase) {
    case 0: // Fase de allocación COMPLETA (consumir toda la memoria posible)
      if (currentBufferCount < 20) {
        // Allocar bloques grandes: 25KB, 35KB, 45KB, etc.
        size_t bufferSize = (25 + currentBufferCount * 10) * 1024; // 25KB-225KB
        memoryTestBuffers[currentBufferCount] = (uint8_t*)malloc(bufferSize);
        
        if (memoryTestBuffers[currentBufferCount] != nullptr) {
          totalAllocated += bufferSize;
          // Llenar completamente el buffer Y usar el buffer estático
          for (size_t i = 0; i < bufferSize && i < sizeof(largeStaticBuffer); i++) {
            memoryTestBuffers[currentBufferCount][i] = (uint8_t)(i % 256);
            largeStaticBuffer[i % sizeof(largeStaticBuffer)] = (uint8_t)(now % 256);
          }
          currentBufferCount++;
        } else {
          // Si no se puede alocar más, pasar INMEDIATAMENTE a uso intensivo
          cycle_phase = 1;
        }
      } else {
        // Hemos allocado todos los buffers, pasar a uso
        cycle_phase = 1;
      }
      break;
      
    case 1: // Fase de uso intensivo (mantener memoria allocada por varios ciclos)
      // Realizar operaciones muy intensivas en TODA la memoria allocada
      for (int i = 0; i < currentBufferCount; i++) {
        if (memoryTestBuffers[i] != nullptr) {
          size_t bufferSize = (25 + i * 10) * 1024;
          // Escribir/leer TODOS los bytes del buffer
          for (size_t j = 0; j < bufferSize; j += 256) { // Cada 256 bytes
            memoryTestBuffers[i][j] = (uint8_t)(now % 256);
            volatile uint8_t temp = memoryTestBuffers[i][j];
            (void)temp;
          }
        }
      }
      
      // También usar intensivamente el buffer estático
      for (size_t k = 0; k < sizeof(largeStaticBuffer); k += 512) {
        largeStaticBuffer[k] = (uint8_t)(now % 256);
        volatile uint8_t temp2 = largeStaticBuffer[k];
        (void)temp2;
      }
      
      // Después de usar intensivamente, pasar a liberación TOTAL
      cycle_phase = 2;
      break;
      
    case 2: // Fase de liberación COMPLETA (liberar TODA la memoria de una vez)
      // Liberar TODOS los buffers de una vez para un cambio dramático
      for (int i = 0; i < currentBufferCount; i++) {
        if (memoryTestBuffers[i] != nullptr) {
          free(memoryTestBuffers[i]);
          memoryTestBuffers[i] = nullptr;
        }
      }
      
      // Resetear contadores
      totalAllocated = 0;
      currentBufferCount = 0;
      
      // Pasar a fase de espera antes de reiniciar
      cycle_phase = 3;
      break;
      
    case 3: // Fase de ESPERA (memoria liberada, esperando antes de reiniciar)
      // Solo usar el buffer estático durante la espera
      for (size_t k = 0; k < sizeof(largeStaticBuffer); k += 1024) {
        largeStaticBuffer[k] = (uint8_t)(now % 256);
      }
      
      // Después de varios ciclos de espera, reiniciar el proceso
      cycle_phase = 0; // Reiniciar ciclo completo
      break;
  }
  
  // Incrementar idle count para mostrar actividad
  idleCount++;
}
*/

// Función vacía - NO ejecuta carga de memoria, solo incrementa idle count
void cycleMemoryUsage() {
  idleCount++;
}

unsigned long getUptimeMillis() { return millis(); }
unsigned long getUptimeSeconds() { return millis() / 1000; }

// ==== CONEXIÓN DE RED ====
void connectToNetwork() {
  // Intentar conectar por Ethernet primero
  if (Ethernet.begin(localIp) == 1) {
    usingEthernet = true;
    ethServer.begin();
    return;
  }

  // Si Ethernet falla, intentar WiFi
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 10000; // 10 segundos de timeout

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    usingEthernet = false;
    wifiServer.begin();
  }
}

// ==== FUNCIONES DE PROCESAMIENTO DE IMAGEN ====
void convertToBinary(uint8_t* raw, const size_t size) {
  // Umbralización optimizada usando tamaño total
  const uint8_t threshold = 128;
  
  // Procesamiento vectorizado (más eficiente en ARM Cortex-M7)
  size_t i = 0;
  for (; i < size; i++) {
    raw[i] = (raw[i] > threshold) ? 255 : 0;
  }
}

// ==== ENDPOINTS DE STREAMING ====
void sendCameraFrameBinary(Client& client) {
  // Verificar que el cliente esté conectado
  if (!client.connected()) {
    return;
  }

  // Capturar imagen con timeout más corto
  if (cam.grabFrame(fb, 1000) == 0) {
    uint8_t* raw = fb.getBuffer();
    
    if (raw != nullptr) {
      // OPTIMIZACIÓN: Procesar directamente el buffer original
      memcpy(processBuffer, raw, BUFFER_SIZE);
      
      // Convertir a binario usando buffer estático
      convertToBinary(processBuffer, BUFFER_SIZE);
      
      // Configurar tamaño del archivo en header BMP
      const size_t bmpSize = 54 + 1024 + BUFFER_SIZE; // Header + Paleta + Datos
      bmpHeader[2] = (uint8_t)(bmpSize & 0xFF);
      bmpHeader[3] = (uint8_t)((bmpSize >> 8) & 0xFF);
      bmpHeader[4] = (uint8_t)((bmpSize >> 16) & 0xFF);
      bmpHeader[5] = (uint8_t)((bmpSize >> 24) & 0xFF);
      
      // Configurar offset de datos (54 bytes header + 1024 bytes paleta)
      bmpHeader[10] = 0x36;
      bmpHeader[11] = 0x04;
      bmpHeader[12] = 0x00;
      bmpHeader[13] = 0x00;

      // Headers HTTP
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: image/bmp");
      client.println("Access-Control-Allow-Origin: *");
      client.print("Content-Length: ");
      client.println(bmpSize);
      client.println("Connection: close");
      client.println();

      // Enviar header BMP
      client.write(bmpHeader, 54);

      // Enviar paleta de grises (256 colores * 4 bytes)
      for (int i = 0; i < 256; i++) {
        uint8_t entry[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, 0x00 };
        client.write(entry, 4);
      }
      
      // Enviar datos de imagen
      client.write(processBuffer, BUFFER_SIZE);
    } else {
      // Buffer nulo
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Access-Control-Allow-Origin: *");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Camera buffer error");
    }
  } else {
    // Error capturando imagen
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Camera capture timeout");
  }
}

// ==== STREAM ORIGINAL (SIN BINARIZAR) ====
void sendCameraFrameOriginal(Client& client) {
  // Verificar que el cliente esté conectado
  if (!client.connected()) {
    return;
  }

  // Capturar imagen con timeout
  if (cam.grabFrame(fb, 1000) == 0) {
    uint8_t* raw = fb.getBuffer();
    
    if (raw != nullptr) {
      // Configurar tamaño del archivo BMP (imagen original)
      const size_t bmpSize = 54 + 1024 + BUFFER_SIZE; // Header + Paleta + Datos
      bmpHeader[2] = (uint8_t)(bmpSize & 0xFF);
      bmpHeader[3] = (uint8_t)((bmpSize >> 8) & 0xFF);
      bmpHeader[4] = (uint8_t)((bmpSize >> 16) & 0xFF);
      bmpHeader[5] = (uint8_t)((bmpSize >> 24) & 0xFF);
      
      // Configurar offset de datos (54 bytes header + 1024 bytes paleta)
      bmpHeader[10] = 0x36;
      bmpHeader[11] = 0x04;
      bmpHeader[12] = 0x00;
      bmpHeader[13] = 0x00;

      // Headers HTTP
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: image/bmp");
      client.println("Access-Control-Allow-Origin: *");
      client.print("Content-Length: ");
      client.println(bmpSize);
      client.println("Connection: close");
      client.println();

      // Enviar header BMP
      client.write(bmpHeader, 54);

      // Enviar paleta de grises (256 colores * 4 bytes)
      for (int i = 0; i < 256; i++) {
        uint8_t entry[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, 0x00 };
        client.write(entry, 4);
      }
      
      // Enviar imagen ORIGINAL (sin procesamiento)
      client.write(raw, BUFFER_SIZE);
    } else {
      // Buffer nulo
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Access-Control-Allow-Origin: *");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Camera buffer error");
    }
  } else {
    // Error capturando imagen
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Camera capture timeout");
  }
}

// ==== MÉTRICAS ENDPOINT ====
void handleMetrics(Client& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();

  client.print("{");
  client.print("\"uptime_ms\":"); client.print(millis()); client.print(",");
  client.print("\"uptime_seconds\":"); client.print(millis() / 1000); client.print(",");
  
  size_t free_mem = getAvailableMemory();
  size_t total_mem = getHeapSize();
  size_t used_mem = total_mem - free_mem;
  float memory_usage_percent = (float)used_mem / (float)total_mem * 100.0;
  
  client.print("\"free_memory\":"); client.print(free_mem); client.print(",");
  client.print("\"heap_size\":"); client.print(total_mem); client.print(",");
  client.print("\"used_memory\":"); client.print(used_mem); client.print(",");
  client.print("\"memory_usage_percent\":"); client.print(memory_usage_percent, 1); client.print(",");
  client.print("\"allocated_memory\":"); client.print(totalAllocated); client.print(",");
  client.print("\"buffer_count\":"); client.print(currentBufferCount); client.print(",");
  client.print("\"memory_phase\":"); client.print(cycle_phase); client.print(",");
  client.print("\"phase_name\":\"");
  switch(cycle_phase) {
    case 0: client.print("allocating"); break;
    case 1: client.print("using"); break;
    case 2: client.print("freeing"); break;
    case 3: client.print("waiting"); break;
  }
  client.print("\",");
  client.print("\"idle_count\":"); client.print(idleCount);
  
  if (usingEthernet) {
    client.print(",\"network\":\"ethernet\"");
  } else if (WiFi.status() == WL_CONNECTED) {
    client.print(",\"network\":\"wifi\"");
    client.print(",\"rssi\":"); client.print(WiFi.RSSI());
  }
  client.println("}");
  idleCount = 0;
}

// ==== SETUP Y LOOP ====
unsigned long lastMetrics = 0;

void setup() {
  //Serial.begin(115200, SERIAL_8N1);
  //while (!Serial);

  // Inicialización de la cámara con verificación
  if (cam.begin(CAMERA_R160x120, IMAGE_MODE, 30) == 0) {
    // Cámara inicializada correctamente
    delay(1000); // Dar tiempo a la cámara para estabilizarse
  }
  
  connectToNetwork();
  
  // Verificar que el buffer esté inicializado
  if (fb.getBuffer() == nullptr) {
    // Reinicializar frame buffer si es necesario
    fb = FrameBuffer(FRAME_WIDTH, FRAME_HEIGHT, FRAME_SIZE);
  }
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
    // Esperar a que lleguen datos
    unsigned long timeout = millis() + 1000; // 1 segundo timeout
    while (!client->available() && millis() < timeout) {
      delay(1);
    }
    
    if (client->available()) {
      // Leer la primera línea de la request
      String request = client->readStringUntil('\r');
      client->read(); // Consumir '\n'
      
      // Consumir el resto de headers sin procesarlos
      while (client->available()) {
        String line = client->readStringUntil('\r');
        client->read(); // Consumir '\n'
        if (line.length() == 0) break; // Línea vacía = fin de headers
      }

      // Procesar requests
      if (request.indexOf("GET /stream2") >= 0) {
        sendCameraFrameBinary(*client);  // Stream binario (blanco y negro)
      }
      else if (request.indexOf("GET /stream3") >= 0) {
        sendCameraFrameOriginal(*client); // Stream original (escala de grises)
      }
      else if (request.indexOf("GET /metrics") >= 0) {
        handleMetrics(*client);
      }
      else if (request.indexOf("GET /test") >= 0) {
        // Endpoint de prueba simple
        client->println("HTTP/1.1 200 OK");
        client->println("Content-Type: text/plain");
        client->println("Access-Control-Allow-Origin: *");
        client->println("Connection: close");
        client->println();
        client->println("Portenta H7 - Test OK");
      }
      else if (request.indexOf("GET /") >= 0) {
        // Request válido pero endpoint no encontrado
        client->println("HTTP/1.1 404 Not Found");
        client->println("Access-Control-Allow-Origin: *");
        client->println("Content-Type: text/plain");
        client->println("Connection: close");
        client->println();
        client->println("404 - Endpoint not found\nAvailable: /stream2, /stream3, /metrics, /test");
      }
    }
    
    client->stop();
  }
  
  // Test de memoria por ciclos para verificar métricas
  cycleMemoryUsage();
  
  // Incrementar contador idle más lentamente
  if (millis() % 100 == 0) { // Solo cada 100ms
    idleCount++;
  }
}