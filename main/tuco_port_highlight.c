#include "tuco_port_highlight.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "port_highlight";

#define TUCO_PORT_HIGHLIGHT_DURATION_US (3 * 1000 * 1000LL)
#define TUCO_PORT_HIGHLIGHT_BLINK_US    (500 * 1000LL)

typedef struct {
    bool active;
    uint8_t ports[BOARD_SNAPSHOT_PORTS_PER_SLOT];
    uint8_t port_count;
    uint8_t slot;
    bool slot_highlight;
    int64_t expires_at_us;
} port_highlight_state_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static port_highlight_state_t s_state;

static bool port_is_connected(const board_snapshot_t *snapshot, uint8_t port)
{
    if (snapshot == NULL) return true;
    for (uint8_t index = 0; index < snapshot->link_count; ++index) {
        const board_link_t *link = &snapshot->links[index];
        if (link->first_port == port || link->second_port == port) return true;
    }
    return false;
}

void tuco_port_highlight_init(void)
{
    portENTER_CRITICAL(&s_lock);
    s_state = (port_highlight_state_t){0};
    portEXIT_CRITICAL(&s_lock);
}

void tuco_port_highlight_show(uint8_t output_port, uint8_t input_port)
{
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_lock);
    s_state.active = true;
    s_state.ports[0] = output_port;
    s_state.ports[1] = input_port;
    s_state.port_count = 2U;
    s_state.slot_highlight = false;
    s_state.expires_at_us = now + TUCO_PORT_HIGHLIGHT_DURATION_US;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "show output=%u input=%u duration_ms=3000",
             output_port, input_port);
}

void tuco_port_highlight_show_slot(uint8_t slot)
{
    const int64_t now = esp_timer_get_time();

    if (slot >= BOARD_SNAPSHOT_SLOT_COUNT) return;
    portENTER_CRITICAL(&s_lock);
    s_state.active = true;
    s_state.slot = slot;
    s_state.port_count = BOARD_SNAPSHOT_PORTS_PER_SLOT;
    s_state.slot_highlight = true;
    for (uint8_t local_port = 0; local_port < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local_port) {
        s_state.ports[local_port] = (uint8_t)(slot * BOARD_SNAPSHOT_PORTS_PER_SLOT + local_port);
    }
    s_state.expires_at_us = now + TUCO_PORT_HIGHLIGHT_DURATION_US;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "show slot=%u ports=4 duration_ms=3000", slot);
}

void tuco_port_highlight_clear(void)
{
    bool was_active;

    portENTER_CRITICAL(&s_lock);
    was_active = s_state.active;
    s_state.active = false;
    portEXIT_CRITICAL(&s_lock);
    if (was_active) ESP_LOGI(TAG, "cleared");
}

bool tuco_port_highlight_is_active(void)
{
    bool active;
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_lock);
    if (s_state.active && now >= s_state.expires_at_us) s_state.active = false;
    active = s_state.active;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

bool tuco_port_highlight_get(const board_snapshot_t *snapshot,
                             uint8_t ports[BOARD_SNAPSHOT_PORTS_PER_SLOT],
                             uint8_t *port_count,
                             bool *visible)
{
    port_highlight_state_t state;
    const int64_t now = esp_timer_get_time();

    if (ports == NULL || port_count == NULL || visible == NULL) return false;
    portENTER_CRITICAL(&s_lock);
    if (s_state.active && now >= s_state.expires_at_us) s_state.active = false;
    state = s_state;
    portEXIT_CRITICAL(&s_lock);
    if (!state.active) return false;

    if (state.slot_highlight && (snapshot == NULL || state.slot >= BOARD_SNAPSHOT_SLOT_COUNT ||
                                 snapshot->slots[state.slot].present)) {
        tuco_port_highlight_clear();
        return false;
    }
    for (uint8_t index = 0; index < state.port_count; ++index) {
        if (port_is_connected(snapshot, state.ports[index])) {
            tuco_port_highlight_clear();
            return false;
        }
    }

    memcpy(ports, state.ports, state.port_count);
    *port_count = state.port_count;
    *visible = ((now / TUCO_PORT_HIGHLIGHT_BLINK_US) & 1LL) == 0;
    return true;
}
