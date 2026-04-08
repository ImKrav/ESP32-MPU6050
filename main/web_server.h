/**
 * @file web_server.h
 * @brief Servidor HTTP + WebSocket embebido para visualización del péndulo de torsión
 *
 * Sirve una interfaz web con gráfica en tiempo real de la velocidad angular Z
 * y proporciona datos del giroscopio mediante WebSocket a ~50 Hz (push).
 *
 * Arquitectura:
 * - GET  /          → Página web con interfaz gráfica (HTML/CSS/JS embebido)
 * - WS   /ws        → WebSocket para datos en tiempo real (50 Hz push)
 * - POST /reset     → Reinicia el ángulo acumulado y parámetros MAS
 * - POST /calibrate → Recalibra el bias del giroscopio (~5s)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia el servidor HTTP + WebSocket en el puerto 80
 *
 * Registra los handlers y crea una tarea FreeRTOS ("ws_push")
 * que lee el MPU-6050 a 50 Hz y empuja los datos por WebSocket.
 *
 * @return ESP_OK si el servidor inició correctamente
 */
esp_err_t web_server_start(void);

/**
 * @brief Detiene el servidor HTTP + WebSocket y la tarea de push
 *
 * @return ESP_OK si el servidor se detuvo correctamente
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif
