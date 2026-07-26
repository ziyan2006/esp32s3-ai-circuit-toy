#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "key_input.h"
#include "joystick_input.h"
#include "shooter_core.h"
#include "app_ui.h"
#include "baseboard_ir.h"
#include "block_i2c.h"
#include "audio_self_test.h"
#include "assistant_mode.h"
#include "assistant_router.h"
#include "remote_assistant.h"
#include "board_snapshot.h"
#include "board_mapping.h"
#include "circuit_debug.h"
#include "campaign_progress.h"
#include "circuit_logic.h"
#include "game_judge.h"
#include "game_logic_self_test.h"
#include "play_mode.h"
#include "progress_sync.h"
#include "c6_network_test.h"
#include "tuco_agent.h"
#include "tuco_port_highlight.h"
#include "volcengine_voice.h"

/*
 * The GPIO46 LED chain is updated from complete IR scan matrices. The shared
 * 74HC595/165 IR bus remains exclusively owned by the IR scan task.
 */
#define WS2812_DATA_GPIO             GPIO_NUM_46
#define WS2812_LED_COUNT             BOARD_MAPPING_PORT_COUNT
#define WS2812_BITS_PER_LED          24U
#define WS2812_BUFFER_SIZE            (WS2812_LED_COUNT * 3U)
#define WS2812_RMT_RESOLUTION_HZ     10000000U
#define WS2812_BIT_TICKS              12U
#define WS2812_DATA_TIME_US           ((WS2812_LED_COUNT * WS2812_BITS_PER_LED * WS2812_BIT_TICKS) / 10U)

static const char *TAG = "ws2812_diag";

static rmt_channel_handle_t s_rmt_channel;
static rmt_encoder_handle_t s_bytes_encoder;
static esp_ldo_channel_handle_t s_ldo4_channel;
static uint8_t s_pixels[WS2812_BUFFER_SIZE];
static uint8_t s_last_transmitted_pixels[WS2812_BUFFER_SIZE];
static bool s_last_transmitted_valid;
static int64_t s_last_transmitted_us;
static uint32_t s_completed_frames;
typedef struct {
    key_input_state_t keys;
    joystick_input_state_t joystick;
    uint32_t completed_ir_scans;
    uint16_t ir_link_pairs;
    bool programmer_owns_input;
} input_state_t;

static QueueHandle_t s_input_state_queue;
static TaskHandle_t s_ws2812_task;

static void shooter_self_test_task(void *arg)
{
    TaskHandle_t caller = (TaskHandle_t)arg;
    shooter_core_run_self_tests();
    xTaskNotifyGive(caller);
    vTaskDelete(NULL);
}

static esp_err_t run_shooter_self_tests(void)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (xTaskCreate(shooter_self_test_task, "shooter_test", 8192, caller, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0U) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "six-stage shooter core self-tests passed");
    return ESP_OK;
}

typedef struct {
    uint8_t green;
    uint8_t red;
    uint8_t blue;
} ws2812_color_t;

static void ws2812_dump_gpio46_configuration(void)
{
    ESP_LOGI(TAG, "GPIO46 configuration dump follows");
    ESP_ERROR_CHECK(gpio_dump_io_configuration(stdout, 1ULL << WS2812_DATA_GPIO));
}

static esp_err_t ws2812_enable_board_power(void)
{
    const esp_ldo_channel_config_t ldo_config = {
        .chan_id = 4,
        .voltage_mv = 3300,
        .flags.adjustable = true,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_config, &s_ldo4_channel);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "LDO4 enabled at 3300 mV (sample-compatible board power)");
    }
    return err;
}

static void ws2812_clear_frame(void)
{
    memset(s_pixels, 0, sizeof(s_pixels));
}

static void ws2812_set_led_color(uint8_t led, const ws2812_color_t *color)
{
    const size_t pixel_offset = (size_t)led * 3U;

    s_pixels[pixel_offset + 0U] = color->green;
    s_pixels[pixel_offset + 1U] = color->red;
    s_pixels[pixel_offset + 2U] = color->blue;
}

