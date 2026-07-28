#include "audio_self_test.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "bsp/esp32_p4_function_ev_board.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#define AUDIO_SAMPLE_RATE_HZ       16000U
#define AUDIO_TEST_TONE_HZ         1000U
#define AUDIO_BLOCK_SAMPLES        256U
#define AUDIO_EFFECT_OUTPUT_VOLUME 70
#define AUDIO_VOICE_OUTPUT_VOLUME  100
#define AUDIO_INPUT_GAIN_DB        24.0f
#define AUDIO_DETECT_PERIOD_BLOCKS 32U
#define AUDIO_UI_UPDATE_PERIOD_BLOCKS 4U
#define AUDIO_MIN_RMS              120.0f
#define AUDIO_MIN_TONE_RATIO       0.12f
#define AUDIO_EFFECT_QUEUE_LENGTH  1U
#define AUDIO_PHASE_STEP_PER_HZ    268435U /* 2^32 / 16000 Hz */
#define AUDIO_VOICE_CAPTURE_BUFFER_SIZE (256U * 1024U)
#define AUDIO_VOICE_PLAY_BUFFER_SIZE    (256U * 1024U)
#define AUDIO_VOICE_PLAY_PREBUFFER_BYTES (96U * 1024U) /* 3.07 s at 16 kHz mono PCM16 */
#define AUDIO_VOICE_PLAY_REBUFFER_BYTES  8192U /* 256 ms after an underrun */
#define AUDIO_VOICE_PLAY_WRITE_BLOCK_BYTES 1024U
#define AUDIO_SPEAKER_TASK_PRIORITY 8U
#define AUDIO_MIC_TASK_PRIORITY     5U

static const char *TAG = "audio_test";

static const int16_t s_sine_cycle[16] = {
    0, 3827, 7071, 9239, 10000, 9239, 7071, 3827,
    0, -3827, -7071, -9239, -10000, -9239, -7071, -3827,
};

static esp_codec_dev_handle_t s_speaker;
static esp_codec_dev_handle_t s_microphone;
static int16_t s_tone_block[AUDIO_BLOCK_SAMPLES];
static int16_t s_silence_block[AUDIO_BLOCK_SAMPLES];
static volatile bool s_tone_enabled;
static volatile float s_latest_rms;
static volatile float s_latest_tone_ratio;
static QueueHandle_t s_effect_queue;
static RingbufHandle_t s_voice_capture_ring;
static RingbufHandle_t s_voice_play_ring;
static volatile bool s_voice_capture_enabled;
static volatile bool s_voice_playback_active;
static volatile bool s_voice_playback_finishing;
static volatile bool s_voice_playback_ready;
static volatile bool s_voice_playback_started;
static volatile uint8_t s_master_volume = AUDIO_EFFECT_OUTPUT_VOLUME;
static size_t s_voice_playback_queued_bytes;
static uint8_t s_voice_playback_push_block[AUDIO_VOICE_PLAY_WRITE_BLOCK_BYTES];
static size_t s_voice_playback_push_fill;
static portMUX_TYPE s_voice_playback_lock = portMUX_INITIALIZER_UNLOCKED;
static audio_voice_playback_stats_t s_voice_playback_stats;
static int64_t s_voice_first_enqueue_us;
static int64_t s_voice_first_write_started_us;
static int64_t s_voice_last_write_finished_us;
static int64_t s_voice_last_successful_write_us;
static BaseType_t s_speaker_task_affinity = tskNO_AFFINITY;

static uint8_t audio_effect_output_volume(void)
{
    return s_master_volume;
}

static uint8_t audio_voice_output_volume(void)
{
    if (s_master_volume == 0U) {
        return 0U;
    }
    const uint32_t scaled = ((uint32_t)s_master_volume * AUDIO_VOICE_OUTPUT_VOLUME +
                             (AUDIO_EFFECT_OUTPUT_VOLUME / 2U)) /
                            AUDIO_EFFECT_OUTPUT_VOLUME;
    return (uint8_t)(scaled > AUDIO_VOICE_OUTPUT_VOLUME ? AUDIO_VOICE_OUTPUT_VOLUME : scaled);
}

