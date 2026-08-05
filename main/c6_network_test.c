#include "c6_network_test.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "c6_network";

#define C6_WIFI_CONNECTED BIT0
#define C6_WIFI_FAILED    BIT1
#define C6_CONNECT_TIMEOUT_MS 15000U

typedef enum {
    C6_COMMAND_AUTO_CONNECT = 0,
    C6_COMMAND_SCAN,
    C6_COMMAND_CONNECT_PRESET,
    C6_COMMAND_CONNECT_TEMPORARY,
} c6_command_type_t;

typedef struct {
    c6_command_type_t type;
    uint8_t preset_index;
    char ssid[C6_NETWORK_SSID_MAX + 1U];
    char password[C6_NETWORK_PASSWORD_MAX + 1U];
} c6_command_t;

typedef struct {
    const char *ssid;
    const char *password;
} c6_preset_t;

static const c6_preset_t s_presets[] = {
    {CONFIG_TUCO_C6_WIFI_PRESET_1_SSID, CONFIG_TUCO_C6_WIFI_PRESET_1_PASSWORD},
    {CONFIG_TUCO_C6_WIFI_PRESET_2_SSID, CONFIG_TUCO_C6_WIFI_PRESET_2_PASSWORD},
    {CONFIG_TUCO_C6_WIFI_PRESET_3_SSID, CONFIG_TUCO_C6_WIFI_PRESET_3_PASSWORD},
};

static EventGroupHandle_t s_wifi_events;
static QueueHandle_t s_command_queue;
static esp_netif_t *s_wifi_netif;
static c6_network_status_t s_status;
static c6_network_scan_result_t s_scan_results[C6_NETWORK_SCAN_MAX];
static portMUX_TYPE s_network_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_disconnect_requested;

static void c6_status_set(c6_network_state_t state, bool connected, bool manual, const char *ssid)
{
    portENTER_CRITICAL(&s_network_lock);
    s_status.state = state;
    s_status.connected = connected;
    s_status.manual_connection = manual;
    strlcpy(s_status.current_ssid, ssid == NULL ? "" : ssid, sizeof(s_status.current_ssid));
    portEXIT_CRITICAL(&s_network_lock);
}

static void c6_status_mark_disconnected(void)
{
    portENTER_CRITICAL(&s_network_lock);
    s_status.state = C6_NETWORK_FAILED;
    s_status.connected = false;
    portEXIT_CRITICAL(&s_network_lock);
}

static esp_err_t c6_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static void c6_wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_disconnect_requested) {
            s_disconnect_requested = false;
            return;
        }
        c6_status_mark_disconnected();
        xEventGroupSetBits(s_wifi_events, C6_WIFI_FAILED);
    }
}

static void c6_ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (s_wifi_netif != NULL) {
            esp_netif_dns_info_t primary_dns = {0};
            esp_netif_dns_info_t backup_dns = {0};
            esp_netif_dns_info_t fallback_dns = {
                .ip = ESP_IP4ADDR_INIT(223, 5, 5, 5),
            };
            esp_netif_dns_info_t secondary_fallback_dns = {
                .ip = ESP_IP4ADDR_INIT(119, 29, 29, 29),
            };
            const esp_err_t primary_err = esp_netif_get_dns_info(
                s_wifi_netif, ESP_NETIF_DNS_MAIN, &primary_dns);
            const esp_err_t backup_err = esp_netif_get_dns_info(
                s_wifi_netif, ESP_NETIF_DNS_BACKUP, &backup_dns);
            const esp_err_t fallback_err = esp_netif_set_dns_info(
                s_wifi_netif, ESP_NETIF_DNS_FALLBACK, &fallback_dns);
            const esp_err_t secondary_fallback_err = esp_netif_set_dns_info(
                s_wifi_netif, ESP_NETIF_DNS_BACKUP, &secondary_fallback_dns);
            ESP_LOGI(TAG, "Using DHCP DNS result=%s/%s, fallback=%s/%s",
                     esp_err_to_name(primary_err), esp_err_to_name(backup_err),
                     esp_err_to_name(fallback_err), esp_err_to_name(secondary_fallback_err));
        }
        xEventGroupSetBits(s_wifi_events, C6_WIFI_CONNECTED);
    }
}

static esp_err_t c6_wifi_stack_init(void)
{
    ESP_RETURN_ON_ERROR(c6_init_nvs(), TAG, "initialize NVS");
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) return ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "initialize remote Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "set Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    c6_wifi_event_handler, NULL),
                        TAG, "register Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    c6_ip_event_handler, NULL),
                        TAG, "register IP events");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start remote Wi-Fi");
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    return ESP_OK;
}

