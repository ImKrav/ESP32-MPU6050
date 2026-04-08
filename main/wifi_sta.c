/**
 * @file wifi_sta.c
 * @brief Implementación del WiFi en modo Station (STA) para ESP-IDF v6.0
 *
 * Conecta el ESP32 a una red WiFi existente. Una vez conectado,
 * el servidor web es accesible desde la IP asignada por DHCP.
 */

#include "wifi_sta.h"

#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "WIFI_STA";

/* ── Bits para el event group ── */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_count = 0;
static char s_ip_str[16] = "0.0.0.0";

/**
 * @brief Manejador de eventos WiFi y IP
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Conectando a la red WiFi...");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_STA_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Desconectado. Reintentando... (%d/%d)",
                     s_retry_count, WIFI_STA_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "No se pudo conectar a '%s' tras %d intentos",
                     WIFI_STA_SSID, WIFI_STA_MAX_RETRY);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔═══════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║  CONECTADO A LA RED WiFi                  ║");
        ESP_LOGI(TAG, "╚═══════════════════════════════════════════╝");
        ESP_LOGI(TAG, "  SSID:    %s", WIFI_STA_SSID);
        ESP_LOGI(TAG, "  IP:      %s", s_ip_str);
        ESP_LOGI(TAG, "  URL:     http://%s", s_ip_str);
        ESP_LOGI(TAG, "");

        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_sta_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Inicializando WiFi Station (STA)");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    /* ── 1. Crear event group ── */
    s_wifi_event_group = xEventGroupCreate();

    /* ── 2. Inicializar NVS (requerido por WiFi) ── */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupta, borrando y reinicializando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "  NVS inicializada");

    /* ── 3. Inicializar stack TCP/IP ── */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "  TCP/IP stack inicializado");

    /* ── 4. Crear loop de eventos por defecto ── */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "  Event loop creado");

    /* ── 5. Crear interfaz de red STA ── */
    esp_netif_create_default_wifi_sta();
    ESP_LOGI(TAG, "  Interfaz de red STA creada");

    /* ── 6. Inicializar WiFi ── */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_LOGI(TAG, "  WiFi inicializado");

    /* ── 7. Registrar manejadores de eventos ── */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, &instance_got_ip));

    /* ── 8. Configurar parámetros de la red ── */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_STA_SSID,
            .password = WIFI_STA_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* ── 9. Iniciar WiFi ── */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "  WiFi STA iniciado, conectando a '%s'...", WIFI_STA_SSID);

    /* ── 10. Esperar conexión o fallo ── */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "  Conectado exitosamente a '%s'", WIFI_STA_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "  FALLO: No se pudo conectar a '%s'", WIFI_STA_SSID);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "  Error inesperado en la conexión WiFi");
    return ESP_FAIL;
}

const char* wifi_sta_get_ip(void)
{
    return s_ip_str;
}
