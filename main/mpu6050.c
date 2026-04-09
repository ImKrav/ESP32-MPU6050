/**
 * @file mpu6050.c
 * @brief Implementación del driver MPU-6050 para ESP-IDF v6.0
 *
 * Usa la nueva API I2C (driver/i2c_master.h) en lugar del driver legacy.
 *
 * Funcionalidades:
 * 1. Lectura de velocidad angular ω(t) del eje Z con compensación de bias
 * 2. Integración numérica para obtener θ(t) = ∫ω(t)dt
 * 3. Detección de parámetros del MAS (Movimiento Armónico Simple):
 *    - Período T: detectado por cruces por cero de ω(t)
 *    - Frecuencia angular natural ωₙ = 2π/T
 *    - Amplitud máxima de ω en cada ciclo
 *
 * Ecuaciones del péndulo de torsión (MAS amortiguado):
 *
 *   θ(t) = θ₀ · cos(ωₙt + φ) · e^(-γt)
 *   ω(t) = dθ/dt = -θ₀ · [ωₙ·sin(ωₙt+φ) + γ·cos(ωₙt+φ)] · e^(-γt)
 *
 *   T = 2π/ωₙ              (período)
 *   f = 1/T = ωₙ/(2π)      (frecuencia)
 *   ωₙ = √(κ/I)            (frecuencia natural, κ=cte torsión, I=momento inercia)
 *
 * La detección del período se basa en los cruces por cero de ω(t):
 * - Cada cruce por cero ascendente marca medio período
 * - Dos cruces consecutivos en la misma dirección = un período completo
 */

#include "mpu6050.h"

#include <string.h>
#include <math.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MPU6050";

/* ── Handles I2C (nueva API v6.0) ── */
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_mpu_dev = NULL;

/* ── Calibración del bias ── */
static float s_gyro_bias_z = 0.0f;         /**< Offset del giroscopio en °/s medido en reposo */
static bool  s_calibrated = false;

/* ── Estado de integración del ángulo ── */
static float s_angle_z = 0.0f;
static int64_t s_last_read_us = 0;

/* ── Sensibilidad actual ── */
static float s_gyro_lsb_sensitivity = MPU6050_GYRO_FS_250_LSB;

/* ── Detección de parámetros del MAS ── */
static float    s_prev_omega = 0.0f;           /**< ω anterior (para detector de cruces por cero) */
static int64_t  s_last_zero_cross_us = 0;      /**< Timestamp del último cruce por cero ascendente */
static float    s_detected_period_s = 0.0f;     /**< Período detectado T */
static float    s_detected_freq_hz = 0.0f;      /**< Frecuencia f = 1/T */
static float    s_detected_omega_n = 0.0f;      /**< ωₙ = 2π/T */
static float    s_cycle_max_omega = 0.0f;       /**< Máx |ω| en el ciclo actual */
static float    s_detected_amplitude = 0.0f;    /**< Amplitud detectada (max |ω|) */
static bool     s_mas_valid = false;            /**< ¿Se detectó al menos un período? */
static uint32_t s_zero_cross_count = 0;         /**< Conteo de cruces por cero ascendentes */
static uint32_t s_oscillation_count = 0;        /**< Oscilaciones completas detectadas */

/* ══════════════════════════════════════════════════════════════
 *  Funciones auxiliares I2C (nueva API)
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Escribe un byte en un registro del MPU-6050
 */
static esp_err_t mpu6050_write_reg(uint8_t reg_addr, uint8_t value)
{
    uint8_t write_buf[2] = { reg_addr, value };
    return i2c_master_transmit(s_mpu_dev, write_buf, sizeof(write_buf), -1);
}

/**
 * @brief Lee N bytes a partir de un registro del MPU-6050
 */
static esp_err_t mpu6050_read_reg(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_mpu_dev, &reg_addr, 1, data, len, -1);
}

/**
 * @brief Lee un solo byte de un registro
 */
static esp_err_t mpu6050_read_reg_byte(uint8_t reg_addr, uint8_t *value)
{
    return mpu6050_read_reg(reg_addr, value, 1);
}

/**
 * @brief Lee el valor crudo del giroscopio Z (sin calibración)
 */