static esp_err_t ws2812_init(void)
{
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = WS2812_DATA_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    const rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 3,
            .level1 = 0,
            .duration1 = 9,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 9,
            .level1 = 0,
            .duration1 = 3,
        },
        .flags.msb_first = true,
    };

    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel_config, &s_rmt_channel), TAG, "create RMT TX channel");
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&encoder_config, &s_bytes_encoder), TAG, "create RMT bytes encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rmt_channel), TAG, "enable RMT TX channel");
    ESP_RETURN_ON_ERROR(gpio_input_enable(WS2812_DATA_GPIO), TAG, "enable GPIO46 input readback");

    ESP_LOGI(TAG, "Sample-compatible RMT ready: GPIO=%d resolution=%u Hz bytes=%u data=%u us",
             WS2812_DATA_GPIO,
             WS2812_RMT_RESOLUTION_HZ,
             WS2812_BUFFER_SIZE,
             WS2812_DATA_TIME_US);
    ws2812_dump_gpio46_configuration();
    return ESP_OK;
}

static esp_err_t ws2812_transmit_frame(void)
{
    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };

    ESP_RETURN_ON_ERROR(
        rmt_transmit(s_rmt_channel, s_bytes_encoder, s_pixels, sizeof(s_pixels), &transmit_config),
        TAG,
        "transmit WS2812 frame");
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(s_rmt_channel, pdMS_TO_TICKS(20)), TAG, "wait for WS2812 frame");
    /* A WS2812 latches a complete frame only after a low reset interval. Keep
     * this gap even when an IR notification and a switch notification arrive
     * back-to-back. */
    esp_rom_delay_us(80);
    memcpy(s_last_transmitted_pixels, s_pixels, sizeof(s_pixels));
    s_last_transmitted_valid = true;
    s_last_transmitted_us = esp_timer_get_time();

    ++s_completed_frames;
    if (s_completed_frames <= 3U || (s_completed_frames % 25U) == 0U) {
        ESP_LOGI(TAG, "Connection frame %" PRIu32 " complete; GPIO%d readback=%d",
                 s_completed_frames,
                 WS2812_DATA_GPIO,
                 gpio_get_level(WS2812_DATA_GPIO));
    }
    return ESP_OK;
}

static bool ws2812_frame_needs_transmit(void)
{
    const bool heartbeat_due = !s_last_transmitted_valid ||
                               esp_timer_get_time() - s_last_transmitted_us >= 250000;
    return heartbeat_due || memcmp(s_last_transmitted_pixels, s_pixels, sizeof(s_pixels)) != 0;
}

static ws2812_color_t ws2812_scaled_color(uint8_t color_index)
{
    const board_connection_color_t source = board_snapshot_get_color(color_index);
    const uint8_t maximum = source.red > source.green ?
        (source.red > source.blue ? source.red : source.blue) :
        (source.green > source.blue ? source.green : source.blue);
    const uint16_t scale = maximum > 112U ? maximum : 112U;
    return (ws2812_color_t) {
        .green = (uint8_t)((uint16_t)source.green * 112U / scale),
        .red = (uint8_t)((uint16_t)source.red * 112U / scale),
        .blue = (uint8_t)((uint16_t)source.blue * 112U / scale),
    };
}

static void ws2812_render_connections(const board_snapshot_t *snapshot)
{
    static uint8_t previous_links = UINT8_MAX;
    static uint8_t previous_invalid = UINT8_MAX;
    static uint8_t previous_ignored = UINT8_MAX;
    ws2812_clear_frame();
    for (uint8_t index = 0; index < snapshot->link_count; ++index) {
        const board_link_t *link = &snapshot->links[index];
        const ws2812_color_t color = ws2812_scaled_color(
            board_link_is_error(link) ? BOARD_SNAPSHOT_INVALID_COLOR : link->color_index);
        const uint8_t first_led = board_mapping_ws2812_index_for_port(link->first_port);
        const uint8_t second_led = board_mapping_ws2812_index_for_port(link->second_port);
        if (first_led < WS2812_LED_COUNT) ws2812_set_led_color(first_led, &color);
        if (second_led < WS2812_LED_COUNT) ws2812_set_led_color(second_led, &color);
    }
    if (snapshot->link_count != previous_links || snapshot->invalid_link_count != previous_invalid ||
        snapshot->ignored_link_count != previous_ignored) {
        ESP_LOGI(TAG, "IR links=%u valid=%u ignored=%u invalid=%u",
                 snapshot->link_count,
                 snapshot->link_count - snapshot->invalid_link_count - snapshot->ignored_link_count,
                 snapshot->ignored_link_count,
                 snapshot->invalid_link_count);
        previous_links = snapshot->link_count;
        previous_invalid = snapshot->invalid_link_count;
        previous_ignored = snapshot->ignored_link_count;
    }
}

