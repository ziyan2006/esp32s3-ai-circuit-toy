#include "assistant_diagnostics.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "assistant_diag";

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == NULL || capacity == 0U) return;
    snprintf(destination, capacity, "%s", source == NULL ? "" : source);
}

void assistant_response_reset(assistant_response_t *response, uint32_t request_id)
{
    if (response == NULL) return;
    memset(response, 0, sizeof(*response));
    response->request_id = request_id;
}

void assistant_response_fail(assistant_response_t *response,
                             assistant_error_code_t code,
                             assistant_stage_t stage,
                             esp_err_t esp_error,
                             int http_status,
                             bool retryable,
                             const char *detail)
{
    if (response == NULL) return;
    response->error_code = code;
    response->stage = stage;
    response->esp_error = esp_error;
    response->http_status = http_status;
    response->retryable = retryable;
    response->text[0] = '\0';
    copy_text(response->detail, sizeof(response->detail), detail);
}

assistant_error_code_t assistant_error_from_backend(const char *backend_code,
                                                    int http_status)
{
    if (backend_code != NULL && strcmp(backend_code, "LLM_TIMEOUT") == 0) {
        return ASSISTANT_ERROR_REQUEST_TIMEOUT;
    }
    if (backend_code != NULL && strcmp(backend_code, "ACTION_INVALID") == 0) {
        return ASSISTANT_ERROR_ACTION_FAILED;
    }
    if (backend_code != NULL &&
        (strcmp(backend_code, "LLM_PROTOCOL_ERROR") == 0 ||
         strcmp(backend_code, "REQUEST_INVALID") == 0 ||
         strcmp(backend_code, "RULE_UNSUPPORTED") == 0)) {
        return ASSISTANT_ERROR_INVALID_PROTOCOL;
    }
    if (http_status == 401 || http_status == 403) {
        return ASSISTANT_ERROR_HTTP_UNAUTHORIZED;
    }
    if (http_status == 429) return ASSISTANT_ERROR_HTTP_RATE_LIMITED;
    if (http_status >= 500) return ASSISTANT_ERROR_HTTP_SERVER;
    return ASSISTANT_ERROR_INVALID_PROTOCOL;
}

const char *assistant_error_display_text(assistant_error_code_t code,
                                         assistant_stage_t stage)
{
    switch (code) {
        case ASSISTANT_ERROR_NONE: return "";
        case ASSISTANT_ERROR_NETWORK_OFFLINE: return "网络未连接";
        case ASSISTANT_ERROR_CONNECT_FAILED: return "后端连接失败";
        case ASSISTANT_ERROR_REQUEST_TIMEOUT:
            return stage == ASSISTANT_STAGE_ASR_RESPONSE ? "语音识别超时" : "助教响应超时";
        case ASSISTANT_ERROR_HTTP_UNAUTHORIZED: return "后端未授权";
        case ASSISTANT_ERROR_HTTP_RATE_LIMITED: return "请求太频繁";
        case ASSISTANT_ERROR_HTTP_SERVER: return "后端服务异常";
        case ASSISTANT_ERROR_EMPTY_RESPONSE: return "后端没有回复";
        case ASSISTANT_ERROR_INVALID_JSON: return "后端格式错误";
        case ASSISTANT_ERROR_INVALID_PROTOCOL: return "后端协议错误";
        case ASSISTANT_ERROR_ACTION_FAILED: return "亮灯执行失败";
        case ASSISTANT_ERROR_ASR_FAILED: return "语音识别失败";
        case ASSISTANT_ERROR_TTS_FAILED: return "语音播放失败";
        default: return "助教链路异常";
    }
}