typedef struct {
    uint16_t duration_ms;
    uint16_t start_hz;
    uint16_t end_hz;
    uint16_t peak;
} audio_effect_segment_t;

typedef struct {
    const audio_effect_segment_t *segments;
    uint8_t segment_count;
} audio_effect_pattern_t;

static const audio_effect_segment_t s_select_effect[] = {{55, 900, 1120, 3200}};
static const audio_effect_segment_t s_confirm_effect[] = {
    {60, 680, 880, 4600}, {85, 1040, 1360, 5000},
};
static const audio_effect_segment_t s_back_effect[] = {
    {65, 800, 610, 3600}, {75, 500, 330, 3000},
};
static const audio_effect_segment_t s_error_effect[] = {
    {105, 240, 170, 5000}, {75, 180, 120, 4200},
};
static const audio_effect_segment_t s_shoot_effect[] = {{48, 1550, 820, 4300}};
static const audio_effect_segment_t s_destroy_effect[] = {
    {58, 520, 280, 5600}, {55, 270, 145, 4100},
};
static const audio_effect_segment_t s_player_hit_effect[] = {
    {105, 210, 115, 6400}, {85, 130, 82, 4800},
};
static const audio_effect_segment_t s_power_effect[] = {
    {75, 450, 760, 4500}, {75, 820, 1220, 5000}, {105, 1320, 1780, 5200},
};
static const audio_effect_segment_t s_win_effect[] = {
    {120, 523, 523, 4800}, {120, 659, 659, 5000}, {190, 784, 784, 5400},
};
static const audio_effect_segment_t s_lose_effect[] = {
    {140, 440, 330, 5000}, {140, 294, 220, 4500}, {180, 196, 130, 4200},
};

static audio_effect_pattern_t effect_pattern(audio_effect_t effect)
{
    switch (effect) {
    case AUDIO_EFFECT_SELECT: return (audio_effect_pattern_t) {
        s_select_effect, (uint8_t)(sizeof(s_select_effect) / sizeof(s_select_effect[0]))};
    case AUDIO_EFFECT_CONFIRM: return (audio_effect_pattern_t) {
        s_confirm_effect, (uint8_t)(sizeof(s_confirm_effect) / sizeof(s_confirm_effect[0]))};
    case AUDIO_EFFECT_BACK: return (audio_effect_pattern_t) {
        s_back_effect, (uint8_t)(sizeof(s_back_effect) / sizeof(s_back_effect[0]))};
    case AUDIO_EFFECT_ERROR: return (audio_effect_pattern_t) {
        s_error_effect, (uint8_t)(sizeof(s_error_effect) / sizeof(s_error_effect[0]))};
    case AUDIO_EFFECT_SHOOT: return (audio_effect_pattern_t) {
        s_shoot_effect, (uint8_t)(sizeof(s_shoot_effect) / sizeof(s_shoot_effect[0]))};
    case AUDIO_EFFECT_ENEMY_DESTROYED: return (audio_effect_pattern_t) {
        s_destroy_effect, (uint8_t)(sizeof(s_destroy_effect) / sizeof(s_destroy_effect[0]))};
    case AUDIO_EFFECT_PLAYER_HIT: return (audio_effect_pattern_t) {
        s_player_hit_effect, (uint8_t)(sizeof(s_player_hit_effect) / sizeof(s_player_hit_effect[0]))};
    case AUDIO_EFFECT_POWER: return (audio_effect_pattern_t) {
        s_power_effect, (uint8_t)(sizeof(s_power_effect) / sizeof(s_power_effect[0]))};
    case AUDIO_EFFECT_WIN: return (audio_effect_pattern_t) {
        s_win_effect, (uint8_t)(sizeof(s_win_effect) / sizeof(s_win_effect[0]))};
    case AUDIO_EFFECT_LOSE: return (audio_effect_pattern_t) {
        s_lose_effect, (uint8_t)(sizeof(s_lose_effect) / sizeof(s_lose_effect[0]))};
    default: return (audio_effect_pattern_t) {NULL, 0};
    }
}

