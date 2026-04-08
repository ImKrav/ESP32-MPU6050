/**
 * @file mpu6050.h
 * @brief Driver para el sensor MPU-6050 usando la nueva API I2C de ESP-IDF v6.0
 *
 * Lee la velocidad angular del eje Z del giroscopio para un péndulo de torsión.
 * Utiliza driver/i2c_master.h (nueva API, el driver legacy está deprecado en v6.0).
 *
 * Incluye:
 * - Calibración automática del bias del giroscopio al iniciar
 * - Integración numérica compensada para obtener el ángulo θ(t)
 * - Detección de parámetros de MAS (período, frecuencia natural)
 *
 * Ecuaciones del Movimiento Armónico Simple (MAS) para péndulo de torsión:
 *
 *   θ(t) = θ₀ · cos(ωₙt + φ) · e^(-γt)     (ángulo)
 *   ω(t) = dθ/dt                              (velocidad angular)
 *
 *   Donde:
 *     ωₙ = 2π/T         frecuencia angular natural [rad/s]
 *     T                  período de oscilación [s]
 *     θ₀                 amplitud inicial [°]
 *     γ                  coeficiente de amortiguamiento [1/s]
 *     φ                  fase inicial [rad]
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configuración de hardware ── */
#define MPU6050_I2C_PORT        0           /**< Puerto I2C a usar */
#define MPU6050_I2C_SDA_PIN     21          /**< GPIO para SDA */
#define MPU6050_I2C_SCL_PIN     22          /**< GPIO para SCL */
#define MPU6050_I2C_FREQ_HZ     400000      /**< Frecuencia I2C: 400 kHz (Fast Mode) */
#define MPU6050_ADDR            0x68        /**< Dirección I2C del MPU-6050 (AD0 = GND) */

/* ── Registros del MPU-6050 ── */
#define MPU6050_REG_WHO_AM_I    0x75        /**< Registro de identificación (retorna 0x68) */
#define MPU6050_REG_PWR_MGMT_1  0x6B        /**< Gestión de energía 1 */
#define MPU6050_REG_PWR_MGMT_2  0x6C        /**< Gestión de energía 2 */
#define MPU6050_REG_SMPLRT_DIV  0x19        /**< Divisor de sample rate */
#define MPU6050_REG_CONFIG      0x1A        /**< Configuración general (DLPF) */
#define MPU6050_REG_GYRO_CONFIG 0x1B        /**< Configuración del giroscopio */
#define MPU6050_REG_ACCEL_CONFIG 0x1C       /**< Configuración del acelerómetro */
#define MPU6050_REG_GYRO_XOUT_H 0x43       /**< Giroscopio X - byte alto */
#define MPU6050_REG_GYRO_YOUT_H 0x45       /**< Giroscopio Y - byte alto */
#define MPU6050_REG_GYRO_ZOUT_H 0x47       /**< Giroscopio Z - byte alto */
#define MPU6050_REG_GYRO_ZOUT_L 0x48       /**< Giroscopio Z - byte bajo */
#define MPU6050_REG_TEMP_OUT_H  0x41       /**< Temperatura - byte alto */

/* ── Valores de sensibilidad (LSB por °/s) ── */
#define MPU6050_GYRO_FS_250_LSB     131.0f  /**< ±250 °/s  */
#define MPU6050_GYRO_FS_500_LSB      65.5f  /**< ±500 °/s  */
#define MPU6050_GYRO_FS_1000_LSB     32.8f  /**< ±1000 °/s */
#define MPU6050_GYRO_FS_2000_LSB     16.4f  /**< ±2000 °/s */

/* ── Calibración ── */
#define MPU6050_CALIBRATION_SAMPLES  500     /**< Muestras para calibrar bias (~5s a 100Hz) */
#define MPU6050_NOISE_THRESHOLD      0.15f   /**< Umbral de ruido en °/s (bajo este valor se considera 0) */

