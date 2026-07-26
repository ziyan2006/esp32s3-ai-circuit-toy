#include "volcengine_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_self_test.h"
#include "assistant_mode.h"
#include "assistant_router.h"
#include "c6_network_test.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "remote_assistant.h"
#include "tuco_agent.h"
#include "zlib.h"

static const char *TAG = "voice_assistant";

#define AUDIO_CHUNK_BYTES 6400U
#define MAX_RECORDING_MS 8000U
#define MIN_AUDIO_BYTES 640U
#define FRAME_BUFFER_LIMIT (64U * 1024U)
#define REQUEST_TIMEOUT_MS 30000U
#define UNCLEAR_REPLY "我没明白，换个说法试试。"
#define THINKING_ANNOUNCEMENT "稍等，我思考一下"

typedef enum { CLOUD_NONE, CLOUD_ASR, CLOUD_TTS } cloud_mode_t;
typedef enum { VOICE_READY, VOICE_RECORDING, VOICE_ASR, VOICE_AGENT, VOICE_TTS, VOICE_WAIT_AGENT, VOICE_ERROR } voice_phase_t;
typedef enum { TTS_KIND_NONE, TTS_KIND_THINKING_ANNOUNCEMENT, TTS_KIND_AGENT_REPLY } tts_kind_t;

static esp_websocket_client_handle_t s_client;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_event_sem;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static voice_assistant_status_t s_status;
static volatile bool s_play_active;
static volatile bool s_programmer_owns_input;
static volatile bool s_key_pressed;
static volatile bool s_previous_key_pressed;
static volatile uint32_t s_press_id;
static volatile uint32_t s_consumed_press_id;
static volatile uint16_t s_level_id;
static bool s_session_play_active;
static uint16_t s_session_level_id;
static cloud_mode_t s_mode;
static voice_phase_t s_phase;
static bool s_connected;
static bool s_final_sent;
static bool s_tts_started;
static bool s_tts_audio_received;
static bool s_tts_finished;
static bool s_tts_session_started;
static bool s_tts_finish_sent;
static bool s_playback_finishing;
static bool s_transport_error;
static int s_handshake_status;
static uint32_t s_audio_bytes;
static uint32_t s_sequence;
static volatile uint32_t s_connection_generation;
static TickType_t s_deadline;
static TickType_t s_agent_deadline;
static TickType_t s_tts_next_request;
static uint8_t *s_frame_buffer;
static size_t s_frame_size;
static size_t s_frame_expected;
static char s_final_text[512];
static char s_tts_text[sizeof(s_final_text)];
static char s_agent_reply[sizeof(s_final_text)];
static uint32_t s_agent_request_id;
static bool s_agent_reply_ready;
static tts_kind_t s_tts_kind;
static size_t s_tts_text_offset;
static char s_request_id[37];
static char s_error_reason[48];

static void status_set(voice_assistant_state_t state, bool visible, bool error, const char *text);

static void cancel_pending_agent(void)
{
    if (s_agent_request_id != 0U) {
        assistant_router_cancel(s_agent_request_id);
        s_agent_request_id = 0U;
    }
    s_agent_reply_ready = false;
}

static void start_tts_text(const char *text, tts_kind_t kind)
{
    strlcpy(s_tts_text, text, sizeof(s_tts_text));
    s_tts_kind = kind;
    s_tts_started = false;
    s_tts_audio_received = false;
    s_tts_finished = false;
    s_tts_session_started = false;
    s_tts_finish_sent = false;
    s_playback_finishing = false;
    s_tts_text_offset = 0U;
    s_phase = VOICE_TTS;
}

static void start_unclear_reply(void)
{
    cancel_pending_agent();
    start_tts_text(UNCLEAR_REPLY, TTS_KIND_AGENT_REPLY);
    status_set(VOICE_ASSISTANT_THINKING, true, false, "播放中");
}

static void status_set(voice_assistant_state_t state, bool visible, bool error, const char *text)
{
    portENTER_CRITICAL(&s_lock);
    const bool changed = s_status.state != state || s_status.visible != visible ||
                         s_status.error != error || strcmp(s_status.text, text) != 0;
    s_status.state = state;
    s_status.visible = visible;
    s_status.error = error;
    strlcpy(s_status.text, text, sizeof(s_status.text));
    if (changed) ++s_status.generation;
    portEXIT_CRITICAL(&s_lock);
}

