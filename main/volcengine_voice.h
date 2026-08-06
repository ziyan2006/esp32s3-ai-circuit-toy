#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    VOICE_ASSISTANT_DISABLED = 0,
    VOICE_ASSISTANT_READY,
    VOICE_ASSISTANT_RECORDING,
    VOICE_ASSISTANT_THINKING,
    VOICE_ASSISTANT_PLAYING,
    VOICE_ASSISTANT_ERROR,
} voice_assistant_state_t;

typedef enum {
    VOICE_GAMEPLAY_PROMPT_INVALID_LINK = 0,
    VOICE_GAMEPLAY_PROMPT_WRONG_BLOCK_COUNT,
    VOICE_GAMEPLAY_PROMPT_INCOMPLETE_CIRCUIT,
    VOICE_GAMEPLAY_PROMPT_CHECK_FAILED,
    VOICE_GAMEPLAY_PROMPT_CHECK_CANCELLED,
    VOICE_GAMEPLAY_PROMPT_CHECK_PASSED,
    VOICE_GAMEPLAY_PROMPT_COUNT,
} voice_gameplay_prompt_t;

typedef struct {
    voice_assistant_state_t state;
    uint32_t generation;
    bool visible;
    bool error;
    char text[96];
} voice_assistant_status_t;

esp_err_t voice_assistant_init(void);

/* Called from the existing input/UI update path. It never waits for Wi-Fi or TLS. */
void voice_assistant_update(bool play_active,
                            bool programmer_owns_input,
                            bool key0_pressed,
                            uint16_t level_id);

/*
 * Queue one low-priority fixed gameplay prompt. It never invokes ASR or an
 * assistant backend, and is dropped while a manual voice interaction is busy.
 */
esp_err_t voice_assistant_request_gameplay_prompt(voice_gameplay_prompt_t prompt);

esp_err_t voice_assistant_gameplay_prompt_self_test_run(void);
esp_err_t voice_assistant_diagnostics_self_test_run(void);

void voice_assistant_get_status(voice_assistant_status_t *status);
