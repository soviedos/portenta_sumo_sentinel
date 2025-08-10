# Configuración de WiFi - Portenta H7

## Configuración inicial requerida

Antes de compilar y cargar el código, debes configurar las credenciales WiFi:

### Paso 1: Crear archivo de configuración

1. **Copia el archivo de ejemplo:**
   ```bash
   cp config.example.h config.h
   ```

2. **Edita el archivo config.h** con tus credenciales WiFi:
   ```cpp
   const char* WIFI_SSID = "TU_WIFI_SSID";
   const char* WIFI_PASSWORD = "TU_WIFI_PASSWORD";
   ```

3. **Ajusta la IP Ethernet si es necesario:**
   ```cpp
   #define ETHERNET_IP_1 192
   #define ETHERNET_IP_2 168
   #define ETHERNET_IP_3 0
   #define ETHERNET_IP_4 74
   ```

### Paso 2: Compilar y cargar

1. Abre `portenta_h7.ino` en Arduino IDE
2. Verifica que `config.h` esté en la misma carpeta
3. Compila y carga el código al Portenta H7

## Seguridad

- ✅ `config.h` está en `.gitignore` - NO se subirá a GitHub
- ✅ `config.example.h` SÍ se sube como plantilla
- ✅ Las credenciales permanecen locales y seguras

## Estructura de archivos

```
portenta_h7/
├── portenta_h7.ino     # Código principal
├── config.h            # Credenciales WiFi (NO subir a Git)
├── config.example.h    # Plantilla de configuración
└── README_CONFIG.md    # Este archivo
```

## Troubleshooting

**Error: "config.h: No such file or directory"**
- Solución: Copia `config.example.h` como `config.h` y edita las credenciales

**Error: No se conecta al WiFi**
- Verifica que WIFI_SSID y WIFI_PASSWORD sean correctos
- Asegúrate de que el Portenta esté en rango del router
