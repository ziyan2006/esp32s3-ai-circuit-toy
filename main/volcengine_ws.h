#ifndef VOLCENGINE_WS_H
#define VOLCENGINE_WS_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialize the Volcano Engine WebSocket client.
 * 
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t volcengine_ws_init(void);

/**
 * @brief Connect to Volcano Engine WebSocket endpoint.
 * 
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t volcengine_ws_connect(void);

/**
 * @brief Send a chunk of PCM audio data to Volcano Engine.
 * 
 * @param data Pointer to the PCM audio buffer.
 * @param size Size of the buffer in bytes.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t volcengine_ws_send_audio(const uint8_t *data, size_t size);

/**
 * @brief Start an AI dialogue session and wait until it is ready for audio.
 *
 * @return esp_err_t ESP_OK when SessionStarted is received.
 */
esp_err_t volcengine_ws_prepare_session(void);

/**
 * @brief Check if the WebSocket is currently connected to the server.
 * 
 * @return true if connected.
 */
bool volcengine_ws_is_connected(void);

/**
 * @brief Check whether an AI session is starting, active, or closing.
 */
bool volcengine_ws_is_session_active(void);

/**
 * @brief Check whether the server has recognized speech in the current session.
 */
bool volcengine_ws_has_asr_activity(void);

/**
 * @brief Commit the current audio buffer and request a response from the model.
 * 
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t volcengine_ws_commit_and_respond(void);

/**
 * @brief Cancel the current session without waiting for an AI response.
 *
 * @return esp_err_t ESP_OK when FinishSession is sent.
 */
esp_err_t volcengine_ws_cancel_session(void);

#endif // VOLCENGINE_WS_H
