#include "baseboard_ir.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include "board_snapshot.h"
#include "board_mapping.h"
#include "play_mode.h"

#define IR_MOSI_GPIO                 GPIO_NUM_21
#define IR_MISO_GPIO                 GPIO_NUM_22
#define IR_CLOCK_GPIO                GPIO_NUM_6
#define IR_TX_LATCH_GPIO             GPIO_NUM_5
#define IR_RX_LOAD_GPIO              GPIO_NUM_4
#define IR_OUTPUT_ENABLE_GPIO        GPIO_NUM_3

#define IR_SLOT_PERIOD_US            3200U
#define IR_SETTLE_US                 2400U
#define IR_SAMPLE_COUNT              3U
#define IR_INPUT_DIVIDER             5U
#define IR_LOG_INTERVAL_SCANS        10U
#define IR_IDLE_INPUT_PERIOD_MS       20U
#define IR_RX_ACTIVE_HIGH             1
#if BASEBOARD_IR_PORT_COUNT == 64U
#define IR_RX_PORT_MASK               UINT64_MAX
#else
#define IR_RX_PORT_MASK               ((1ULL << BASEBOARD_IR_PORT_COUNT) - 1ULL)
#endif

typedef struct {
    uint8_t input_byte;
    uint64_t raw_rx;
} ir_snapshot_t;

static const char *TAG = "baseboard_ir";
static portMUX_TYPE s_matrix_lock = portMUX_INITIALIZER_UNLOCKED;
static baseboard_ir_matrix_t s_matrix;
static volatile uint8_t s_latest_input_byte = UINT8_MAX;
static TaskHandle_t s_input_task;
static TaskHandle_t s_ws2812_task;

static void baseboard_ir_shift_595(uint64_t mask)
{
    for (int bit = BASEBOARD_IR_PORT_COUNT - 1; bit >= 0; --bit) {
        gpio_set_level(IR_MOSI_GPIO, (mask >> bit) & 1ULL);
        esp_rom_delay_us(1);
        gpio_set_level(IR_CLOCK_GPIO, 1);
        esp_rom_delay_us(1);
        gpio_set_level(IR_CLOCK_GPIO, 0);
        esp_rom_delay_us(1);
    }
}

static void baseboard_ir_latch_595(uint64_t mask)
{
    gpio_set_level(IR_OUTPUT_ENABLE_GPIO, 1);
    gpio_set_level(IR_TX_LATCH_GPIO, 0);
    baseboard_ir_shift_595(mask);
    gpio_set_level(IR_TX_LATCH_GPIO, 1);
    esp_rom_delay_us(2);
    gpio_set_level(IR_TX_LATCH_GPIO, 0);
    gpio_set_level(IR_OUTPUT_ENABLE_GPIO, 0);
}

static void baseboard_ir_disable_emitters(void)
{
    baseboard_ir_latch_595(0ULL);
}

static ir_snapshot_t baseboard_ir_read_165_snapshot(void)
{
    ir_snapshot_t snapshot = {0};

    gpio_set_level(IR_RX_LOAD_GPIO, 0);
    esp_rom_delay_us(2);
    gpio_set_level(IR_RX_LOAD_GPIO, 1);
    esp_rom_delay_us(2);

    for (uint8_t bit = 0; bit < 8U; ++bit) {
        snapshot.input_byte = (uint8_t)((snapshot.input_byte << 1) |
                                        (gpio_get_level(IR_MISO_GPIO) ? 1U : 0U));
        gpio_set_level(IR_CLOCK_GPIO, 1);
        esp_rom_delay_us(1);
        gpio_set_level(IR_CLOCK_GPIO, 0);
        esp_rom_delay_us(1);
    }
    for (uint8_t bit = 0; bit < BASEBOARD_IR_PORT_COUNT; ++bit) {
        snapshot.raw_rx = (snapshot.raw_rx << 1) |
                          (gpio_get_level(IR_MISO_GPIO) ? 1ULL : 0ULL);
        gpio_set_level(IR_CLOCK_GPIO, 1);
        esp_rom_delay_us(1);
        gpio_set_level(IR_CLOCK_GPIO, 0);
        esp_rom_delay_us(1);
    }
    return snapshot;
}

