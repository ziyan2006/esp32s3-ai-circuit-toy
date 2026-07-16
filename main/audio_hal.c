#include "audio_hal.h"
#include "config.h"
#include "bsp/esp-box-3.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO_HAL";

static esp_codec_dev_handle_t s_speaker_dev = NULL;
static esp_codec_dev_handle_t s_mic_dev = NULL;
static RingbufHandle_t s_play_ringbuf = NULL;

#define AUDIO_PLAY_RINGBUF_SIZE (256 * 1024)

static void audio_play_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio play task started.");
    while (1) {
        size_t item_size = 0;
        uint8_t *data = (uint8_t *)xRingbufferReceive(s_play_ringbuf, &item_size, pdMS_TO_TICKS(100));
        if (data != NULL) {
            if (s_speaker_dev != NULL) {
                esp_err_t result = esp_codec_dev_write(s_speaker_dev, data, item_size);
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Speaker write failed: %s", esp_err_to_name(result));
                }
            }
            vRingbufferReturnItem(s_play_ringbuf, (void *)data);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t audio_hal_init(void)
{
    ESP_LOGI(TAG, "Initializing audio HAL via esp-box-3 BSP...");

    // 1. Initialize BSP audio (which sets up I2S with default settings)
    esp_err_t ret = bsp_audio_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Initialize speaker and microphone codec device handles
    s_speaker_dev = bsp_audio_codec_speaker_init();
    if (s_speaker_dev == NULL) {
        ESP_LOGE(TAG, "bsp_audio_codec_speaker_init failed");
        return ESP_FAIL;
    }

    s_mic_dev = bsp_audio_codec_microphone_init();
    if (s_mic_dev == NULL) {
        ESP_LOGE(TAG, "bsp_audio_codec_microphone_init failed");
        return ESP_FAIL;
    }

    // 3. Configure audio format: 16kHz, 16bit, mono (1 channel)
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };

    ret = esp_codec_dev_open(s_speaker_dev, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open speaker dev: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_codec_dev_open(s_mic_dev, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open microphone dev: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set initial output volume
    esp_codec_dev_set_out_vol(s_speaker_dev, 70);

    // 4. Initialize Playback RingBuffer and Task
    s_play_ringbuf = xRingbufferCreateWithCaps(
        AUDIO_PLAY_RINGBUF_SIZE,
        RINGBUF_TYPE_NOSPLIT,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_play_ringbuf == NULL) {
        ESP_LOGE(TAG, "Failed to create %u-byte playback ringbuffer in PSRAM",
                 (unsigned int)AUDIO_PLAY_RINGBUF_SIZE);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Playback ringbuffer allocated in PSRAM: %u bytes",
             (unsigned int)AUDIO_PLAY_RINGBUF_SIZE);

    xTaskCreatePinnedToCore(audio_play_task, "audio_play_task", 4096, NULL, 5, NULL, 1);

    return ESP_OK;
}

esp_err_t audio_record_frame(uint8_t *buffer, size_t size, size_t *bytes_read, TickType_t timeout)
{
    if (s_mic_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = esp_codec_dev_read(s_mic_dev, buffer, size);
    if (ret == ESP_OK) {
        *bytes_read = size;
    } else {
        *bytes_read = 0;
    }
    return ret;
}

esp_err_t audio_play_queue_push(const uint8_t *buffer, size_t size)
{
    if (s_play_ringbuf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ret = xRingbufferSend(s_play_ringbuf, (void *)buffer, size, pdMS_TO_TICKS(500));
    return (ret == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t audio_hal_set_volume(int volume)
{
    if (s_speaker_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_vol(s_speaker_dev, volume);
}
