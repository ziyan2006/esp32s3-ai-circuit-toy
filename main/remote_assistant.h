#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "assistant_diagnostics.h"
#include "esp_err.h"

esp_err_t remote_assistant_init(void);
bool remote_assistant_is_configured(void);
void remote_assistant_begin_level_session(uint16_t level_id);
void remote_assistant_end_level_session(void);
esp_err_t remote_assistant_submit(const char *text, uint16_t level_id, uint32_t *out_request_id);
esp_err_t remote_assistant_take_response(uint32_t request_id,
                                         assistant_response_t *out_response);
void remote_assistant_cancel(uint32_t request_id);
