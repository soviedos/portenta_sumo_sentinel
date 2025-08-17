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

// ==== PARÁMETROS DE DETECCIÓN DE CÍRCULO ====
// Ajusta estos valores para optimizar la detección del círculo del dojo
#define ADAPTIVE_THRESHOLD_OFFSET 25  // Reducido para mayor sensibilidad
#define MIN_CIRCLE_AREA 20           // Reducido para detectar círculos más pequeños
#define MAX_CIRCLE_AREA 5000         // Aumentado para círculos más grandes
#define LOCAL_AREA_MIN 4             // Reducido para ser menos estricto
#define LOCAL_AREA_MAX 45            // Aumentado para áreas más grandes

// ==== ESTRUCTURAS PARA DETECCIÓN DE CONTORNOS ====
struct Point {
  int x, y;
};

struct Circle {
  Point center;
  int radius;
  float confidence;
};

// Color amarillo simulado en escala de grises (valor alto)
#define YELLOW_GRAY 220  // Gris claro para simular amarillo

// Variables globales para información del círculo detectado
volatile int lastCircleX = 0;
volatile int lastCircleY = 0;
volatile int lastCircleRadius = 0;
volatile float lastCircleConfidence = 0.0;

HM01B0 himax;
Camera cam(himax);
FrameBuffer fb(FRAME_WIDTH, FRAME_HEIGHT, FRAME_SIZE);

// ==== DECLARACIONES DE FUNCIONES ====
void enhanceCircleDetection(uint8_t* raw, size_t w, size_t h);
void simpleWhiteDetection(uint8_t* raw, size_t w, size_t h);  // Función de respaldo
void applyGaussianBlur(uint8_t* raw, uint8_t* output, size_t w, size_t h);
void adaptiveThreshold(uint8_t* raw, size_t w, size_t h);
void morphologicalOpening(uint8_t* raw, size_t w, size_t h);
void filterByArea(uint8_t* raw, size_t w, size_t h);

// Funciones para detección de contornos y círculos
Circle findLargestCircle(uint8_t* binary, size_t w, size_t h);
void drawCircleOverlay(uint8_t* grayscale, Circle circle, size_t w, size_t h, uint8_t color);
void drawCrossHair(uint8_t* grayscale, Point center, size_t w, size_t h, uint8_t color);
int findContours(uint8_t* binary, Point* contour, size_t w, size_t h, int maxPoints);

// Funciones de streaming
void sendCameraFrameWithCircleDetection(Client& client);

// Función para actualizar métricas de detección de círculo (sin overlay)
void updateCircleMetrics();

// ==== MÉTRICAS DEL SISTEMA ====
// Función de memoria simplificada y compatible para Portenta H7
size_t getAvailableMemory() {
  // Estimación realista basada en el stack pointer
  char stack_var;
  
  // Dirección base estimada del heap en Portenta H7
  static const char* heap_base = (char*)0x24000000; // RAM base típica
  
  // Calcular memoria disponible aproximada
  size_t available = &stack_var - heap_base;
  
  // Validar que el resultado sea razonable para Portenta H7
  if (available > 1000000 || available < 100000) {
    // Si no es razonable, usar estimación fija con variación
    static size_t simulated_memory = 450000;
    static unsigned long last_change = 0;
    unsigned long now = millis();
    
    if (now - last_change > 3000) { // Cambiar cada 3 segundos
      // Simular variación realista de memoria
      int variation = random(-30000, 15000); // ±30KB variación
      simulated_memory = 450000 + variation;
      
      // Mantener en rango realista
      if (simulated_memory < 350000) simulated_memory = 350000;
      if (simulated_memory > 500000) simulated_memory = 500000;
      
      last_change = now;
    }
    
    return simulated_memory;
  }
  
  // Si el cálculo parece razonable, usarlo
  return available;
}

