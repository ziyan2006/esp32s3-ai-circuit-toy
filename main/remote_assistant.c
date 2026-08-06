#include "remote_assistant.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "assistant_diagnostics.h"
#include "c6_network_test.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "learning_activity.h"
#include "sdkconfig.h"
#include "tuco_agent.h"

#define REMOTE_TEXT_MAX 512U
#define REMOTE_REQUEST_TEXT_MAX 384U
#define REMOTE_RESPONSE_MAX 2048U
#define REMOTE_HTTP_TIMEOUT_MIN_MS 45000U
#define REMOTE_TOOL_ONLY_REPLY "请看亮起的提示，再完成这一步。"

static const char *TAG = "remote_assistant";
typedef struct {
    uint32_t id;
    uint16_t level;
    bool has_learning_activity;
    learning_activity_state_t learning_activity;
    char text[REMOTE_REQUEST_TEXT_MAX];
} remote_request_t;
typedef struct {
    bool ready;
    uint32_t id;
    assistant_response_t response;
} remote_result_t;
static SemaphoreHandle_t s_lock;
static uint32_t s_next_id;
static uint32_t s_active_id;
static uint32_t s_session_counter;
static remote_result_t s_result;

static const char *learning_activity_kind_name(learning_activity_kind_t kind)
{
    switch (kind) {
    case LEARNING_ACTIVITY_KIND_BINARY_SLOTS: return "binary_slots";
    case LEARNING_ACTIVITY_KIND_HALF_ADDER: return "half_adder";
    case LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY: return "three_input_parity";
    case LEARNING_ACTIVITY_KIND_THREE_INPUT_CARRY: return "three_input_carry";
    case LEARNING_ACTIVITY_KIND_FULL_ADDER: return "full_adder";
    default: return NULL;
    }
}

static void add_activity_bits(cJSON *object, const char *name, uint8_t packed_bits,
                              uint8_t slot_count)
{
    cJSON *bits = cJSON_AddArrayToObject(object, name);
    if (bits == NULL) return;
    for (uint8_t column = 0U; column < slot_count; ++column) {
        const uint8_t shift = slot_count - 1U - column;
        cJSON_AddItemToArray(bits, cJSON_CreateNumber((packed_bits >> shift) & 1U));
    }
}

static cJSON *create_learning_activity_json(const learning_activity_state_t *state)
{
    static const char *binary_roles[] = {"8", "4", "2", "1"};
    static const char *half_adder_roles[] = {"A", "B", "个位", "进位"};
    static const char *three_input_parity_roles[] = {"A", "B", "进位输入"};
    static const char *full_adder_roles[] = {"A", "B", "进位输入", "个位", "进位输出"};
    static const uint8_t binary_weights[] = {8U, 4U, 2U, 1U};
    const char *const *roles = state->kind == LEARNING_ACTIVITY_KIND_BINARY_SLOTS ? binary_roles :
                               state->kind == LEARNING_ACTIVITY_KIND_HALF_ADDER ? half_adder_roles :
                               (state->kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY ||
                                state->kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_CARRY) ?
                                   three_input_parity_roles :
                               state->kind == LEARNING_ACTIVITY_KIND_FULL_ADDER ? full_adder_roles : NULL;
    const char *kind = learning_activity_kind_name(state->kind);
    if (roles == NULL || kind == NULL || state->slot_count == 0U) return NULL;

    cJSON *activity = cJSON_CreateObject();
    cJSON *role_array = cJSON_AddArrayToObject(activity, "slot_roles");
    if (activity == NULL || role_array == NULL) { cJSON_Delete(activity); return NULL; }
    cJSON_AddStringToObject(activity, "kind", kind);
    cJSON_AddStringToObject(activity, "stage", "practice");
    cJSON_AddNumberToObject(activity, "round_index", state->round_index + 1U);
    cJSON_AddNumberToObject(activity, "round_total", state->round_total);
    for (uint8_t column = 0U; column < state->slot_count; ++column) {
        cJSON_AddItemToArray(role_array, cJSON_CreateString(roles[column]));
    }
    add_activity_bits(activity, "slot_bits", state->bits, state->slot_count);
    add_activity_bits(activity, "target_bits", state->target_bits, state->slot_count);
    if (state->kind == LEARNING_ACTIVITY_KIND_BINARY_SLOTS) {
        cJSON *weights = cJSON_AddArrayToObject(activity, "slot_weights");
        if (weights == NULL) { cJSON_Delete(activity); return NULL; }
        for (uint8_t column = 0U; column < state->slot_count; ++column) {
            cJSON_AddItemToArray(weights, cJSON_CreateNumber(binary_weights[column]));
        }
        cJSON_AddNumberToObject(activity, "target_decimal", state->target_decimal);
        cJSON_AddNumberToObject(activity, "current_decimal", state->current_decimal);
    } else if (state->kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_PARITY ||
               state->kind == LEARNING_ACTIVITY_KIND_THREE_INPUT_CARRY) {
        cJSON_AddNumberToObject(activity, "target_decimal", state->target_decimal);
        cJSON_AddNumberToObject(activity, "current_decimal", state->current_decimal);
    }
    cJSON_AddBoolToObject(activity, "solved", state->solved);
    cJSON_AddBoolToObject(activity, "complete", state->complete);
    return activity;
}

