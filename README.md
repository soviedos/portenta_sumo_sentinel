# Portenta Sumo Sentinel

**Sistema de visión y streaming para competencias de sumo robótico usando Arduino Portenta H7**

Universidad Cenfotec - Escuela de Ingeniería del Software  
Proyecto de Investigación Aplicada 2 - MISIA  
Desarrollador: Sergio Oviedo Seas

## Descripción

Portenta Sumo Sentinel es un sistema embebido basado en Arduino Portenta H7 que proporciona streaming de video en tiempo real y métricas del sistema para el monitoreo de competencias de sumo robótico. El sistema actúa como backend de datos, proporcionando endpoints HTTP que son consumidos por un servidor web externo (Raspberry Pi 4 con nginx) que aloja la interfaz de usuario.

## Características principales

- **Streaming de video en tiempo real** con cámara HiMax HM01B0
- **Conectividad dual** WiFi y Ethernet con failover automático
- **Múltiples endpoints de streaming** (/stream1, /stream2, /stream3)
- **API de métricas** del sistema en tiempo real
- **Backend HTTP** que proporciona datos al servidor web externo
- **Soporte CORS** para integración con servidor nginx en Raspberry Pi

## Hardware requerido

### Hardware requerido:
- **Arduino Portenta H7** (Dual Core ARM Cortex-M7 y M4)
- **Portenta Vision Shield** con cámara HiMax HM01B0
- **Cable Ethernet** (opcional)
- **Antena WiFi** (incluida con Portenta)
- **Raspberry Pi 4** (servidor web externo con nginx)

### Especificaciones técnicas:
- **Resolución de cámara:** 160x120 píxeles
- **Formato de imagen:** Escala de grises (8-bit)
- **Conectividad:** WiFi 802.11b/g/n + Ethernet 10/100
- **Puerto del backend:** 81
- **IP estática Ethernet:** 192.168.0.74
- **Servidor web:** Raspberry Pi 4 con nginx (puerto 80/443)

## Estructura del proyecto

```
portenta_sumo_sentinel/
├── .gitignore
├── LICENSE
├── README.md
└── portenta_h7/
    └── portenta_h7.ino    # Código principal del Arduino
```

## Configuración inicial

### Configuración de red:

Editar las siguientes variables en `portenta_h7.ino`:

```cpp
// Configuración WiFi
const char* ssid = "TU_WIFI_SSID";
const char* password = "TU_WIFI_PASSWORD";

// IP estática para Ethernet
IPAddress localIp(192, 168, 0, 74);
```

### Librerías requeridas:

```cpp
#include "camera.h"
#include "himax.h"
#include <WiFi.h>
#include <SPI.h>
#include <PortentaEthernet.h>
#include <Ethernet.h>
#include <mbed.h>
```

## Instalación y configuración

### 1. Configuración del IDE Arduino:

1. Instalar **Arduino IDE 2.0+** o usar **VS Code con Arduino extension**
2. Agregar el board manager: `https://downloads.arduino.cc/packages/package_index.json`
3. Instalar **Arduino Mbed OS Portenta Boards**
4. Seleccionar board: **Arduino Portenta H7 (M7 core)**

### 2. Instalación de librerías:

Instalar las siguientes librerías desde el Library Manager:
- **Arduino_Portenta_Camera**
- **Arduino_Portenta_Vision**
- **WiFi** (incluida con Mbed)
- **Ethernet** (incluida con Mbed)

### 3. Configuración de hardware:

1. Conectar **Portenta Vision Shield** al Portenta H7
2. Conectar cable **USB-C** para programación
3. Opcional: Conectar cable **Ethernet** para conectividad cableada

### 4. Carga del código:

1. Abrir `portenta_h7/portenta_h7.ino`
2. Configurar SSID y password WiFi en `config.h`
3. Verificar y subir el código al Portenta H7

### 5. Ajuste de parámetros de detección:

El sistema incluye parámetros configurables para optimizar la detección del círculo blanco del dojo:

```cpp
#define ADAPTIVE_THRESHOLD_OFFSET 40  // Diferencia mínima con el promedio local
#define MIN_CIRCLE_AREA 50           // Área mínima del círculo (píxeles)
#define MAX_CIRCLE_AREA 3000         // Área máxima del círculo (píxeles)
#define LOCAL_AREA_MIN 8             // Mínimo de píxeles blancos en área 7x7
#define LOCAL_AREA_MAX 35            // Máximo de píxeles blancos en área 7x7
```

**Guía de ajuste:**
- Si detecta demasiadas luces externas: **Aumentar** `ADAPTIVE_THRESHOLD_OFFSET`
- Si no detecta el círculo completo: **Disminuir** `ADAPTIVE_THRESHOLD_OFFSET`
- Si detecta objetos muy pequeños: **Aumentar** `LOCAL_AREA_MIN`
- Si no detecta círculos grandes: **Aumentar** `LOCAL_AREA_MAX`

## Arquitectura del sistema

### Componentes de la solución:

1. **Arduino Portenta H7** (Este proyecto)
   - Backend HTTP en puerto 81
   - Captura y streaming de video
   - Recolección de métricas del sistema
   - IP: 192.168.0.74:81

2. **Raspberry Pi 4** (Servidor web externo)
   - nginx server en puerto 80/443
   - Hosting de la UI Interface
   - Proxy reverso para endpoints del Portenta
   - Gestión de contenido estático

3. **UI Interface** (Frontend web)
   - Aplicación HTML/CSS/JS
   - Consumo de APIs del Portenta
   - Visualización de streams y métricas
   - Servida desde Raspberry Pi

### Flujo de datos:

```
[Portenta H7] → [Raspberry Pi 4 + nginx] → [Usuario/Navegador]
     ↓                    ↓                        ↓
- Captura video      - Sirve UI Interface    - Visualiza streams
- Genera métricas    - Proxy a Portenta      - Controla filtros
- Endpoints HTTP     - Gestiona CORS         - Monitorea métricas
```

### Conectividad automática:

El sistema implementa un mecanismo de failover automático:

1. **Prioridad 1:** Intenta conexión por **Ethernet**
2. **Prioridad 2:** Si falla Ethernet, conecta por **WiFi**
3. **IP estática:** Ethernet usa 192.168.0.74
4. **IP dinámica:** WiFi obtiene IP por DHCP

### Endpoints disponibles:

#### API de métricas (Backend):
- `http://192.168.0.74:81/metrics` - Datos JSON del sistema

#### Streaming de video (Backend):
- `http://192.168.0.74:81/stream1` - Stream original de la cámara (escala de grises)
- `http://192.168.0.74:81/stream2` - Stream con detección de círculo mejorada (blanco y negro)
- `http://192.168.0.74:81/stream3` - **Stream con detección de círculos y overlay en gris claro**

**Algoritmo de detección de círculos con overlay (compatible con HM01B0):**
El sistema trabaja en escala de grises nativa y incluye:

1. **Detección en escala de grises** - Compatible con HM01B0 HiMax camera
2. **Detecta píxeles blancos** usando algoritmo optimizado
3. **Encuentra contornos** y analiza componentes conectados
4. **Identifica el círculo más grande** basado en:
   - Centroide del componente
   - Radio promedio desde el centro
   - Métrica de circularidad (baja varianza = más circular)
5. **Dibuja overlay en gris claro (220)** sobre la imagen original:
   - Círculo marcado con línea de gris claro
   - Cruz en el centro del círculo
6. **Calcula coordenadas exactas** del centro (x, y) y radio en píxeles

**Información del círculo detectado:**
- Coordenadas del centro: (x, y) en píxeles
- Radio del círculo en píxeles
- Nivel de confianza de la detección (0.0 - 1.0)
- Estado de detección (detectado/no detectado)

