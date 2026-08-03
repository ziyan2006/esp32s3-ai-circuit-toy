#include "block_i2c.h"

#include <string.h>

#include "bsp/esp32_p4_function_ev_board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "ssd1315_oled.h"
#include "audio_self_test.h"
#include "app_ui.h"
#include "board_mapping.h"
#include "board_snapshot.h"
#include "learning_activity.h"
#include "play_mode.h"

#define PROGRAMMER_I2C_PORT       I2C_NUM_0
#define PROGRAMMER_SDA_GPIO       GPIO_NUM_32
#define PROGRAMMER_SCL_GPIO       GPIO_NUM_33
#define BASEBOARD_TCA_RESET_GPIO  GPIO_NUM_47
#define PROGRAMMER_I2C_SPEED_HZ   400000U
#define BASEBOARD_I2C_SPEED_HZ    100000U
#define I2C_TIMEOUT_TICKS         pdMS_TO_TICKS(20)
#define I2C_PROBE_TIMEOUT_TICKS   pdMS_TO_TICKS(5)

#define OLED_ADDR_PRIMARY         0x3CU
#define OLED_ADDR_SECONDARY       0x3DU
#define EEPROM_ADDR_PRIMARY       0x50U
#define EEPROM_ADDR_SECONDARY     0x51U
#define TCA_ADDR_FIRST            0x70U
#define TCA_ADDR_SECOND            0x71U
#define SLOT_COUNT                BOARD_MAPPING_SLOT_COUNT
#define SLOT_MISS_LIMIT           3U
#define SLOT_ID_CONFIRM_SAMPLES   2U
#define TCA_SETTLE_US             1000U
#define BASEBOARD_SLOT_PERIOD_MS  30U
#define BASEBOARD_CONFIRM_REVISIT_MS 8U
#define BASEBOARD_RENDER_RETRY_MIN_MS 200U
#define BASEBOARD_RENDER_RETRY_MAX_MS 800U
#define BASEBOARD_RENDER_ATTEMPTS 2U
#define BASEBOARD_OLED_TASK_PRIORITY 6U
#define PROGRAMMER_SEARCH_PERIOD_MS 25U
#define PROGRAMMER_HEALTH_PERIOD_MS 100U
#define PROGRAMMER_INSERT_SAMPLES  2U
#define PROGRAMMER_REMOVE_SAMPLES  3U
#define PROGRAMMER_RENDER_COALESCE_MS 35U
#define I2C_SLOW_OPERATION_US      50000

typedef enum {
    PROGRAMMER_COMMAND_PREVIOUS,
    PROGRAMMER_COMMAND_NEXT,
    PROGRAMMER_COMMAND_CONFIRM,
} programmer_command_t;

typedef struct {
    bool present;
    bool oled_present;
    bool oled_initialized;
    bool has_rendered;
    bool render_queued;
    uint8_t miss_count;
    uint8_t raw_id;
    ssd1315_gate_t displayed_gate;
    bool id_valid;
    uint8_t candidate_raw_id;
    ssd1315_gate_t candidate_gate;
    bool candidate_id_valid;
    uint8_t candidate_count;
    uint8_t render_failures;
    TickType_t render_retry_after;
} slot_state_t;

typedef struct {
    i2c_master_dev_handle_t oled;
    ssd1315_gate_t gate;
    bool success;
    uint32_t generation;
    TickType_t due;
} programmer_render_request_t;

static const char *TAG = "block_i2c";

static bool baseboard_scan_is_active(void)
{
    return play_mode_is_active() || learning_activity_is_active();
}
static i2c_master_bus_handle_t s_programmer_bus;
static i2c_master_bus_handle_t s_baseboard_bus;
static i2c_master_dev_handle_t s_programmer_oled_3c;
static i2c_master_dev_handle_t s_programmer_oled_3d;
static i2c_master_dev_handle_t s_programmer_eeprom_50;
static i2c_master_dev_handle_t s_programmer_eeprom_51;
static i2c_master_dev_handle_t s_baseboard_tca_70;
static i2c_master_dev_handle_t s_baseboard_tca_71;
static i2c_master_dev_handle_t s_baseboard_oled_3c;
static i2c_master_dev_handle_t s_baseboard_oled_3d;
static i2c_master_dev_handle_t s_baseboard_eeprom_50;
static SemaphoreHandle_t s_baseboard_lock;
static SemaphoreHandle_t s_programmer_bus_lock;
static QueueHandle_t s_programmer_command_queue;
static QueueHandle_t s_programmer_render_queue;
static QueueHandle_t s_baseboard_render_queue;
static slot_state_t s_slots[SLOT_COUNT];
static portMUX_TYPE s_slots_lock = portMUX_INITIALIZER_UNLOCKED;

static volatile bool s_programmer_present;
static uint8_t s_programmer_eeprom_addr;
static ssd1315_gate_t s_programmer_selection = SSD1315_GATE_INPUT;
static bool s_programmer_success;
static TickType_t s_programmer_success_started;
static i2c_master_dev_handle_t s_programmer_active_eeprom;
static i2c_master_dev_handle_t s_programmer_active_oled;
static uint8_t s_programmer_oled_addr;
static uint32_t s_programmer_oled_generation;
static bool s_programmer_was_present;
static uint8_t s_programmer_last_id = 0xFFU;
static uint8_t s_programmer_good_samples;
static uint8_t s_programmer_bad_samples;

static i2c_master_dev_handle_t add_device(i2c_master_bus_handle_t bus, uint8_t address, uint32_t speed_hz)
{
    i2c_master_dev_handle_t device = NULL;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = speed_hz,
        .scl_wait_us = 0,
        .flags.disable_ack_check = false,
    };
    if (i2c_master_bus_add_device(bus, &config, &device) != ESP_OK) {
        return NULL;
    }
    return device;
}