static esp_err_t mpu6050_read_raw_z(int16_t *raw_z)
{
    uint8_t raw[2] = {0};
    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_GYRO_ZOUT_H, raw, 2);
    if (ret != ESP_OK) return ret;
    *raw_z = (int16_t)((raw[0] << 8) | raw[1]);
    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════
 *  Calibración del Bias del Giroscopio
 *
 *  El giroscopio tiene un offset inherente (bias) que causa
 *  drift en la integración. Se mide promediando N lecturas
 *  con el sensor en REPOSO absoluto.
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Calibra el bias del giroscopio Z
 *
 * Toma MPU6050_CALIBRATION_SAMPLES lecturas y calcula el promedio.
 * EL SENSOR DEBE ESTAR COMPLETAMENTE EN REPOSO durante la calibración.
 *
 * @return ESP_OK si la calibración fue exitosa
 */
static esp_err_t mpu6050_calibrate_bias(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  CALIBRANDO GIROSCOPIO — NO MOVER SENSOR ║");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "  Tomando %d muestras...", MPU6050_CALIBRATION_SAMPLES);

    float sum = 0.0f;
    int valid_samples = 0;

    for (int i = 0; i < MPU6050_CALIBRATION_SAMPLES; i++) {
        int16_t raw_z = 0;
        esp_err_t ret = mpu6050_read_raw_z(&raw_z);
        if (ret == ESP_OK) {
            sum += (float)raw_z / s_gyro_lsb_sensitivity;
            valid_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(10)); /* ~100 Hz */

        /* Progreso cada 100 muestras */
        if ((i + 1) % 100 == 0) {
            ESP_LOGI(TAG, "  Progreso: %d/%d muestras", i + 1, MPU6050_CALIBRATION_SAMPLES);
        }
    }

    if (valid_samples < MPU6050_CALIBRATION_SAMPLES / 2) {
        ESP_LOGE(TAG, "Calibración fallida: solo %d/%d muestras válidas",
                 valid_samples, MPU6050_CALIBRATION_SAMPLES);
        return ESP_FAIL;
    }

    s_gyro_bias_z = sum / (float)valid_samples;
    s_calibrated = true;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  ✓ Calibración completada");
    ESP_LOGI(TAG, "    Bias Z: %.4f °/s (promedio de %d muestras)", s_gyro_bias_z, valid_samples);
    ESP_LOGI(TAG, "    Este offset será restado de cada lectura");
    ESP_LOGI(TAG, "");

    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════
 *  API Pública
 * ══════════════════════════════════════════════════════════════ */

esp_err_t mpu6050_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Inicializando MPU-6050 (I2C nueva API)");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    /* ── 1. Configurar e inicializar el bus I2C Master ── */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = MPU6050_I2C_PORT,
        .sda_io_num = MPU6050_I2C_SDA_PIN,
        .scl_io_num = MPU6050_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al crear bus I2C master: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Bus I2C master creado (SDA=GPIO%d, SCL=GPIO%d, %d Hz)",
             MPU6050_I2C_SDA_PIN, MPU6050_I2C_SCL_PIN, MPU6050_I2C_FREQ_HZ);

    /* ── 2. Agregar el dispositivo MPU-6050 al bus ── */
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_ADDR,
        .scl_speed_hz = MPU6050_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_mpu_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al agregar dispositivo MPU-6050: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Dispositivo MPU-6050 agregado (addr=0x%02X)", MPU6050_ADDR);

    /* ── 3. Verificar comunicación: leer WHO_AM_I ── */
    uint8_t who_am_i = 0;
    ret = mpu6050_read_reg_byte(MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al leer WHO_AM_I: %s", esp_err_to_name(ret));
        return ret;
    }

    if (who_am_i != 0x68) {
        ESP_LOGE(TAG, "WHO_AM_I incorrecto: 0x%02X (esperado 0x68)", who_am_i);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "✓ WHO_AM_I verificado: 0x%02X", who_am_i);

    /* ── 4. Despertar el sensor (limpiar bit SLEEP) ── */
    ret = mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al despertar sensor: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Sensor despierto (SLEEP=0)");

    /* Espera para estabilización */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* ── 5. Configurar Sample Rate Divider ── */
    /* Sample Rate = Gyroscope Output Rate / (1 + SMPLRT_DIV)
     * Con DLPF habilitado, Gyro Output Rate = 1 kHz
     * SMPLRT_DIV = 9 → Sample Rate = 1000 / (1+9) = 100 Hz
     */
    ret = mpu6050_write_reg(MPU6050_REG_SMPLRT_DIV, 9);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al configurar sample rate: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ Sample rate configurado: 100 Hz");

    /* ── 6. Configurar DLPF (Digital Low Pass Filter) ── */
    /* CONFIG register (0x1A):
     * DLPF_CFG = 3 → Gyro BW=42Hz, delay=4.8ms
     * Bueno para péndulo de torsión: filtra ruido de alta frecuencia
     * sin atenuar las oscilaciones del péndulo (típicamente < 5 Hz)
     */
    ret = mpu6050_write_reg(MPU6050_REG_CONFIG, 0x03);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al configurar DLPF: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ DLPF configurado: BW=42 Hz");

    /* ── 7. Configurar escala del giroscopio: ±2000 °/s ── */
    /* Rango máximo para capturar movimientos bruscos sin saturación.
     * - ±2000 °/s → 16.4 LSB/°/s → resolución de ~0.061 °/s por bit
     * - Suficiente para detectar oscilaciones del péndulo
     * - Evita clipping en movimientos rápidos
     */
    ret = mpu6050_write_reg(MPU6050_REG_GYRO_CONFIG, MPU6050_GYRO_FS_2000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al configurar giroscopio: %s", esp_err_to_name(ret));
        return ret;
    }
    s_gyro_lsb_sensitivity = MPU6050_GYRO_FS_2000_LSB;
    ESP_LOGI(TAG, "✓ Giroscopio configurado: ±2000 °/s (sensibilidad: %.1f LSB/°/s)",
             s_gyro_lsb_sensitivity);

    /* ── 8. Calibrar bias del giroscopio ── */
    /* IMPORTANTE: El péndulo debe estar EN REPOSO durante esto */
    ret = mpu6050_calibrate_bias();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Calibración fallida, usando bias=0. El ángulo tendrá drift.");
        s_gyro_bias_z = 0.0f;
    }

    /* ── 9. Inicializar estado de integración y MAS ── */
    s_angle_z = 0.0f;
    s_last_read_us = esp_timer_get_time();
    s_prev_omega = 0.0f;
    s_last_zero_cross_us = 0;
    s_detected_period_s = 0.0f;
    s_detected_freq_hz = 0.0f;
    s_detected_omega_n = 0.0f;
    s_cycle_max_omega = 0.0f;
    s_detected_amplitude = 0.0f;
    s_mas_valid = false;
    s_zero_cross_count = 0;
    s_oscillation_count = 0;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  MPU-6050 inicializado correctamente ✓");
    ESP_LOGI(TAG, "  Bias Z: %.4f °/s", s_gyro_bias_z);
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    return ESP_OK;
}

