/**
 * @file web_server.h
 * @brief Servidor HTTP embebido para visualización del péndulo de torsión
 *
 * Sirve una interfaz web con gráfica en tiempo real de la velocidad angular Z
 * y proporciona un endpoint JSON para datos del giroscopio.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia el servidor HTTP en el puerto 80
 *
 * Registra los handlers:
 * - GET /      → Página web con interfaz gráfica
 * - GET /data  → JSON con datos del giroscopio en tiempo real
 * - POST /reset → Reinicia el ángulo acumulado a 0°
 *
 * @return ESP_OK si el servidor inició correctamente
 */
esp_err_t web_server_start(void);

/**
 * @brief Detiene el servidor HTTP
 *
 * @return ESP_OK si el servidor se detuvo correctamente
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif
