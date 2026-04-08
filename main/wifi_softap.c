/**
 * @file wifi_softap.c
 * @brief Implementación del WiFi SoftAP para ESP-IDF v6.0
 *
 * Configura el ESP32 como Access Point para que los dispositivos
 * se conecten directamente y accedan al servidor web.
 */

#include "wifi_softap.h"

#include <string.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_AP";

/**
 * @brief Manejador de eventos WiFi
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Cliente conectado, MAC: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Cliente desconectado, MAC: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

esp_err_t wifi_softap_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  Inicializando WiFi SoftAP");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    /* ── 1. Inicializar NVS (requerido por WiFi) ── */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupta, borrando y reinicializando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS inicializada");

    /* ── 2. Inicializar stack TCP/IP ── */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "✓ TCP/IP stack inicializado");

    /* ── 3. Crear loop de eventos por defecto ── */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "✓ Event loop creado");

    /* ── 4. Crear interfaz de red AP ── */
    esp_netif_create_default_wifi_ap();
    ESP_LOGI(TAG, "✓ Interfaz de red AP creada");

    /* ── 5. Inicializar WiFi con configuración por defecto ── */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_LOGI(TAG, "✓ WiFi inicializado");

    /* ── 6. Registrar manejador de eventos ── */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL
    ));

    /* ── 7. Configurar parámetros del Access Point ── */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    /* Si la contraseña está vacía, usar AP abierto */
    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    /* ── 8. Iniciar WiFi ── */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "═══════════════════════════════════════════");
    ESP_LOGI(TAG, "  WiFi SoftAP iniciado ✓");
    ESP_LOGI(TAG, "  SSID:      %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "  Password:  %s", WIFI_AP_PASS);
    ESP_LOGI(TAG, "  Canal:     %d", WIFI_AP_CHANNEL);
    ESP_LOGI(TAG, "  IP:        192.168.4.1");
    ESP_LOGI(TAG, "  URL:       http://192.168.4.1");
    ESP_LOGI(TAG, "═══════════════════════════════════════════");

    return ESP_OK;
}