static esp_err_t c6_scan(void)
{
    wifi_scan_config_t config = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&config, true), TAG, "scan Wi-Fi networks");
    uint16_t count = 0U;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&count), TAG, "get AP count");
    if (count > C6_NETWORK_SCAN_MAX) count = C6_NETWORK_SCAN_MAX;
    wifi_ap_record_t records[C6_NETWORK_SCAN_MAX] = {0};
    uint16_t fetched = count;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&fetched, records), TAG, "get AP records");
    portENTER_CRITICAL(&s_network_lock);
    s_status.scan_count = (uint8_t)fetched;
    for (uint16_t index = 0U; index < fetched; ++index) {
        memcpy(s_scan_results[index].ssid, records[index].ssid, sizeof(records[index].ssid));
        s_scan_results[index].ssid[C6_NETWORK_SSID_MAX] = '\0';
        s_scan_results[index].rssi = records[index].rssi;
        s_scan_results[index].requires_password = records[index].authmode != WIFI_AUTH_OPEN;
    }
    portEXIT_CRITICAL(&s_network_lock);
    return ESP_OK;
}

static esp_err_t c6_connect(const char *ssid, const char *password, bool manual)
{
    if (ssid == NULL || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;
    c6_status_set(C6_NETWORK_CONNECTING, false, manual, ssid);
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password == NULL ? "" : password, sizeof(config.sta.password));
    s_disconnect_requested = true;
    if (esp_wifi_disconnect() != ESP_OK) s_disconnect_requested = false;
    xEventGroupClearBits(s_wifi_events, C6_WIFI_CONNECTED | C6_WIFI_FAILED);
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "set station config");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect station");
    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, C6_WIFI_CONNECTED | C6_WIFI_FAILED,
                                                  pdTRUE, pdFALSE, pdMS_TO_TICKS(C6_CONNECT_TIMEOUT_MS));
    if ((bits & C6_WIFI_CONNECTED) == 0U) {
        c6_status_set(C6_NETWORK_FAILED, false, manual, ssid);
        return ESP_FAIL;
    }
    c6_status_set(C6_NETWORK_CONNECTED, true, manual, ssid);
    ESP_LOGI(TAG, "Wi-Fi connected");
    return ESP_OK;
}

static esp_err_t c6_connect_saved(void)
{
    wifi_config_t config = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &config), TAG,
                        "read saved station config");
    if (config.sta.ssid[0] == '\0') return ESP_ERR_NOT_FOUND;
    ESP_LOGI(TAG, "reconnecting saved Wi-Fi profile: %s", (char *)config.sta.ssid);
    return c6_connect((const char *)config.sta.ssid, (const char *)config.sta.password, true);
}

static esp_err_t c6_auto_connect(void)
{
    if (c6_connect_saved() == ESP_OK) return ESP_OK;
    c6_status_set(C6_NETWORK_SCANNING, false, false, "");
    esp_err_t err = c6_scan();
    if (err != ESP_OK) {
        c6_status_set(C6_NETWORK_FAILED, false, false, "");
        return err;
    }
    c6_network_scan_result_t scan_results[C6_NETWORK_SCAN_MAX];
    const uint8_t scan_count = c6_network_get_scan_results(scan_results, C6_NETWORK_SCAN_MAX);
    int8_t order[3] = {-1, -1, -1};
    int8_t rssi[3] = {-127, -127, -127};
    for (uint8_t result = 0U; result < scan_count; ++result) {
        for (uint8_t preset = 0U; preset < 3U; ++preset) {
            if (s_presets[preset].ssid[0] != '\0' &&
                strcmp(s_presets[preset].ssid, scan_results[result].ssid) == 0) {
                rssi[preset] = scan_results[result].rssi;
            }
        }
    }
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt) {
        int8_t best = -1;
        for (uint8_t preset = 0U; preset < 3U; ++preset) {
            if (rssi[preset] > -127 && (best < 0 || rssi[preset] > rssi[(uint8_t)best])) best = (int8_t)preset;
        }
        if (best < 0) break;
        order[attempt] = best;
        rssi[(uint8_t)best] = -127;
    }
    for (uint8_t attempt = 0U; attempt < 3U && order[attempt] >= 0; ++attempt) {
        const c6_preset_t *preset = &s_presets[(uint8_t)order[attempt]];
        if (c6_connect(preset->ssid, preset->password, false) == ESP_OK) return ESP_OK;
    }
    c6_status_set(C6_NETWORK_FAILED, false, false, "");
    return ESP_FAIL;
}

