#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define C6_NETWORK_SCAN_MAX 12U
#define C6_NETWORK_SSID_MAX 32U
#define C6_NETWORK_PASSWORD_MAX 63U

typedef enum {
    C6_NETWORK_IDLE = 0,
    C6_NETWORK_SCANNING,
    C6_NETWORK_CONNECTING,
    C6_NETWORK_CONNECTED,
    C6_NETWORK_FAILED,
} c6_network_state_t;

typedef struct {
    char ssid[C6_NETWORK_SSID_MAX + 1U];
    int8_t rssi;
    bool requires_password;
} c6_network_scan_result_t;

typedef struct {
    c6_network_state_t state;
    bool connected;
    bool manual_connection;
    uint8_t scan_count;
    char current_ssid[C6_NETWORK_SSID_MAX + 1U];
} c6_network_status_t;

esp_err_t c6_network_test_start(void);
bool c6_network_is_connected(void);
void c6_network_get_status(c6_network_status_t *status);
uint8_t c6_network_get_scan_results(c6_network_scan_result_t *results, uint8_t capacity);
const char *c6_network_get_preset_ssid(uint8_t preset_index);
esp_err_t c6_network_request_scan(void);
esp_err_t c6_network_connect_scan_result(uint8_t index, const char *password);
esp_err_t c6_network_connect_preset(uint8_t preset_index);