bool remote_assistant_is_configured(void)
{
    return strncmp(CONFIG_TUCO_REMOTE_ASSISTANT_URL, "http://", 7U) == 0 ||
           strncmp(CONFIG_TUCO_REMOTE_ASSISTANT_URL, "https://", 8U) == 0;
}

static void store_result(uint32_t id, const assistant_response_t *response)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active_id == id) {
        s_result = (remote_result_t){ .ready = true, .id = id };
        if (response != NULL) s_result.response = *response;
    }
    xSemaphoreGive(s_lock);
}

static void finish_elapsed(assistant_response_t *response, int64_t started_us)
{
    response->elapsed_ms = (uint32_t)((esp_timer_get_time() - started_us) / 1000);
}

static void fail_transport(assistant_response_t *response, esp_err_t err, const char *detail)
{
    const bool timeout = err == ESP_ERR_TIMEOUT;
    assistant_response_fail(response,
                            timeout ? ASSISTANT_ERROR_REQUEST_TIMEOUT :
                                      ASSISTANT_ERROR_CONNECT_FAILED,
                            ASSISTANT_STAGE_BACKEND_CONNECT,
                            timeout ? ESP_ERR_TIMEOUT : err,
                            0, true, detail);
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(value) && value->valuestring != NULL ? value->valuestring : NULL;
}

static void copy_trace_id(const cJSON *reply, assistant_response_t *response)
{
    const char *trace_id = json_string(reply, "trace_id");
    if (trace_id != NULL) strlcpy(response->trace_id, trace_id, sizeof(response->trace_id));
}

static void parse_backend_error(const cJSON *reply, int status,
                                assistant_response_t *response)
{
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(reply, "error");
    const char *backend_code = cJSON_IsObject(error) ? json_string(error, "code") : NULL;
    const char *backend_stage = cJSON_IsObject(error) ? json_string(error, "stage") : NULL;
    const char *message = cJSON_IsObject(error) ? json_string(error, "message") : NULL;
    const cJSON *retryable_json = cJSON_IsObject(error) ?
        cJSON_GetObjectItemCaseSensitive(error, "retryable") : NULL;
    const bool retryable = cJSON_IsBool(retryable_json) ? cJSON_IsTrue(retryable_json) :
                           status == 429 || status >= 500;
    const assistant_error_code_t code = assistant_error_from_backend(backend_code, status);
    const assistant_stage_t stage = code == ASSISTANT_ERROR_ACTION_FAILED ?
                                        ASSISTANT_STAGE_ACTION :
                                    code == ASSISTANT_ERROR_INVALID_PROTOCOL ?
                                        ASSISTANT_STAGE_BACKEND_PROTOCOL :
                                        ASSISTANT_STAGE_BACKEND_HTTP;
    copy_trace_id(reply, response);
    assistant_response_fail(response, code, stage,
                            code == ASSISTANT_ERROR_REQUEST_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL,
                            status, retryable,
                            message == NULL ? "backend returned an error" : message);
    ESP_LOGW(TAG,
             "request=%lu backend_error code=%s backend_stage=%s http_status=%d retryable=%d trace_id=%s",
             (unsigned long)response->request_id,
             backend_code == NULL ? "(missing)" : backend_code,
             backend_stage == NULL ? "(missing)" : backend_stage,
             status, retryable, response->trace_id);
}