static void ws2812_render_debug(const board_snapshot_t *snapshot,
                                const circuit_debug_state_t *debug)
{
    static uint32_t previous_generation = UINT32_MAX;
    static uint32_t previous_topology = UINT32_MAX;
    static const ws2812_color_t signal_high = {.green = 112, .red = 0, .blue = 0};
    static const ws2812_color_t signal_low = {.green = 0, .red = 112, .blue = 0};
    const ws2812_color_t error_color = ws2812_scaled_color(BOARD_SNAPSHOT_INVALID_COLOR);
    bool error_ports[BOARD_SNAPSHOT_PORT_COUNT] = {0};
    bool suppressed_ports[BOARD_SNAPSHOT_PORT_COUNT] = {0};
    circuit_signal_state_t signals;
    const uint8_t assigned_inputs = debug->input_count < CIRCUIT_DEBUG_SWITCH_COUNT ?
                                    debug->input_count : CIRCUIT_DEBUG_SWITCH_COUNT;
    const uint8_t input_values = circuit_debug_compact_input_values(debug);
    const bool propagated = circuit_logic_propagate(snapshot, debug->input_slots,
                                                     assigned_inputs, input_values,
                                                     &signals);

    if (debug->generation != previous_generation ||
        snapshot->topology_revision != previous_topology) {
        uint32_t known_mask = 0;
        uint32_t value_mask = 0;
        if (propagated) {
            for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
                if (signals.known[slot]) known_mask |= 1UL << slot;
                if (signals.known[slot] && signals.values[slot]) value_mask |= 1UL << slot;
            }
        }
        ESP_LOGI(TAG, "debug gen=%" PRIu32 " switches=0x%X inputs=%u misplaced=0x%04" PRIX16
                      " links=%u known=0x%04" PRIX32 " values=0x%04" PRIX32,
                 debug->generation, debug->switch_mask, assigned_inputs,
                 debug->misplaced_input_mask,
                 snapshot->link_count, known_mask, value_mask);
        previous_generation = debug->generation;
        previous_topology = snapshot->topology_revision;
    }

    ws2812_clear_frame();
    for (uint8_t index = 0; index < snapshot->link_count; ++index) {
        const board_link_t *link = &snapshot->links[index];
        if (link->first_port >= BOARD_SNAPSHOT_PORT_COUNT ||
            link->second_port >= BOARD_SNAPSHOT_PORT_COUNT) continue;

        if (link->valid) {
            bool value;
            if (!propagated || !circuit_logic_get_link_signal(snapshot, &signals, link, &value)) {
                continue;
            }
            const ws2812_color_t *color = value ? &signal_high : &signal_low;
            const uint8_t first_led = board_mapping_ws2812_index_for_port(link->first_port);
            const uint8_t second_led = board_mapping_ws2812_index_for_port(link->second_port);
            if (first_led < WS2812_LED_COUNT) ws2812_set_led_color(first_led, color);
            if (second_led < WS2812_LED_COUNT) ws2812_set_led_color(second_led, color);
        } else if (link->error == BOARD_LINK_DIRECTION_ERROR ||
                   link->error == BOARD_LINK_MULTIPLE_CONNECTIONS) {
            error_ports[link->first_port] = true;
            error_ports[link->second_port] = true;
        } else {
            /* A cable that does not end on two usable block ports remains dark. */
            suppressed_ports[link->first_port] = true;
            suppressed_ports[link->second_port] = true;
        }
    }

    if (propagated) {
        for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
            const ssd1315_gate_t gate = snapshot->slots[slot].gate;
            if (!snapshot->slots[slot].present || !snapshot->slots[slot].id_valid ||
                (gate != SSD1315_GATE_INPUT && gate != SSD1315_GATE_OUTPUT) ||
                !signals.known[slot]) continue;
            const ws2812_color_t *color = signals.values[slot] ? &signal_high : &signal_low;
            for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
                const uint8_t port = slot * BOARD_SNAPSHOT_PORTS_PER_SLOT + local;
                /* INPUT/OUTPUT are intentionally four-way debug indicators.
                 * Their four LEDs follow the block value even when an unused
                 * physical side has a dangling optical detection. Intermediate
                 * gates still keep the dangling-line-off policy. */
                const bool is_io_block = gate == SSD1315_GATE_INPUT ||
                                         gate == SSD1315_GATE_OUTPUT;
                if (error_ports[port] || (!is_io_block && suppressed_ports[port])) continue;
                const uint8_t led = board_mapping_ws2812_index_for_port(port);
                if (led < WS2812_LED_COUNT) ws2812_set_led_color(led, color);
            }
        }
    }

    for (uint8_t port = 0; port < BOARD_SNAPSHOT_PORT_COUNT; ++port) {
        if (!error_ports[port]) continue;
        const uint8_t led = board_mapping_ws2812_index_for_port(port);
        if (led < WS2812_LED_COUNT) ws2812_set_led_color(led, &error_color);
    }
}