static void log_optional_codec_control(const char *operation, int result)
{
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "%s unavailable: %s", operation, esp_err_to_name(result));
    }
}

static void fill_tone_block(void)
{
    for (uint16_t index = 0; index < AUDIO_BLOCK_SAMPLES; ++index) {
        s_tone_block[index] = s_sine_cycle[index % 16U];
    }
}

typedef struct {
    audio_effect_pattern_t pattern;
    uint8_t segment_index;
    uint32_t segment_sample;
    uint32_t segment_samples;
    uint32_t phase;
} audio_effect_player_t;

static void effect_player_start(audio_effect_player_t *player, audio_effect_t effect)
{
    player->pattern = effect_pattern(effect);
    player->segment_index = 0U;
    player->segment_sample = 0U;
    player->segment_samples = 0U;
    player->phase = 0U;
    if (player->pattern.segments != NULL && player->pattern.segment_count > 0U) {
        player->segment_samples = (uint32_t)player->pattern.segments[0].duration_ms *
                                  AUDIO_SAMPLE_RATE_HZ / 1000U;
    }
}

static bool effect_player_advance(audio_effect_player_t *player)
{
    ++player->segment_index;
    player->segment_sample = 0U;
    if (player->pattern.segments == NULL ||
        player->segment_index >= player->pattern.segment_count) {
        player->segment_samples = 0U;
        return false;
    }
    player->segment_samples = (uint32_t)player->pattern.segments[player->segment_index].duration_ms *
                              AUDIO_SAMPLE_RATE_HZ / 1000U;
    return true;
}

static int16_t effect_player_sample(audio_effect_player_t *player)
{
    const audio_effect_segment_t *segment;
    uint32_t frequency;
    uint32_t envelope = 1024U;
    int32_t sample;

    while (player->pattern.segments != NULL &&
           player->segment_index < player->pattern.segment_count &&
           player->segment_sample >= player->segment_samples) {
        if (!effect_player_advance(player)) return 0;
    }
    if (player->pattern.segments == NULL ||
        player->segment_index >= player->pattern.segment_count) {
        return 0;
    }

    segment = &player->pattern.segments[player->segment_index];
    if (segment->end_hz == segment->start_hz || player->segment_samples <= 1U) {
        frequency = segment->start_hz;
    } else {
        frequency = (uint32_t)((int32_t)segment->start_hz +
            ((int32_t)segment->end_hz - (int32_t)segment->start_hz) *
            (int32_t)player->segment_sample / (int32_t)player->segment_samples);
    }
    if (player->segment_sample < 24U) {
        envelope = player->segment_sample * 1024U / 24U;
    } else if (player->segment_samples > 40U &&
               player->segment_sample + 40U > player->segment_samples) {
        envelope = (player->segment_samples - player->segment_sample) * 1024U / 40U;
    }
    if (frequency > 7900U) frequency = 7900U;
    player->phase += frequency * AUDIO_PHASE_STEP_PER_HZ;
    sample = (int32_t)s_sine_cycle[player->phase >> 28] * segment->peak / 10000;
    sample = sample * (int32_t)envelope / 1024;
    ++player->segment_sample;
    return (int16_t)sample;
}

static void drain_ringbuffer(RingbufHandle_t ring)
{
    if (ring == NULL) return;
    for (;;) {
        size_t item_size = 0U;
        void *item = xRingbufferReceive(ring, &item_size, 0);
        if (item == NULL) return;
        vRingbufferReturnItem(ring, item);
    }
}

