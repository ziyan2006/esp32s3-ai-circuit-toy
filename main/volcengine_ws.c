#include "volcengine_ws.h"
#include "config.h"
#include "audio_hal.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "VOLC_WS";

extern void ui_update_status(const char *status);

#define VOLC_MSG_FULL_CLIENT 0x1
#define VOLC_MSG_AUDIO_CLIENT 0x2
#define VOLC_MSG_FULL_SERVER 0x9
#define VOLC_MSG_AUDIO_SERVER 0xB
#define VOLC_MSG_ERROR 0xF

#define VOLC_FLAG_WITH_EVENT 0x4
#define VOLC_SERIALIZATION_RAW 0x0
#define VOLC_SERIALIZATION_JSON 0x1

#define VOLC_EVENT_START_CONNECTION 1
#define VOLC_EVENT_FINISH_CONNECTION 2
#define VOLC_EVENT_CONNECTION_STARTED 50
#define VOLC_EVENT_CONNECTION_FAILED 51
#define VOLC_EVENT_CONNECTION_FINISHED 52
#define VOLC_EVENT_START_SESSION 100
#define VOLC_EVENT_FINISH_SESSION 102
#define VOLC_EVENT_SESSION_STARTED 150
#define VOLC_EVENT_SESSION_FINISHED 152
#define VOLC_EVENT_SESSION_FAILED 153
#define VOLC_EVENT_TASK_REQUEST 200
#define VOLC_EVENT_TTS_SENTENCE_START 350
#define VOLC_EVENT_TTS_RESPONSE 352
#define VOLC_EVENT_TTS_ENDED 359
#define VOLC_EVENT_END_ASR 400
#define VOLC_EVENT_ASR_RESPONSE 451
#define VOLC_EVENT_CHAT_RESPONSE 550
#define VOLC_EVENT_CHAT_ENDED 559

static esp_websocket_client_handle_t s_ws_client = NULL;
static bool s_transport_connected = false;
static bool s_protocol_ready = false;
static bool s_session_ready = false;
static bool s_session_starting = false;
static bool s_session_closing = false;
static bool s_session_cancelled = false;
static bool s_asr_activity = false;
static bool s_tts_completed = false;
static char s_session_id[37] = {0};
static char s_dialog_id[64] = {0};
static uint8_t *s_rx_buffer = NULL;
static size_t s_rx_buffer_size = 0;

static void write_u32_be(uint8_t *destination, uint32_t value)
{
    destination[0] = (value >> 24) & 0xFF;
    destination[1] = (value >> 16) & 0xFF;
    destination[2] = (value >> 8) & 0xFF;
    destination[3] = value & 0xFF;
}

static uint32_t read_u32_be(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

static void generate_uuid(char output[37])
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    snprintf(output, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11],
             bytes[12], bytes[13], bytes[14], bytes[15]);
}

static bool is_connection_event(uint32_t event)
{
    return event == VOLC_EVENT_START_CONNECTION ||
           event == VOLC_EVENT_FINISH_CONNECTION ||
           event == VOLC_EVENT_CONNECTION_STARTED ||
           event == VOLC_EVENT_CONNECTION_FAILED ||
           event == VOLC_EVENT_CONNECTION_FINISHED;
}