static bool configured(void)
{
#if CONFIG_TUCO_VOLCENGINE_ENABLED
    const bool assistant_configured = assistant_mode_get() == ASSISTANT_MODE_REMOTE ?
        remote_assistant_is_configured() : tuco_agent_is_configured();
    return CONFIG_TUCO_VOLCENGINE_API_KEY[0] != '\0' && assistant_configured;
#else
    return false;
#endif
}

static void uuid(char out[37])
{
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
             b[12], b[13], b[14], b[15]);
}

static bool gzip_compress(const uint8_t *input, size_t input_len, uint8_t **output, size_t *output_len)
{
    z_stream stream = {0};
    const uLong bound = compressBound((uLong)input_len);
    uint8_t *buffer = malloc((size_t)bound + 32U);
    if (buffer == NULL) return false;
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        free(buffer);
        return false;
    }
    stream.next_in = (Bytef *)input;
    stream.avail_in = (uInt)input_len;
    stream.next_out = buffer;
    stream.avail_out = (uInt)((size_t)bound + 32U);
    const int result = deflate(&stream, Z_FINISH);
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        free(buffer);
        return false;
    }
    *output = buffer;
    *output_len = stream.total_out;
    return true;
}

static bool gzip_decompress(const uint8_t *input, size_t input_len, uint8_t **output, size_t *output_len)
{
    z_stream stream = {0};
    uint8_t *buffer = malloc(FRAME_BUFFER_LIMIT);
    if (buffer == NULL) return false;
    if (inflateInit2(&stream, 15 + 32) != Z_OK) { free(buffer); return false; }
    stream.next_in = (Bytef *)input;
    stream.avail_in = (uInt)input_len;
    stream.next_out = buffer;
    stream.avail_out = FRAME_BUFFER_LIMIT;
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END) { free(buffer); return false; }
    *output = buffer;
    *output_len = stream.total_out;
    return true;
}

static void be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static esp_err_t send_frame(const uint8_t *data, size_t len)
{
    if (s_client == NULL || !s_connected || len > INT32_MAX) return ESP_ERR_INVALID_STATE;
    return esp_websocket_client_send_bin(s_client, (const char *)data, (int)len,
                                         pdMS_TO_TICKS(REQUEST_TIMEOUT_MS)) < 0
               ? ESP_FAIL : ESP_OK;
}

static esp_err_t send_asr_start(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *user = cJSON_AddObjectToObject(root, "user");
    cJSON_AddStringToObject(user, "uid", "tuco-device");
    cJSON *audio = cJSON_AddObjectToObject(root, "audio");
    cJSON_AddStringToObject(audio, "format", "pcm");
    cJSON_AddStringToObject(audio, "codec", "raw");
    cJSON_AddNumberToObject(audio, "rate", 16000);
    cJSON_AddNumberToObject(audio, "bits", 16);
    cJSON_AddNumberToObject(audio, "channel", 1);
    cJSON_AddStringToObject(audio, "language", "zh-CN");
    cJSON *request = cJSON_AddObjectToObject(root, "request");
    cJSON_AddStringToObject(request, "model_name", "bigmodel");
    cJSON_AddBoolToObject(request, "enable_itn", true);
    cJSON_AddBoolToObject(request, "enable_punc", true);
    cJSON_AddBoolToObject(request, "enable_ddc", true);
    cJSON_AddBoolToObject(request, "show_utterances", true);
    cJSON_AddBoolToObject(request, "enable_nonstream", false);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) return ESP_ERR_NO_MEM;
    uint8_t *compressed = NULL; size_t compressed_len = 0;
    const bool compressed_ok = gzip_compress((const uint8_t *)json, strlen(json),
                                              &compressed, &compressed_len);
    free(json);
    if (!compressed_ok) return ESP_ERR_NO_MEM;
    uint8_t *frame = malloc(12U + compressed_len);
    if (frame == NULL) { free(compressed); return ESP_ERR_NO_MEM; }
    memcpy(frame, "\x11\x11\x11\x00", 4);
    be32(frame + 4, 1U); be32(frame + 8, (uint32_t)compressed_len);
    memcpy(frame + 12, compressed, compressed_len);
    free(compressed);
    const esp_err_t result = send_frame(frame, 12U + compressed_len);
    free(frame);
    return result;
}

