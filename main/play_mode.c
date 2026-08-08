#include "play_mode.h"

#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_active;
static uint32_t s_generation;
static uint16_t s_level_id;

void play_mode_set_active(bool active)
{
    portENTER_CRITICAL(&s_lock);
    if (s_active != active) {
        s_active = active;
        ++s_generation;
    }
    portEXIT_CRITICAL(&s_lock);
}

void play_mode_set_level(uint16_t level_id)
{
    portENTER_CRITICAL(&s_lock);
    if (s_level_id != level_id) {
        s_level_id = level_id;
        ++s_generation;
    }
    portEXIT_CRITICAL(&s_lock);
}

bool play_mode_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_lock);
    active = s_active;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

uint32_t play_mode_generation(void)
{
    uint32_t generation;
    portENTER_CRITICAL(&s_lock);
    generation = s_generation;
    portEXIT_CRITICAL(&s_lock);
    return generation;
}

uint16_t play_mode_level_id(void)
{
    uint16_t level_id;
    portENTER_CRITICAL(&s_lock);
    level_id = s_level_id;
    portEXIT_CRITICAL(&s_lock);
    return level_id;
}