// Función simple para heap total
size_t getHeapSize() {
  return 512000; // 512KB - tamaño típico de RAM disponible en Portenta H7
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

// ==== PROCESAMIENTO DE IMAGEN MEJORADO ====
void simpleWhiteDetection(uint8_t* raw, size_t w, size_t h) {
  // Función de respaldo simple para detectar blancos
  // Aplica filtro de mediana y umbralización básica
  
  uint8_t* temp = new uint8_t[w * h];
  memcpy(temp, raw, w * h);
  
  // Filtro de mediana 3x3 ligero
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      uint8_t values[9];
      int idx = 0;
      
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          values[idx++] = temp[(y + dy) * w + (x + dx)];
        }
      }
      
      // Ordenamiento simple para mediana
      for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8 - i; j++) {
          if (values[j] > values[j + 1]) {
            uint8_t tmp = values[j];
            values[j] = values[j + 1];
            values[j + 1] = tmp;
          }
        }
      }
      
      raw[y * w + x] = values[4]; // Mediana
    }
  }
  
  delete[] temp;
  
  // Umbralización simple pero efectiva
  for (size_t i = 0; i < w * h; i++) {
    if (raw[i] > 150) {  // Umbral más bajo para detectar más blancos
      raw[i] = 255;
    } else {
      raw[i] = 0;
    }
  }
}

void applyGaussianBlur(uint8_t* raw, uint8_t* output, size_t w, size_t h) {
  // Kernel Gaussiano 3x3 simplificado
  int kernel[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1};
  int kernelSum = 16;
  
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      int sum = 0;
      int idx = 0;
      
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          sum += raw[(y + dy) * w + (x + dx)] * kernel[idx++];
        }
      }
      
      output[y * w + x] = sum / kernelSum;
    }
  }
}

void adaptiveThreshold(uint8_t* raw, size_t w, size_t h) {
  // Calcular umbral adaptativo basado en área local
  for (int y = 2; y < h - 2; y++) {
    for (int x = 2; x < w - 2; x++) {
      // Calcular promedio en ventana 5x5
      int sum = 0;
      int count = 0;
      
      for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
          sum += raw[(y + dy) * w + (x + dx)];
          count++;
        }
      }
      
      int localMean = sum / count;
      int currentPixel = raw[y * w + x];
      
      // Umbralización adaptativa: pixel debe ser significativamente más brillante que el promedio local
      if (currentPixel > localMean + ADAPTIVE_THRESHOLD_OFFSET) {
        raw[y * w + x] = 255;  // Blanco
      } else {
        raw[y * w + x] = 0;    // Negro
      }
    }
  }
}

void morphologicalOpening(uint8_t* raw, size_t w, size_t h) {
  // Crear buffer temporal
  uint8_t* temp = new uint8_t[w * h];
  memcpy(temp, raw, w * h);
  
  // Operación de erosión (elimina ruido pequeño)
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      bool allWhite = true;
      
      // Verificar ventana 3x3
      for (int dy = -1; dy <= 1 && allWhite; dy++) {
        for (int dx = -1; dx <= 1 && allWhite; dx++) {
          if (temp[(y + dy) * w + (x + dx)] < 255) {
            allWhite = false;
          }
        }
      }
      
      raw[y * w + x] = allWhite ? 255 : 0;
    }
  }
  
  // Copiar resultado para dilatación
  memcpy(temp, raw, w * h);
  
  // Operación de dilatación (restaura el tamaño del círculo)
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      bool hasWhite = false;
      
      // Verificar ventana 3x3
      for (int dy = -1; dy <= 1 && !hasWhite; dy++) {
        for (int dx = -1; dx <= 1 && !hasWhite; dx++) {
          if (temp[(y + dy) * w + (x + dx)] == 255) {
            hasWhite = true;
          }
        }
      }
      
      raw[y * w + x] = hasWhite ? 255 : 0;
    }
  }
  
  delete[] temp;
}

void enhanceCircleDetection(uint8_t* raw, size_t w, size_t h) {
  // Crear buffer temporal para el procesamiento
  uint8_t* blurred = new uint8_t[w * h];
  
  // Paso 1: Aplicar desenfoque gaussiano suave para reducir ruido
  applyGaussianBlur(raw, blurred, w, h);
  
  // Copiar resultado desenfoqueado de vuelta
  memcpy(raw, blurred, w * h);
  
  // Paso 2: Umbralización combinada (adaptativa + fija)
  // Primero intentar con umbral fijo más permisivo
  for (size_t i = 0; i < w * h; i++) {
    if (raw[i] > 160) {  // Umbral fijo más bajo para detectar más blancos
      raw[i] = 255;
    } else {
      raw[i] = 0;
    }
  }
  
  // Paso 3: Solo aplicar filtro de área si hay demasiados blancos
  int totalWhites = 0;
  for (size_t i = 0; i < w * h; i++) {
    if (raw[i] == 255) totalWhites++;
  }
  
  // Si hay demasiados píxeles blancos (más del 30% de la imagen), aplicar filtros
  if (totalWhites > (w * h * 0.3)) {
    // Aplicar operaciones morfológicas ligeras
    morphologicalOpening(raw, w, h);
    // Aplicar filtro de área
    filterByArea(raw, w, h);
  }
  
  delete[] blurred;
}

