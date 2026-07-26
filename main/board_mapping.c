#include "board_mapping.h"

#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_mapping";

/* Canonical slots follow the cascade/module order in the 16-slot mapping:
 * modules 1..4 use TCA 0x70 and modules 5..8 use TCA 0x71. Each module's
 * upper and lower sockets occupy adjacent software slots. */
static const board_slot_mapping_t s_slots[BOARD_MAPPING_SLOT_COUNT] = {
    {1, 0, 0, 3, 0x70, 0},
    {1, 1, 1, 3, 0x70, 1},
    {2, 0, 0, 2, 0x70, 2},
    {2, 1, 1, 2, 0x70, 3},
    {3, 0, 0, 1, 0x70, 4},
    {3, 1, 1, 1, 0x70, 5},
    {4, 0, 0, 0, 0x70, 6},
    {4, 1, 1, 0, 0x70, 7},
    {5, 0, 2, 0, 0x71, 0},
    {5, 1, 3, 0, 0x71, 1},
    {6, 0, 2, 1, 0x71, 2},
    {6, 1, 3, 1, 0x71, 3},
    {7, 0, 2, 2, 0x71, 4},
    {7, 1, 3, 2, 0x71, 5},
    {8, 0, 2, 3, 0x71, 6},
    {8, 1, 3, 3, 0x71, 7},
};

const board_slot_mapping_t *board_mapping_slot(uint8_t slot)
{
    return slot < BOARD_MAPPING_SLOT_COUNT ? &s_slots[slot] : NULL;
}

uint8_t board_mapping_slot_for_port(uint8_t port)
{
    return port < BOARD_MAPPING_PORT_COUNT ? port / BOARD_MAPPING_PORTS_PER_SLOT : UINT8_MAX;
}

uint8_t board_mapping_local_port(uint8_t port)
{
    return port < BOARD_MAPPING_PORT_COUNT ? port % BOARD_MAPPING_PORTS_PER_SLOT : UINT8_MAX;
}

board_port_side_t board_mapping_port_side(uint8_t slot, uint8_t local_port)
{
    static const board_port_side_t upper_slot_sides[BOARD_MAPPING_PORTS_PER_SLOT] = {
        BOARD_PORT_SIDE_RIGHT,
        BOARD_PORT_SIDE_DOWN,
        BOARD_PORT_SIDE_LEFT,
        BOARD_PORT_SIDE_UP,
    };
    static const board_port_side_t lower_slot_sides[BOARD_MAPPING_PORTS_PER_SLOT] = {
        BOARD_PORT_SIDE_LEFT,
        BOARD_PORT_SIDE_UP,
        BOARD_PORT_SIDE_RIGHT,
        BOARD_PORT_SIDE_DOWN,
    };
    if (slot >= BOARD_MAPPING_SLOT_COUNT || local_port >= BOARD_MAPPING_PORTS_PER_SLOT) {
        return BOARD_PORT_SIDE_UP;
    }
    return s_slots[slot].module_slot == 0U ? upper_slot_sides[local_port] :
                                            lower_slot_sides[local_port];
}

uint8_t board_mapping_ir_rx_raw_bit_to_port(uint8_t raw_bit)
{
    if (raw_bit >= BOARD_MAPPING_PORT_COUNT) return UINT8_MAX;
    const uint8_t group_count = BOARD_MAPPING_PORT_COUNT / 8U;
    return (uint8_t)((group_count - 1U - raw_bit / 8U) * 8U + raw_bit % 8U);
}

uint8_t board_mapping_ws2812_index_for_port(uint8_t port)
{
    return port < BOARD_MAPPING_PORT_COUNT ? port : UINT8_MAX;
}

const char *board_mapping_side_name(board_port_side_t side)
{
    switch (side) {
    case BOARD_PORT_SIDE_UP: return "UP";
    case BOARD_PORT_SIDE_DOWN: return "DOWN";
    case BOARD_PORT_SIDE_LEFT: return "LEFT";
    case BOARD_PORT_SIDE_RIGHT: return "RIGHT";
    default: return "?";
    }
}

esp_err_t board_mapping_validate(void)
{
    bool grid_used[4][4] = {0};
    bool route_used[2][8] = {0};
    bool raw_port_used[BOARD_MAPPING_PORT_COUNT] = {0};

    for (uint8_t slot = 0; slot < BOARD_MAPPING_SLOT_COUNT; ++slot) {
        const board_slot_mapping_t *mapping = &s_slots[slot];
        const uint8_t expected_module = (uint8_t)(slot / 2U) + 1U;
        const uint8_t expected_module_slot = slot % 2U;
        const uint8_t expected_tca = expected_module <= 4U ? 0x70U : 0x71U;
        const uint8_t expected_channel = (uint8_t)(((expected_module - 1U) % 4U) * 2U +
                                                   expected_module_slot);
        ESP_RETURN_ON_FALSE(mapping->module_number == expected_module &&
                                mapping->module_slot == expected_module_slot,
                            ESP_ERR_INVALID_STATE, TAG, "slot %u module order", slot + 1U);
        ESP_RETURN_ON_FALSE(mapping->tca_address == expected_tca &&
                                mapping->tca_channel == expected_channel,
                            ESP_ERR_INVALID_STATE, TAG, "slot %u TCA route", slot + 1U);
        ESP_RETURN_ON_FALSE(mapping->module_number >= 1U && mapping->module_number <= 8U,
                            ESP_ERR_INVALID_STATE, TAG, "slot %u module", slot + 1U);
        ESP_RETURN_ON_FALSE(mapping->module_slot <= 1U && mapping->grid_row < 4U &&
                                mapping->grid_column < 4U && mapping->tca_channel < 8U,
                            ESP_ERR_INVALID_STATE, TAG, "slot %u bounds", slot + 1U);
        ESP_RETURN_ON_FALSE(!grid_used[mapping->grid_row][mapping->grid_column],
                            ESP_ERR_INVALID_STATE, TAG, "slot %u duplicate grid", slot + 1U);
        grid_used[mapping->grid_row][mapping->grid_column] = true;

        const uint8_t tca = mapping->tca_address == 0x70U ? 0U :
                            (mapping->tca_address == 0x71U ? 1U : UINT8_MAX);
        ESP_RETURN_ON_FALSE(tca != UINT8_MAX && !route_used[tca][mapping->tca_channel],
                            ESP_ERR_INVALID_STATE, TAG, "slot %u duplicate TCA route", slot + 1U);
        route_used[tca][mapping->tca_channel] = true;

        bool side_used[4] = {0};
        for (uint8_t local = 0; local < BOARD_MAPPING_PORTS_PER_SLOT; ++local) {
            const board_port_side_t side = board_mapping_port_side(slot, local);
            ESP_RETURN_ON_FALSE(side <= BOARD_PORT_SIDE_RIGHT && !side_used[side],
                                ESP_ERR_INVALID_STATE, TAG, "slot %u port sides", slot + 1U);
            side_used[side] = true;
        }
    }

    for (uint8_t raw_bit = 0; raw_bit < BOARD_MAPPING_PORT_COUNT; ++raw_bit) {
        const uint8_t port = board_mapping_ir_rx_raw_bit_to_port(raw_bit);
        ESP_RETURN_ON_FALSE(port < BOARD_MAPPING_PORT_COUNT && !raw_port_used[port],
                            ESP_ERR_INVALID_STATE, TAG, "IR raw bit mapping");
        raw_port_used[port] = true;
    }
    ESP_LOGI(TAG, "16-slot map validated: 4x4 grid, 16 TCA routes, 64 port sides and IR bits unique");
    return ESP_OK;
}