esp_err_t mpu6050_read_gyro_z(mpu6050_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    /* ── Leer valor crudo del giroscopio Z ── */
    int16_t raw_z = 0;
    ret = mpu6050_read_raw_z(&raw_z);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al leer giroscopio Z: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── Convertir a °/s y RESTAR el bias calibrado ── */
    float gyro_z_raw_dps = (float)raw_z / s_gyro_lsb_sensitivity;
    float gyro_z_dps = gyro_z_raw_dps - s_gyro_bias_z;

    /* Aplicar umbral de ruido (dead zone) */
    if (fabsf(gyro_z_dps) < MPU6050_NOISE_THRESHOLD) {
        gyro_z_dps = 0.0f;
    }

    /* ── Integración numérica: θ(t) = ∫ω(t)dt ──
     *
     * Usamos integración rectangular (Euler) con Δt medido:
     *   θ(n) = θ(n-1) + ω(n) × Δt
     *
     * El bias ya fue restado, lo que minimiza el drift acumulativo.
     */
    int64_t now_us = esp_timer_get_time();
    float dt_s = (float)(now_us - s_last_read_us) / 1000000.0f;

    if (dt_s > 0.0f && dt_s < 1.0f) {
        s_angle_z += gyro_z_dps * dt_s;
    }
    s_last_read_us = now_us;

    /* ══════════════════════════════════════════════════════
     *  Detección de Parámetros del MAS (Movimiento Armónico Simple)
     *
     *  Para un péndulo de torsión:
     *    ω(t) = -θ₀·ωₙ·sin(ωₙt + φ)  (caso sin amortiguamiento)
     *
     *  ω(t) cruza por cero cuando θ(t) alcanza un extremo (máximo/mínimo).
     *  La distancia temporal entre dos cruces por cero ASCENDENTES
     *  consecutivos = T (período completo).
     *
     *  De T obtenemos:
     *    f  = 1/T           [Hz]
     *    ωₙ = 2π·f = 2π/T  [rad/s]
     *
     *  Y de la ecuación del MAS:
     *    κ = I·ωₙ²          (constante de torsión, si I es conocida)
     * ══════════════════════════════════════════════════════ */

    /* Detector de cruce por cero ascendente: ω pasa de negativo a positivo */
    if (s_prev_omega < -MPU6050_NOISE_THRESHOLD && gyro_z_dps >= 0.0f) {

        if (s_last_zero_cross_us > 0) {
            /* Calcular período entre dos cruces ascendentes consecutivos */
            float period = (float)(now_us - s_last_zero_cross_us) / 1000000.0f;

            /* Validar que el período sea razonable para un péndulo (0.5s a 60s) */
            if (period > 0.5f && period < 60.0f) {
                /* Suavizar con promedio exponencial (EMA) para estabilidad */
                if (s_detected_period_s > 0.0f) {
                    s_detected_period_s = 0.7f * s_detected_period_s + 0.3f * period;
                } else {
                    s_detected_period_s = period;
                }

                s_detected_freq_hz = 1.0f / s_detected_period_s;
                /* ωₙ = 2π/T = 2πf */
                s_detected_omega_n = 2.0f * (float)M_PI * s_detected_freq_hz;

                s_detected_amplitude = s_cycle_max_omega;
                s_mas_valid = true;
                s_oscillation_count++;

                ESP_LOGD(TAG, "MAS: T=%.3fs, f=%.3fHz, ωₙ=%.3f rad/s, |ω|max=%.2f°/s, osc #%lu",
                         s_detected_period_s, s_detected_freq_hz,
                         s_detected_omega_n, s_detected_amplitude,
                         (unsigned long)s_oscillation_count);
            }
        }

        s_last_zero_cross_us = now_us;
        s_zero_cross_count++;
        s_cycle_max_omega = 0.0f; /* Reset para el nuevo ciclo */
    }

    /* Rastrear amplitud máxima de |ω| en el ciclo actual */
    float abs_omega = fabsf(gyro_z_dps);
    if (abs_omega > s_cycle_max_omega) {
        s_cycle_max_omega = abs_omega;
    }

    /* Guardar ω para la próxima iteración (detector de cruces) */
    s_prev_omega = gyro_z_dps;

    /* ── Rellenar estructura de salida ── */
    data->gyro_z_raw = raw_z;
    data->gyro_z_dps = gyro_z_dps;
    data->angle_z_deg = s_angle_z;

    data->period_s = s_detected_period_s;
    data->frequency_hz = s_detected_freq_hz;
    data->omega_n = s_detected_omega_n;
    data->amplitude_dps = s_detected_amplitude;
    data->mas_valid = s_mas_valid;
    data->oscillations = s_oscillation_count;

    data->timestamp_ms = now_us / 1000;
    data->bias_z_dps = s_gyro_bias_z;

    return ESP_OK;
}