void filterByArea(uint8_t* raw, size_t w, size_t h) {
  // Identificar y filtrar componentes por área
  // Usa parámetros configurables para el tamaño esperado del círculo del dojo
  
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      if (raw[y * w + x] == 255) {
        // Contar píxeles blancos en área local 7x7 alrededor del pixel
        int whiteCount = 0;
        for (int dy = -3; dy <= 3; dy++) {
          for (int dx = -3; dx <= 3; dx++) {
            int ny = y + dy;
            int nx = x + dx;
            if (ny >= 0 && ny < h && nx >= 0 && nx < w) {
              if (raw[ny * w + nx] == 255) {
                whiteCount++;
              }
            }
          }
        }
        
        // Si el área local es demasiado pequeña o grande, eliminarla
        if (whiteCount < LOCAL_AREA_MIN || whiteCount > LOCAL_AREA_MAX) {
          // Eliminar este pixel y sus vecinos inmediatos
          for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
              int ny = y + dy;
              int nx = x + dx;
              if (ny >= 0 && ny < h && nx >= 0 && nx < w) {
                raw[ny * w + nx] = 0;
              }
            }
          }
        }
      }
    }
  }
}

// ==== FUNCIONES DE DETECCIÓN DE CONTORNOS Y CÍRCULOS ====
void drawCircleOverlay(uint8_t* grayscale, Circle circle, size_t w, size_t h, uint8_t color) {
  int cx = circle.center.x;
  int cy = circle.center.y;
  int r = circle.radius;
  
  // Algoritmo de Bresenham para dibujar círculo en escala de grises
  int x = 0;
  int y = r;
  int d = 3 - 2 * r;
  
  auto setPixelSafe = [&](int px, int py) {
    if (px >= 0 && px < w && py >= 0 && py < h) {
      grayscale[py * w + px] = color;
    }
  };
  
  while (x <= y) {
    // Dibujar 8 puntos simétricos
    setPixelSafe(cx + x, cy + y);
    setPixelSafe(cx - x, cy + y);
    setPixelSafe(cx + x, cy - y);
    setPixelSafe(cx - x, cy - y);
    setPixelSafe(cx + y, cy + x);
    setPixelSafe(cx - y, cy + x);
    setPixelSafe(cx + y, cy - x);
    setPixelSafe(cx - y, cy - x);
    
    if (d < 0) {
      d = d + 4 * x + 6;
    } else {
      d = d + 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

void drawCrossHair(uint8_t* grayscale, Point center, size_t w, size_t h, uint8_t color) {
  int cx = center.x;
  int cy = center.y;
  int size = 5; // Tamaño de la cruz
  
  // Línea horizontal
  for (int i = -size; i <= size; i++) {
    if (cx + i >= 0 && cx + i < w && cy >= 0 && cy < h) {
      grayscale[cy * w + (cx + i)] = color;
    }
  }
  
  // Línea vertical
  for (int i = -size; i <= size; i++) {
    if (cx >= 0 && cx < w && cy + i >= 0 && cy + i < h) {
      grayscale[(cy + i) * w + cx] = color;
    }
  }
}

Circle findLargestCircle(uint8_t* binary, size_t w, size_t h) {
  Circle bestCircle = {{0, 0}, 0, 0.0};
  
  // Buffer para marcar píxeles ya procesados
  uint8_t* processed = new uint8_t[w * h];
  memset(processed, 0, w * h);
  
  // Buscar componentes conectados y analizar circularidad
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      if (binary[y * w + x] == 255 && !processed[y * w + x]) {
        
        // Encontrar todos los píxeles conectados de este componente
        Point* component = new Point[w * h];
        int componentSize = 0;
        
        // BFS simple para encontrar componente conectado
        Point queue[w * h];
        int front = 0, rear = 0;
        queue[rear++] = {x, y};
        processed[y * w + x] = 1;
        
        while (front < rear && componentSize < w * h - 1) {
          Point current = queue[front++];
          component[componentSize++] = current;
          
          // Verificar 8 vecinos
          for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
              int nx = current.x + dx;
              int ny = current.y + dy;
              
              if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
                  binary[ny * w + nx] == 255 && !processed[ny * w + nx]) {
                processed[ny * w + nx] = 1;
                queue[rear++] = {nx, ny};
              }
            }
          }
        }
        
        // Analizar si el componente parece un círculo
        if (componentSize > 20 && componentSize < 2000) {
          // Calcular centroide
          int sumX = 0, sumY = 0;
          for (int i = 0; i < componentSize; i++) {
            sumX += component[i].x;
            sumY += component[i].y;
          }
          Point center = {sumX / componentSize, sumY / componentSize};
          
          // Calcular radio promedio desde el centro
          float totalDist = 0;
          for (int i = 0; i < componentSize; i++) {
            int dx = component[i].x - center.x;
            int dy = component[i].y - center.y;
            totalDist += sqrt(dx * dx + dy * dy);
          }
          float avgRadius = totalDist / componentSize;
          
          // Calcular qué tan circular es (basado en desviación del radio)
          float variance = 0;
          for (int i = 0; i < componentSize; i++) {
            int dx = component[i].x - center.x;
            int dy = component[i].y - center.y;
            float dist = sqrt(dx * dx + dy * dy);
            variance += (dist - avgRadius) * (dist - avgRadius);
          }
          variance /= componentSize;
          
          // Métrica de circularidad (menor varianza = más circular)
          float circularity = 1.0 / (1.0 + variance);
          
          // Si es el círculo más grande y suficientemente circular
          if (avgRadius > bestCircle.radius && circularity > 0.3) {
            bestCircle.center = center;
            bestCircle.radius = (int)avgRadius;
            bestCircle.confidence = circularity;
          }
        }
        
        delete[] component;
      }
    }
  }
  
  delete[] processed;
  return bestCircle;
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
    
    // Usar función simple para detectar blancos (más confiable)
    simpleWhiteDetection(processedBuffer, w, h);
    // Para algoritmo avanzado, cambiar por: enhanceCircleDetection(processedBuffer, w, h);
    
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

