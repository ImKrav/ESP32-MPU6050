/**
 * @file wifi_sta.h
 * @brief Configuración WiFi en modo Station (STA) para el ESP32
 *
 * El ESP32 se conecta a una red WiFi existente y obtiene una IP por DHCP.
 * El servidor web será accesible desde esa IP en la red local.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuración de la red WiFi ── */
#define WIFI_STA_SSID           "kr"            /**< Nombre de la red WiFi */
#define WIFI_STA_PASS           "12345678"       /**< Contraseña de la red */
#define WIFI_STA_MAX_RETRY      10               /**< Intentos máximos de conexión */

/**
 * @brief Inicializa el WiFi en modo Station y se conecta a la red configurada
 *
 * Bloquea hasta que se obtiene una IP o se agotan los reintentos.
 *
 * @return ESP_OK si se conectó exitosamente y obtuvo IP
 */
esp_err_t wifi_sta_init(void);

/**
 * @brief Obtiene la IP asignada al ESP32 como string
 *
 * @return Puntero a string con la IP (ej: "192.168.1.105"), o "0.0.0.0" si no conectado
 */
const char* wifi_sta_get_ip(void);

#ifdef __cplusplus
}
#endif
