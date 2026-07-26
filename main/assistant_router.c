#include "assistant_router.h"

#include <stdbool.h>

#include "assistant_mode.h"
#include "esp_log.h"
#include "remote_assistant.h"
#include "tuco_agent.h"

static const char *TAG = "assistant_router";
static bool s_initialized;
static assistant_mode_t s_active_mode = ASSISTANT_MODE_LOCAL;
static uint32_t s_active_request_id;
static bool s_session_active;
static assistant_mode_t s_session_mode = ASSISTANT_MODE_LOCAL;
static uint16_t s_session_level_id;

static void begin_session_in_mode(assistant_mode_t mode, uint16_t level_id)
{
    if (mode == ASSISTANT_MODE_REMOTE) remote_assistant_begin_level_session(level_id);
    else tuco_agent_begin_level_session(level_id);
}

static void end_session_in_mode(assistant_mode_t mode)
{
    if (mode == ASSISTANT_MODE_REMOTE) remote_assistant_end_level_session();
    else tuco_agent_end_level_session();
}

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
    if (s_active_request_id != 0U) {
        if (s_active_mode == ASSISTANT_MODE_REMOTE) remote_assistant_cancel(s_active_request_id);
        else tuco_agent_cancel(s_active_request_id);
        s_active_request_id = 0U;
    }
    if (s_session_active) {
        end_session_in_mode(s_session_mode);
        s_session_mode = assistant_mode_get();
        begin_session_in_mode(s_session_mode, s_session_level_id);
    }
    ESP_LOGI(TAG, "assistant mode changed to %s",
             assistant_mode_get() == ASSISTANT_MODE_REMOTE ? "remote" : "local");
}

void assistant_router_begin_level_session(uint16_t level_id)
{
    if (s_session_active) end_session_in_mode(s_session_mode);
    s_session_mode = assistant_mode_get();
    s_session_level_id = level_id;
    s_session_active = true;
    begin_session_in_mode(s_session_mode, level_id);
}

void assistant_router_end_level_session(void)
{
    if (!s_session_active) return;
    end_session_in_mode(s_session_mode);
    s_session_active = false;
    s_session_level_id = 0U;
}

esp_err_t assistant_router_submit(const char *text, uint16_t level_id, uint32_t *out_request_id)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_active_mode = assistant_mode_get();
    const esp_err_t err = s_active_mode == ASSISTANT_MODE_REMOTE ?
        remote_assistant_submit(text, level_id, out_request_id) :
        tuco_agent_submit(text, level_id, out_request_id);
    if (err == ESP_OK && out_request_id != NULL) s_active_request_id = *out_request_id;
    return err;
}

esp_err_t assistant_router_take_result(uint32_t request_id, char *output, size_t output_size)
{
    const esp_err_t err = s_active_mode == ASSISTANT_MODE_REMOTE ?
        remote_assistant_take_result(request_id, output, output_size) :
        tuco_agent_take_result(request_id, output, output_size);
    if (err != ESP_ERR_NOT_FOUND && request_id == s_active_request_id) s_active_request_id = 0U;
    return err;
}

void assistant_router_cancel(uint32_t request_id)
{
    if (s_active_mode == ASSISTANT_MODE_REMOTE) remote_assistant_cancel(request_id);
    else tuco_agent_cancel(request_id);
    if (request_id == s_active_request_id) s_active_request_id = 0U;
}

esp_err_t assistant_router_self_test_run(void)
{
    const assistant_mode_t mode = assistant_mode_get();
    if (mode != ASSISTANT_MODE_LOCAL && mode != ASSISTANT_MODE_REMOTE) return ESP_FAIL;
    ESP_LOGI(TAG, "assistant router self-test passed");
    return ESP_OK;
}
