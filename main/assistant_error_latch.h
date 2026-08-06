#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "assistant_diagnostics.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    uint32_t shown_at_ms;
    uint32_t minimum_until_ms;
    assistant_error_code_t code;
    assistant_stage_t stage;
    char text[96];
} assistant_error_latch_t;

void assistant_error_latch_reset(assistant_error_latch_t *latch);
void assistant_error_latch_raise(assistant_error_latch_t *latch,
                                 uint32_t now_ms,
                                 uint32_t minimum_duration_ms,
                                 assistant_error_code_t code,
                                 assistant_stage_t stage,
                                 const char *text);
bool assistant_error_latch_blocks_status(const assistant_error_latch_t *latch,
                                         uint32_t now_ms);
void assistant_error_latch_clear_for_retry(assistant_error_latch_t *latch);
void assistant_error_latch_clear_for_exit(assistant_error_latch_t *latch);
esp_err_t assistant_error_latch_self_test_run(void);

#ifdef __cplusplus
}
#endif