const char *assistant_stage_name(assistant_stage_t stage)
{
    switch (stage) {
        case ASSISTANT_STAGE_NONE: return "none";
        case ASSISTANT_STAGE_NETWORK: return "network";
        case ASSISTANT_STAGE_ASR_CONNECT: return "asr_connect";
        case ASSISTANT_STAGE_ASR_STREAM: return "asr_stream";
        case ASSISTANT_STAGE_ASR_RESPONSE: return "asr_response";
        case ASSISTANT_STAGE_BACKEND_CONNECT: return "backend_connect";
        case ASSISTANT_STAGE_BACKEND_HTTP: return "backend_http";
        case ASSISTANT_STAGE_BACKEND_PROTOCOL: return "backend_protocol";
        case ASSISTANT_STAGE_ACTION: return "action";
        case ASSISTANT_STAGE_TTS_CONNECT: return "tts_connect";
        case ASSISTANT_STAGE_TTS_STREAM: return "tts_stream";
        case ASSISTANT_STAGE_TTS_PLAYBACK: return "tts_playback";
        default: return "unknown";
    }
}

esp_err_t assistant_diagnostics_self_test_run(void)
{
    assistant_response_t response;
    assistant_response_reset(&response, 42U);
    ESP_RETURN_ON_FALSE(response.request_id == 42U &&
                            response.error_code == ASSISTANT_ERROR_NONE &&
                            response.text[0] == '\0' && response.detail[0] == '\0',
                        ESP_FAIL, TAG, "response reset");
    assistant_response_fail(&response, ASSISTANT_ERROR_REQUEST_TIMEOUT,
                            ASSISTANT_STAGE_BACKEND_HTTP, ESP_ERR_TIMEOUT,
                            504, true, "timeout");
    ESP_RETURN_ON_FALSE(response.request_id == 42U &&
                            response.error_code == ASSISTANT_ERROR_REQUEST_TIMEOUT &&
                            response.stage == ASSISTANT_STAGE_BACKEND_HTTP &&
                            response.esp_error == ESP_ERR_TIMEOUT &&
                            response.http_status == 504 && response.retryable &&
                            strcmp(response.detail, "timeout") == 0,
                        ESP_FAIL, TAG, "response failure");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend(NULL, 401) ==
                            ASSISTANT_ERROR_HTTP_UNAUTHORIZED,
                        ESP_FAIL, TAG, "401 mapping");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend("", 403) ==
                            ASSISTANT_ERROR_HTTP_UNAUTHORIZED,
                        ESP_FAIL, TAG, "403 mapping");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend(NULL, 429) ==
                            ASSISTANT_ERROR_HTTP_RATE_LIMITED,
                        ESP_FAIL, TAG, "429 mapping");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend(NULL, 500) ==
                            ASSISTANT_ERROR_HTTP_SERVER,
                        ESP_FAIL, TAG, "500 mapping");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend("LLM_TIMEOUT", 504) ==
                            ASSISTANT_ERROR_REQUEST_TIMEOUT,
                        ESP_FAIL, TAG, "timeout mapping");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend("LLM_PROTOCOL_ERROR", 502) ==
                            ASSISTANT_ERROR_INVALID_PROTOCOL,
                        ESP_FAIL, TAG, "protocol mapping");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend("ACTION_INVALID", 422) ==
                            ASSISTANT_ERROR_ACTION_FAILED,
                        ESP_FAIL, TAG, "action mapping");
    ESP_RETURN_ON_FALSE(assistant_error_from_backend("UNKNOWN", 418) ==
                            ASSISTANT_ERROR_INVALID_PROTOCOL,
                        ESP_FAIL, TAG, "unknown mapping");
    ESP_RETURN_ON_FALSE(strcmp(assistant_error_display_text(
                                   ASSISTANT_ERROR_REQUEST_TIMEOUT,
                                   ASSISTANT_STAGE_ASR_RESPONSE),
                               "语音识别超时") == 0,
                        ESP_FAIL, TAG, "ASR timeout text");
    ESP_RETURN_ON_FALSE(strcmp(assistant_error_display_text(
                                   ASSISTANT_ERROR_TTS_FAILED,
                                   ASSISTANT_STAGE_TTS_PLAYBACK),
                               "语音播放失败") == 0,
                        ESP_FAIL, TAG, "TTS text");
    ESP_LOGI(TAG, "assistant diagnostics self-test passed");
    return ESP_OK;
}
