#include "assistant_error_latch.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "assistant_latch";

static bool deadline_pending(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(deadline_ms - now_ms) > 0;
}

void assistant_error_latch_reset(assistant_error_latch_t *latch)
{
    if (latch == NULL) return;
    memset(latch, 0, sizeof(*latch));
}

void assistant_error_latch_raise(assistant_error_latch_t *latch,
                                 uint32_t now_ms,
                                 uint32_t minimum_duration_ms,
                                 assistant_error_code_t code,
                                 assistant_stage_t stage,
                                 const char *text)
{
    if (latch == NULL) return;
    latch->active = true;
    latch->shown_at_ms = now_ms;
    latch->minimum_until_ms = now_ms + minimum_duration_ms;
    latch->code = code;
    latch->stage = stage;
    snprintf(latch->text, sizeof(latch->text), "%s", text == NULL ? "" : text);
}

bool assistant_error_latch_blocks_status(const assistant_error_latch_t *latch,
                                         uint32_t now_ms)
{
    return latch != NULL && latch->active &&
           deadline_pending(now_ms, latch->minimum_until_ms);
}

void assistant_error_latch_clear_for_retry(assistant_error_latch_t *latch)
{
    assistant_error_latch_reset(latch);
}

void assistant_error_latch_clear_for_exit(assistant_error_latch_t *latch)
{
    assistant_error_latch_reset(latch);
}

esp_err_t assistant_error_latch_self_test_run(void)
{
    assistant_error_latch_t latch;
    assistant_error_latch_reset(&latch);
    assistant_error_latch_raise(&latch, 1000U, 6000U,
                                ASSISTANT_ERROR_CONNECT_FAILED,
                                ASSISTANT_STAGE_BACKEND_CONNECT,
                                "后端连接失败");
    ESP_RETURN_ON_FALSE(assistant_error_latch_blocks_status(&latch, 6999U),
                        ESP_FAIL, TAG, "minimum duration");
    ESP_RETURN_ON_FALSE(!assistant_error_latch_blocks_status(&latch, 7000U),
                        ESP_FAIL, TAG, "minimum duration expiry");
    assistant_error_latch_raise(&latch, 3000U, 6000U,
                                ASSISTANT_ERROR_HTTP_SERVER,
                                ASSISTANT_STAGE_BACKEND_HTTP,
                                "后端服务异常");
    ESP_RETURN_ON_FALSE(latch.code == ASSISTANT_ERROR_HTTP_SERVER &&
                            latch.minimum_until_ms == 9000U &&
                            assistant_error_latch_blocks_status(&latch, 8999U),
                        ESP_FAIL, TAG, "new error replaces old error");
    assistant_error_latch_clear_for_retry(&latch);
    ESP_RETURN_ON_FALSE(!latch.active, ESP_FAIL, TAG, "retry clear");
    assistant_error_latch_raise(&latch, 10U, 6000U,
                                ASSISTANT_ERROR_ASR_FAILED,
                                ASSISTANT_STAGE_ASR_RESPONSE,
                                "语音识别失败");
    assistant_error_latch_clear_for_exit(&latch);
    ESP_RETURN_ON_FALSE(!latch.active, ESP_FAIL, TAG, "exit clear");
    const uint32_t wrapped_start = UINT32_MAX - 1000U;
    assistant_error_latch_raise(&latch, wrapped_start, 6000U,
                                ASSISTANT_ERROR_REQUEST_TIMEOUT,
                                ASSISTANT_STAGE_BACKEND_HTTP,
                                "助教响应超时");
    ESP_RETURN_ON_FALSE(assistant_error_latch_blocks_status(
                            &latch, wrapped_start + 5000U),
                        ESP_FAIL, TAG, "wrap pending");
    ESP_RETURN_ON_FALSE(!assistant_error_latch_blocks_status(
                            &latch, wrapped_start + 6000U),
                        ESP_FAIL, TAG, "wrap expiry");
    ESP_LOGI(TAG, "assistant error latch self-test passed");
    return ESP_OK;
}