static void ws2812_render_port_highlight(const board_snapshot_t *snapshot)
{
    static const ws2812_color_t highlight_color = {.green = 180, .red = 180, .blue = 0};
    uint8_t ports[BOARD_SNAPSHOT_PORTS_PER_SLOT];
    uint8_t port_count;
    bool visible;

    if (!tuco_port_highlight_get(snapshot, ports, &port_count, &visible) || !visible) return;
    for (uint8_t index = 0; index < port_count; ++index) {
        const uint8_t led = board_mapping_ws2812_index_for_port(ports[index]);
        if (led < WS2812_LED_COUNT) ws2812_set_led_color(led, &highlight_color);
    }
}

static void ws2812_connection_task(void *arg)
{
    board_snapshot_t snapshot;
    /* Force one post-initialization black frame. The first frame in app_main
     * can race the WS2812 power-up latch, especially for the tail LEDs. */
    bool output_active = true;
    uint32_t rendered_debug_generation = UINT32_MAX;

    (void)arg;
    for (;;) {
        const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        if (!play_mode_is_active()) {
            if (output_active) {
                ws2812_clear_frame();
                ESP_ERROR_CHECK(ws2812_transmit_frame());
                output_active = false;
            }
            continue;
        }
        circuit_debug_state_t debug;
        const bool have_snapshot = board_snapshot_get(&snapshot);
        if (have_snapshot) circuit_debug_sync_snapshot(&snapshot);
        if (!circuit_debug_get_state(&debug)) continue;
        const bool debug_changed = debug.generation != rendered_debug_generation;
        const bool heartbeat_due = debug.enabled &&
                                   (!s_last_transmitted_valid ||
                                    esp_timer_get_time() - s_last_transmitted_us >= 250000);
        const bool highlight_active = tuco_port_highlight_is_active();
        if (have_snapshot && (notified != 0U || debug_changed || heartbeat_due || highlight_active)) {
            if (debug.enabled) {
                board_snapshot_t stable_snapshot;
                if (circuit_debug_get_render_snapshot(&stable_snapshot)) {
                    ws2812_render_debug(&stable_snapshot, &debug);
                } else {
                    ws2812_render_debug(&snapshot, &debug);
                }
            } else {
                ws2812_render_connections(&snapshot);
            }
            ws2812_render_port_highlight(&snapshot);
            if (ws2812_frame_needs_transmit()) {
                ESP_ERROR_CHECK(ws2812_transmit_frame());
            }
            output_active = true;
            rendered_debug_generation = debug.generation;
        }
    }
}

