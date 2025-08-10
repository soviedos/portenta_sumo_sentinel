// config.example.h - Plantilla de configuración WiFi
// INSTRUCCIONES:
// 1. Copia este archivo como "config.h" en la misma carpeta
// 2. Edita las credenciales WiFi en config.h
// 3. NO subas config.h a GitHub (ya está en .gitignore)

#ifndef CONFIG_H
#define CONFIG_H

// ==== CONFIGURACIÓN DE RED ====
// Cambiar estas credenciales por las de tu red WiFi
const char* WIFI_SSID = "TU_WIFI_SSID";
const char* WIFI_PASSWORD = "TU_WIFI_PASSWORD";

// IP estática para Ethernet (cambiar si es necesario)
#define ETHERNET_IP_1 192
#define ETHERNET_IP_2 168
#define ETHERNET_IP_3 0
#define ETHERNET_IP_4 74

#endif // CONFIG_H