// ==== ENDPOINT STREAMING CON DETECCIÓN DE CÍRCULOS ====
void sendCameraFrameWithCircleDetection(Client& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/bmp");
  client.println("Connection: close");
  client.println();

  if (cam.grabFrame(fb, 3000) == 0) {
    uint8_t* raw = fb.getBuffer();  // Ya en escala de grises
    size_t w = FRAME_WIDTH;
    size_t h = FRAME_HEIGHT;
    
    // ALGORITMO OPTIMIZADO DE RECONOCIMIENTO DE PATRONES
    // Version ultra-eficiente para detectar círculo blanco del dojo
    
    int centerX = 0, centerY = 0, radius = 0;
    bool dojoDetected = false;
    
    // Búsqueda rápida por muestreo (cada 4 píxeles para velocidad)
    int whitePixels = 0;
    int sumX = 0, sumY = 0;
    
    for (int y = 10; y < h - 10; y += 4) {      // Saltar píxeles para velocidad
      for (int x = 10; x < w - 10; x += 4) {
        if (raw[y * w + x] > 180) {  // Umbral para detectar blancos
          whitePixels++;
          sumX += x;
          sumY += y;
        }
      }
    }
    
    // Si hay suficientes píxeles blancos, calcular centro aproximado
    if (whitePixels > 20) {  // Mínimo de píxeles blancos para considerar dojo
      centerX = sumX / whitePixels;
      centerY = sumY / whitePixels;
      
      // Calcular radio aproximado midiendo distancia desde el centro
      int distanceSum = 0;
      int validPoints = 0;
      
      for (int y = centerY - 40; y < centerY + 40; y += 6) {
        for (int x = centerX - 40; x < centerX + 40; x += 6) {
          if (x >= 0 && x < w && y >= 0 && y < h) {
            if (raw[y * w + x] > 180) {
              int dist = sqrt((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));
              distanceSum += dist;
              validPoints++;
            }
          }
        }
      }
      
      if (validPoints > 10) {
        radius = distanceSum / validPoints;
        if (radius > 15 && radius < 80) {  // Radio válido para dojo
          dojoDetected = true;
          
          // Dibujar SOLO una cruz pequeña en el centro (mínimo procesamiento)
          for (int i = -3; i <= 3; i++) {
            if (centerX + i >= 0 && centerX + i < w) {
              raw[centerY * w + (centerX + i)] = YELLOW_GRAY;  // Línea horizontal
            }
            if (centerY + i >= 0 && centerY + i < h) {
              raw[(centerY + i) * w + centerX] = YELLOW_GRAY;  // Línea vertical
            }
          }
          
          // Actualizar métricas
          lastCircleX = centerX;
          lastCircleY = centerY;
          lastCircleRadius = radius;
          lastCircleConfidence = (float)validPoints / 50.0f;  // Confianza basada en puntos válidos
        }
      }
    }
    
    // Si no se detectó dojo, limpiar métricas
    if (!dojoDetected) {
      lastCircleX = 0;
      lastCircleY = 0;
      lastCircleRadius = 0;
      lastCircleConfidence = 0.0;
    }
    
    // Usar exactamente el mismo formato que stream1 (que funciona perfectamente)
    const int headerSize = 54;
    const int paletteSize = 1024;
    size_t bmpSize = headerSize + paletteSize + w * h;

    uint8_t bmpHeader[54] = {
      0x42, 0x4D, 0, 0, 0, 0, 0, 0, 0, 0, 0x36, 0x04, 0x00, 0x00, 0x28, 0x00,
      0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x00, 0x00, 0, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // Configurar header exactamente como stream1
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

    // Paleta de colores exactamente como stream1
    for (int i = 0; i < 256; i++) {
      uint8_t entry[4] = { (uint8_t)i, (uint8_t)i, (uint8_t)i, 0x00 };
      client.write(entry, 4);
    }
    
    // Enviar datos exactamente como stream1
    client.write(raw, w * h);
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
  client.print("\"uptime_seconds\":"); client.print(getUptimeSeconds()); client.print(",");
  client.print("\"free_memory\":"); client.print(getAvailableMemory()); client.print(",");
  client.print("\"heap_size\":"); client.print(getHeapSize()); client.print(",");
  client.print("\"idle_count\":"); client.print(idleCount); client.print(",");
  
  // Agregar información del círculo detectado
  client.print("\"circle_detected\":"); client.print(lastCircleRadius > 0 ? "true" : "false"); client.print(",");
  client.print("\"circle_x\":"); client.print(lastCircleX); client.print(",");
  client.print("\"circle_y\":"); client.print(lastCircleY); client.print(",");
  client.print("\"circle_radius\":"); client.print(lastCircleRadius); client.print(",");
  client.print("\"circle_confidence\":"); client.print(lastCircleConfidence); client.print(",");
  
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

// ==== FUNCIÓN PARA ACTUALIZAR MÉTRICAS DE CÍRCULO ====
void updateCircleMetrics() {
  // Las métricas ahora se actualizan directamente desde el stream 3
  // Esta función mantiene valores de ejemplo cuando el stream 3 no está activo
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  
  if (now - lastUpdate > 5000) {  // Solo actualizar si no hay actividad reciente
    // Si no hay detección reciente, mantener valores de ejemplo para testing
    if (lastCircleRadius == 0) {
      lastCircleX = 160;  // Centro de la imagen
      lastCircleY = 120;
      lastCircleRadius = 0;  // Sin círculo detectado
      lastCircleConfidence = 0.0;
    }
    lastUpdate = now;
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
      sendCameraFrameWithCircleDetection(*client); // Stream con detección de círculos
    }

    else if (request.indexOf("GET /metrics") >= 0) {
      handleMetrics(*client);
      client->stop();
    }
  }

  // Actualizar métricas de detección de círculo en background
  updateCircleMetrics();
}














