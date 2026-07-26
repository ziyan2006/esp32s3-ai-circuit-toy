/*
 * TuCo embeds only the ESP-Claw agent loop. IM/event routing is intentionally
 * excluded; completed replies are consumed directly by tuco_agent.c.
 */
#include "claw_core_internal.h"

void claw_core_publish_out_message_if_requested(const claw_core_request_item_t *request,
                                                const claw_core_response_item_t *response)
{
    (void)request;
    (void)response;
}

esp_err_t claw_core_publish_stage_text(const claw_core_request_t *request, const char *text)
{
    (void)request;
    (void)text;
    return ESP_OK;
}

void claw_core_publish_stage_tool_calls(const claw_core_request_t *request,
                                        const claw_core_llm_response_t *response,
                                        uint32_t iteration)
{
    (void)request;
    (void)response;
    (void)iteration;
}

void claw_core_publish_stage_note_for_round(claw_core_state_t *core,
                                            const claw_core_request_t *request,
                                            uint32_t iteration)
{
    (void)core;
    (void)request;
    (void)iteration;
}
