#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t audio_self_test_init(void);
void audio_self_test_set_tone_enabled(bool enabled);
void audio_self_test_get_microphone_level(float *out_rms, float *out_tone_ratio);
void audio_self_test_set_master_volume(uint8_t volume_percent);
uint8_t audio_self_test_get_master_volume(void);

/* Cloud voice uses the existing microphone reader and speaker writer, so the codec
 * is never accessed concurrently from a second task. */
esp_err_t audio_self_test_voice_capture_begin(void);
void audio_self_test_voice_capture_end(void);
esp_err_t audio_self_test_voice_capture_read(uint8_t *buffer,
                                             size_t capacity,
                                             size_t *bytes_read,
                                             TickType_t timeout);
esp_err_t audio_self_test_voice_playback_begin(void);
esp_err_t audio_self_test_voice_playback_push(const uint8_t *buffer,
                                              size_t size,
                                              TickType_t timeout);
void audio_self_test_voice_playback_finish(void);
void audio_self_test_voice_playback_abort(void);
bool audio_self_test_voice_playback_is_active(void);

typedef struct {
    uint32_t queued_bytes;
    uint32_t peak_queued_bytes;
    uint32_t push_items;
    uint64_t pushed_bytes;
    uint32_t write_items;
    uint64_t written_bytes;
    uint32_t write_errors;
    uint64_t first_write_delay_us;
    uint32_t underrun_count;
    uint32_t rebuffer_count;
    uint64_t total_write_duration_us;
    uint64_t playback_write_span_us;
    uint64_t max_write_duration_us;
    uint64_t max_successful_write_interval_us;
    uint32_t min_write_size;
    uint32_t max_write_size;
    int32_t task_affinity_core;
    uint32_t cores_used_mask;
} audio_voice_playback_stats_t;

void audio_self_test_voice_playback_get_stats(audio_voice_playback_stats_t *stats);

typedef enum {
    AUDIO_EFFECT_SELECT = 0,
    AUDIO_EFFECT_CONFIRM,
    AUDIO_EFFECT_BACK,
    AUDIO_EFFECT_ERROR,
    AUDIO_EFFECT_SHOOT,
    AUDIO_EFFECT_ENEMY_DESTROYED,
    AUDIO_EFFECT_PLAYER_HIT,
    AUDIO_EFFECT_POWER,
    AUDIO_EFFECT_WIN,
    AUDIO_EFFECT_LOSE,
    AUDIO_EFFECT_COUNT,
} audio_effect_t;

void audio_self_test_play_effect(audio_effect_t effect);