static uint64_t baseboard_ir_to_logical_ports(uint64_t raw_rx)
{
    uint64_t logical_rx = 0;

    /*
     * Inside every baseboard, bit0..bit3 map to ports1..ports4. The 165
     * serial cascade, however, reaches MISO from the last 8-bit group first.
     * Reverse the eight serial 8-bit groups while preserving bit order within a
     * group so raw data maps back to ascending logical port numbers.
     */
    for (uint8_t raw_bit = 0; raw_bit < BASEBOARD_IR_PORT_COUNT; ++raw_bit) {
        if ((raw_rx & (1ULL << raw_bit)) != 0ULL) {
            const uint8_t logical_port = board_mapping_ir_rx_raw_bit_to_port(raw_bit);
            if (logical_port == UINT8_MAX) continue;
            logical_rx |= 1ULL << logical_port;
        }
    }
    return logical_rx;
}

static uint8_t baseboard_ir_popcount(uint64_t value)
{
    return (uint8_t)__builtin_popcountll(value);
}

static uint16_t baseboard_ir_count_link_pairs(const uint64_t *logical_rx)
{
    uint16_t link_pairs = 0;

    for (uint8_t first = 0; first < BASEBOARD_IR_PORT_COUNT; ++first) {
        for (uint8_t second = first + 1U; second < BASEBOARD_IR_PORT_COUNT; ++second) {
            const bool first_detects_second = (logical_rx[first] & (1ULL << second)) != 0ULL;
            const bool second_detects_first = (logical_rx[second] & (1ULL << first)) != 0ULL;

            if (first_detects_second || second_detects_first) {
                ++link_pairs;
            }
        }
    }
    return link_pairs;
}

static void baseboard_ir_log_link_pairs(const uint64_t *logical_rx, uint16_t link_pairs)
{
    ESP_LOGI(TAG, "raw cross-port links changed: %u", link_pairs);
    for (uint8_t first = 0; first < BASEBOARD_IR_PORT_COUNT; ++first) {
        for (uint8_t second = first + 1U; second < BASEBOARD_IR_PORT_COUNT; ++second) {
            const bool forward = (logical_rx[first] & (1ULL << second)) != 0ULL;
            const bool reverse = (logical_rx[second] & (1ULL << first)) != 0ULL;
            if (forward || reverse) {
                ESP_LOGI(TAG, "raw link P%u<->P%u forward=%u reverse=%u",
                         first + 1U, second + 1U, forward, reverse);
            }
        }
    }
}

esp_err_t baseboard_ir_init(void)
{
    const gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << IR_MOSI_GPIO) | (1ULL << IR_CLOCK_GPIO) |
                        (1ULL << IR_TX_LATCH_GPIO) | (1ULL << IR_RX_LOAD_GPIO) |
                        (1ULL << IR_OUTPUT_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t input_config = {
        .pin_bit_mask = 1ULL << IR_MISO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_config(&input_config);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(IR_OUTPUT_ENABLE_GPIO, 1);
    gpio_set_level(IR_MOSI_GPIO, 0);
    gpio_set_level(IR_CLOCK_GPIO, 0);
    gpio_set_level(IR_TX_LATCH_GPIO, 0);
    gpio_set_level(IR_RX_LOAD_GPIO, 1);
    baseboard_ir_disable_emitters();
    ESP_LOGI(TAG, "%u-port IR bus ready: slot=%u us settle=%u us samples=%u",
             (unsigned)BASEBOARD_IR_PORT_COUNT,
             IR_SLOT_PERIOD_US,
             IR_SETTLE_US,
             IR_SAMPLE_COUNT);
    return ESP_OK;
}

void baseboard_ir_set_input_task(TaskHandle_t task)
{
    s_input_task = task;
}

void baseboard_ir_set_ws2812_task(TaskHandle_t task)
{
    s_ws2812_task = task;
}

uint8_t baseboard_ir_get_latest_input_byte(void)
{
    return s_latest_input_byte;
}

void baseboard_ir_get_link_status(uint32_t *completed_scans, uint16_t *link_pairs)
{
    portENTER_CRITICAL(&s_matrix_lock);
    if (completed_scans != NULL) {
        *completed_scans = s_matrix.completed_scans;
    }
    if (link_pairs != NULL) {
        *link_pairs = s_matrix.link_pairs;
    }
    portEXIT_CRITICAL(&s_matrix_lock);
}

bool baseboard_ir_get_matrix(baseboard_ir_matrix_t *matrix)
{
    if (matrix == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_matrix_lock);
    *matrix = s_matrix;
    portEXIT_CRITICAL(&s_matrix_lock);
    return true;
}

void baseboard_ir_task(void *arg)
{
    uint8_t tx_port = 0;
    uint8_t input_divider = 0;
    uint32_t local_completed_scans = 0;
    uint32_t local_slot_overruns = 0;
    int64_t next_slot_us = esp_timer_get_time();
    int64_t previous_scan_finished_us = 0;
    bool was_play_active = false;
    uint16_t previous_link_pairs = UINT16_MAX;

    (void)arg;
    for (;;) {
        const bool play_active = play_mode_is_active();
        if (!play_active) {
            if (was_play_active) {
                baseboard_ir_disable_emitters();
                portENTER_CRITICAL(&s_matrix_lock);
                memset(&s_matrix, 0, sizeof(s_matrix));
                portEXIT_CRITICAL(&s_matrix_lock);
                tx_port = 0;
                input_divider = 0;
            }
            ir_snapshot_t samples[IR_SAMPLE_COUNT];
            for (uint8_t sample = 0; sample < IR_SAMPLE_COUNT; ++sample) {
                samples[sample] = baseboard_ir_read_165_snapshot();
            }
            s_latest_input_byte = (uint8_t)((samples[0].input_byte & samples[1].input_byte) |
                                            (samples[0].input_byte & samples[2].input_byte) |
                                            (samples[1].input_byte & samples[2].input_byte));
            if (s_input_task != NULL) xTaskNotifyGive(s_input_task);
            was_play_active = false;
            vTaskDelay(pdMS_TO_TICKS(IR_IDLE_INPUT_PERIOD_MS));
            continue;
        }
        if (!was_play_active) {
            tx_port = 0;
            input_divider = 0;
            local_completed_scans = 0;
            local_slot_overruns = 0;
            previous_scan_finished_us = 0;
            previous_link_pairs = UINT16_MAX;
            portENTER_CRITICAL(&s_matrix_lock);
            memset(&s_matrix, 0, sizeof(s_matrix));
            portEXIT_CRITICAL(&s_matrix_lock);
            next_slot_us = esp_timer_get_time();
            was_play_active = true;
        }

        ir_snapshot_t samples[IR_SAMPLE_COUNT];
        baseboard_ir_latch_595(1ULL << tx_port);
        esp_rom_delay_us(IR_SETTLE_US);
        for (uint8_t sample = 0; sample < IR_SAMPLE_COUNT; ++sample) {
            samples[sample] = baseboard_ir_read_165_snapshot();
        }
        /* The next latch raises OE before shifting the next emitter mask, so
         * avoid an extra 64-bit zero shift between adjacent port samples. */

        const uint8_t input_byte = (uint8_t)((samples[0].input_byte & samples[1].input_byte) |
                                             (samples[0].input_byte & samples[2].input_byte) |
                                             (samples[1].input_byte & samples[2].input_byte));
        const uint64_t raw_rx = (samples[0].raw_rx & samples[1].raw_rx) |
                                (samples[0].raw_rx & samples[2].raw_rx) |
                                (samples[1].raw_rx & samples[2].raw_rx);
        const uint64_t active_rx = IR_RX_ACTIVE_HIGH ? raw_rx : (~raw_rx & IR_RX_PORT_MASK);
        const uint64_t logical_rx = baseboard_ir_to_logical_ports(active_rx);

        s_latest_input_byte = input_byte;
        portENTER_CRITICAL(&s_matrix_lock);
        s_matrix.raw_rx[tx_port] = raw_rx;
        s_matrix.logical_rx[tx_port] = logical_rx;
        portEXIT_CRITICAL(&s_matrix_lock);

        if (++input_divider == IR_INPUT_DIVIDER) {
            input_divider = 0;
            if (s_input_task != NULL) {
                xTaskNotifyGive(s_input_task);
            }
        }

        ++tx_port;
        if (tx_port == BASEBOARD_IR_PORT_COUNT) {
            tx_port = 0;
            ++local_completed_scans;
            baseboard_ir_disable_emitters();
            const uint16_t link_pairs = baseboard_ir_count_link_pairs(s_matrix.logical_rx);
            portENTER_CRITICAL(&s_matrix_lock);
            s_matrix.completed_scans = local_completed_scans;
            s_matrix.slot_overruns = local_slot_overruns;
            s_matrix.link_pairs = link_pairs;
            portEXIT_CRITICAL(&s_matrix_lock);
            baseboard_ir_matrix_t published;
            (void)baseboard_ir_get_matrix(&published);
            board_snapshot_publish_ir(&published);
            if (s_ws2812_task != NULL) {
                xTaskNotifyGive(s_ws2812_task);
            }

            if (link_pairs != previous_link_pairs) {
                baseboard_ir_log_link_pairs(s_matrix.logical_rx, link_pairs);
                previous_link_pairs = link_pairs;
            }

            const int64_t scan_finished_us = esp_timer_get_time();
            const int64_t scan_period_us = previous_scan_finished_us == 0 ? 0 :
                                           scan_finished_us - previous_scan_finished_us;
            previous_scan_finished_us = scan_finished_us;
            if ((local_completed_scans % IR_LOG_INTERVAL_SCANS) == 0U) {
                uint16_t self_rx = 0;
                uint16_t cross_rx = 0;
                for (uint8_t port = 0; port < BASEBOARD_IR_PORT_COUNT; ++port) {
                    const uint64_t row = s_matrix.logical_rx[port];
                    self_rx += (row & (1ULL << port)) != 0ULL;
                    cross_rx += baseboard_ir_popcount(row & ~(1ULL << port));
                }
                const uint32_t hz_milli = scan_period_us > 0 ?
                                          (uint32_t)(1000000000LL / scan_period_us) : 0U;
                ESP_LOGI(TAG, "scan=%" PRIu32 " period=%" PRId64 "us hz=%u.%03u "
                              "links=%u self_rx=%u cross_rx=%u overruns=%" PRIu32,
                         local_completed_scans,
                         scan_period_us,
                         hz_milli / 1000U,
                         hz_milli % 1000U,
                         link_pairs,
                         self_rx,
                         cross_rx,
                         local_slot_overruns);
            }

            /* Yield once per full scan so the idle task can service the WDT. */
            vTaskDelay(pdMS_TO_TICKS(1));
            next_slot_us = esp_timer_get_time();
            continue;
        }

        next_slot_us += IR_SLOT_PERIOD_US;
        const int64_t now_us = esp_timer_get_time();
        if (now_us > next_slot_us) {
            ++local_slot_overruns;
            next_slot_us = now_us;
        } else {
            const int64_t wait_us = next_slot_us - now_us;
            if (wait_us > 1000) {
                vTaskDelay(pdMS_TO_TICKS((uint32_t)(wait_us / 1000)));
            }
            while (esp_timer_get_time() < next_slot_us) {
            }
        }

    }
}