static void request_remote(const remote_request_t *request, assistant_response_t *response)
{
    const int64_t started_us = esp_timer_get_time();
    assistant_response_reset(response, request->id);
    char *snapshot = NULL;
    esp_err_t err = tuco_agent_build_circuit_snapshot(request->level, &snapshot);
    if (err != ESP_OK) {
        assistant_response_fail(response, ASSISTANT_ERROR_INVALID_PROTOCOL,
                                ASSISTANT_STAGE_BACKEND_PROTOCOL, err, 0, false,
                                "failed to build circuit snapshot");
        finish_elapsed(response, started_us);
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *circuit = cJSON_Parse(snapshot);
    cJSON_free(snapshot);
    if (root == NULL || circuit == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(circuit);
        assistant_response_fail(response, ASSISTANT_ERROR_INVALID_JSON,
                                ASSISTANT_STAGE_BACKEND_PROTOCOL, ESP_ERR_NO_MEM,
                                0, false, "failed to encode circuit snapshot");
        finish_elapsed(response, started_us);
        return;
    }
    char session[64];
    snprintf(session, sizeof(session), "fw-%u-%lu", (unsigned)request->level, (unsigned long)s_session_counter);
    cJSON_AddStringToObject(root, "session_id", session);
    cJSON_AddStringToObject(root, "user_text", request->text);
    cJSON_AddItemToObject(root, "circuit_snapshot", circuit);
    if (request->has_learning_activity) {
        cJSON *activity = create_learning_activity_json(&request->learning_activity);
        if (activity == NULL) {
            cJSON_Delete(root);
            assistant_response_fail(response, ASSISTANT_ERROR_INVALID_JSON,
                                    ASSISTANT_STAGE_BACKEND_PROTOCOL, ESP_ERR_NO_MEM,
                                    0, false, "failed to encode learning activity");
            finish_elapsed(response, started_us);
            return;
        }
        cJSON_AddItemToObject(root, "learning_activity", activity);
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        assistant_response_fail(response, ASSISTANT_ERROR_INVALID_JSON,
                                ASSISTANT_STAGE_BACKEND_PROTOCOL, ESP_ERR_NO_MEM,
                                0, false, "failed to encode request body");
        finish_elapsed(response, started_us);
        return;
    }

    const int timeout_ms = CONFIG_TUCO_REMOTE_ASSISTANT_TIMEOUT_MS < REMOTE_HTTP_TIMEOUT_MIN_MS ?
        REMOTE_HTTP_TIMEOUT_MIN_MS : CONFIG_TUCO_REMOTE_ASSISTANT_TIMEOUT_MS;
    ESP_LOGI(TAG, "request=%lu POST timeout=%dms", (unsigned long)request->id, timeout_ms);
    esp_http_client_config_t config = {
        .url = CONFIG_TUCO_REMOTE_ASSISTANT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
        .buffer_size = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        cJSON_free(body);
        fail_transport(response, ESP_ERR_NO_MEM, "HTTP client initialization failed");
        finish_elapsed(response, started_us);
        return;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    err = esp_http_client_open(client, strlen(body));
    const size_t body_length = strlen(body);
    if (err == ESP_OK && esp_http_client_write(client, body, body_length) != (int)body_length) {
        err = errno == ETIMEDOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }
    cJSON_free(body);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "request=%lu HTTP open/write failed: %s",
                 (unsigned long)request->id, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        fail_transport(response, err, "HTTP open or write failed");
        finish_elapsed(response, started_us);
        return;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        fail_transport(response, errno == ETIMEDOUT ? ESP_ERR_TIMEOUT : ESP_FAIL,
                       "HTTP response headers failed");
        finish_elapsed(response, started_us);
        return;
    }
    const int status = esp_http_client_get_status_code(client);
    response->http_status = status;
    char response_body[REMOTE_RESPONSE_MAX] = {0};
    int total = 0;
    int read_error = 0;
    while (total < (int)sizeof(response_body) - 1) {
        const int read = esp_http_client_read(client, response_body + total,
                                              sizeof(response_body) - 1U - (size_t)total);
        if (read < 0) { read_error = read; break; }
        if (read == 0) break;
        total += read;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "request=%lu HTTP status=%d bytes=%d", (unsigned long)request->id, status, total);
    if (read_error < 0) {
        fail_transport(response, errno == ETIMEDOUT ? ESP_ERR_TIMEOUT : ESP_FAIL,
                       "HTTP response read failed");
        finish_elapsed(response, started_us);
        return;
    }
    if (total == 0) {
        assistant_response_fail(response, ASSISTANT_ERROR_EMPTY_RESPONSE,
                                ASSISTANT_STAGE_BACKEND_HTTP, ESP_ERR_INVALID_SIZE,
                                status, true, "empty HTTP body");
        finish_elapsed(response, started_us);
        return;
    }

    cJSON *reply = cJSON_Parse(response_body);
    if (reply == NULL) {
        if (status < 200 || status >= 300) {
            assistant_response_fail(response, assistant_error_from_backend(NULL, status),
                                    ASSISTANT_STAGE_BACKEND_HTTP, ESP_FAIL,
                                    status, status == 429 || status >= 500,
                                    "HTTP error response is not JSON");
        } else {
            assistant_response_fail(response, ASSISTANT_ERROR_INVALID_JSON,
                                    ASSISTANT_STAGE_BACKEND_PROTOCOL,
                                    ESP_ERR_INVALID_RESPONSE, status, false,
                                    "response body is not JSON");
        }
        finish_elapsed(response, started_us);
        return;
    }
    if (status < 200 || status >= 300) {
        parse_backend_error(reply, status, response);
        cJSON_Delete(reply);
        finish_elapsed(response, started_us);
        return;
    }
    copy_trace_id(reply, response);
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(reply, "assistant_text");
    const cJSON *tool = cJSON_GetObjectItemCaseSensitive(reply, "tool_call");
    const bool has_text = cJSON_IsString(text) && text->valuestring != NULL &&
                          text->valuestring[0] != '\0';
    if (has_text) {
        strlcpy(response->text, text->valuestring, sizeof(response->text));
    }
    const bool has_tool = cJSON_IsObject(tool);
    if (!has_tool) {
        cJSON_Delete(reply);
        if (!has_text) {
            assistant_response_fail(response, ASSISTANT_ERROR_INVALID_PROTOCOL,
                                    ASSISTANT_STAGE_BACKEND_PROTOCOL,
                                    ESP_ERR_INVALID_RESPONSE, status, false,
                                    "success response has neither text nor tool call");
        }
        finish_elapsed(response, started_us);
        return;
    }
    if (request->has_learning_activity) {
        ESP_LOGW(TAG, "ignored tool call during learning activity request=%lu",
                 (unsigned long)request->id);
        cJSON_Delete(reply);
        if (!has_text) {
            strlcpy(response->text, "这一轮我们先用 0 和 1 想一想，不需要操作电路。",
                    sizeof(response->text));
        }
        finish_elapsed(response, started_us);
        return;
    }
    const cJSON *name = tool == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(tool, "name");
    const cJSON *arguments = tool == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(tool, "arguments");
    if (!cJSON_IsString(name) || name->valuestring == NULL || arguments == NULL) {
        cJSON_Delete(reply);
        if (!has_text) {
            assistant_response_fail(response, ASSISTANT_ERROR_INVALID_PROTOCOL,
                                    ASSISTANT_STAGE_BACKEND_PROTOCOL,
                                    ESP_ERR_INVALID_RESPONSE, status, false,
                                    "tool call is missing name or arguments");
        }
        finish_elapsed(response, started_us);
        return;
    }
    char tool_name[64];
    strlcpy(tool_name, name->valuestring, sizeof(tool_name));
    char *arguments_json = cJSON_PrintUnformatted(arguments);
    cJSON_Delete(reply);
    if (arguments_json == NULL) {
        if (!has_text) {
            assistant_response_fail(response, ASSISTANT_ERROR_INVALID_PROTOCOL,
                                    ASSISTANT_STAGE_BACKEND_PROTOCOL, ESP_ERR_NO_MEM,
                                    status, false, "tool arguments cannot be encoded");
        }
        finish_elapsed(response, started_us);
        return;
    }
    err = tuco_agent_execute_remote_tool(request->id, tool_name, arguments_json);
    cJSON_free(arguments_json);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "remote tool request=%lu name=%s failed: %s",
                 (unsigned long)request->id, tool_name, esp_err_to_name(err));
        assistant_response_fail(response, ASSISTANT_ERROR_ACTION_FAILED,
                                ASSISTANT_STAGE_ACTION, err, status, true,
                                "remote tool execution failed");
        finish_elapsed(response, started_us);
        return;
    }
    if (!has_text) strlcpy(response->text, REMOTE_TOOL_ONLY_REPLY, sizeof(response->text));
    finish_elapsed(response, started_us);
}

