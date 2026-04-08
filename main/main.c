/**
 * @file main.c
 * @brief Punto de entrada — Péndulo de Torsión con MPU-6050 y ESP32
 *
 * Flujo de inicialización:
 * 1. Inicializar MPU-6050 (I2C + configuración + calibración de bias)
 * 2. Inicializar WiFi SoftAP (crear red "PenduloTorsion")
 * 3. Iniciar servidor HTTP (interfaz web en http://192.168.4.1)
 *
 * El sensor MPU-6050 debe estar EN REPOSO durante la calibración inicial (~5s).
 *
 * @author Práctica de Física — ESP-IDF v6.0
 * @date 2026
 */

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mpu6050.h"
#include "wifi_sta.h"
#include "web_server.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "║    PÉNDULO DE TORSIÓN — GIROSCOPIO EJE Z         ║");
    ESP_LOGI(TAG, "║    ESP32 WROOM 32D + MPU-6050                    ║");
    ESP_LOGI(TAG, "║    ESP-IDF v6.0                                   ║");
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "║    Ecuación MAS: θ(t) = θ₀·cos(ωₙt+φ)·e^(-γt)  ║");
    ESP_LOGI(TAG, "║    ω(t) = dθ/dt   |   ωₙ = 2π/T = √(κ/I)      ║");
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    /* ═══════════════════════════════════════════════════
     *  1. Inicializar MPU-6050
     *     IMPORTANTE: ¡No mover el sensor durante este paso!
     *     La calibración del bias toma ~5 segundos.
     * ═══════════════════════════════════════════════════ */
    ESP_LOGW(TAG, "⚠ No mover el sensor durante la calibración (~5s)...");
    ESP_LOGI(TAG, "");

    esp_err_t ret = mpu6050_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔═══════════════════════════════════════════════╗");
        ESP_LOGE(TAG, "║  ERROR: No se pudo inicializar el MPU-6050   ║");
        ESP_LOGE(TAG, "║                                               ║");
        ESP_LOGE(TAG, "║  Verificar:                                   ║");
        ESP_LOGE(TAG, "║  • Conexiones SDA (GPIO21) y SCL (GPIO22)    ║");
        ESP_LOGE(TAG, "║  • Alimentación 3.3V al MPU-6050             ║");
        ESP_LOGE(TAG, "║  • Pin AD0 conectado a GND (addr 0x68)       ║");
        ESP_LOGE(TAG, "╚═══════════════════════════════════════════════╝");
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "El sistema continuará sin sensor. La interfaz web");
        ESP_LOGE(TAG, "mostrará errores en las lecturas.");
        ESP_LOGE(TAG, "");
    }

    /* Lectura de prueba */
    if (ret == ESP_OK) {
        mpu6050_data_t test_data = {0};
        if (mpu6050_read_gyro_z(&test_data) == ESP_OK) {
            ESP_LOGI(TAG, "Lectura de prueba (con bias compensado):");
            ESP_LOGI(TAG, "  ω(t) = %.4f °/s (raw: %d, bias: %.4f °/s)",
                     test_data.gyro_z_dps, test_data.gyro_z_raw, test_data.bias_z_dps);
            ESP_LOGI(TAG, "  θ(t) = %.4f °", test_data.angle_z_deg);
        }

        float temp = 0;
        if (mpu6050_read_temperature(&temp) == ESP_OK) {
            ESP_LOGI(TAG, "  Temp  = %.1f °C", temp);
        }
    }

    /* ═══════════════════════════════════════════════════
     *  2. Conectar a WiFi (modo Station)
     * ═══════════════════════════════════════════════════ */
    esp_err_t wifi_ret = wifi_sta_init();
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo conectar al WiFi. Reiniciando en 5s...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    /* ═══════════════════════════════════════════════════
     *  3. Iniciar Servidor Web
     * ═══════════════════════════════════════════════════ */
    ESP_ERROR_CHECK(web_server_start());

    /* ═══════════════════════════════════════════════════
     *  Sistema listo
     * ═══════════════════════════════════════════════════ */
    const char *ip = wifi_sta_get_ip();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "║   ✓ Sistema listo                                ║");
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "║   Red WiFi: %s", WIFI_STA_SSID);
    ESP_LOGI(TAG, "║   IP asignada: %s", ip);
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "║   Abrir en navegador:");
    ESP_LOGI(TAG, "║   http://%s", ip);
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "║   Datos mostrados:                               ║");
    ESP_LOGI(TAG, "║   • ω(t) velocidad angular [°/s]                ║");
    ESP_LOGI(TAG, "║   • θ(t) ángulo de rotación [°] (integración)   ║");
    ESP_LOGI(TAG, "║   • Parámetros MAS: T, f, ωₙ                    ║");
    ESP_LOGI(TAG, "║                                                   ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, ">> Abre http://%s en cualquier dispositivo de la misma red", ip);

    /* Loop de monitoreo */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        if (ret == ESP_OK) {
            mpu6050_data_t data = {0};
            float temp = 0;
            if (mpu6050_read_gyro_z(&data) == ESP_OK) {
                mpu6050_read_temperature(&temp);
                ESP_LOGI(TAG, "[Monitor] ω=%+7.2f°/s | θ=%+8.2f° | T=%.3fs | f=%.3fHz | Temp=%.1f°C",
                         data.gyro_z_dps, data.angle_z_deg,
                         data.period_s, data.frequency_hz, temp);
            }
        }
    }
}
