#include "campaign_progress.h"

#include <stdio.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "campaign_progress";

#define CAMPAIGN_PROGRESS_NAMESPACE "tuco_game"
#define CAMPAIGN_PROGRESS_CATALOG_VERSION_KEY "catalog_ver"
#define CAMPAIGN_PROGRESS_CATALOG_VERSION 2U

static nvs_handle_t s_handle;

static esp_err_t migrate_catalog_progress(void)
{
    uint8_t catalog_version = 0U;
    esp_err_t err = nvs_get_u8(s_handle, CAMPAIGN_PROGRESS_CATALOG_VERSION_KEY,
                               &catalog_version);
    if (err == ESP_OK && catalog_version >= CAMPAIGN_PROGRESS_CATALOG_VERSION) {
        return ESP_OK;
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;

    uint8_t old_full_adder_completed = 0U;
    err = nvs_get_u8(s_handle, "done_504", &old_full_adder_completed);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;

    err = nvs_erase_key(s_handle, "done_503");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    err = nvs_erase_key(s_handle, "done_504");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (old_full_adder_completed == 1U) {
        err = nvs_set_u8(s_handle, "done_503", 1U);
        if (err != ESP_OK) return err;
    }
    err = nvs_set_u8(s_handle, CAMPAIGN_PROGRESS_CATALOG_VERSION_KEY,
                     CAMPAIGN_PROGRESS_CATALOG_VERSION);
    if (err != ESP_OK) return err;
    err = nvs_commit(s_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "migrated campaign catalog to version %u; full adder completed=%u",
                 CAMPAIGN_PROGRESS_CATALOG_VERSION,
                 old_full_adder_completed == 1U ? 1U : 0U);
    }
    return err;
}

static esp_err_t ensure_nvs_initialized(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs recovery; erasing and reinitializing the NVS partition");
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    if (err == ESP_ERR_INVALID_STATE) return ESP_OK;
    return err;
}

static esp_err_t make_level_key(uint16_t level_id, char key[sizeof("done_65535")])
{
    const int written = snprintf(key, sizeof("done_65535"), "done_%u", (unsigned)level_id);
    return written > 0 && (size_t)written < sizeof("done_65535") ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t campaign_progress_init(void)
{
    if (s_handle != 0) return ESP_OK;

    esp_err_t err = ensure_nvs_initialized();
    if (err != ESP_OK) return err;
    err = nvs_open(CAMPAIGN_PROGRESS_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        s_handle = 0;
        ESP_LOGE(TAG, "open persistent campaign namespace failed: %s", esp_err_to_name(err));
        return err;
    }
    err = migrate_catalog_progress();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "migrate persistent campaign catalog failed: %s", esp_err_to_name(err));
        nvs_close(s_handle);
        s_handle = 0;
    }
    return err;
}

esp_err_t campaign_progress_load(const campaign_node_t *nodes,
                                 uint16_t node_count,
                                 bool *completed)
{
    if (nodes == NULL || completed == NULL) return ESP_ERR_INVALID_ARG;
    if (s_handle == 0) return ESP_ERR_INVALID_STATE;

    for (uint16_t index = 0; index < node_count; ++index) {
        completed[index] = false;
        char key[sizeof("done_65535")];
        esp_err_t err = make_level_key(nodes[index].id, key);
        if (err != ESP_OK) return err;
        uint8_t value = 0;
        err = nvs_get_u8(s_handle, key, &value);
        if (err == ESP_OK) {
            completed[index] = value == 1U;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "read level %u progress failed: %s",
                     (unsigned)nodes[index].id, esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t campaign_progress_mark_completed(uint16_t level_id)
{
    if (s_handle == 0) return ESP_ERR_INVALID_STATE;
    char key[sizeof("done_65535")];
    esp_err_t err = make_level_key(level_id, key);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(s_handle, key, 1U);
    if (err == ESP_OK) err = nvs_commit(s_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save level %u progress failed: %s",
                 (unsigned)level_id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t campaign_progress_clear(void)
{
    if (s_handle == 0) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_erase_all(s_handle);
    if (err == ESP_OK) err = nvs_commit(s_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "clear persistent campaign progress failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "persistent campaign progress cleared");
    }
    return err;
}