static void remote_task(void *arg)
{
    remote_request_t *request = arg;
    assistant_response_t response;
    request_remote(request, &response);
    ESP_LOGI(TAG,
             "request=%lu result_code=%d stage=%s esp_error=%s http_status=%d elapsed_ms=%lu trace_id=%s",
             (unsigned long)request->id, response.error_code,
             assistant_stage_name(response.stage), esp_err_to_name(response.esp_error),
             response.http_status, (unsigned long)response.elapsed_ms, response.trace_id);
    store_result(request->id, &response);
    free(request);
    vTaskDelete(NULL);
}

esp_err_t remote_assistant_init(void)
{
    if (s_lock != NULL) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    return s_lock == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

void remote_assistant_begin_level_session(uint16_t level_id)
{
    (void)level_id;
    ++s_session_counter;
    if (s_session_counter == 0U) ++s_session_counter;
}

void remote_assistant_end_level_session(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_active_id = 0U;
    memset(&s_result, 0, sizeof(s_result));
    xSemaphoreGive(s_lock);
}

esp_err_t remote_assistant_submit(const char *text, uint16_t level_id, uint32_t *out_request_id)
{
    if (out_request_id != NULL) *out_request_id = 0U;
    if (s_lock == NULL || text == NULL || text[0] == '\0' || !remote_assistant_is_configured() || !c6_network_is_connected()) return ESP_ERR_INVALID_STATE;
    remote_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active_id != 0U) { xSemaphoreGive(s_lock); free(request); return ESP_ERR_INVALID_STATE; }
    request->id = ++s_next_id;
    if (request->id == 0U) request->id = ++s_next_id;
    request->level = level_id;
    request->has_learning_activity = learning_activity_get_state(&request->learning_activity);
    strlcpy(request->text, text, sizeof(request->text));
    s_active_id = request->id;
    memset(&s_result, 0, sizeof(s_result));
    xSemaphoreGive(s_lock);
    const uint32_t request_id = request->id;
    if (xTaskCreate(remote_task, "remote_assist", 8192, request, 4, NULL) != pdPASS) {
        remote_assistant_cancel(request_id);
        free(request);
        return ESP_ERR_NO_MEM;
    }
    if (out_request_id != NULL) *out_request_id = request_id;
    return ESP_OK;
}

esp_err_t remote_assistant_take_response(uint32_t request_id,
                                         assistant_response_t *out_response)
{
    if (s_lock == NULL || out_response == NULL || request_id == 0U) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_result.ready || s_result.id != request_id) { xSemaphoreGive(s_lock); return ESP_ERR_NOT_FOUND; }
    *out_response = s_result.response;
    s_active_id = 0U;
    memset(&s_result, 0, sizeof(s_result));
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void remote_assistant_cancel(uint32_t request_id)
{
    if (s_lock == NULL || request_id == 0U) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active_id == request_id) { s_active_id = 0U; memset(&s_result, 0, sizeof(s_result)); }
    xSemaphoreGive(s_lock);
}
