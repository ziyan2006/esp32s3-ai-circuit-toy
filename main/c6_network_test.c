#include "c6_network_test.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "c6_network_test";

#define C6_WIFI_CONNECTED BIT0
#define C6_WIFI_FAILED    BIT1
#define C6_WIFI_MAX_RETRY 8
#define C6_WIFI_SCAN_MAX  20

static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_sta_netif;
static uint8_t s_retry_count;
static volatile bool s_network_connected;

static bool c6_credentials_configured(void)
{
    return CONFIG_TUCO_C6_WIFI_SSID[0] != '\0';
}

static esp_err_t c6_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

static void c6_wifi_event_handler(void *arg,
                                  esp_event_base_t event_base,
                                  int32_t event_id,
                                  void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "C6 station interface started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        const unsigned reason = event == NULL ? 0U : (unsigned)event->reason;
        s_network_connected = false;
        if (s_retry_count < C6_WIFI_MAX_RETRY) {
            ++s_retry_count;
            ESP_LOGW(TAG, "C6 Wi-Fi disconnected (reason=%u); retry %u/%u",
                     reason, s_retry_count, C6_WIFI_MAX_RETRY);
            (void)esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "C6 Wi-Fi connection failed after %u retries; last reason=%u",
                     C6_WIFI_MAX_RETRY, reason);
            xEventGroupSetBits(s_wifi_events, C6_WIFI_FAILED);
        }
    }
}

static void c6_ip_event_handler(void *arg,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void *event_data)
{
    (void)arg;
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "C6 Wi-Fi got IP " IPSTR " gateway " IPSTR,
             IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw));
    s_network_connected = true;
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_events, C6_WIFI_FAILED);
    xEventGroupSetBits(s_wifi_events, C6_WIFI_CONNECTED);
}

static esp_err_t c6_wifi_stack_init(void)
{
    ESP_RETURN_ON_ERROR(c6_init_nvs(), TAG, "initialize NVS");

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "initialize remote Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &c6_wifi_event_handler, NULL),
                        TAG, "register Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    &c6_ip_event_handler, NULL),
                        TAG, "register IP events");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start remote Wi-Fi");

    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "C6 Wi-Fi power save disabled for realtime voice latency");
    } else {
        ESP_LOGW(TAG, "C6 Wi-Fi power-save setting unavailable: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "ESP-Hosted Wi-Fi stack started through the on-board ESP32-C6");
    return ESP_OK;
}

static esp_err_t c6_scan_and_log(void)
{
    wifi_scan_config_t scan_config = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_config, true), TAG, "scan Wi-Fi networks");

    uint16_t ap_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "get AP count");
    if (ap_count > C6_WIFI_SCAN_MAX) {
        ap_count = C6_WIFI_SCAN_MAX;
    }

    wifi_ap_record_t records[C6_WIFI_SCAN_MAX] = {0};
    uint16_t record_count = ap_count;
    if (record_count > 0) {
        ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&record_count, records), TAG,
                            "get AP records");
    }
    ESP_LOGI(TAG, "C6 Wi-Fi scan complete: %u access point(s)", record_count);
    for (uint16_t index = 0; index < record_count; ++index) {
        char ssid[sizeof(records[index].ssid) + 1] = {0};
        memcpy(ssid, records[index].ssid, sizeof(records[index].ssid));
        ESP_LOGI(TAG, "  AP[%u] SSID=\"%s\" RSSI=%d channel=%u auth=%u",
                 index, ssid, records[index].rssi, records[index].primary,
                 records[index].authmode);
    }
    return ESP_OK;
}

static void c6_dns_probe(void)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result = NULL;
    const int err = getaddrinfo("example.com", "80", &hints, &result);
    if (err != 0 || result == NULL) {
        ESP_LOGE(TAG, "C6 DNS probe failed: err=%d", err);
        return;
    }

    char address[INET_ADDRSTRLEN] = {0};
    const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address));
    ESP_LOGI(TAG, "C6 DNS probe succeeded: example.com -> %s", address);
    freeaddrinfo(result);
}

static void c6_network_test_task(void *arg)
{
    (void)arg;
    esp_err_t err = c6_wifi_stack_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6/ESP-Hosted initialization failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    err = c6_scan_and_log();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6 Wi-Fi scan failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Continuing with station connection despite scan failure");
    }

    if (!c6_credentials_configured()) {
        ESP_LOGI(TAG, "Scan-only mode: set TUCO_C6_WIFI_SSID/PASSWORD for Internet test");
        vTaskDelete(NULL);
        return;
    }

    wifi_config_t station_config = {0};
    strlcpy((char *)station_config.sta.ssid, CONFIG_TUCO_C6_WIFI_SSID,
            sizeof(station_config.sta.ssid));
    strlcpy((char *)station_config.sta.password, CONFIG_TUCO_C6_WIFI_PASSWORD,
            sizeof(station_config.sta.password));
    ESP_LOGI(TAG, "Connecting C6 to configured SSID \"%s\"", CONFIG_TUCO_C6_WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station_config));
    s_retry_count = 0;
    ESP_ERROR_CHECK(esp_wifi_connect());

    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                                  C6_WIFI_CONNECTED | C6_WIFI_FAILED,
                                                  pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if ((bits & C6_WIFI_CONNECTED) != 0) {
        c6_dns_probe();
    } else {
        ESP_LOGE(TAG, "C6 Internet test timed out before receiving an IP address");
    }

    /* Keep a small service loop alive so powering the hotspot later does not
     * require rebooting the toy. Event callbacks still handle short outages. */
    for (;;) {
        if (!s_network_connected) {
            s_retry_count = 0;
            xEventGroupClearBits(s_wifi_events, C6_WIFI_FAILED | C6_WIFI_CONNECTED);
            const esp_err_t reconnect_err = esp_wifi_connect();
            if (reconnect_err != ESP_OK) {
                ESP_LOGW(TAG, "C6 background reconnect failed to start: %s",
                         esp_err_to_name(reconnect_err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t c6_network_test_start(void)
{
    s_network_connected = false;
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(c6_network_test_task, "c6_wifi_test", 6144, NULL, 4, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

bool c6_network_is_connected(void)
{
    return s_network_connected;
}
