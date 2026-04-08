/**
 * @file wifi_softap.h
 * @brief Configuración WiFi en modo SoftAP para el ESP32
 *
 * El ESP32 crea su propia red WiFi a la que los dispositivos se conectan
 * para acceder al servidor web del péndulo de torsión.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuración del Access Point ── */
#define WIFI_AP_SSID            "PenduloTorsion"    /**< Nombre de la red WiFi */
#define WIFI_AP_PASS            "fisica2026"        /**< Contraseña (min 8 caracteres) */
#define WIFI_AP_CHANNEL         1                   /**< Canal WiFi */
#define WIFI_AP_MAX_CONN        4                   /**< Máx. conexiones simultáneas */

/**
 * @brief Inicializa el WiFi en modo SoftAP
 *
 * Crea una red WiFi con SSID "PenduloTorsion" y contraseña "fisica2026".
 * El servidor será accesible en http://192.168.4.1
 *
 * @return ESP_OK si la inicialización fue exitosa
 */
esp_err_t wifi_softap_init(void);

#ifdef __cplusplus
}
#endif
