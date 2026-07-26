#include "key_input.h"

#include <stdint.h>
#include <string.h>

#define SW1_BIT_MASK              (1U << 0)
#define SW2_BIT_MASK              (1U << 1)
#define SW3_BIT_MASK              (1U << 2)
#define SW4_BIT_MASK              (1U << 3)

/* Physical order: left=Key3, center=Key1, right=Key0. */
#define KEY3_BIT_MASK             (1U << 4)
#define KEY1_BIT_MASK             (1U << 6)
#define KEY0_BIT_MASK             (1U << 7)

#define SWITCH_DEBOUNCE_SAMPLES   3U

typedef struct {
    bool initialized;
    bool stable_state;
    bool candidate_state;
    uint8_t matching_samples;
} switch_debouncer_t;

static switch_debouncer_t s_switch_debouncers[4];

esp_err_t key_input_init(void)
{
    memset(s_switch_debouncers, 0, sizeof(s_switch_debouncers));
    return ESP_OK;
}

static bool key_input_debounce_switch(switch_debouncer_t *debouncer, bool raw_state)
{
    if (!debouncer->initialized) {
        debouncer->initialized = true;
        debouncer->stable_state = raw_state;
        debouncer->candidate_state = raw_state;
        debouncer->matching_samples = 1U;
        return raw_state;
    }

    if (raw_state != debouncer->candidate_state) {
        debouncer->candidate_state = raw_state;
        debouncer->matching_samples = 1U;
        return debouncer->stable_state;
    }

    if (debouncer->candidate_state != debouncer->stable_state &&
        ++debouncer->matching_samples >= SWITCH_DEBOUNCE_SAMPLES) {
        debouncer->stable_state = debouncer->candidate_state;
    }

    return debouncer->stable_state;
}

key_input_state_t key_input_update(uint8_t input_byte)
{
    return (key_input_state_t) {
        .key0_pressed = (input_byte & KEY0_BIT_MASK) == 0U,
        .key1_pressed = (input_byte & KEY1_BIT_MASK) == 0U,
        .key3_pressed = (input_byte & KEY3_BIT_MASK) == 0U,
        .sw1_on = key_input_debounce_switch(&s_switch_debouncers[0],
                                             (input_byte & SW1_BIT_MASK) == 0U),
        .sw2_on = key_input_debounce_switch(&s_switch_debouncers[1],
                                             (input_byte & SW2_BIT_MASK) == 0U),
        .sw3_on = key_input_debounce_switch(&s_switch_debouncers[2],
                                             (input_byte & SW3_BIT_MASK) == 0U),
        .sw4_on = key_input_debounce_switch(&s_switch_debouncers[3],
                                             (input_byte & SW4_BIT_MASK) == 0U),
    };
}