static esp_err_t send_asr_audio(const uint8_t *pcm, size_t pcm_len, bool last)
{
    uint8_t *compressed = NULL; size_t compressed_len = 0;
    if (!gzip_compress(pcm, pcm_len, &compressed, &compressed_len)) return ESP_ERR_NO_MEM;
    uint8_t frame[12U + compressBound(AUDIO_CHUNK_BYTES) + 32U];
    const int32_t sequence = last ? -(int32_t)s_sequence : (int32_t)s_sequence;
    memcpy(frame, last ? "\x11\x23\x01\x00" : "\x11\x21\x01\x00", 4);
    be32(frame + 4, (uint32_t)sequence); be32(frame + 8, (uint32_t)compressed_len);
    memcpy(frame + 12, compressed, compressed_len);
    free(compressed);
    ++s_sequence;
    return send_frame(frame, 12U + compressed_len);
}

static esp_err_t send_tts_event(uint32_t event, bool with_session, const char *json)
{
    const size_t json_len = strlen(json);
    const size_t session_len = with_session ? strlen(s_request_id) : 0U;
    const size_t frame_len = 12U + (with_session ? 4U + session_len : 0U) + json_len;
    uint8_t *frame = malloc(frame_len);
    if (frame == NULL) return ESP_ERR_NO_MEM;
    memcpy(frame, "\x11\x14\x10\x00", 4);  // FullClientRequest with event.
    be32(frame + 4, event);
    size_t offset = 8U;
    if (with_session) {
        be32(frame + offset, (uint32_t)session_len); offset += 4U;
        memcpy(frame + offset, s_request_id, session_len); offset += session_len;
    }
    be32(frame + offset, (uint32_t)json_len); offset += 4U;
    memcpy(frame + offset, json, json_len);
    const esp_err_t result = send_frame(frame, frame_len);
    free(frame);
    ESP_LOGI(TAG, "TTS 发送事件=%lu, 会话=%d", (unsigned long)event, with_session);
    return result;
}

static esp_err_t send_tts_start_session(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "event", 100);
    cJSON *params = cJSON_AddObjectToObject(root, "req_params");
    cJSON_AddStringToObject(params, "speaker", CONFIG_TUCO_VOLCENGINE_TTS_VOICE);
    cJSON *audio = cJSON_AddObjectToObject(params, "audio_params");
    cJSON_AddStringToObject(audio, "format", "pcm");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddBoolToObject(audio, "enable_subtitle", false);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) return ESP_ERR_NO_MEM;
    const esp_err_t result = send_tts_event(100U, true, json);
    free(json);
    return result;
}

static esp_err_t send_tts_task(const char *text, size_t text_len)
{
    char fragment[5] = {0};
    if (text == NULL || text_len == 0U || text_len >= sizeof(fragment)) return ESP_ERR_INVALID_ARG;
    memcpy(fragment, text, text_len);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "event", 200);
    cJSON *params = cJSON_AddObjectToObject(root, "req_params");
    cJSON_AddStringToObject(params, "speaker", CONFIG_TUCO_VOLCENGINE_TTS_VOICE);
    cJSON *audio = cJSON_AddObjectToObject(params, "audio_params");
    cJSON_AddStringToObject(audio, "format", "pcm");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddBoolToObject(audio, "enable_subtitle", false);
    cJSON_AddStringToObject(params, "text", fragment);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) return ESP_ERR_NO_MEM;
    const esp_err_t result = send_tts_event(200U, true, json);
    free(json);
    return result;
}

static size_t utf8_character_length(const char *text)
{
    const uint8_t first = (uint8_t)text[0];
    if ((first & 0x80U) == 0U) return 1U;
    if ((first & 0xE0U) == 0xC0U && ((uint8_t)text[1] & 0xC0U) == 0x80U) return 2U;
    if ((first & 0xF0U) == 0xE0U && ((uint8_t)text[1] & 0xC0U) == 0x80U &&
        ((uint8_t)text[2] & 0xC0U) == 0x80U) return 3U;
    if ((first & 0xF8U) == 0xF0U && ((uint8_t)text[1] & 0xC0U) == 0x80U &&
        ((uint8_t)text[2] & 0xC0U) == 0x80U && ((uint8_t)text[3] & 0xC0U) == 0x80U) return 4U;
    return 1U;
}

static void clear_client(void)
{
    esp_websocket_client_handle_t client = s_client;
    ++s_connection_generation;
    s_client = NULL; s_connected = false; s_mode = CLOUD_NONE;
    free(s_frame_buffer); s_frame_buffer = NULL; s_frame_size = s_frame_expected = 0U;
    if (client != NULL) {
        (void)esp_websocket_client_stop(client);
        (void)esp_websocket_client_destroy(client);
    }
}

