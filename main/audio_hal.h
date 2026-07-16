#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * @brief Initialize I2C, configure the ES8311 audio codec, and set up I2S.
 * 
 * @return esp_err_t ESP_OK on success, appropriate error code on failure.
 */
esp_err_t audio_hal_init(void);

/**
 * @brief Set output speaker volume.
 * 
 * @param vol_percent Volume level from 0 to 100.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t audio_hal_set_volume(int vol_percent);

/**
 * @brief Read recorded audio frame from the microphone.
 * 
 * @param buffer Buffer to store recorded audio (PCM).
 * @param size Size of the buffer in bytes.
 * @param bytes_read Number of bytes successfully read.
 * @param timeout TickType_t timeout for reading.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t audio_record_frame(uint8_t *buffer, size_t size, size_t *bytes_read, TickType_t timeout);

/**
 * @brief Write audio frame to be played on the speaker.
 * 
 * @param buffer Buffer containing the audio data (PCM).
 * @param size Size of the buffer in bytes.
 * @param bytes_written Number of bytes successfully written.
 * @param timeout TickType_t timeout for writing.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t audio_play_frame(const uint8_t *buffer, size_t size, size_t *bytes_written, TickType_t timeout);

/**
 * @brief Push received audio data into the playback queue/ringbuffer.
 * 
 * @param data Audio data buffer.
 * @param size Size of the data in bytes.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t audio_play_queue_push(const uint8_t *data, size_t size);

#endif // AUDIO_HAL_H
