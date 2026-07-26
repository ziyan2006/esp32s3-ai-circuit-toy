#include "assistant_mode.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define ASSISTANT_MODE_NAMESPACE "assistant"
#define ASSISTANT_MODE_KEY "mode"

static const char *TAG = "assistant_mode";
static nvs_handle_t s_handle;
static assistant_mode_t s_mode = ASSISTANT_MODE_LOCAL;

static assistant_mode_t mode_from_value(uint8_t value)
{
    return value == (uint8_t)ASSISTANT_MODE_REMOTE ? ASSISTANT_MODE_REMOTE : ASSISTANT_MODE_LOCAL;
}

esp_err_t assistant_mode_init(void)
{
    if (s_handle != 0) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    if (err != ESP_OK) return err;

    err = nvs_open(ASSISTANT_MODE_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) return err;

    uint8_t value = (uint8_t)ASSISTANT_MODE_LOCAL;
    err = nvs_get_u8(s_handle, ASSISTANT_MODE_KEY, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_mode = ASSISTANT_MODE_LOCAL;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    s_mode = mode_from_value(value);
    if (value != (uint8_t)s_mode) {
        ESP_LOGW(TAG, "invalid persisted mode=%u; restoring local mode", (unsigned)value);
        err = nvs_set_u8(s_handle, ASSISTANT_MODE_KEY, (uint8_t)s_mode);
        if (err == ESP_OK) err = nvs_commit(s_handle);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

assistant_mode_t assistant_mode_get(void)
{
    return s_mode;
}

esp_err_t assistant_mode_set(assistant_mode_t mode)
{
    if (mode != ASSISTANT_MODE_LOCAL && mode != ASSISTANT_MODE_REMOTE) return ESP_ERR_INVALID_ARG;
    if (s_handle == 0) return ESP_ERR_INVALID_STATE;

    esp_err_t err = nvs_set_u8(s_handle, ASSISTANT_MODE_KEY, (uint8_t)mode);
    if (err != ESP_OK) return err;
    err = nvs_commit(s_handle);
    if (err == ESP_OK) s_mode = mode;
    return err;
}

esp_err_t assistant_mode_self_test_run(void)
{
    if (mode_from_value(0U) != ASSISTANT_MODE_LOCAL ||
        mode_from_value(1U) != ASSISTANT_MODE_REMOTE ||
        mode_from_value(2U) != ASSISTANT_MODE_LOCAL) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "assistant mode self-test passed");
    return ESP_OK;
}