static void input_scan_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        input_state_t state = {
            .keys = key_input_update(baseboard_ir_get_latest_input_byte()),
            .joystick = joystick_input_read(),
        };
        const uint8_t switch_mask = (state.keys.sw1_on ? 1U : 0U) |
                                    (state.keys.sw2_on ? 2U : 0U) |
                                    (state.keys.sw3_on ? 4U : 0U) |
                                    (state.keys.sw4_on ? 8U : 0U);
        if (circuit_debug_update_switches(switch_mask) && s_ws2812_task != NULL) {
            xTaskNotifyGive(s_ws2812_task);
        }
        baseboard_ir_get_link_status(&state.completed_ir_scans, &state.ir_link_pairs);

        block_i2c_submit_input(&state.keys, &state.joystick);
        state.programmer_owns_input = block_i2c_programmer_present();
        xQueueOverwrite(s_input_state_queue, &state);
    }
}

static void ui_update_task(void *arg)
{
    input_state_t state;

    (void)arg;
    for (;;) {
        if (xQueueReceive(s_input_state_queue, &state, portMAX_DELAY) == pdPASS) {
            ESP_ERROR_CHECK(app_ui_update(&state.keys,
                                          &state.joystick,
                                          state.completed_ir_scans,
                                          state.ir_link_pairs,
                                          state.programmer_owns_input));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting hardware services; IR and WS2812 stay idle until a level starts");
    ESP_ERROR_CHECK(campaign_progress_init());
    ESP_ERROR_CHECK(assistant_mode_init());
    ESP_ERROR_CHECK(progress_sync_init());
    esp_err_t c6_test_err = c6_network_test_start();
    if (c6_test_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start C6 network test: %s", esp_err_to_name(c6_test_err));
    }
    play_mode_set_active(false);
    board_snapshot_init();
    tuco_port_highlight_init();
    circuit_debug_init();
    ESP_ERROR_CHECK(game_logic_self_test_run());
    ESP_ERROR_CHECK(assistant_mode_self_test_run());
    ESP_ERROR_CHECK(tuco_agent_context_self_test_run());
    ESP_ERROR_CHECK(run_shooter_self_tests());
    ESP_ERROR_CHECK(game_judge_init());
    ESP_ERROR_CHECK(ws2812_enable_board_power());
    ESP_ERROR_CHECK(ws2812_init());
    ws2812_clear_frame();
    ESP_ERROR_CHECK(ws2812_transmit_frame());
    ESP_ERROR_CHECK(key_input_init());
    ESP_ERROR_CHECK(joystick_input_init());
    ESP_ERROR_CHECK(baseboard_ir_init());
    ESP_ERROR_CHECK(app_ui_init());
    ESP_ERROR_CHECK(audio_self_test_init());
    ESP_ERROR_CHECK(tuco_agent_init());
    ESP_ERROR_CHECK(remote_assistant_init());
    ESP_ERROR_CHECK(assistant_router_init());
    ESP_ERROR_CHECK(assistant_router_self_test_run());
    tuco_agent_serial_start();
    ESP_ERROR_CHECK(voice_assistant_init());
    ESP_ERROR_CHECK(block_i2c_init());

    s_input_state_queue = xQueueCreate(1, sizeof(input_state_t));
    ESP_ERROR_CHECK(s_input_state_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    TaskHandle_t input_task = NULL;
    s_ws2812_task = NULL;
    BaseType_t task_created = xTaskCreate(input_scan_task, "input_scan", 3072, NULL, 5, &input_task);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    baseboard_ir_set_input_task(input_task);
    task_created = xTaskCreate(ws2812_connection_task, "ws2812_links", 6144, NULL, 5, &s_ws2812_task);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    baseboard_ir_set_ws2812_task(s_ws2812_task);
    task_created = xTaskCreate(baseboard_ir_task, "baseboard_ir", 4096, NULL, 7, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    task_created = xTaskCreate(ui_update_task, "ui_update", 8192, NULL, 4, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