static void report_error(const char *text)
{
    ESP_LOGE(TAG, "%s", text);
    cancel_pending_agent();
    audio_self_test_voice_capture_end();
    audio_self_test_voice_playback_abort();
    clear_client();
    s_phase = VOICE_ERROR;
    status_set(VOICE_ASSISTANT_ERROR, true, true, "失败");
}

static esp_err_t poll_agent_result(void)
{
    if (s_agent_request_id == 0U) return ESP_OK;

    char reply[sizeof(s_agent_reply)] = {0};
    const esp_err_t result = assistant_router_take_result(s_agent_request_id, reply, sizeof(reply));
    if (result == ESP_OK) {
        strlcpy(s_agent_reply, reply, sizeof(s_agent_reply));
        s_agent_request_id = 0U;
        s_agent_reply_ready = true;
        ESP_LOGI(TAG, "Agent 回复: %s", s_agent_reply);
        return ESP_OK;
    }
    if (result != ESP_ERR_NOT_FOUND) return result;
    if (xTaskGetTickCount() < s_agent_deadline) return ESP_OK;

    cancel_pending_agent();
    return ESP_ERR_TIMEOUT;
}

static void callback_error(const char *reason)
{
    strlcpy(s_error_reason, reason, sizeof(s_error_reason));
    s_transport_error = true;
}

static void parse_asr_frame(const uint8_t *data, size_t len)
{
    if (len < 8U) { callback_error("ASR 协议错误"); return; }
    const size_t header = (data[0] & 0x0FU) * 4U;
    const uint8_t type = data[1] >> 4U; const uint8_t flags = data[1] & 0x0FU;
    const uint8_t serialization = data[2] >> 4U; const uint8_t compression = data[2] & 0x0FU;
    size_t offset = header;
    int32_t sequence = 0;
    if ((flags & 1U) != 0U) { if (offset + 4U > len) return; sequence = (int32_t)read_be32(data + offset); offset += 4U; }
    uint32_t code = 0;
    if (type == 0x0FU) { if (offset + 4U > len) return; code = read_be32(data + offset); offset += 4U; }
    if (offset + 4U > len) return;
    const uint32_t payload_len = read_be32(data + offset); offset += 4U;
    if (payload_len > len - offset) return;
    uint8_t *payload = (uint8_t *)(data + offset); size_t decoded_len = payload_len;
    uint8_t *decoded = NULL;
    if (compression == 1U && payload_len > 0U) {
        if (!gzip_decompress(payload, payload_len, &decoded, &decoded_len)) {
            callback_error("ASR 压缩帧错误");
            return;
        }
        payload = decoded;
    }
    if (code != 0U) {
        free(decoded); ESP_LOGE(TAG, "ASR 服务错误 code=%lu", (unsigned long)code);
        callback_error("ASR 服务错误"); return;
    }
    if (serialization == 1U && decoded_len > 0U) {
        char *json = strndup((const char *)payload, decoded_len);
        cJSON *root = json == NULL ? NULL : cJSON_Parse(json);
        cJSON *result = root == NULL ? NULL : cJSON_GetObjectItem(root, "result");
        cJSON *text = result == NULL ? NULL : cJSON_GetObjectItem(result, "text");
        if (cJSON_IsString(text) && text->valuestring[0] != '\0') strlcpy(s_final_text, text->valuestring, sizeof(s_final_text));
        cJSON_Delete(root); free(json);
    }
    free(decoded);
    if (sequence < 0 || (flags & 3U) == 3U) {
        if (s_final_text[0] == '\0') {
            ESP_LOGI(TAG, "ASR returned no usable text; playing fallback reply");
            start_unclear_reply();
            return;
        }
        ESP_LOGI(TAG, "识别文本: %s", s_final_text);
        s_agent_request_id = 0U;
        s_phase = VOICE_AGENT;
        status_set(VOICE_ASSISTANT_THINKING, true, false, "思考中");
    }
}