static void c6_network_task(void *arg)
{
    (void)arg;
    if (c6_wifi_stack_init() != ESP_OK) {
        c6_status_set(C6_NETWORK_FAILED, false, false, "");
        vTaskDelete(NULL);
        return;
    }
    c6_command_t command = {.type = C6_COMMAND_AUTO_CONNECT};
    for (;;) {
        if (xQueueReceive(s_command_queue, &command, pdMS_TO_TICKS(5000)) != pdPASS) {
            c6_network_status_t status;
            c6_network_get_status(&status);
            if (!status.connected && !status.manual_connection) command.type = C6_COMMAND_AUTO_CONNECT;
            else continue;
        }
        if (command.type == C6_COMMAND_SCAN) {
            c6_network_status_t previous;
            c6_network_get_status(&previous);
            c6_status_set(C6_NETWORK_SCANNING, previous.connected,
                          previous.manual_connection, previous.current_ssid);
            (void)c6_scan();
            c6_status_set(previous.connected ? C6_NETWORK_CONNECTED : C6_NETWORK_IDLE,
                          previous.connected, previous.manual_connection, previous.current_ssid);
        } else if (command.type == C6_COMMAND_CONNECT_TEMPORARY) {
            (void)c6_connect(command.ssid, command.password, true);
        } else if (command.type == C6_COMMAND_CONNECT_PRESET && command.preset_index < 3U) {
            (void)c6_connect(s_presets[command.preset_index].ssid,
                             s_presets[command.preset_index].password, false);
        } else if (command.type == C6_COMMAND_AUTO_CONNECT) {
            (void)c6_auto_connect();
        }
    }
}

esp_err_t c6_network_test_start(void)
{
    s_wifi_events = xEventGroupCreate();
    s_command_queue = xQueueCreate(4U, sizeof(c6_command_t));
    if (s_wifi_events == NULL || s_command_queue == NULL) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    return xTaskCreate(c6_network_task, "c6_wifi", 6144, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool c6_network_is_connected(void)
{
    c6_network_status_t status;
    c6_network_get_status(&status);
    return status.connected;
}

void c6_network_get_status(c6_network_status_t *status)
{
    if (status == NULL) return;
    portENTER_CRITICAL(&s_network_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_network_lock);
}

uint8_t c6_network_get_scan_results(c6_network_scan_result_t *results, uint8_t capacity)
{
    portENTER_CRITICAL(&s_network_lock);
    const uint8_t count = s_status.scan_count < capacity ? s_status.scan_count : capacity;
    if (results != NULL && count > 0U) memcpy(results, s_scan_results, count * sizeof(*results));
    portEXIT_CRITICAL(&s_network_lock);
    return count;
}

const char *c6_network_get_preset_ssid(uint8_t preset_index)
{
    return preset_index < 3U ? s_presets[preset_index].ssid : "";
}

static esp_err_t c6_submit(const c6_command_t *command)
{
    if (s_command_queue == NULL) return ESP_ERR_INVALID_STATE;
    return xQueueSend(s_command_queue, command, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t c6_network_request_scan(void)
{
    const c6_command_t command = {.type = C6_COMMAND_SCAN};
    c6_network_status_t previous;
    c6_network_get_status(&previous);
    c6_status_set(C6_NETWORK_SCANNING, previous.connected,
                  previous.manual_connection, previous.current_ssid);
    const esp_err_t err = c6_submit(&command);
    if (err != ESP_OK) c6_status_set(previous.state, previous.connected,
                                     previous.manual_connection, previous.current_ssid);
    return err;
}

esp_err_t c6_network_connect_scan_result(uint8_t index, const char *password)
{
    c6_network_scan_result_t results[C6_NETWORK_SCAN_MAX];
    const uint8_t count = c6_network_get_scan_results(results, C6_NETWORK_SCAN_MAX);
    if (index >= count) return ESP_ERR_INVALID_ARG;
    c6_command_t command = {.type = C6_COMMAND_CONNECT_TEMPORARY};
    strlcpy(command.ssid, results[index].ssid, sizeof(command.ssid));
    strlcpy(command.password, password == NULL ? "" : password, sizeof(command.password));
    return c6_submit(&command);
}

esp_err_t c6_network_connect_preset(uint8_t preset_index)
{
    const c6_command_t command = {.type = C6_COMMAND_CONNECT_PRESET, .preset_index = preset_index};
    return preset_index < 3U ? c6_submit(&command) : ESP_ERR_INVALID_ARG;
}