**Limitaciones de hardware:**
- La cámara HM01B0 solo soporta `CAMERA_GRAYSCALE` de forma confiable
- El overlay se muestra en gris claro (valor 220) para simular resaltado
- RGB565 no está soportado de forma estable en esta configuración

#### Página principal (Solo para debug):
- `http://192.168.0.74:81/` - Interfaz básica de monitoreo

**Nota:** Los usuarios acceden a la interfaz principal a través del servidor nginx en Raspberry Pi, no directamente al Portenta.

### Formato de métricas:

El endpoint `/metrics` retorna datos en formato JSON incluyendo información del círculo detectado:

```json
{
  "uptime_ms": 123456,
  "free_memory": 65536,
  "idle_count": 12345,
  "circle_detected": true,
  "circle_x": 80,
  "circle_y": 60,
  "circle_radius": 25,
  "circle_confidence": 0.85,
  "network": "wifi",
  "ip": "192.168.0.74",
  "rssi": -45
}
```

**Descripción de campos:**
- `uptime_ms`: Tiempo de funcionamiento en milisegundos
- `free_memory`: Memoria RAM libre en bytes
- `idle_count`: Contador de ciclos inactivos del CPU
- `circle_detected`: Boolean - si se detectó un círculo válido
- `circle_x`, `circle_y`: Coordenadas del centro del círculo detectado
- `circle_radius`: Radio del círculo en píxeles
- `circle_confidence`: Nivel de confianza de la detección (0.0-1.0)
- `network`: Tipo de conexión ("wifi" o "ethernet")
- `ip`: Dirección IP asignada
- `rssi`: Intensidad de señal WiFi en dBm (solo para WiFi)

## Configuración avanzada

### Parámetros de cámara:

```cpp
#define IMAGE_MODE CAMERA_GRAYSCALE
#define FRAME_WIDTH 160
#define FRAME_HEIGHT 120
#define FRAME_SIZE 2
```

### Configuración del servidor:

```cpp
WiFiServer wifiServer(81);    // Puerto WiFi
EthernetServer ethServer(81); // Puerto Ethernet
```

### Memoria y rendimiento:

El sistema incluye monitoreo de memoria y CPU:
- **Función de memoria libre:** Calcula RAM disponible
- **Thread de medición CPU:** Cuenta ciclos inactivos
- **Actualización de métricas:** Cada 5 segundos

## Integración con servidor externo

### Configuración de nginx (Raspberry Pi 4):

El Portenta H7 actúa como backend de datos. Para la integración completa, el servidor nginx debe configurarse como proxy reverso:

```nginx
# Configuración sugerida para nginx
server {
    listen 80;
    server_name tu-dominio.com;
    
    # Servir UI Interface
    location / {
        root /var/www/html/UI_Interface;
        index index.html;
    }
    
    # Proxy para métricas del Portenta
    location /api/metrics {
        proxy_pass http://192.168.0.74:81/metrics;
        proxy_set_header Host $host;
        add_header Access-Control-Allow-Origin *;
    }
    
    # Proxy para streams del Portenta
    location /api/stream1 {
        proxy_pass http://192.168.0.74:81/stream1;
        proxy_set_header Host $host;
    }
    
    location /api/stream2 {
        proxy_pass http://192.168.0.74:81/stream2;
        proxy_set_header Host $host;
    }
    
    location /api/stream3 {
        proxy_pass http://192.168.0.74:81/stream3;
        proxy_set_header Host $host;
    }
}
```

### Ventajas de la arquitectura distribuida:

1. **Separación de responsabilidades:**
   - Portenta: Captura y procesamiento de datos
   - Raspberry Pi: Interfaz web y gestión de usuarios

2. **Escalabilidad:**
   - El Portenta se enfoca en tareas de tiempo real
   - El servidor web maneja múltiples usuarios simultáneos