static void parse_tts_frame(const uint8_t *data, size_t len)
{
    if (len < 8U) { callback_error("TTS 协议错误"); return; }
    const size_t header = (data[0] & 0x0FU) * 4U;
    const uint8_t type = data[1] >> 4U;
    const uint8_t flags = data[1] & 0x0FU;
    size_t offset = header;
    if (header > len) { callback_error("TTS 协议错误"); return; }
    if (type == 0x0FU) {
        if (offset + 4U <= len) ESP_LOGE(TAG, "TTS 服务错误 code=%lu", (unsigned long)read_be32(data + offset));
        callback_error("TTS 服务错误"); return;
    }
    if ((flags == 1U || flags == 3U) && offset + 4U <= len) offset += 4U;
    uint32_t event = 0U;
    if (flags == 4U) {
        if (offset + 4U > len) { callback_error("TTS 协议错误"); return; }
        event = read_be32(data + offset); offset += 4U;
        const bool connection_event = event == 50U || event == 51U || event == 52U;
        if (!connection_event) {
            if (offset + 4U > len) { callback_error("TTS 协议错误"); return; }
            const uint32_t session_len = read_be32(data + offset); offset += 4U;
            if (session_len > len - offset) { callback_error("TTS 协议错误"); return; }
            offset += session_len;
        }
        if (connection_event && offset + 4U <= len) {
            const uint32_t connect_len = read_be32(data + offset); offset += 4U;
            if (connect_len > len - offset) { callback_error("TTS 协议错误"); return; }
            offset += connect_len;
        }
    }
    if (offset + 4U > len) { callback_error("TTS 协议错误"); return; }
    const uint32_t payload_len = read_be32(data + offset); offset += 4U;
    if (payload_len > len - offset) { callback_error("TTS 协议错误"); return; }
    ESP_LOGI(TAG, "TTS 帧 type=%u flags=%u event=%lu payload=%lu",
             (unsigned)type, (unsigned)flags, (unsigned long)event, (unsigned long)payload_len);
    if (type == 0x0BU) {
        if (payload_len == 0U) return;
        if (!s_tts_started) {
            if (audio_self_test_voice_playback_begin() != ESP_OK) { callback_error("播放缓冲失败"); return; }
            s_tts_started = true; status_set(VOICE_ASSISTANT_PLAYING, true, false, "播放中");
        }
        if (audio_self_test_voice_playback_push(data + offset, payload_len, pdMS_TO_TICKS(1000)) != ESP_OK) {
            callback_error("播放缓冲失败"); return;
        }
        s_tts_audio_received = true;
        s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(REQUEST_TIMEOUT_MS);
        return;
    }
    if (type != 0x09U || flags != 4U) return;
    if (event == 50U) {
        if (send_tts_start_session() != ESP_OK) callback_error("TTS 会话创建失败");
    } else if (event == 150U) {
        s_tts_session_started = true;
        s_tts_next_request = xTaskGetTickCount();
    } else if (event == 51U || event == 153U) {
        callback_error("TTS 服务错误");
    } else if (event == 152U || event == 359U) {
        if (!s_tts_audio_received) { callback_error("TTS 没有返回音频"); return; }
        s_tts_finished = true;
    }
}

static void websocket_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    if ((uint32_t)(uintptr_t)args != s_connection_generation) return;
    esp_websocket_event_data_t *event = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        s_connected = true; s_transport_error = false;
        if (s_mode == CLOUD_ASR && send_asr_start() != ESP_OK) callback_error("ASR 请求发送失败");
        if (s_mode == CLOUD_TTS) {
            uuid(s_request_id);
            if (send_tts_event(1U, false, "{}") != ESP_OK) callback_error("TTS 请求发送失败");
        }
        xSemaphoreGive(s_event_sem); return;
    }
    if (event_id == WEBSOCKET_EVENT_ERROR) {
        s_handshake_status = event == NULL ? 0 : event->error_handle.esp_ws_handshake_status_code;
        callback_error(s_handshake_status == 401 || s_handshake_status == 403 ? "云端服务未授权" : "云端连接失败");
        xSemaphoreGive(s_event_sem); return;
    }
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_CLOSED) {
        s_connected = false; xSemaphoreGive(s_event_sem); return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA || event == NULL || event->data_ptr == NULL || event->data_len == 0U ||
        (event->op_code != 0x2U && event->op_code != 0x0U)) return;
    const size_t total = event->payload_len > 0U ? event->payload_len : event->data_len;
    if (event->payload_offset == 0U) {
        free(s_frame_buffer); s_frame_buffer = malloc(total); s_frame_size = total; s_frame_expected = 0U;
    }
    if (s_frame_buffer == NULL || event->payload_offset + event->data_len > s_frame_size) return;
    memcpy(s_frame_buffer + event->payload_offset, event->data_ptr, event->data_len);
    s_frame_expected = event->payload_offset + event->data_len;
    if (s_frame_expected == s_frame_size) {
        if (s_mode == CLOUD_ASR) parse_asr_frame(s_frame_buffer, s_frame_size);
        else if (s_mode == CLOUD_TTS) parse_tts_frame(s_frame_buffer, s_frame_size);
        free(s_frame_buffer); s_frame_buffer = NULL; s_frame_size = s_frame_expected = 0U;
        xSemaphoreGive(s_event_sem);
    }
}