static void playback_queue_reset(void)
{
    portENTER_CRITICAL(&s_voice_playback_lock);
    s_voice_playback_queued_bytes = 0U;
    s_voice_playback_stats.queued_bytes = 0U;
    s_voice_playback_ready = false;
    s_voice_playback_started = false;
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

static void playback_stats_reset(void)
{
    portENTER_CRITICAL(&s_voice_playback_lock);
    memset(&s_voice_playback_stats, 0, sizeof(s_voice_playback_stats));
    s_voice_playback_stats.task_affinity_core =
        s_speaker_task_affinity == tskNO_AFFINITY ? -1 : (int32_t)s_speaker_task_affinity;
    s_voice_first_enqueue_us = 0;
    s_voice_first_write_started_us = 0;
    s_voice_last_write_finished_us = 0;
    s_voice_last_successful_write_us = 0;
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

static void playback_queue_add(size_t size)
{
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_voice_playback_lock);
    s_voice_playback_queued_bytes += size;
    s_voice_playback_stats.queued_bytes = (uint32_t)s_voice_playback_queued_bytes;
    if (s_voice_playback_stats.queued_bytes > s_voice_playback_stats.peak_queued_bytes) {
        s_voice_playback_stats.peak_queued_bytes = s_voice_playback_stats.queued_bytes;
    }
    ++s_voice_playback_stats.push_items;
    s_voice_playback_stats.pushed_bytes += size;
    if (s_voice_first_enqueue_us == 0) {
        s_voice_first_enqueue_us = now_us;
    }
    const size_t ready_bytes = s_voice_playback_started
                                   ? AUDIO_VOICE_PLAY_REBUFFER_BYTES
                                   : AUDIO_VOICE_PLAY_PREBUFFER_BYTES;
    if (s_voice_playback_queued_bytes >= ready_bytes) {
        s_voice_playback_ready = true;
    }
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

static void playback_queue_remove(size_t size)
{
    portENTER_CRITICAL(&s_voice_playback_lock);
    if (size >= s_voice_playback_queued_bytes) {
        s_voice_playback_queued_bytes = 0U;
    } else {
        s_voice_playback_queued_bytes -= size;
    }
    s_voice_playback_stats.queued_bytes = (uint32_t)s_voice_playback_queued_bytes;
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

static esp_err_t playback_flush_push_block(TickType_t timeout)
{
    if (s_voice_playback_push_fill == 0U) return ESP_OK;
    if (xRingbufferSend(s_voice_play_ring, s_voice_playback_push_block,
                        s_voice_playback_push_fill, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    playback_queue_add(s_voice_playback_push_fill);
    s_voice_playback_push_fill = 0U;
    return ESP_OK;
}

static void playback_force_ready(void)
{
    portENTER_CRITICAL(&s_voice_playback_lock);
    s_voice_playback_ready = s_voice_playback_queued_bytes > 0U;
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

static void playback_pause_for_rebuffer(void)
{
    portENTER_CRITICAL(&s_voice_playback_lock);
    s_voice_playback_ready = false;
    ++s_voice_playback_stats.underrun_count;
    ++s_voice_playback_stats.rebuffer_count;
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

static void playback_record_write(size_t size, int result,
                                  int64_t write_started_us, int64_t write_finished_us)
{
    const uint64_t duration_us = (uint64_t)(write_finished_us - write_started_us);
    const BaseType_t core_id = xPortGetCoreID();

    portENTER_CRITICAL(&s_voice_playback_lock);
    s_voice_playback_stats.total_write_duration_us += duration_us;
    if (duration_us > s_voice_playback_stats.max_write_duration_us) {
        s_voice_playback_stats.max_write_duration_us = duration_us;
    }
    if (result == ESP_CODEC_DEV_OK) {
        if (s_voice_playback_stats.write_items == 0U) {
            s_voice_first_write_started_us = write_started_us;
            if (s_voice_first_enqueue_us != 0) {
                s_voice_playback_stats.first_write_delay_us =
                    (uint64_t)(write_started_us - s_voice_first_enqueue_us);
            }
        }
        s_voice_last_write_finished_us = write_finished_us;
        s_voice_playback_stats.playback_write_span_us =
            (uint64_t)(s_voice_last_write_finished_us - s_voice_first_write_started_us);
        if (s_voice_last_successful_write_us != 0) {
            const uint64_t interval_us =
                (uint64_t)(write_started_us - s_voice_last_successful_write_us);
            if (interval_us > s_voice_playback_stats.max_successful_write_interval_us) {
                s_voice_playback_stats.max_successful_write_interval_us = interval_us;
            }
        }
        s_voice_last_successful_write_us = write_started_us;
        ++s_voice_playback_stats.write_items;
        s_voice_playback_stats.written_bytes += size;
        if (s_voice_playback_stats.min_write_size == 0U ||
            size < s_voice_playback_stats.min_write_size) {
            s_voice_playback_stats.min_write_size = (uint32_t)size;
        }
        if (size > s_voice_playback_stats.max_write_size) {
            s_voice_playback_stats.max_write_size = (uint32_t)size;
        }
        if (core_id >= 0 && core_id < 32) {
            s_voice_playback_stats.cores_used_mask |= 1UL << (uint32_t)core_id;
        }
    } else {
        ++s_voice_playback_stats.write_errors;
    }
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

void audio_self_test_voice_playback_get_stats(audio_voice_playback_stats_t *stats)
{
    if (stats == NULL) return;
    portENTER_CRITICAL(&s_voice_playback_lock);
    *stats = s_voice_playback_stats;
    portEXIT_CRITICAL(&s_voice_playback_lock);
}

static void playback_log_summary(void)
{
    audio_voice_playback_stats_t stats;
    audio_self_test_voice_playback_get_stats(&stats);
    const uint64_t expected_us = stats.written_bytes * 1000000ULL /
                                 (AUDIO_SAMPLE_RATE_HZ * sizeof(int16_t));
    const int64_t overhead_us = (int64_t)stats.playback_write_span_us -
                                (int64_t)expected_us;
    ESP_LOGI(TAG,
             "voice playback summary: queued=%lu peak=%lu push=%lu/%lluB "
             "write=%lu/%lluB errors=%lu first_delay=%lluus underrun=%lu rebuffer=%lu "
             "max_write=%lluus max_interval=%lluus size=%lu..%lu affinity=%ld cores=0x%lx",
             (unsigned long)stats.queued_bytes,
             (unsigned long)stats.peak_queued_bytes,
             (unsigned long)stats.push_items,
             (unsigned long long)stats.pushed_bytes,
             (unsigned long)stats.write_items,
             (unsigned long long)stats.written_bytes,
             (unsigned long)stats.write_errors,
             (unsigned long long)stats.first_write_delay_us,
             (unsigned long)stats.underrun_count,
             (unsigned long)stats.rebuffer_count,
             (unsigned long long)stats.max_write_duration_us,
             (unsigned long long)stats.max_successful_write_interval_us,
             (unsigned long)stats.min_write_size,
             (unsigned long)stats.max_write_size,
             (long)stats.task_affinity_core,
             (unsigned long)stats.cores_used_mask);
    ESP_LOGI(TAG,
             "voice playback timing: total_write=%lluus span=%lluus expected=%lluus overhead=%lldus",
             (unsigned long long)stats.total_write_duration_us,
             (unsigned long long)stats.playback_write_span_us,
             (unsigned long long)expected_us,
             (long long)overhead_us);
}

static void speaker_task(void *arg)
{
    audio_effect_t requested;
    audio_effect_player_t effect_player = {0};
    int output_volume = -1;

    (void)arg;
    s_speaker_task_affinity = xTaskGetCoreID(NULL);
    ESP_LOGI(TAG, "speaker task priority=%u affinity=%ld",
             (unsigned)uxTaskPriorityGet(NULL),
             s_speaker_task_affinity == tskNO_AFFINITY
                 ? -1L : (long)s_speaker_task_affinity);

    for (;;) {
        const bool use_voice_volume = s_voice_playback_active;
        const uint8_t requested_volume = use_voice_volume ?
            audio_voice_output_volume() : audio_effect_output_volume();
        if (output_volume != requested_volume) {
            log_optional_codec_control(
                use_voice_volume ? "speaker voice volume" : "speaker effect volume",
                esp_codec_dev_set_out_vol(s_speaker, requested_volume));
            output_volume = requested_volume;
        }
        if (xQueueReceive(s_effect_queue, &requested, 0) == pdPASS) {
            effect_player_start(&effect_player, requested);
        }

        int result;
        if (s_voice_playback_active &&
            (s_voice_playback_ready || s_voice_playback_finishing)) {
            size_t voice_size = 0U;
            void *voice = xRingbufferReceive(s_voice_play_ring, &voice_size, 0);
            if (voice != NULL) {
                s_voice_playback_started = true;
                const int64_t write_started_us = esp_timer_get_time();
                result = esp_codec_dev_write(s_speaker, voice, voice_size);
                const int64_t write_finished_us = esp_timer_get_time();
                playback_record_write(voice_size, result, write_started_us, write_finished_us);
                vRingbufferReturnItem(s_voice_play_ring, voice);
                playback_queue_remove(voice_size);
            } else {
                if (s_voice_playback_finishing) {
                    s_voice_playback_finishing = false;
                    s_voice_playback_active = false;
                    playback_queue_reset();
                    playback_log_summary();
                } else {
                    playback_pause_for_rebuffer();
                }
                result = esp_codec_dev_write(s_speaker, s_silence_block, sizeof(s_silence_block));
            }
        } else if (s_voice_playback_active) {
            result = esp_codec_dev_write(s_speaker, s_silence_block, sizeof(s_silence_block));
        } else if (s_tone_enabled) {
            while (xQueueReceive(s_effect_queue, &requested, 0) == pdPASS) {
            }
            effect_player.pattern = (audio_effect_pattern_t) {NULL, 0};
            result = esp_codec_dev_write(s_speaker, s_tone_block, sizeof(s_tone_block));
        } else if (effect_player.pattern.segments != NULL) {
            int16_t effect_block[AUDIO_BLOCK_SAMPLES];
            for (uint16_t index = 0; index < AUDIO_BLOCK_SAMPLES; ++index) {
                effect_block[index] = effect_player_sample(&effect_player);
            }
            result = esp_codec_dev_write(s_speaker, effect_block, sizeof(effect_block));
        } else {
            result = esp_codec_dev_write(s_speaker, s_silence_block, sizeof(s_silence_block));
        }
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "speaker write failed: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static void microphone_task(void *arg)
{
    int16_t capture[AUDIO_BLOCK_SAMPLES];
    uint32_t block_count = 0;

    (void)arg;
    for (;;) {
        const int result = esp_codec_dev_read(s_microphone, capture, sizeof(capture));
        if (result != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "microphone read failed: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (s_voice_capture_enabled && s_voice_capture_ring != NULL &&
            xRingbufferSend(s_voice_capture_ring, capture, sizeof(capture), 0) != pdTRUE) {
            ESP_LOGW(TAG, "voice capture buffer full; dropping %u-byte PCM frame",
                     (unsigned)sizeof(capture));
        }

        ++block_count;
        if (block_count % AUDIO_UI_UPDATE_PERIOD_BLOCKS != 0U) {
            continue;
        }

        uint64_t sample_energy = 0;
        int64_t in_phase = 0;
        int64_t quadrature = 0;
        uint64_t reference_energy = 0;
        for (uint16_t index = 0; index < AUDIO_BLOCK_SAMPLES; ++index) {
            const int32_t sample = capture[index];
            const int32_t sine = s_sine_cycle[index % 16U];
            const int32_t cosine = s_sine_cycle[(index + 4U) % 16U];

            sample_energy += (uint64_t)((int64_t)sample * sample);
            reference_energy += (uint64_t)((int64_t)sine * sine);
            in_phase += (int64_t)sample * sine;
            quadrature += (int64_t)sample * cosine;
        }

        const float rms = sqrtf((float)sample_energy / AUDIO_BLOCK_SAMPLES);
        const float tone_ratio = sample_energy == 0U ? 0.0f :
            (float)((double)in_phase * in_phase + (double)quadrature * quadrature) /
            ((float)sample_energy * reference_energy);
        const bool detected = rms >= AUDIO_MIN_RMS && tone_ratio >= AUDIO_MIN_TONE_RATIO;

        s_latest_rms = rms;
        s_latest_tone_ratio = tone_ratio;

        if (block_count % AUDIO_DETECT_PERIOD_BLOCKS == 0U) {
            ESP_LOGI(TAG, "mic rms=%.1f tone_%uHz=%.3f %s", rms, AUDIO_TEST_TONE_HZ,
                     tone_ratio, detected ? "DETECTED" : "NOT_DETECTED");
        }
    }
}

esp_err_t audio_self_test_init(void)
{
    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0x01,
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .mclk_multiple = 0,
    };

    s_effect_queue = xQueueCreate(AUDIO_EFFECT_QUEUE_LENGTH, sizeof(audio_effect_t));
    s_voice_capture_ring = xRingbufferCreateWithCaps(AUDIO_VOICE_CAPTURE_BUFFER_SIZE,
                                                     RINGBUF_TYPE_NOSPLIT,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_voice_play_ring = xRingbufferCreateWithCaps(AUDIO_VOICE_PLAY_BUFFER_SIZE,
                                                  RINGBUF_TYPE_NOSPLIT,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_effect_queue == NULL || s_voice_capture_ring == NULL || s_voice_play_ring == NULL) {
        return ESP_ERR_NO_MEM;
    }
    fill_tone_block();
    s_tone_enabled = false;
    s_latest_rms = 0.0f;
    s_latest_tone_ratio = 0.0f;
    s_voice_capture_enabled = false;
    s_voice_playback_active = false;
    s_voice_playback_finishing = false;
    playback_queue_reset();
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "initialize ES8311 control I2C bus");
    ESP_RETURN_ON_ERROR(bsp_audio_init(&i2s_config), TAG, "initialize 16 kHz I2S duplex bus");
    s_speaker = bsp_audio_codec_speaker_init();
    s_microphone = bsp_audio_codec_microphone_init();
    if (s_speaker == NULL || s_microphone == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (esp_codec_dev_open(s_speaker, &sample_info) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(s_microphone, &sample_info) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    log_optional_codec_control("speaker effect volume",
                               esp_codec_dev_set_out_vol(s_speaker,
                                                       audio_effect_output_volume()));
    log_optional_codec_control("speaker unmute", esp_codec_dev_set_out_mute(s_speaker, false));
    log_optional_codec_control("microphone gain", esp_codec_dev_set_in_gain(s_microphone, AUDIO_INPUT_GAIN_DB));
    if (xTaskCreate(speaker_task, "audio_tone", 3072, NULL,
                    AUDIO_SPEAKER_TASK_PRIORITY, NULL) != pdPASS ||
        xTaskCreate(microphone_task, "audio_mic", 4096, NULL,
                    AUDIO_MIC_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "audio ready at %u Hz; cloud capture/playback buffers=%u/%u bytes in PSRAM; "
             "voice write block=%u bytes",
             AUDIO_SAMPLE_RATE_HZ, AUDIO_VOICE_CAPTURE_BUFFER_SIZE,
             AUDIO_VOICE_PLAY_BUFFER_SIZE, AUDIO_VOICE_PLAY_WRITE_BLOCK_BYTES);
    return ESP_OK;
}

void audio_self_test_set_tone_enabled(bool enabled)
{
    s_tone_enabled = enabled;
}

void audio_self_test_set_master_volume(uint8_t volume_percent)
{
    s_master_volume = volume_percent > 100U ? 100U : volume_percent;
}

uint8_t audio_self_test_get_master_volume(void)
{
    return s_master_volume;
}

void audio_self_test_play_effect(audio_effect_t effect)
{
    if (s_effect_queue == NULL || effect >= AUDIO_EFFECT_COUNT ||
        s_voice_capture_enabled || s_voice_playback_active) {
        return;
    }
    (void)xQueueOverwrite(s_effect_queue, &effect);
}

void audio_self_test_get_microphone_level(float *out_rms, float *out_tone_ratio)
{
    if (out_rms != NULL) {
        *out_rms = s_latest_rms;
    }
    if (out_tone_ratio != NULL) {
        *out_tone_ratio = s_latest_tone_ratio;
    }
}

esp_err_t audio_self_test_voice_capture_begin(void)
{
    if (s_voice_capture_ring == NULL) return ESP_ERR_INVALID_STATE;
    s_voice_capture_enabled = false;
    drain_ringbuffer(s_voice_capture_ring);
    s_voice_capture_enabled = true;
    return ESP_OK;
}

void audio_self_test_voice_capture_end(void)
{
    s_voice_capture_enabled = false;
}

esp_err_t audio_self_test_voice_capture_read(uint8_t *buffer,
                                             size_t capacity,
                                             size_t *bytes_read,
                                             TickType_t timeout)
{
    if (buffer == NULL || bytes_read == NULL || capacity == 0U) return ESP_ERR_INVALID_ARG;
    if (s_voice_capture_ring == NULL) return ESP_ERR_INVALID_STATE;
    *bytes_read = 0U;
    size_t item_size = 0U;
    void *item = xRingbufferReceive(s_voice_capture_ring, &item_size, timeout);
    if (item == NULL) return ESP_ERR_TIMEOUT;
    if (item_size > capacity) {
        vRingbufferReturnItem(s_voice_capture_ring, item);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(buffer, item, item_size);
    vRingbufferReturnItem(s_voice_capture_ring, item);
    *bytes_read = item_size;
    return ESP_OK;
}

esp_err_t audio_self_test_voice_playback_begin(void)
{
    if (s_voice_play_ring == NULL) return ESP_ERR_INVALID_STATE;
    s_voice_playback_active = false;
    s_voice_playback_finishing = false;
    drain_ringbuffer(s_voice_play_ring);
    s_voice_playback_push_fill = 0U;
    playback_queue_reset();
    playback_stats_reset();
    s_voice_playback_active = true;
    return ESP_OK;
}

esp_err_t audio_self_test_voice_playback_push(const uint8_t *buffer,
                                              size_t size,
                                              TickType_t timeout)
{
    if (buffer == NULL || size == 0U) return ESP_ERR_INVALID_ARG;
    if (s_voice_play_ring == NULL) return ESP_ERR_INVALID_STATE;
    while (size > 0U) {
        if (s_voice_playback_push_fill == AUDIO_VOICE_PLAY_WRITE_BLOCK_BYTES) {
            ESP_RETURN_ON_ERROR(playback_flush_push_block(timeout), TAG,
                                "enqueue full voice playback block");
        }
        const size_t available = AUDIO_VOICE_PLAY_WRITE_BLOCK_BYTES -
                                 s_voice_playback_push_fill;
        const size_t copy_size = size < available ? size : available;
        memcpy(s_voice_playback_push_block + s_voice_playback_push_fill,
               buffer, copy_size);
        s_voice_playback_push_fill += copy_size;
        buffer += copy_size;
        size -= copy_size;
        if (s_voice_playback_push_fill == AUDIO_VOICE_PLAY_WRITE_BLOCK_BYTES) {
            ESP_RETURN_ON_ERROR(playback_flush_push_block(timeout), TAG,
                                "enqueue full voice playback block");
        }
    }
    return ESP_OK;
}

void audio_self_test_voice_playback_finish(void)
{
    if (s_voice_playback_active) {
        if (playback_flush_push_block(0) != ESP_OK) {
            ESP_LOGW(TAG, "voice playback buffer full while flushing final %u-byte block",
                     (unsigned)s_voice_playback_push_fill);
        }
        s_voice_playback_finishing = true;
        playback_force_ready();
    }
}

void audio_self_test_voice_playback_abort(void)
{
    s_voice_playback_active = false;
    s_voice_playback_finishing = false;
    s_voice_playback_push_fill = 0U;
    drain_ringbuffer(s_voice_play_ring);
    playback_queue_reset();
}

bool audio_self_test_voice_playback_is_active(void)
{
    return s_voice_playback_active;
}