/**
 * @brief Rangos de escala completa del giroscopio
 */
typedef enum {
    MPU6050_GYRO_FS_250  = 0x00,    /**< ±250 °/s (máxima sensibilidad) */
    MPU6050_GYRO_FS_500  = 0x08,    /**< ±500 °/s */
    MPU6050_GYRO_FS_1000 = 0x10,    /**< ±1000 °/s */
    MPU6050_GYRO_FS_2000 = 0x18,    /**< ±2000 °/s */
} mpu6050_gyro_fs_t;

/**
 * @brief Estructura con los datos del giroscopio y análisis MAS
 */
typedef struct {
    /* ── Datos directos del sensor ── */
    float gyro_z_dps;       /**< Velocidad angular ω eje Z en °/s (calibrada, sin bias) */
    int16_t gyro_z_raw;     /**< Valor crudo del eje Z (16-bit complemento a 2) */

    /* ── Ángulo por integración numérica ── */
    float angle_z_deg;      /**< Ángulo θ acumulado eje Z por integración [°] */

    /* ── Parámetros del MAS (Movimiento Armónico Simple) ── */
    float period_s;         /**< Período T detectado de la oscilación [s] */
    float frequency_hz;     /**< Frecuencia f = 1/T [Hz] */
    float omega_n;          /**< Frecuencia angular natural ωₙ = 2π/T [rad/s] */
    float amplitude_dps;    /**< Amplitud máx de ω detectada en el último ciclo [°/s] */
    bool  mas_valid;        /**< true si los parámetros del MAS son válidos */
    uint32_t oscillations;  /**< Número de oscilaciones detectadas */

    /* ── Metadatos ── */
    int64_t timestamp_ms;   /**< Timestamp en milisegundos desde boot */
    float bias_z_dps;       /**< Bias calibrado del giroscopio [°/s] */
} mpu6050_data_t;

/**
 * @brief Inicializa el bus I2C y configura el MPU-6050
 *
 * - Configura I2C en modo master con los pines SDA/SCL definidos
 * - Verifica comunicación con WHO_AM_I
 * - Despierta el sensor (limpia bit SLEEP en PWR_MGMT_1)
 * - Configura el giroscopio a ±250 °/s
 * - Configura DLPF para filtrado de ruido
 * - Realiza calibración automática del bias (el sensor DEBE estar en reposo)
 *
 * @return ESP_OK si la inicialización fue exitosa
 */
esp_err_t mpu6050_init(void);

/**
 * @brief Lee el valor actual del giroscopio en el eje Z
 *
 * Retorna velocidad angular ω(t) compensada por bias,
 * ángulo θ(t) por integración numérica, y parámetros del MAS
 * si se han detectado suficientes oscilaciones.
 *
 * @param[out] data Puntero a estructura donde se almacenarán los datos
 * @return ESP_OK si la lectura fue exitosa
 */
esp_err_t mpu6050_read_gyro_z(mpu6050_data_t *data);

/**
 * @brief Reinicia el ángulo acumulado a 0°, los contadores del MAS,
 *        y limpia los buffers de datos
 */
void mpu6050_reset_angle(void);

/**
 * @brief Recalibra manualmente el bias del giroscopio Z
 *
 * Toma MPU6050_CALIBRATION_SAMPLES lecturas con el sensor en REPOSO
 * y actualiza el offset. También reinicia el ángulo y MAS.
 *
 * IMPORTANTE: El sensor DEBE estar completamente quieto durante
 * la calibración (~5 segundos).
 *
 * @return ESP_OK si la calibración fue exitosa
 */
esp_err_t mpu6050_recalibrate(void);

/**
 * @brief Obtiene la temperatura del sensor en °C
 *
 * @param[out] temp_c Puntero donde se guardará la temperatura
 * @return ESP_OK si la lectura fue exitosa
 */
esp_err_t mpu6050_read_temperature(float *temp_c);

#ifdef __cplusplus
}
#endif