static esp_err_t open_cloud(cloud_mode_t mode)
{
    char *headers = malloc(512U); if (headers == NULL) return ESP_ERR_NO_MEM;
    char request_id[37], connect_id[37]; uuid(request_id); uuid(connect_id);
    const char *uri = mode == CLOUD_ASR ? CONFIG_TUCO_VOLCENGINE_ASR_WS_URL : CONFIG_TUCO_VOLCENGINE_TTS_WS_URL;
    const char *resource = mode == CLOUD_ASR ? CONFIG_TUCO_VOLCENGINE_ASR_RESOURCE_ID : CONFIG_TUCO_VOLCENGINE_TTS_RESOURCE_ID;
    if (mode == CLOUD_ASR) {
        snprintf(headers, 512U,
                 "X-Api-Key: %s\r\nX-Api-Resource-Id: %s\r\nX-Api-Request-Id: %s\r\n"
                 "X-Api-Sequence: -1\r\n",
                 CONFIG_TUCO_VOLCENGINE_API_KEY, resource, request_id);
    } else {
        snprintf(headers, 512U, "X-Api-Key: %s\r\nX-Api-Resource-Id: %s\r\nX-Api-Connect-Id: %s\r\n",
                 CONFIG_TUCO_VOLCENGINE_API_KEY, resource, connect_id);
    }
    const esp_websocket_client_config_t config = {
        .uri = uri, .headers = headers, .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 8192, .network_timeout_ms = 15000, .reconnect_timeout_ms = 3000,
        .disable_auto_reconnect = true, .disable_pingpong_discon = true,
    };
    ++s_connection_generation;
    if (s_connection_generation == 0U) ++s_connection_generation;
    const void *handler_arg = (const void *)(uintptr_t)s_connection_generation;
    s_mode = mode; s_connected = false; s_transport_error = false; s_handshake_status = 0;
    s_client = esp_websocket_client_init(&config); free(headers);
    if (s_client == NULL) { s_mode = CLOUD_NONE; return ESP_FAIL; }
    if (esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_handler, (void *)handler_arg) != ESP_OK ||
        esp_websocket_client_start(s_client) != ESP_OK) { clear_client(); return ESP_FAIL; }
    return ESP_OK;
}

static size_t read_capture(uint8_t *buffer, size_t capacity, TickType_t timeout)
{
    size_t total = 0;
    while (total + 512U <= capacity) {
        size_t got = 0;
        if (audio_self_test_voice_capture_read(buffer + total, capacity - total, &got,
                                                total == 0 ? timeout : 0) != ESP_OK || got == 0) break;
        total += got;
    }
    return total;
}