static void log_slow_operation(const char *operation, int64_t started_us)
{
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    if (elapsed_us >= I2C_SLOW_OPERATION_US) {
        ESP_LOGW(TAG, "%s took %lld us", operation, (long long)elapsed_us);
    }
}

static bool tick_is_due(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t baseboard_render_retry_ms(uint8_t failures)
{
    uint32_t delay_ms = BASEBOARD_RENDER_RETRY_MIN_MS;
    for (uint8_t retry = 1U; retry < failures && delay_ms < BASEBOARD_RENDER_RETRY_MAX_MS; ++retry) {
        delay_ms *= 2U;
    }
    return delay_ms > BASEBOARD_RENDER_RETRY_MAX_MS ? BASEBOARD_RENDER_RETRY_MAX_MS : delay_ms;
}

static i2c_master_dev_handle_t programmer_eeprom_handle(uint8_t address)
{
    return address == EEPROM_ADDR_PRIMARY ? s_programmer_eeprom_50 : s_programmer_eeprom_51;
}

static i2c_master_dev_handle_t programmer_oled_handle(uint8_t address)
{
    return address == OLED_ADDR_PRIMARY ? s_programmer_oled_3c : s_programmer_oled_3d;
}

static ssd1315_gate_t programmer_first_available_gate(void)
{
    ssd1315_gate_t candidate = SSD1315_GATE_INPUT;
    for (uint8_t count = 0; count < SSD1315_GATE_NULL; ++count) {
        if (app_ui_gate_is_unlocked(candidate)) return candidate;
        candidate = ssd1315_gate_next(candidate, 1);
    }
    return SSD1315_GATE_INPUT;
}

static ssd1315_gate_t programmer_next_available_gate(ssd1315_gate_t current,
                                                     int direction)
{
    if (!app_ui_gate_is_unlocked(current)) current = programmer_first_available_gate();
    for (uint8_t count = 0; count < SSD1315_GATE_NULL; ++count) {
        current = ssd1315_gate_next(current, direction);
        if (app_ui_gate_is_unlocked(current)) return current;
    }
    return programmer_first_available_gate();
}

static ssd1315_gate_t programmer_gate_for_id(uint8_t id)
{
    ssd1315_gate_t gate;
    if (!ssd1315_gate_from_eeprom_id(id, &gate) || !app_ui_gate_is_unlocked(gate)) {
        return programmer_first_available_gate();
    }
    return gate;
}

static esp_err_t programmer_gate_selection_self_test(void)
{
    ESP_RETURN_ON_FALSE(ssd1315_gate_next(SSD1315_GATE_INPUT, 1) == SSD1315_GATE_OUTPUT,
                        ESP_ERR_INVALID_STATE, TAG, "programmer gate order input/output");
    ESP_RETURN_ON_FALSE(ssd1315_gate_next(SSD1315_GATE_OUTPUT, 1) == SSD1315_GATE_NAND,
                        ESP_ERR_INVALID_STATE, TAG, "programmer gate order NAND");
    ESP_RETURN_ON_FALSE(ssd1315_gate_next(SSD1315_GATE_NAND, 1) == SSD1315_GATE_NOT,
                        ESP_ERR_INVALID_STATE, TAG, "programmer gate order NOT");
    const ssd1315_gate_t first = programmer_first_available_gate();
    const ssd1315_gate_t next = programmer_next_available_gate(first, 1);
    /* The real campaign progress may already unlock gates after a reboot. The
     * self-test must validate the current filtered sequence, not assume a
     * blank save file where INPUT is the only available candidate. */
    ESP_RETURN_ON_FALSE(first == SSD1315_GATE_INPUT && app_ui_gate_is_unlocked(first) &&
                            app_ui_gate_is_unlocked(next),
                        ESP_ERR_INVALID_STATE, TAG, "programmer available gate filtering");
    return ESP_OK;
}

static esp_err_t eeprom_read_id(i2c_master_dev_handle_t device, uint8_t *id)
{
    const uint8_t offset = 0x00;
    if (device == NULL || id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(device, &offset, 1, id, 1, I2C_TIMEOUT_TICKS);
}

static esp_err_t baseboard_read_id(i2c_master_dev_handle_t device, uint8_t *id)
{
    const uint8_t offset = 0x00;
    if (device == NULL || id == NULL) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(device, &offset, 1, id, 1, I2C_PROBE_TIMEOUT_TICKS);
}

static esp_err_t eeprom_write_id(i2c_master_dev_handle_t device, uint8_t id)
{
    const uint8_t request[] = {0x00, id};
    esp_err_t err = i2c_master_transmit(device, request, sizeof(request), I2C_TIMEOUT_TICKS);
    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t retry = 0; retry < 10U; ++retry) {
        vTaskDelay(pdMS_TO_TICKS(2));
        if (i2c_master_probe(s_programmer_bus, s_programmer_eeprom_addr, I2C_PROBE_TIMEOUT_TICKS) == ESP_OK) {
            uint8_t readback = 0;
            err = eeprom_read_id(device, &readback);
            return (err == ESP_OK && readback == id) ? ESP_OK : ESP_FAIL;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static void baseboard_disable_all_channels(void)
{
    const uint8_t disabled = 0x00;
    if (s_baseboard_tca_70 != NULL) {
        (void)i2c_master_transmit(s_baseboard_tca_70, &disabled, 1, I2C_TIMEOUT_TICKS);
    }
    if (s_baseboard_tca_71 != NULL) {
        (void)i2c_master_transmit(s_baseboard_tca_71, &disabled, 1, I2C_TIMEOUT_TICKS);
    }
}

/* Reset the downstream muxes before resetting the shared controller.  This
 * prevents a block holding SDA low from keeping the long baseboard chain
 * attached while the controller performs its recovery sequence. */
static esp_err_t baseboard_recover_bus_locked(void)
{
    esp_err_t err = ESP_OK;
    if (gpio_set_level(BASEBOARD_TCA_RESET_GPIO, 0) != ESP_OK) {
        err = ESP_FAIL;
    }
    esp_rom_delay_us(200);
    const esp_err_t bus_err = i2c_master_bus_reset(s_baseboard_bus);
    if (bus_err != ESP_OK && err == ESP_OK) {
        err = bus_err;
    }
    if (gpio_set_level(BASEBOARD_TCA_RESET_GPIO, 1) != ESP_OK && err == ESP_OK) {
        err = ESP_FAIL;
    }
    esp_rom_delay_us(TCA_SETTLE_US);
    baseboard_disable_all_channels();
    ESP_LOGW(TAG, "baseboard I2C recovery: reset=%s result=%s",
             esp_err_to_name(bus_err), esp_err_to_name(err));
    return err;
}

static esp_err_t baseboard_access_begin(void)
{
    esp_err_t err = esp_lv_adapter_lock(-1);
    if (err != ESP_OK) return err;
    if (xSemaphoreTake(s_baseboard_lock, portMAX_DELAY) != pdTRUE) {
        esp_lv_adapter_unlock();
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void baseboard_access_end(void)
{
    xSemaphoreGive(s_baseboard_lock);
    esp_lv_adapter_unlock();
}

static esp_err_t baseboard_select_slot(uint8_t slot)
{
    const board_slot_mapping_t *mapping = board_mapping_slot(slot);
    i2c_master_dev_handle_t selected = NULL;
    if (mapping == NULL) return ESP_ERR_INVALID_ARG;
    const uint8_t enable = (uint8_t)(1U << mapping->tca_channel);

    if (mapping->tca_address == TCA_ADDR_FIRST) {
        const uint8_t disabled = 0x00;
        if (s_baseboard_tca_71 != NULL) {
            (void)i2c_master_transmit(s_baseboard_tca_71, &disabled, 1, I2C_TIMEOUT_TICKS);
        }
        selected = s_baseboard_tca_70;
    } else if (mapping->tca_address == TCA_ADDR_SECOND) {
        const uint8_t disabled = 0x00;
        if (s_baseboard_tca_70 != NULL) {
            (void)i2c_master_transmit(s_baseboard_tca_70, &disabled, 1, I2C_TIMEOUT_TICKS);
        }
        selected = s_baseboard_tca_71;
    }
    else return ESP_ERR_INVALID_ARG;
    return selected == NULL ? ESP_ERR_INVALID_STATE :
                              i2c_master_transmit(selected, &enable, 1, I2C_TIMEOUT_TICKS);
}

static void baseboard_deselect_slot(uint8_t slot)
{
    (void)slot;
    baseboard_disable_all_channels();
}

static i2c_master_dev_handle_t baseboard_probe_oled(void)
{
    if (i2c_master_probe(s_baseboard_bus, OLED_ADDR_PRIMARY, I2C_PROBE_TIMEOUT_TICKS) == ESP_OK) {
        return s_baseboard_oled_3c;
    }
    if (i2c_master_probe(s_baseboard_bus, OLED_ADDR_SECONDARY, I2C_PROBE_TIMEOUT_TICKS) == ESP_OK) {
        return s_baseboard_oled_3d;
    }
    return NULL;
}

static void baseboard_publish_slots(uint32_t completed_scans)
{
    board_slot_identity_t identities[BOARD_SNAPSHOT_SLOT_COUNT] = {0};
    portENTER_CRITICAL(&s_slots_lock);
    for (uint8_t slot = 0; slot < SLOT_COUNT; ++slot) {
        identities[slot] = (board_slot_identity_t) {
            .present = s_slots[slot].present,
            .id_valid = s_slots[slot].id_valid,
            .raw_id = s_slots[slot].raw_id,
            .gate = s_slots[slot].id_valid ? s_slots[slot].displayed_gate : SSD1315_GATE_NULL,
        };
    }
    portEXIT_CRITICAL(&s_slots_lock);
    board_snapshot_publish_slots(identities, completed_scans);
}

static uint8_t baseboard_reset_transient_slot_state(void)
{
    uint8_t cached_slots = 0;
    portENTER_CRITICAL(&s_slots_lock);
    for (uint8_t slot = 0; slot < SLOT_COUNT; ++slot) {
        if (s_slots[slot].present) ++cached_slots;
        s_slots[slot].miss_count = 0;
        s_slots[slot].candidate_count = 0;
        s_slots[slot].render_queued = false;
        if (!s_slots[slot].has_rendered) {
            s_slots[slot].render_retry_after = 0;
        }
    }
    portEXIT_CRITICAL(&s_slots_lock);
    return cached_slots;
}

static void baseboard_queue_render(uint8_t slot)
{
    bool should_queue = false;
    const TickType_t now = xTaskGetTickCount();
    portENTER_CRITICAL(&s_slots_lock);
    slot_state_t *state = &s_slots[slot];
    if (state->present && !state->has_rendered && !state->render_queued &&
        tick_is_due(now, state->render_retry_after)) {
        state->render_queued = true;
        should_queue = true;
    }
    portEXIT_CRITICAL(&s_slots_lock);

    if (should_queue && xQueueSend(s_baseboard_render_queue, &slot, 0) != pdPASS) {
        portENTER_CRITICAL(&s_slots_lock);
        s_slots[slot].render_queued = false;
        portEXIT_CRITICAL(&s_slots_lock);
    }
}

static bool baseboard_scan_slot(uint8_t slot, bool *needs_confirmation)
{
    const int64_t started_us = esp_timer_get_time();
    if (needs_confirmation != NULL) {
        *needs_confirmation = false;
    }
    uint8_t id = 0xFFU;
    ssd1315_gate_t gate = SSD1315_GATE_NULL;
    esp_err_t err = baseboard_access_begin();
    if (err == ESP_OK) {
        err = baseboard_select_slot(slot);
        if (err == ESP_OK) {
            esp_rom_delay_us(TCA_SETTLE_US);
            /* Empty slots are expected. A direct short EEPROM read avoids the
             * driver's error-level probe log for a normal missing device. */
            err = baseboard_read_id(s_baseboard_eeprom_50, &id);
            baseboard_deselect_slot(slot);
        } else {
            baseboard_disable_all_channels();
        }
        baseboard_access_end();
    }

    const bool id_read_ok = err == ESP_OK;
    const bool id_valid = id_read_ok && ssd1315_gate_from_eeprom_id(id, &gate);
    bool identity_changed = false;
    bool confirmed_identity_seen = false;
    portENTER_CRITICAL(&s_slots_lock);
    slot_state_t *state = &s_slots[slot];
    if (id_read_ok) {
        state->miss_count = 0;
        if (state->present && state->raw_id == id && state->id_valid == id_valid &&
            state->displayed_gate == gate) {
            state->candidate_count = 0;
            confirmed_identity_seen = true;
        } else {
            const bool same_candidate = state->candidate_count > 0U &&
                                        state->candidate_raw_id == id &&
                                        state->candidate_id_valid == id_valid &&
                                        state->candidate_gate == gate;
            if (!same_candidate) {
                state->candidate_raw_id = id;
                state->candidate_id_valid = id_valid;
                state->candidate_gate = gate;
                state->candidate_count = 1U;
            } else if (state->candidate_count < SLOT_ID_CONFIRM_SAMPLES) {
                ++state->candidate_count;
            }

            if (state->candidate_count >= SLOT_ID_CONFIRM_SAMPLES) {
                identity_changed = !state->present || state->raw_id != id ||
                                   state->id_valid != id_valid ||
                                   state->displayed_gate != gate;
                if (!state->present) {
                    state->oled_present = false;
                    state->oled_initialized = false;
                }
                state->present = true;
                state->raw_id = id;
                state->id_valid = id_valid;
                state->displayed_gate = gate;
                state->candidate_count = 0;
                state->has_rendered = false;
                state->render_failures = 0;
                state->render_retry_after = 0;
                confirmed_identity_seen = true;
            }
        }
    } else {
        state->candidate_count = 0;
        /* Keep the removal debounce, but invalidate the OLED display on the
         * first miss so a quick unplug/replug is rendered again. */
        if (state->present && state->miss_count == 0U) {
            state->oled_present = false;
            state->oled_initialized = false;
            state->has_rendered = false;
            state->render_failures = 0;
            state->render_retry_after = 0;
        }
        if (state->miss_count < SLOT_MISS_LIMIT) ++state->miss_count;
        if (state->present && state->miss_count >= SLOT_MISS_LIMIT) {
            identity_changed = true;
            memset(state, 0, sizeof(*state));
            state->raw_id = 0xFFU;
            state->displayed_gate = SSD1315_GATE_NULL;
        }
    }
    portEXIT_CRITICAL(&s_slots_lock);

    if (needs_confirmation != NULL) {
        portENTER_CRITICAL(&s_slots_lock);
        *needs_confirmation = s_slots[slot].candidate_count > 0U &&
                              s_slots[slot].candidate_count < SLOT_ID_CONFIRM_SAMPLES;
        portEXIT_CRITICAL(&s_slots_lock);
    }
    if (confirmed_identity_seen) baseboard_queue_render(slot);
    log_slow_operation("baseboard identity scan", started_us);
    return identity_changed;
}

static void baseboard_oled_task(void *arg)
{
    uint8_t slot;
    (void)arg;
    for (;;) {
        if (xQueueReceive(s_baseboard_render_queue, &slot, portMAX_DELAY) != pdPASS) continue;

        uint8_t expected_id;
        ssd1315_gate_t expected_gate;
        bool present;
        bool initialized;
        portENTER_CRITICAL(&s_slots_lock);
        present = s_slots[slot].present;
        expected_id = s_slots[slot].raw_id;
        expected_gate = s_slots[slot].displayed_gate;
        initialized = s_slots[slot].oled_initialized;
        portEXIT_CRITICAL(&s_slots_lock);

        const int64_t started_us = esp_timer_get_time();
        esp_err_t err = ESP_ERR_NOT_FOUND;
        bool recovered = false;
        uint8_t attempts = 0;
        for (attempts = 0; attempts < BASEBOARD_RENDER_ATTEMPTS; ++attempts) {
            i2c_master_dev_handle_t oled = NULL;
            if (present && play_mode_is_active()) {
                err = baseboard_access_begin();
                if (err == ESP_OK) {
                    err = baseboard_select_slot(slot);
                    if (err == ESP_OK) {
                        esp_rom_delay_us(TCA_SETTLE_US);
                        oled = baseboard_probe_oled();
                        err = oled != NULL ? ESP_OK : ESP_ERR_NOT_FOUND;
                        if (err == ESP_OK && !initialized) err = ssd1315_oled_init(oled);
                        if (err == ESP_OK) err = ssd1315_oled_show_gate(oled, expected_gate);
                        baseboard_deselect_slot(slot);
                    } else {
                        baseboard_disable_all_channels();
                    }
                    baseboard_access_end();
                }
            }
            if (err == ESP_OK || attempts + 1U >= BASEBOARD_RENDER_ATTEMPTS) {
                break;
            }

            if (!present || !play_mode_is_active()) {
                break;
            }

            if (baseboard_access_begin() == ESP_OK) {
                (void)baseboard_recover_bus_locked();
                baseboard_access_end();
            }
            initialized = false;
            recovered = true;
        }

        bool identity_still_matches;
        portENTER_CRITICAL(&s_slots_lock);
        slot_state_t *state = &s_slots[slot];
        identity_still_matches = state->present && state->raw_id == expected_id &&
                                 state->displayed_gate == expected_gate;
        state->render_queued = false;
        if (err == ESP_OK && identity_still_matches) {
            state->oled_present = true;
            state->oled_initialized = true;
            state->has_rendered = true;
            state->render_failures = 0;
            state->render_retry_after = 0;
        } else if (identity_still_matches) {
            state->oled_present = false;
            state->oled_initialized = false;
            state->has_rendered = false;
            if (state->render_failures < UINT8_MAX) ++state->render_failures;
            state->render_retry_after = xTaskGetTickCount() +
                                        pdMS_TO_TICKS(baseboard_render_retry_ms(state->render_failures));
        }
        portEXIT_CRITICAL(&s_slots_lock);

        if (err == ESP_OK && identity_still_matches) {
            const board_slot_mapping_t *mapping = board_mapping_slot(slot);
            const int64_t render_us = esp_timer_get_time() - started_us;
            ESP_LOGI(TAG,
                     "slot=%u module=%u grid=%u,%u tca=0x%02X ch=%u id=0x%02X display=%s attempts=%u recovered=%u %lldus",
                     slot + 1U, mapping->module_number, mapping->grid_row, mapping->grid_column,
                     mapping->tca_address, mapping->tca_channel, expected_id,
                     ssd1315_gate_name(expected_gate), attempts + 1U, recovered,
                     (long long)render_us);
        } else if (present && play_mode_is_active()) {
            uint8_t failures;
            portENTER_CRITICAL(&s_slots_lock);
            failures = s_slots[slot].render_failures;
            portEXIT_CRITICAL(&s_slots_lock);
            ESP_LOGW(TAG, "slot=%u OLED update failed: %s attempts=%u recovered=%u retry=%ums failures=%u",
                     slot + 1U, esp_err_to_name(err), attempts + 1U, recovered,
                     (unsigned)baseboard_render_retry_ms(failures), failures);
        }
        log_slow_operation("baseboard OLED render", started_us);
    }
}

static esp_err_t programmer_render_frame(const programmer_render_request_t *request,
                                         bool *initialized)
{
    static uint32_t render_count;
    if (request == NULL || request->oled == NULL || initialized == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t started_us = esp_timer_get_time();
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_programmer_bus_lock, portMAX_DELAY);
    if (!*initialized) {
        err = ssd1315_oled_init(request->oled);
        if (err == ESP_OK) *initialized = true;
    }
    if (err == ESP_OK) {
        err = request->success ? ssd1315_oled_show_success(request->oled) :
                                 ssd1315_oled_show_gate(request->oled, request->gate);
    }
    xSemaphoreGive(s_programmer_bus_lock);
    if (err != ESP_OK) {
        *initialized = false;
        ESP_LOGW(TAG, "programmer OLED update failed: %s", esp_err_to_name(err));
    }
    ++render_count;
    if (render_count <= 3U || (render_count % 20U) == 0U) {
        ESP_LOGI(TAG, "programmer OLED frame=%lu time=%lldus initialized=%u",
                 (unsigned long)render_count,
                 (long long)(esp_timer_get_time() - started_us),
                 *initialized);
    }
    log_slow_operation("programmer OLED render", started_us);
    return err;
}

static void programmer_request_render(uint32_t delay_ms)
{
    const programmer_render_request_t request = {
        .oled = s_programmer_active_oled,
        .gate = s_programmer_selection,
        .success = s_programmer_success,
        .generation = s_programmer_oled_generation,
        .due = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms),
    };
    (void)xQueueOverwrite(s_programmer_render_queue, &request);
}

static void programmer_oled_task(void *arg)
{
    programmer_render_request_t request;
    i2c_master_dev_handle_t last_oled = NULL;
    uint32_t last_generation = UINT32_MAX;
    bool initialized = false;
    (void)arg;

    for (;;) {
        if (xQueueReceive(s_programmer_render_queue, &request, portMAX_DELAY) != pdPASS) continue;

        while (!tick_is_due(xTaskGetTickCount(), request.due)) {
            const TickType_t wait = request.due - xTaskGetTickCount();
            programmer_render_request_t newer;
            if (xQueueReceive(s_programmer_render_queue, &newer, wait) == pdPASS) {
                request = newer;
            }
        }

        if (request.generation != last_generation || request.oled != last_oled) {
            initialized = false;
            last_generation = request.generation;
            last_oled = request.oled;
        }
        if (request.oled != NULL && play_mode_is_active()) {
            (void)programmer_render_frame(&request, &initialized);
        }
    }
}

static void programmer_poll(void)
{
    uint8_t eeprom_address = s_programmer_eeprom_addr;
    uint8_t oled_address = s_programmer_oled_addr;
    uint8_t id = 0xFF;
    i2c_master_dev_handle_t eeprom = s_programmer_active_eeprom;
    i2c_master_dev_handle_t oled = s_programmer_active_oled;
    xSemaphoreTake(s_programmer_bus_lock, portMAX_DELAY);
    bool id_read_ok = eeprom != NULL && eeprom_read_id(eeprom, &id) == ESP_OK;
    bool oled_present = id_read_ok && oled != NULL && oled_address != 0U &&
                        i2c_master_probe(s_programmer_bus, oled_address,
                                         I2C_PROBE_TIMEOUT_TICKS) == ESP_OK;

    if (!id_read_ok) {
        eeprom_address = 0;
        if (i2c_master_probe(s_programmer_bus, EEPROM_ADDR_PRIMARY,
                             I2C_PROBE_TIMEOUT_TICKS) == ESP_OK) {
            eeprom_address = EEPROM_ADDR_PRIMARY;
        } else if (i2c_master_probe(s_programmer_bus, EEPROM_ADDR_SECONDARY,
                                    I2C_PROBE_TIMEOUT_TICKS) == ESP_OK) {
            eeprom_address = EEPROM_ADDR_SECONDARY;
        }
        eeprom = eeprom_address != 0U ? programmer_eeprom_handle(eeprom_address) : NULL;
        id_read_ok = eeprom != NULL && eeprom_read_id(eeprom, &id) == ESP_OK;
    }
    if (!oled_present) {
        oled_address = 0;
        if (i2c_master_probe(s_programmer_bus, OLED_ADDR_PRIMARY,
                             I2C_PROBE_TIMEOUT_TICKS) == ESP_OK) {
            oled_address = OLED_ADDR_PRIMARY;
        } else if (i2c_master_probe(s_programmer_bus, OLED_ADDR_SECONDARY,
                                    I2C_PROBE_TIMEOUT_TICKS) == ESP_OK) {
            oled_address = OLED_ADDR_SECONDARY;
        }
        oled = oled_address != 0U ? programmer_oled_handle(oled_address) : NULL;
        oled_present = oled != NULL;
    }
    xSemaphoreGive(s_programmer_bus_lock);
    const bool complete_sample = id_read_ok && oled != NULL;
    if (!complete_sample) {
        s_programmer_good_samples = 0;
        if (s_programmer_bad_samples < PROGRAMMER_REMOVE_SAMPLES) ++s_programmer_bad_samples;
        if (s_programmer_present && s_programmer_bad_samples >= PROGRAMMER_REMOVE_SAMPLES) {
            ESP_LOGI(TAG, "programmer: block removed");
            s_programmer_present = false;
            s_programmer_was_present = false;
            s_programmer_success = false;
            s_programmer_active_eeprom = NULL;
            s_programmer_active_oled = NULL;
            s_programmer_eeprom_addr = 0;
            s_programmer_oled_addr = 0;
            ++s_programmer_oled_generation;
            programmer_request_render(0);
        } else if (!s_programmer_present) {
            s_programmer_active_eeprom = NULL;
            s_programmer_active_oled = NULL;
            s_programmer_eeprom_addr = 0;
            s_programmer_oled_addr = 0;
        }
        return;
    }

    s_programmer_bad_samples = 0;
    const bool device_changed = s_programmer_active_eeprom != NULL &&
                                (s_programmer_active_eeprom != eeprom ||
                                 s_programmer_active_oled != oled);
    if (device_changed) {
        s_programmer_good_samples = 0;
        ++s_programmer_oled_generation;
    }
    s_programmer_eeprom_addr = eeprom_address;
    s_programmer_oled_addr = oled_address;
    s_programmer_active_eeprom = eeprom;
    s_programmer_active_oled = oled;
    if (!s_programmer_present) {
        if (s_programmer_good_samples < PROGRAMMER_INSERT_SAMPLES) ++s_programmer_good_samples;
        if (s_programmer_good_samples < PROGRAMMER_INSERT_SAMPLES) return;
        s_programmer_present = true;
        ++s_programmer_oled_generation;
    }

    if (!s_programmer_was_present) {
        s_programmer_selection = programmer_gate_for_id(id);
        ESP_LOGI(TAG, "programmer: block inserted id=0x%02X selected=%s", id,
                 ssd1315_gate_name(s_programmer_selection));
        programmer_request_render(0);
        s_programmer_was_present = true;
        s_programmer_last_id = id;
        return;
    }

    if (s_programmer_success && xTaskGetTickCount() - s_programmer_success_started >= pdMS_TO_TICKS(1500)) {
        s_programmer_success = false;
        programmer_request_render(0);
    } else if (!s_programmer_success && id != s_programmer_last_id) {
        s_programmer_selection = programmer_gate_for_id(id);
        s_programmer_last_id = id;
        programmer_request_render(0);
    }
}

static bool programmer_apply_navigation(programmer_command_t command)
{
    if (!s_programmer_present || s_programmer_success || s_programmer_active_eeprom == NULL ||
        (command != PROGRAMMER_COMMAND_PREVIOUS && command != PROGRAMMER_COMMAND_NEXT)) {
        return false;
    }
    s_programmer_selection = programmer_next_available_gate(
        s_programmer_selection, command == PROGRAMMER_COMMAND_PREVIOUS ? -1 : 1);
    return true;
}

static bool programmer_handle_command(programmer_command_t command)
{
    if (!s_programmer_present || s_programmer_success || s_programmer_active_eeprom == NULL ||
        command != PROGRAMMER_COMMAND_CONFIRM) {
        return false;
    }
    if (!app_ui_gate_is_unlocked(s_programmer_selection)) {
        s_programmer_selection = programmer_first_available_gate();
    }
    const uint8_t target_id = ssd1315_gate_to_eeprom_id(s_programmer_selection);
    xSemaphoreTake(s_programmer_bus_lock, portMAX_DELAY);
    const esp_err_t err = eeprom_write_id(s_programmer_active_eeprom, target_id);
    xSemaphoreGive(s_programmer_bus_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "programmer: EEPROM write failed: %s", esp_err_to_name(err));
        audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
        return false;
    }
    s_programmer_success = true;
    s_programmer_success_started = xTaskGetTickCount();
    s_programmer_last_id = target_id;
    programmer_request_render(0);
    ESP_LOGI(TAG, "programmer: EEPROM ID changed to 0x%02X", target_id);
    return true;
}

static void programmer_task(void *arg)
{
    TickType_t last_poll = 0;
    bool was_play_active = false;

    (void)arg;
    for (;;) {
        if (!play_mode_is_active()) {
            if (was_play_active) {
                s_programmer_present = false;
                s_programmer_was_present = false;
                s_programmer_success = false;
                s_programmer_active_eeprom = NULL;
                s_programmer_active_oled = NULL;
                s_programmer_eeprom_addr = 0;
                s_programmer_oled_addr = 0;
                ++s_programmer_oled_generation;
                programmer_request_render(0);
                s_programmer_last_id = 0xFFU;
                s_programmer_good_samples = 0;
                s_programmer_bad_samples = 0;
                programmer_command_t discarded;
                while (xQueueReceive(s_programmer_command_queue, &discarded, 0) == pdPASS) {
                }
            }
            was_play_active = false;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!was_play_active) {
            last_poll = 0;
            was_play_active = true;
        }
        bool selection_changed = false;
        programmer_command_t command;
        while (xQueueReceive(s_programmer_command_queue, &command, 0) == pdPASS) {
            if (command == PROGRAMMER_COMMAND_CONFIRM) {
                if (programmer_handle_command(command)) {
                    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
                }
                selection_changed = false;
            } else if (programmer_apply_navigation(command)) {
                selection_changed = true;
            }
        }
        if (selection_changed) {
            audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
            programmer_request_render(PROGRAMMER_RENDER_COALESCE_MS);
        }

        const TickType_t now = xTaskGetTickCount();
        const TickType_t poll_period = pdMS_TO_TICKS(
            s_programmer_present ? PROGRAMMER_HEALTH_PERIOD_MS : PROGRAMMER_SEARCH_PERIOD_MS);
        if (now - last_poll >= poll_period) {
            last_poll = now;
            programmer_poll();
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void baseboard_i2c_task(void *arg)
{
    uint8_t next_slot = 0;
    uint32_t completed_scans = 0;
    bool was_play_active = false;
    TickType_t next_wake = xTaskGetTickCount();
    int64_t last_scan_finished_us = 0;

    (void)arg;
    for (;;) {
        if (!baseboard_scan_is_active()) {
            if (was_play_active) {
                if (baseboard_access_begin() == ESP_OK) {
                    baseboard_disable_all_channels();
                    baseboard_access_end();
                }
                uint8_t discarded_slot;
                while (xQueueReceive(s_baseboard_render_queue, &discarded_slot, 0) == pdPASS) {
                }
                (void)baseboard_reset_transient_slot_state();
            }
            was_play_active = false;
            next_slot = 0;
            completed_scans = 0;
            last_scan_finished_us = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!was_play_active) {
            const uint8_t cached_slots = baseboard_reset_transient_slot_state();
            next_slot = 0;
            completed_scans = 0;
            last_scan_finished_us = 0;
            next_wake = xTaskGetTickCount();
            was_play_active = true;
            /* Reuse confirmed identities immediately; background scans revalidate them. */
            baseboard_publish_slots(completed_scans);
            ESP_LOGI(TAG, "play mode reused %u cached slot identities", cached_slots);
        }
        bool needs_confirmation = false;
        const bool identity_changed = baseboard_scan_slot(next_slot, &needs_confirmation);
        bool confirmed_identity_changed = false;
        if (needs_confirmation) {
            /* Confirm a newly observed ID immediately instead of waiting for
             * the remaining eleven slots in the current scan round. */
            vTaskDelay(pdMS_TO_TICKS(BASEBOARD_CONFIRM_REVISIT_MS));
            bool second_needs_confirmation = false;
            confirmed_identity_changed = baseboard_scan_slot(next_slot,
                                                              &second_needs_confirmation);
            next_wake = xTaskGetTickCount();
        }
        const bool slot_identity_changed = identity_changed || confirmed_identity_changed;
        next_slot = (uint8_t)((next_slot + 1U) % SLOT_COUNT);
        if (next_slot == 0U) {
            ++completed_scans;
            const int64_t now_us = esp_timer_get_time();
            if (last_scan_finished_us != 0 &&
                (completed_scans <= 3U || (completed_scans % 25U) == 0U)) {
                ESP_LOGI(TAG, "baseboard identity scan=%lu period=%lldus",
                         (unsigned long)completed_scans,
                         (long long)(now_us - last_scan_finished_us));
            }
            last_scan_finished_us = now_us;
        }
        if (slot_identity_changed || next_slot == 0U) baseboard_publish_slots(completed_scans);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(BASEBOARD_SLOT_PERIOD_MS));
    }
}

esp_err_t block_i2c_init(void)
{
    ESP_RETURN_ON_ERROR(programmer_gate_selection_self_test(), TAG,
                        "validate programmer gate order and unlock filtering");
    const i2c_master_bus_config_t programmer_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = PROGRAMMER_I2C_PORT,
        .sda_io_num = PROGRAMMER_SDA_GPIO,
        .scl_io_num = PROGRAMMER_SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&programmer_config, &s_programmer_bus), TAG,
                        "create programmer I2C bus");
    s_programmer_oled_3c = add_device(s_programmer_bus, OLED_ADDR_PRIMARY, PROGRAMMER_I2C_SPEED_HZ);
    s_programmer_oled_3d = add_device(s_programmer_bus, OLED_ADDR_SECONDARY, PROGRAMMER_I2C_SPEED_HZ);
    s_programmer_eeprom_50 = add_device(s_programmer_bus, EEPROM_ADDR_PRIMARY, PROGRAMMER_I2C_SPEED_HZ);
    s_programmer_eeprom_51 = add_device(s_programmer_bus, EEPROM_ADDR_SECONDARY, PROGRAMMER_I2C_SPEED_HZ);
    if (s_programmer_oled_3c == NULL || s_programmer_oled_3d == NULL ||
        s_programmer_eeprom_50 == NULL || s_programmer_eeprom_51 == NULL) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(board_mapping_validate(), TAG, "validate baseboard mapping");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "initialize shared baseboard I2C bus");
    s_baseboard_bus = bsp_i2c_get_handle();
    if (s_baseboard_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_baseboard_tca_70 = add_device(s_baseboard_bus, TCA_ADDR_FIRST, BASEBOARD_I2C_SPEED_HZ);
    s_baseboard_tca_71 = add_device(s_baseboard_bus, TCA_ADDR_SECOND, BASEBOARD_I2C_SPEED_HZ);
    s_baseboard_oled_3c = add_device(s_baseboard_bus, OLED_ADDR_PRIMARY, BASEBOARD_I2C_SPEED_HZ);
    s_baseboard_oled_3d = add_device(s_baseboard_bus, OLED_ADDR_SECONDARY, BASEBOARD_I2C_SPEED_HZ);
    s_baseboard_eeprom_50 = add_device(s_baseboard_bus, EEPROM_ADDR_PRIMARY, BASEBOARD_I2C_SPEED_HZ);
    if (s_baseboard_tca_70 == NULL || s_baseboard_tca_71 == NULL || s_baseboard_oled_3c == NULL ||
        s_baseboard_oled_3d == NULL || s_baseboard_eeprom_50 == NULL) {
        return ESP_FAIL;
    }

    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << BASEBOARD_TCA_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "configure TCA reset");
    ESP_RETURN_ON_ERROR(gpio_set_level(BASEBOARD_TCA_RESET_GPIO, 1), TAG, "release TCA reset");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(-1), TAG, "lock touch while resetting TCA");
    baseboard_disable_all_channels();
    esp_lv_adapter_unlock();

    s_baseboard_lock = xSemaphoreCreateMutex();
    s_programmer_bus_lock = xSemaphoreCreateMutex();
    s_programmer_command_queue = xQueueCreate(8, sizeof(programmer_command_t));
    s_programmer_render_queue = xQueueCreate(1, sizeof(programmer_render_request_t));
    s_baseboard_render_queue = xQueueCreate(SLOT_COUNT, sizeof(uint8_t));
    if (s_baseboard_lock == NULL || s_programmer_bus_lock == NULL ||
        s_programmer_command_queue == NULL || s_programmer_render_queue == NULL ||
        s_baseboard_render_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(programmer_task, "block_program", 4096, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(programmer_oled_task, "program_oled", 4096, NULL, 2, NULL) != pdPASS ||
        xTaskCreate(baseboard_i2c_task, "baseboard_i2c", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(baseboard_oled_task, "baseboard_oled", 4096, NULL,
                    BASEBOARD_OLED_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "I2C ready: programmer 25/100ms poll + 35ms OLED coalescing, "
             "baseboard 100kHz, cached identities + priority-5 revalidation, "
             "fast 2-sample confirmation + priority-%u OLED recovery/retry",
             BASEBOARD_OLED_TASK_PRIORITY);
    return ESP_OK;
}

void block_i2c_submit_input(const key_input_state_t *keys,
                            const joystick_input_state_t *joystick)
{
    static bool initialized;
    static key_input_state_t previous_keys;
    static joystick_input_state_t previous_joystick;

    if (s_programmer_command_queue == NULL || keys == NULL || joystick == NULL) {
        return;
    }
    if (!initialized) {
        previous_keys = *keys;
        previous_joystick = *joystick;
        initialized = true;
        return;
    }

    if (!play_mode_is_active()) {
        previous_keys = *keys;
        previous_joystick = *joystick;
        return;
    }

    programmer_command_t command;
    bool have_command = false;
    if ((keys->key3_pressed && !previous_keys.key3_pressed) ||
        (joystick->left_pressed && !previous_joystick.left_pressed)) {
        command = PROGRAMMER_COMMAND_PREVIOUS;
        have_command = true;
    } else if ((keys->key0_pressed && !previous_keys.key0_pressed) ||
               (joystick->right_pressed && !previous_joystick.right_pressed)) {
        command = PROGRAMMER_COMMAND_NEXT;
        have_command = true;
    } else if (keys->key1_pressed && !previous_keys.key1_pressed) {
        command = PROGRAMMER_COMMAND_CONFIRM;
        have_command = true;
    }
    previous_keys = *keys;
    previous_joystick = *joystick;
    if (have_command && s_programmer_present) {
        (void)xQueueSend(s_programmer_command_queue, &command, 0);
    }
}

bool block_i2c_programmer_present(void)
{
    return play_mode_is_active() && s_programmer_present;
}