static esp_err_t send_event_packet(uint8_t message_type, uint8_t serialization,
                                   uint32_t event, const uint8_t *payload, size_t payload_len)
{
    if (s_ws_client == NULL || !s_transport_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t session_id_len = is_connection_event(event) ? 0 : strlen(s_session_id);
    size_t packet_size = 4 + 4 + 4 + payload_len;
    if (!is_connection_event(event)) {
        packet_size += 4 + session_id_len;
    }

    uint8_t *packet = malloc(packet_size);
    if (packet == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    packet[offset++] = 0x11;
    packet[offset++] = (message_type << 4) | VOLC_FLAG_WITH_EVENT;
    packet[offset++] = serialization << 4;
    packet[offset++] = 0x00;
    write_u32_be(packet + offset, event);
    offset += 4;

    if (!is_connection_event(event)) {
        write_u32_be(packet + offset, session_id_len);
        offset += 4;
        memcpy(packet + offset, s_session_id, session_id_len);
        offset += session_id_len;
    }

    write_u32_be(packet + offset, payload_len);
    offset += 4;
    if (payload_len > 0 && payload != NULL) {
        memcpy(packet + offset, payload, payload_len);
    }

    int sent = esp_websocket_client_send_bin(
        s_ws_client, (const char *)packet, packet_size, portMAX_DELAY);
    free(packet);
    return sent == (int)packet_size ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_start_connection(void)
{
    ESP_LOGI(TAG, "Sending StartConnection");
    return send_event_packet(VOLC_MSG_FULL_CLIENT, VOLC_SERIALIZATION_JSON,
                             VOLC_EVENT_START_CONNECTION, (const uint8_t *)"{}", 2);
}

static esp_err_t send_start_session(void)
{
    s_session_starting = true;
    s_session_cancelled = false;
    s_asr_activity = false;
    s_tts_completed = false;
    generate_uuid(s_session_id);

    cJSON *root = cJSON_CreateObject();
    cJSON *dialog = cJSON_CreateObject();
    if (s_dialog_id[0] != '\0') {
        cJSON_AddStringToObject(dialog, "dialog_id", s_dialog_id);
    }
    cJSON *dialog_extra = cJSON_CreateObject();
    cJSON_AddStringToObject(dialog_extra, "model", VOLCENGINE_MODEL_NAME);
    cJSON_AddStringToObject(dialog_extra, "input_mod", "audio");
    cJSON_AddItemToObject(dialog, "extra", dialog_extra);
    cJSON_AddItemToObject(root, "dialog", dialog);

    cJSON *asr = cJSON_CreateObject();
    cJSON *audio_info = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_info, "format", "pcm");
    cJSON_AddNumberToObject(audio_info, "sample_rate", AUDIO_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio_info, "channel", AUDIO_CHANNELS);
    cJSON_AddItemToObject(asr, "audio_info", audio_info);
    cJSON *asr_extra = cJSON_CreateObject();
    cJSON_AddNumberToObject(asr_extra, "end_smooth_window_ms", 800);
    cJSON_AddStringToObject(asr_extra, "input_mod", "push_to_talk");
    cJSON_AddItemToObject(asr, "extra", asr_extra);
    cJSON_AddItemToObject(root, "asr", asr);

    cJSON *tts = cJSON_CreateObject();
    cJSON_AddStringToObject(tts, "speaker", VOLCENGINE_VOICE_TYPE);
    cJSON *audio_config = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_config, "format", "pcm_s16le");
    cJSON_AddNumberToObject(audio_config, "sample_rate", AUDIO_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio_config, "channel", AUDIO_CHANNELS);
    cJSON_AddItemToObject(tts, "audio_config", audio_config);
    cJSON_AddItemToObject(root, "tts", tts);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending StartSession, session_id=%s", s_session_id);
    esp_err_t result = send_event_packet(
        VOLC_MSG_FULL_CLIENT, VOLC_SERIALIZATION_JSON, VOLC_EVENT_START_SESSION,
        (const uint8_t *)json, strlen(json));
    free(json);
    if (result != ESP_OK) {
        s_session_starting = false;
    }
    return result;
}

static void update_dialog_id(const uint8_t *payload, size_t payload_len)
{
    cJSON *root = cJSON_ParseWithLength((const char *)payload, payload_len);
    if (root == NULL) {
        return;
    }
    cJSON *dialog_id = cJSON_GetObjectItemCaseSensitive(root, "dialog_id");
    if (cJSON_IsString(dialog_id) && dialog_id->valuestring != NULL) {
        strlcpy(s_dialog_id, dialog_id->valuestring, sizeof(s_dialog_id));
    }
    cJSON_Delete(root);
}

static void log_json_event(uint32_t event, const uint8_t *payload, size_t payload_len)
{
    cJSON *root = cJSON_ParseWithLength((const char *)payload, payload_len);
    if (root == NULL) {
        ESP_LOGW(TAG, "Event %lu returned non-JSON payload (%u bytes)",
                 (unsigned long)event, (unsigned int)payload_len);
        return;
    }

    char *json = cJSON_PrintUnformatted(root);
    if (json != NULL) {
        ESP_LOGI(TAG, "Server event %lu: %s", (unsigned long)event, json);
        free(json);
    }

    cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (cJSON_IsString(text) && text->valuestring != NULL) {
        if (event == VOLC_EVENT_ASR_RESPONSE) {
            ESP_LOGI(TAG, "[ASR] %s", text->valuestring);
        } else if (event == VOLC_EVENT_CHAT_RESPONSE) {
            ESP_LOGI(TAG, "[ASSISTANT] %s", text->valuestring);
        }
    }
    cJSON_Delete(root);
}

static bool read_length_prefixed_field(const uint8_t *data, size_t len, size_t *offset)
{
    if (*offset + 4 > len) {
        return false;
    }
    uint32_t field_len = read_u32_be(data + *offset);
    *offset += 4;
    if (*offset + field_len > len) {
        return false;
    }
    *offset += field_len;
    return true;
}

static bool payload_contains(const uint8_t *payload, size_t payload_len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > payload_len) {
        return false;
    }
    for (size_t offset = 0; offset + needle_len <= payload_len; offset++) {
        if (memcmp(payload + offset, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static void handle_protocol_event(uint32_t event, const uint8_t *payload, size_t payload_len)
{
    switch (event) {
        case VOLC_EVENT_CONNECTION_STARTED:
            s_protocol_ready = true;
            ESP_LOGI(TAG, "Protocol connection established");
            ui_update_status("Ready!\nHold BOOT button to speak.");
            break;
        case VOLC_EVENT_CONNECTION_FAILED:
            ESP_LOGE(TAG, "Protocol connection failed");
            ui_update_status("Volcano protocol connection failed.");
            break;
        case VOLC_EVENT_SESSION_STARTED:
            s_session_starting = false;
            s_session_ready = true;
            s_session_closing = false;
            s_session_cancelled = false;
            s_asr_activity = false;
            s_tts_completed = false;
            ESP_LOGI(TAG, "AI session started successfully");
            update_dialog_id(payload, payload_len);
            log_json_event(event, payload, payload_len);
            break;
        case VOLC_EVENT_SESSION_FINISHED:
            s_session_starting = false;
            s_session_ready = false;
            s_session_closing = false;
            ESP_LOGI(TAG, "AI session finished");
            if (!s_session_cancelled) {
                ui_update_status("Ready!\nHold BOOT button to speak.");
            }
            s_session_cancelled = false;
            s_asr_activity = false;
            s_tts_completed = false;
            break;
        case VOLC_EVENT_SESSION_FAILED:
            s_session_starting = false;
            s_session_ready = false;
            s_session_closing = false;
            s_session_cancelled = false;
            s_asr_activity = false;
            ESP_LOGE(TAG, "AI session failed");
            log_json_event(event, payload, payload_len);
            ui_update_status("AI session failed. Check serial log.");
            break;
        case VOLC_EVENT_TTS_SENTENCE_START:
        case VOLC_EVENT_TTS_RESPONSE:
            ui_update_status("<<< Speaking...");
            log_json_event(event, payload, payload_len);
            break;
        case VOLC_EVENT_TTS_ENDED:
            log_json_event(event, payload, payload_len);
            s_tts_completed = true;
            if (send_event_packet(VOLC_MSG_FULL_CLIENT, VOLC_SERIALIZATION_JSON,
                                  VOLC_EVENT_FINISH_SESSION, (const uint8_t *)"{}", 2) == ESP_OK) {
                s_session_ready = false;
                s_session_closing = true;
            }
            break;
        case VOLC_EVENT_CHAT_ENDED:
            log_json_event(event, payload, payload_len);
            break;
        case VOLC_EVENT_ASR_RESPONSE:
            s_asr_activity = true;
            log_json_event(event, payload, payload_len);
            break;
        case VOLC_EVENT_CHAT_RESPONSE:
            log_json_event(event, payload, payload_len);
            break;
        default:
            log_json_event(event, payload, payload_len);
            break;
    }
}

static void parse_binary_frame(const uint8_t *data, size_t len)
{
    if (len < 4) {
        ESP_LOGW(TAG, "Received protocol frame shorter than 4 bytes");
        return;
    }

    size_t header_size = (data[0] & 0x0F) * 4;
    if (header_size < 4 || header_size > len) {
        ESP_LOGW(TAG, "Invalid protocol header size: %u", (unsigned int)header_size);
        return;
    }

    uint8_t message_type = data[1] >> 4;
    uint8_t flags = data[1] & 0x0F;
    size_t offset = header_size;
    uint32_t event = 0;
    uint32_t error_code = 0;

    if (message_type == VOLC_MSG_ERROR) {
        if (offset + 4 > len) {
            return;
        }
        error_code = read_u32_be(data + offset);
        offset += 4;
    }

    if (flags == VOLC_FLAG_WITH_EVENT) {
        if (offset + 4 > len) {
            return;
        }
        event = read_u32_be(data + offset);
        offset += 4;

        if (!is_connection_event(event) && !read_length_prefixed_field(data, len, &offset)) {
            ESP_LOGW(TAG, "Invalid session ID field in event %lu", (unsigned long)event);
            return;
        }
        if ((event == VOLC_EVENT_CONNECTION_STARTED ||
             event == VOLC_EVENT_CONNECTION_FAILED ||
             event == VOLC_EVENT_CONNECTION_FINISHED) &&
            !read_length_prefixed_field(data, len, &offset)) {
            ESP_LOGW(TAG, "Invalid connection ID field in event %lu", (unsigned long)event);
            return;
        }
    }

    if (offset + 4 > len) {
        return;
    }
    uint32_t payload_len = read_u32_be(data + offset);
    offset += 4;
    if (offset + payload_len > len) {
        ESP_LOGW(TAG, "Truncated protocol payload: expected %lu, available %u",
                 (unsigned long)payload_len, (unsigned int)(len - offset));
        return;
    }

    const uint8_t *payload = data + offset;
    if (message_type == VOLC_MSG_ERROR) {
        ESP_LOGE(TAG, "Server error %lu: %.*s", (unsigned long)error_code,
                 (int)payload_len, (const char *)payload);
        s_session_starting = false;
        s_session_ready = false;
        s_session_closing = false;
        s_session_cancelled = false;
        s_asr_activity = false;
        if (error_code == 55000001 &&
            payload_len > 0 &&
            payload_contains(payload, payload_len, "DialogAudioIdleTimeoutError")) {
            if (s_tts_completed) {
                ESP_LOGW(TAG, "Ignoring audio idle timeout after completed TTS response");
                ui_update_status("Ready!\nHold BOOT button to speak.");
            } else {
                ui_update_status("No speech detected.\nHold BOOT and try again.");
            }
            s_tts_completed = false;
            return;
        }
        char status[96];
        snprintf(status, sizeof(status), "Server error: %lu\nCheck serial log.",
                 (unsigned long)error_code);
        ui_update_status(status);
        return;
    }

    if (message_type == VOLC_MSG_AUDIO_SERVER) {
        if (payload_len > 0) {
            ui_update_status("<<< Speaking...");
            esp_err_t result = audio_play_queue_push(payload, payload_len);
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Audio playback queue rejected %lu bytes: %s",
                         (unsigned long)payload_len, esp_err_to_name(result));
            }
        }
        return;
    }

    if (flags == VOLC_FLAG_WITH_EVENT) {
        handle_protocol_event(event, payload, payload_len);
    }
}

static void handle_websocket_data(esp_websocket_event_data_t *data)
{
    size_t total_len = data->payload_len > 0 ? data->payload_len : data->data_len;
    size_t offset = data->payload_offset;

    if (offset == 0) {
        free(s_rx_buffer);
        s_rx_buffer = malloc(total_len);
        s_rx_buffer_size = total_len;
        if (s_rx_buffer == NULL) {
            s_rx_buffer_size = 0;
            ESP_LOGE(TAG, "Failed to allocate %u-byte WebSocket frame buffer",
                     (unsigned int)total_len);
            return;
        }
    }

    if (s_rx_buffer == NULL || offset + data->data_len > s_rx_buffer_size) {
        ESP_LOGW(TAG, "Invalid WebSocket fragment offset=%u len=%u total=%u",
                 (unsigned int)offset, (unsigned int)data->data_len,
                 (unsigned int)s_rx_buffer_size);
        return;
    }

    memcpy(s_rx_buffer + offset, data->data_ptr, data->data_len);
    if (offset + data->data_len == s_rx_buffer_size) {
        parse_binary_frame(s_rx_buffer, s_rx_buffer_size);
        free(s_rx_buffer);
        s_rx_buffer = NULL;
        s_rx_buffer_size = 0;
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            s_transport_connected = true;
            s_protocol_ready = false;
            s_session_ready = false;
            s_session_closing = false;
            s_session_cancelled = false;
            s_asr_activity = false;
            s_tts_completed = false;
            ESP_LOGI(TAG, "WebSocket transport connected");
            ui_update_status("WebSocket connected. Authenticating...");
            send_start_connection();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            s_transport_connected = false;
            s_protocol_ready = false;
            s_session_starting = false;
            s_session_ready = false;
            s_session_closing = false;
            s_session_cancelled = false;
            s_asr_activity = false;
            s_tts_completed = false;
            ESP_LOGW(TAG, "WebSocket disconnected");
            ui_update_status("WebSocket disconnected. Reconnecting...");
            break;
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x2 || data->op_code == 0x0) {
                handle_websocket_data(data);
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket transport error");
            ui_update_status("WebSocket error. Check serial log.");
            break;
        default:
            break;
    }
}

esp_err_t volcengine_ws_init(void)
{
    ESP_LOGI(TAG, "Initializing Volcano Speech WebSocket client...");

    char request_id[37];
    generate_uuid(request_id);

    char *headers = malloc(1024);
    if (headers == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(headers, 1024,
             "X-Api-App-ID: %s\r\n"
             "X-Api-Access-Key: %s\r\n"
             "X-Api-Resource-Id: volc.speech.dialog\r\n"
             "X-Api-App-Key: %s\r\n"
             "X-Api-Request-Id: %s\r\n",
             VOLCENGINE_APP_ID, VOLCENGINE_API_KEY, VOLCENGINE_APP_KEY, request_id);

    esp_websocket_client_config_t config = {
        .uri = VOLCENGINE_WS_URL,
        .headers = headers,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .reconnect_timeout_ms = 3000,
    };

    s_ws_client = esp_websocket_client_init(&config);
    free(headers);
    if (s_ws_client == NULL) {
        return ESP_FAIL;
    }

    return esp_websocket_register_events(
        s_ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
}

esp_err_t volcengine_ws_connect(void)
{
    if (s_ws_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Connecting to %s...", VOLCENGINE_WS_URL);
    return esp_websocket_client_start(s_ws_client);
}

static esp_err_t ensure_protocol_connection(void)
{
    if (s_ws_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (esp_websocket_client_is_connected(s_ws_client) &&
        s_transport_connected && s_protocol_ready) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "WebSocket connection is stale; restarting client");
    s_transport_connected = false;
    s_protocol_ready = false;
    s_session_ready = false;
    s_session_starting = false;
    s_session_closing = false;
    s_session_cancelled = false;
    s_asr_activity = false;
    s_tts_completed = false;
    ui_update_status("Reconnecting to AI service...");

    esp_err_t stop_ret = esp_websocket_client_stop(s_ws_client);
    if (stop_ret != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket stop before restart returned: %s",
                 esp_err_to_name(stop_ret));
    }

    esp_err_t start_ret = esp_websocket_client_start(s_ws_client);
    if (start_ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket restart failed: %s", esp_err_to_name(start_ret));
        return start_ret;
    }

    for (int retry = 0; retry < 200; retry++) {
        if (esp_websocket_client_is_connected(s_ws_client) &&
            s_transport_connected && s_protocol_ready) {
            ESP_LOGI(TAG, "WebSocket protocol connection restored");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGE(TAG, "Timed out while restoring WebSocket protocol connection");
    return ESP_ERR_TIMEOUT;
}

esp_err_t volcengine_ws_send_audio(const uint8_t *data, size_t size)
{
    if (!s_session_ready || data == NULL || size == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return send_event_packet(VOLC_MSG_AUDIO_CLIENT, VOLC_SERIALIZATION_JSON,
                             VOLC_EVENT_TASK_REQUEST, data, size);
}

esp_err_t volcengine_ws_prepare_session(void)
{
    esp_err_t connection_ret = ensure_protocol_connection();
    if (connection_ret != ESP_OK) {
        return connection_ret;
    }
    if (s_session_ready) {
        return ESP_OK;
    }
    for (int retry = 0; s_session_closing && retry < 40; retry++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_session_closing) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_session_starting && send_start_session() != ESP_OK) {
        return ESP_FAIL;
    }

    for (int retry = 0; retry < 100; retry++) {
        if (s_session_ready) {
            return ESP_OK;
        }
        if (!s_session_starting) {
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    s_session_starting = false;
    return ESP_ERR_TIMEOUT;
}

esp_err_t volcengine_ws_commit_and_respond(void)
{
    if (!s_session_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Sending EndASR for current push-to-talk turn");
    return send_event_packet(VOLC_MSG_FULL_CLIENT, VOLC_SERIALIZATION_JSON,
                             VOLC_EVENT_END_ASR, (const uint8_t *)"{}", 2);
}

esp_err_t volcengine_ws_cancel_session(void)
{
    if (!s_session_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Cancelling current AI session without requesting a response");
    esp_err_t ret = send_event_packet(VOLC_MSG_FULL_CLIENT, VOLC_SERIALIZATION_JSON,
                                      VOLC_EVENT_FINISH_SESSION, (const uint8_t *)"{}", 2);
    if (ret == ESP_OK) {
        s_session_ready = false;
        s_session_closing = true;
        s_session_cancelled = true;
    }
    return ret;
}

bool volcengine_ws_is_connected(void)
{
    return s_ws_client != NULL &&
           esp_websocket_client_is_connected(s_ws_client) &&
           s_transport_connected && s_protocol_ready;
}

bool volcengine_ws_has_asr_activity(void)
{
    return s_asr_activity;
}
