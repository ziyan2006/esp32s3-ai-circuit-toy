#include "assistant_router.h"

#include "assistant_mode.h"
#include "esp_log.h"

static const char *TAG = "assistant_router";
static bool s_initialized;

esp_err_t assistant_router_init(void)
{
    s_initialized = true;
    ESP_LOGI(TAG, "assistant router ready mode=%s",
             assistant_mode_get() == ASSISTANT_MODE_REMOTE ? "remote" : "local");
    return ESP_OK;
}

void assistant_router_handle_mode_change(void)
{
    if (!s_initialized) return;
    ESP_LOGI(TAG, "assistant mode changed to %s",
             assistant_mode_get() == ASSISTANT_MODE_REMOTE ? "remote" : "local");
}

esp_err_t assistant_router_self_test_run(void)
{
    const assistant_mode_t mode = assistant_mode_get();
    if (mode != ASSISTANT_MODE_LOCAL && mode != ASSISTANT_MODE_REMOTE) return ESP_FAIL;
    ESP_LOGI(TAG, "assistant router self-test passed");
    return ESP_OK;
}
