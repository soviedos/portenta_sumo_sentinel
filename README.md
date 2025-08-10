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
2. Configurar SSID y password WiFi
3. Verificar y subir el código al Portenta H7

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
- `http://192.168.0.74:81/stream1` - Stream principal
- `http://192.168.0.74:81/stream2` - Stream procesado
- `http://192.168.0.74:81/stream3` - Stream de análisis

#### Página principal (Solo para debug):
- `http://192.168.0.74:81/` - Interfaz básica de monitoreo

**Nota:** Los usuarios acceden a la interfaz principal a través del servidor nginx en Raspberry Pi, no directamente al Portenta.

### Formato de métricas:

El endpoint `/metrics` retorna datos en formato JSON:

```json
{
  "uptime_ms": 123456,
  "free_memory": 65536,
  "idle_count": 12345,
  "network": "wifi",
  "ip": "192.168.0.74",
  "rssi": -45
}
```

**Descripción de campos:**
- `uptime_ms`: Tiempo de funcionamiento en milisegundos
- `free_memory`: Memoria RAM libre en bytes
- `idle_count`: Contador de ciclos inactivos del CPU
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