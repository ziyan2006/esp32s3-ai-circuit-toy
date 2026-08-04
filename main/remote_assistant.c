#include "remote_assistant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "c6_network_test.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "learning_activity.h"
#include "sdkconfig.h"
#include "tuco_agent.h"

#define REMOTE_TEXT_MAX 512U
#define REMOTE_REQUEST_TEXT_MAX 384U
#define REMOTE_RESPONSE_MAX 2048U
#define REMOTE_TOOL_ONLY_REPLY "请看亮起的提示，再完成这一步。"

static const char *TAG = "remote_assistant";
typedef struct {
    uint32_t id;
    uint16_t level;
    bool has_learning_activity;
    learning_activity_state_t learning_activity;
    char text[REMOTE_REQUEST_TEXT_MAX];
} remote_request_t;
typedef struct { bool ready; uint32_t id; esp_err_t err; char text[REMOTE_TEXT_MAX]; } remote_result_t;
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
    static const char *full_adder_roles[] = {"A", "B", "进位输入", "个位", "进位输出"};
    static const uint8_t binary_weights[] = {8U, 4U, 2U, 1U};
    const char *const *roles = state->kind == LEARNING_ACTIVITY_KIND_BINARY_SLOTS ? binary_roles :
                               state->kind == LEARNING_ACTIVITY_KIND_HALF_ADDER ? half_adder_roles :
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

static void store_result(uint32_t id, esp_err_t err, const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active_id == id) {
        s_result = (remote_result_t){ .ready = true, .id = id, .err = err };
        if (text != NULL) strlcpy(s_result.text, text, sizeof(s_result.text));
    }
    xSemaphoreGive(s_lock);
}

static esp_err_t request_remote(const remote_request_t *request, char *out, size_t out_size)
{
    char *snapshot = NULL;
    esp_err_t err = tuco_agent_build_circuit_snapshot(request->level, &snapshot);
    if (err != ESP_OK) return err;
    cJSON *root = cJSON_CreateObject();
    cJSON *circuit = cJSON_Parse(snapshot);
    cJSON_free(snapshot);
    if (root == NULL || circuit == NULL) { cJSON_Delete(root); cJSON_Delete(circuit); return ESP_ERR_NO_MEM; }
    char session[64];
    snprintf(session, sizeof(session), "fw-%u-%lu", (unsigned)request->level, (unsigned long)s_session_counter);
    cJSON_AddStringToObject(root, "session_id", session);
    cJSON_AddStringToObject(root, "user_text", request->text);
    cJSON_AddItemToObject(root, "circuit_snapshot", circuit);
    if (request->has_learning_activity) {
        cJSON *activity = create_learning_activity_json(&request->learning_activity);
        if (activity == NULL) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
        cJSON_AddItemToObject(root, "learning_activity", activity);
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = CONFIG_TUCO_REMOTE_ASSISTANT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_TUCO_REMOTE_ASSISTANT_TIMEOUT_MS,
        .buffer_size = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) { cJSON_free(body); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    err = esp_http_client_open(client, strlen(body));
    if (err == ESP_OK && esp_http_client_write(client, body, strlen(body)) < 0) err = ESP_FAIL;
    cJSON_free(body);
    if (err != ESP_OK) { esp_http_client_cleanup(client); return err; }
    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    char response[REMOTE_RESPONSE_MAX] = {0};
    int total = 0;
    while (total < (int)sizeof(response) - 1) {
        const int read = esp_http_client_read(client, response + total, sizeof(response) - 1U - (size_t)total);
        if (read <= 0) break;
        total += read;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (status < 200 || status >= 300 || total == 0) return ESP_FAIL;

    cJSON *reply = cJSON_Parse(response);
    if (reply == NULL) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(reply, "assistant_text");
    const cJSON *tool = cJSON_GetObjectItemCaseSensitive(reply, "tool_call");
    const bool has_text = cJSON_IsString(text) && text->valuestring != NULL &&
                          text->valuestring[0] != '\0';
    if (has_text) {
        strlcpy(out, text->valuestring, out_size);
    }
    const bool has_tool = cJSON_IsObject(tool);
    if (!has_tool) {
        cJSON_Delete(reply);
        return has_text ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    if (request->has_learning_activity) {
        ESP_LOGW(TAG, "ignored tool call during learning activity request=%lu",
                 (unsigned long)request->id);
        cJSON_Delete(reply);
        if (has_text) return ESP_OK;
        strlcpy(out, "这一轮我们先用 0 和 1 想一想，不需要操作电路。", out_size);
        return ESP_OK;
    }
    const cJSON *name = tool == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(tool, "name");
    const cJSON *arguments = tool == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(tool, "arguments");
    if (!cJSON_IsString(name) || name->valuestring == NULL || arguments == NULL) {
        cJSON_Delete(reply);
        return has_text ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    char tool_name[64];
    strlcpy(tool_name, name->valuestring, sizeof(tool_name));
    char *arguments_json = cJSON_PrintUnformatted(arguments);
    cJSON_Delete(reply);
    if (arguments_json == NULL) return has_text ? ESP_OK : ESP_ERR_NO_MEM;
    err = tuco_agent_execute_remote_tool(request->id, tool_name, arguments_json);
    cJSON_free(arguments_json);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "remote tool request=%lu name=%s failed: %s",
                 (unsigned long)request->id, tool_name, esp_err_to_name(err));
    }
    if (has_text) return ESP_OK;
    if (err != ESP_OK) return err;
    strlcpy(out, REMOTE_TOOL_ONLY_REPLY, out_size);
    return ESP_OK;
}

static void remote_task(void *arg)
{
    remote_request_t *request = arg;
    char text[REMOTE_TEXT_MAX] = {0};
    const esp_err_t err = request_remote(request, text, sizeof(text));
    if (err != ESP_OK && text[0] == '\0') strlcpy(text, "后端助教暂时无法连接", sizeof(text));
    ESP_LOGI(TAG, "request=%lu result=%s", (unsigned long)request->id, esp_err_to_name(err));
    store_result(request->id, err, text);
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

esp_err_t remote_assistant_take_result(uint32_t request_id, char *output, size_t output_size)
{
    if (s_lock == NULL || output == NULL || output_size == 0U || request_id == 0U) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_result.ready || s_result.id != request_id) { xSemaphoreGive(s_lock); return ESP_ERR_NOT_FOUND; }
    strlcpy(output, s_result.text, output_size);
    const esp_err_t err = s_result.err;
    s_active_id = 0U;
    memset(&s_result, 0, sizeof(s_result));
    xSemaphoreGive(s_lock);
    return err;
}

void remote_assistant_cancel(uint32_t request_id)
{
    if (s_lock == NULL || request_id == 0U) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active_id == request_id) { s_active_id = 0U; memset(&s_result, 0, sizeof(s_result)); }
    xSemaphoreGive(s_lock);
}
