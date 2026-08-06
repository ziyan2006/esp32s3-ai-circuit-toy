#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ASSISTANT_ERROR_NONE = 0,
    ASSISTANT_ERROR_NETWORK_OFFLINE,
    ASSISTANT_ERROR_CONNECT_FAILED,
    ASSISTANT_ERROR_REQUEST_TIMEOUT,
    ASSISTANT_ERROR_HTTP_UNAUTHORIZED,
    ASSISTANT_ERROR_HTTP_RATE_LIMITED,
    ASSISTANT_ERROR_HTTP_SERVER,
    ASSISTANT_ERROR_EMPTY_RESPONSE,
    ASSISTANT_ERROR_INVALID_JSON,
    ASSISTANT_ERROR_INVALID_PROTOCOL,
    ASSISTANT_ERROR_ACTION_FAILED,
    ASSISTANT_ERROR_ASR_FAILED,
    ASSISTANT_ERROR_TTS_FAILED,
} assistant_error_code_t;

typedef enum {
    ASSISTANT_STAGE_NONE = 0,
    ASSISTANT_STAGE_NETWORK,
    ASSISTANT_STAGE_ASR_CONNECT,
    ASSISTANT_STAGE_ASR_STREAM,
    ASSISTANT_STAGE_ASR_RESPONSE,
    ASSISTANT_STAGE_BACKEND_CONNECT,
    ASSISTANT_STAGE_BACKEND_HTTP,
    ASSISTANT_STAGE_BACKEND_PROTOCOL,
    ASSISTANT_STAGE_ACTION,
    ASSISTANT_STAGE_TTS_CONNECT,
    ASSISTANT_STAGE_TTS_STREAM,
    ASSISTANT_STAGE_TTS_PLAYBACK,
} assistant_stage_t;

typedef struct {
    assistant_error_code_t error_code;
    assistant_stage_t stage;
    esp_err_t esp_error;
    int http_status;
    bool retryable;
    uint32_t request_id;
    uint32_t elapsed_ms;
    char trace_id[48];
    char text[512];
    char detail[96];
} assistant_response_t;

void assistant_response_reset(assistant_response_t *response, uint32_t request_id);
void assistant_response_fail(assistant_response_t *response,
                             assistant_error_code_t code,
                             assistant_stage_t stage,
                             esp_err_t esp_error,
                             int http_status,
                             bool retryable,
                             const char *detail);
assistant_error_code_t assistant_error_from_backend(const char *backend_code,
                                                    int http_status);
const char *assistant_error_display_text(assistant_error_code_t code,
                                         assistant_stage_t stage);
const char *assistant_stage_name(assistant_stage_t stage);
esp_err_t assistant_diagnostics_self_test_run(void);

#ifdef __cplusplus
}
#endif