void mpu6050_reset_angle(void)
{
    s_angle_z = 0.0f;
    s_last_read_us = esp_timer_get_time();

    /* También reiniciar detección del MAS */
    s_prev_omega = 0.0f;
    s_last_zero_cross_us = 0;
    s_detected_period_s = 0.0f;
    s_detected_freq_hz = 0.0f;
    s_detected_omega_n = 0.0f;
    s_cycle_max_omega = 0.0f;
    s_detected_amplitude = 0.0f;
    s_mas_valid = false;
    s_zero_cross_count = 0;
    s_oscillation_count = 0;

    ESP_LOGI(TAG, "Ángulo Z y parámetros MAS reiniciados");
}

esp_err_t mpu6050_recalibrate(void)
{
    ESP_LOGI(TAG, "Recalibración manual solicitada desde la web");

    /* Ejecutar calibración de bias (requiere sensor en reposo) */
    esp_err_t ret = mpu6050_calibrate_bias();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Recalibración fallida: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Reiniciar ángulo y MAS con el nuevo bias */
    mpu6050_reset_angle();

    ESP_LOGI(TAG, "Recalibración completada. Nuevo bias: %.4f °/s", s_gyro_bias_z);
    return ESP_OK;
}

esp_err_t mpu6050_read_temperature(float *temp_c)
{
    if (temp_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[2] = {0};
    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_TEMP_OUT_H, raw, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    int16_t raw_temp = (int16_t)((raw[0] << 8) | raw[1]);

    /* Fórmula del datasheet: Temp °C = (raw / 340.0) + 36.53 */
    *temp_c = ((float)raw_temp / 340.0f) + 36.53f;

    return ESP_OK;
}