3. **Mantenimiento:**
   - Actualizaciones de UI sin afectar el Portenta
   - Reinicio independiente de cada componente

4. **Seguridad:**
   - El Portenta no expone directamente la interfaz web
   - nginx gestiona SSL/TLS y autenticación

## Troubleshooting

### Problemas de conexión:

**Error: No se conecta a WiFi**
- Verificar SSID y password
- Comprobar distancia al router
- Revisar configuración de red

**Error: Ethernet no detectado**
- Verificar conexión del cable
- Comprobar que el router soporte la IP estática
- Revisar el hardware Ethernet del Portenta

### Problemas de streaming:

**Error: Imágenes no cargan**
- Verificar que la cámara esté conectada correctamente
- Comprobar que el Vision Shield esté bien conectado
- Revisar la configuración de la cámara

**Error: Métricas no se actualizan**
- Verificar conectividad de red
- Comprobar que el endpoint /metrics responda
- Revisar logs en el monitor serial

### Problemas de rendimiento:

**Error: Sistema lento**
- Reducir resolución de cámara si es necesario
- Optimizar frecuencia de actualización de métricas
- Verificar memoria disponible

## Optimización del algoritmo de detección

### Problemas comunes y soluciones:

### Problemas comunes y soluciones:

**❌ Problema:** Ya no detecta blancos (algoritmo muy estricto)
- **✅ Solución:** Usar `simpleWhiteDetection()` en lugar de `enhanceCircleDetection()`
- **✅ Código:** Cambiar la línea en `sendCameraFrameProcessed()`:
  ```cpp
  // Cambiar de:
  enhanceCircleDetection(processedBuffer, w, h);
  // A:
  simpleWhiteDetection(processedBuffer, w, h);
  ```

**❌ Problema:** Solo detecta la mitad del círculo
- **✅ Solución:** Disminuir `ADAPTIVE_THRESHOLD_OFFSET` de 40 a 25-30
- **✅ Solución:** Aumentar `LOCAL_AREA_MAX` de 35 a 50

**❌ Problema:** Detecta luces externas como círculos
- **✅ Solución:** Aumentar `ADAPTIVE_THRESHOLD_OFFSET` de 40 a 55-60
- **✅ Solución:** Ajustar `LOCAL_AREA_MIN` y `LOCAL_AREA_MAX` para el tamaño específico del círculo del dojo

**❌ Problema:** No detecta el círculo en condiciones de poca luz
- **✅ Solución:** Disminuir `ADAPTIVE_THRESHOLD_OFFSET` gradualmente
- **✅ Solución:** Verificar la iluminación del dojo

**❌ Problema:** Detecta múltiples objetos pequeños
- **✅ Solución:** Aumentar `LOCAL_AREA_MIN` de 8 a 12-15
- **✅ Solución:** Aplicar `morphologicalOpening` más agresivo

### Proceso de calibración:

1. Observar el stream2 (imagen procesada) en tiempo real
2. Ajustar parámetros uno por uno
3. Recompilar y subir el código
4. Evaluar los resultados
5. Repetir hasta obtener detección óptima

## Monitoreo y debugging

### Monitor Serial:

Para habilitar debug, descomentar las líneas `//Serial.print()` en el código:

```cpp
Serial.begin(115200, SERIAL_8N1);
Serial.println("Sistema iniciando...");
```

### Métricas de diagnóstico:

El sistema muestra métricas cada 5 segundos:
- Uptime del sistema
- Memoria libre disponible
- Conteo de ciclos CPU
- Estado de la red

## Licencia

Este proyecto es desarrollado como parte del programa académico de Universidad Cenfotec.

## Contribuidores

- **Sergio Oviedo Seas** - Desarrollador Principal
- **Universidad Cenfotec** - Escuela de Ingeniería del Software
- **MISIA** - Proyecto de Investigación Aplicada 2

---

**© 2025 Universidad Cenfotec | Escuela de Ingeniería del Software**