static void voice_task(void *arg)
{
    (void)arg;
    uint8_t *pcm = malloc(AUDIO_CHUNK_BYTES);
    if (pcm == NULL) { s_task = NULL; vTaskDelete(NULL); return; }
    for (;;) {
        if (!configured()) { status_set(VOICE_ASSISTANT_DISABLED, false, false, ""); vTaskDelay(pdMS_TO_TICKS(250)); continue; }
        if (!s_play_active) {
            cancel_pending_agent();
            if (s_phase != VOICE_READY) {
                audio_self_test_voice_capture_end();
                audio_self_test_voice_playback_abort();
                clear_client();
                s_phase = VOICE_READY;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (!c6_network_is_connected()) {
            if (s_phase != VOICE_READY) {
                cancel_pending_agent();
                audio_self_test_voice_capture_end(); audio_self_test_voice_playback_abort(); clear_client(); s_phase = VOICE_ERROR;
            }
            if (s_play_active && s_key_pressed) status_set(VOICE_ASSISTANT_ERROR, true, true, "失败");
            vTaskDelay(pdMS_TO_TICKS(100)); continue;
        }
        if (s_phase == VOICE_ERROR && !s_key_pressed) { s_phase = VOICE_READY; status_set(VOICE_ASSISTANT_READY, false, false, ""); }
        const bool request_recording = s_play_active && !s_programmer_owns_input && s_key_pressed;
        if (s_transport_error) {
            const char *reason = s_error_reason[0] == '\0' ? "云端连接失败" : s_error_reason;
            s_transport_error = false;
            report_error(reason);
        }
        if (request_recording && (s_phase == VOICE_TTS || s_phase == VOICE_AGENT || s_phase == VOICE_WAIT_AGENT)) {
            cancel_pending_agent();
            audio_self_test_voice_playback_abort(); clear_client(); s_phase = VOICE_READY;
            s_tts_started = false; s_tts_finished = false; s_playback_finishing = false;
        }
        if (request_recording && s_phase == VOICE_READY && s_consumed_press_id != s_press_id) {
            s_consumed_press_id = s_press_id; s_audio_bytes = 0; s_sequence = 2; s_final_text[0] = '\0';
            s_final_sent = false; cancel_pending_agent(); s_tts_kind = TTS_KIND_NONE;
            s_tts_text[0] = '\0'; s_agent_reply[0] = '\0';
            if (audio_self_test_voice_capture_begin() != ESP_OK || open_cloud(CLOUD_ASR) != ESP_OK) { report_error("语音连接失败"); }
            else {
                s_phase = VOICE_RECORDING;
                s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(MAX_RECORDING_MS);
                status_set(VOICE_ASSISTANT_RECORDING, true, false, "录音中");
            }
        }
        if (s_phase == VOICE_RECORDING && !s_connected && xTaskGetTickCount() >= s_deadline) {
            report_error("ASR 连接超时");
        }
        if (s_phase == VOICE_RECORDING && s_connected) {
            const size_t got = read_capture(pcm, AUDIO_CHUNK_BYTES, pdMS_TO_TICKS(20));
            if (got > 0U) {
                if (send_asr_audio(pcm, got, false) != ESP_OK) report_error("语音上传失败");
                else s_audio_bytes += (uint32_t)got;
            }
            if ((!request_recording || xTaskGetTickCount() >= s_deadline) && !s_final_sent) {
                audio_self_test_voice_capture_end(); s_final_sent = true;
                for (;;) {
                    const size_t tail = read_capture(pcm, AUDIO_CHUNK_BYTES, 0);
                    if (tail == 0U) break;
                    if (send_asr_audio(pcm, tail, false) != ESP_OK) { report_error("语音上传失败"); break; }
                    s_audio_bytes += (uint32_t)tail;
                }
                if (s_phase != VOICE_ERROR) {
                    if (s_audio_bytes < MIN_AUDIO_BYTES) start_unclear_reply();
                    else if (send_asr_audio(pcm, 0, true) != ESP_OK) report_error("没有有效录音");
                    else { s_phase = VOICE_ASR; status_set(VOICE_ASSISTANT_THINKING, true, false, "识别中"); s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(REQUEST_TIMEOUT_MS); }
                }
            }
        }
        if (s_phase == VOICE_ASR && xTaskGetTickCount() >= s_deadline) report_error("ASR 响应超时");
        if (s_phase == VOICE_AGENT) {
            if (s_mode == CLOUD_ASR) clear_client();
            if (s_agent_request_id == 0U) {
                if (assistant_router_submit(s_final_text, s_level_id, &s_agent_request_id) != ESP_OK) {
                    report_error("对话服务不可用");
                } else {
                    s_agent_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(40000);
                    status_set(VOICE_ASSISTANT_THINKING, true, false, "思考中");
                    ESP_LOGI(TAG, "播放等待提示");
                    start_tts_text(THINKING_ANNOUNCEMENT, TTS_KIND_THINKING_ANNOUNCEMENT);
                }
            }
        }
        if ((s_phase == VOICE_TTS || s_phase == VOICE_WAIT_AGENT) && s_agent_request_id != 0U) {
            const esp_err_t agent_result = poll_agent_result();
            if (agent_result == ESP_ERR_TIMEOUT) report_error("对话响应超时");
            else if (agent_result != ESP_OK) report_error("对话服务失败");
        }
        if (s_phase == VOICE_TTS && s_mode == CLOUD_ASR) clear_client();
        if (s_phase == VOICE_TTS && s_client == NULL && !s_playback_finishing) {
            if (open_cloud(CLOUD_TTS) != ESP_OK) report_error("TTS 连接失败");
            else {
                s_tts_started = false; s_tts_audio_received = false; s_tts_finished = false;
                s_tts_session_started = false; s_tts_finish_sent = false; s_tts_text_offset = 0U;
                s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(REQUEST_TIMEOUT_MS);
            }
        }
        if (s_phase == VOICE_TTS && s_tts_session_started && !s_tts_finish_sent &&
            xTaskGetTickCount() >= s_tts_next_request) {
            const char *next = s_tts_text + s_tts_text_offset;
            if (*next == '\0') {
                if (send_tts_event(102U, true, "{}") != ESP_OK) callback_error("TTS 会话结束失败");
                else s_tts_finish_sent = true;
            } else {
                const size_t character_len = utf8_character_length(next);
                if (send_tts_task(next, character_len) != ESP_OK) callback_error("TTS 请求发送失败");
                else {
                    s_tts_text_offset += character_len;
                    s_tts_next_request = xTaskGetTickCount() + pdMS_TO_TICKS(5);
                }
            }
        }
        if (s_phase == VOICE_TTS && s_tts_finished) {
            audio_self_test_voice_playback_finish();
            clear_client();
            s_tts_finished = false;
            s_playback_finishing = true;
        }
        if (s_phase == VOICE_TTS && s_playback_finishing &&
            !audio_self_test_voice_playback_is_active()) {
            s_playback_finishing = false;
            if (s_tts_kind == TTS_KIND_THINKING_ANNOUNCEMENT) {
                if (s_agent_reply_ready) {
                    s_agent_reply_ready = false;
                    ESP_LOGI(TAG, "等待提示播放完成，播放 Agent 回复");
                    start_tts_text(s_agent_reply, TTS_KIND_AGENT_REPLY);
                } else {
                    s_phase = VOICE_WAIT_AGENT;
                    status_set(VOICE_ASSISTANT_THINKING, true, false, "思考中");
                }
            } else {
                s_tts_kind = TTS_KIND_NONE;
                s_phase = VOICE_READY;
                status_set(VOICE_ASSISTANT_READY, false, false, "");
            }
        }
        if (s_phase == VOICE_WAIT_AGENT && s_agent_reply_ready) {
            s_agent_reply_ready = false;
            ESP_LOGI(TAG, "Agent 回复已就绪，播放回复");
            start_tts_text(s_agent_reply, TTS_KIND_AGENT_REPLY);
        }
        if (s_phase == VOICE_TTS && !s_tts_finished && !s_playback_finishing &&
            xTaskGetTickCount() >= s_deadline) report_error("TTS 响应超时");
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t voice_assistant_init(void)
{
    memset(&s_status, 0, sizeof(s_status)); s_status.state = VOICE_ASSISTANT_DISABLED;
    s_phase = VOICE_READY; s_event_sem = xSemaphoreCreateBinary();
    if (s_event_sem == NULL) return ESP_ERR_NO_MEM;
    if (!configured()) {
        ESP_LOGW(TAG, "voice disabled; configure Volcengine voice key and selected assistant backend");
    }
    if (xTaskCreate(voice_task, "voice_cloud", 10240, NULL, 4, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void voice_assistant_update(bool play_active, bool programmer_owns_input, bool key0_pressed, uint16_t level_id)
{
    portENTER_CRITICAL(&s_lock);
    s_play_active = play_active; s_programmer_owns_input = programmer_owns_input; s_level_id = level_id;
    const bool pressed = play_active && !programmer_owns_input && key0_pressed;
    if (pressed && !s_previous_key_pressed) ++s_press_id;
    s_key_pressed = pressed; s_previous_key_pressed = key0_pressed;
    portEXIT_CRITICAL(&s_lock);
    if (play_active && (!s_session_play_active || s_session_level_id != level_id)) {
        assistant_router_begin_level_session(level_id);
        s_session_play_active = true;
        s_session_level_id = level_id;
    } else if (!play_active && s_session_play_active) {
        assistant_router_end_level_session();
        s_session_play_active = false;
    }
    if (play_active && !programmer_owns_input && s_phase == VOICE_READY && configured()) {
        status_set(VOICE_ASSISTANT_READY, true, false, "可说话");
    }
}

void voice_assistant_get_status(voice_assistant_status_t *status)
{
    if (status == NULL) return;
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}
