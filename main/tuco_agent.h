#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t tuco_agent_init(void);
bool tuco_agent_is_configured(void);
bool tuco_agent_is_busy(void);
esp_err_t tuco_agent_context_self_test_run(void);
void tuco_agent_begin_level_session(uint16_t level_id);
void tuco_agent_end_level_session(void);
esp_err_t tuco_agent_submit(const char *text, uint16_t level_id, uint32_t *out_request_id);
esp_err_t tuco_agent_take_result(uint32_t request_id, char *output, size_t output_size);
void tuco_agent_cancel(uint32_t request_id);
esp_err_t tuco_agent_build_circuit_snapshot(uint16_t level_id, char **out_json);
esp_err_t tuco_agent_execute_external_tool(uint32_t request_id, const char *name,
                                           const char *arguments_json, char *output,
                                           size_t output_size);
void tuco_agent_serial_start(void);
