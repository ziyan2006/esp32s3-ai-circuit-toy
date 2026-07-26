#include "progress_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "c6_network_test.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "progress_sync";

#define PROGRESS_SYNC_LEVEL_COUNT 16U
#define PROGRESS_SYNC_RESPONSE_CAPACITY 4096U
#define PROGRESS_SYNC_TIMEOUT_MS 12000U
#define PROGRESS_SYNC_ASYNC_POLL_MS 20U

typedef struct {
    uint16_t level_id;
    const char *level_key;
} progress_sync_level_t;

typedef struct {
    char body[PROGRESS_SYNC_RESPONSE_CAPACITY];
    size_t used;
    bool truncated;
} progress_sync_response_t;

typedef struct {
    bool completed[PROGRESS_SYNC_LEVEL_COUNT];
    uint8_t completed_count;
} progress_sync_snapshot_t;

static const progress_sync_level_t s_levels[PROGRESS_SYNC_LEVEL_COUNT] = {
    {102U, "Level 1-1"}, {103U, "Level 1-2"},
    {201U, "Level 2-1"}, {202U, "Level 2-2"}, {203U, "Level 2-3"},
    {301U, "Level 3-1"}, {302U, "Level 3-2"},
    {401U, "Level 4-1"}, {402U, "Level 4-2"}, {403U, "Level 4-3"},
    {501U, "Level 5-1"}, {502U, "Level 5-2"}, {503U, "Level 5-3"},
    {504U, "Level 5-4"}, {601U, "Level 6-1"}, {602U, "Level 6-2"},
};

static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static progress_sync_status_t s_status = {.state = PROGRESS_SYNC_READY};
static progress_sync_snapshot_t s_snapshot;

static int32_t find_node_index(const campaign_node_t *nodes, uint16_t node_count,
                               uint16_t level_id)
{
    for (uint16_t index = 0U; index < node_count; ++index) {
        if (nodes[index].id == level_id) return (int32_t)index;
    }
    return -1;
}

static esp_err_t progress_sync_http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == NULL ||
        event->data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }

    progress_sync_response_t *response = (progress_sync_response_t *)event->user_data;
    const size_t available = sizeof(response->body) - 1U - response->used;
    const size_t copied = (size_t)event->data_len < available ?
                              (size_t)event->data_len : available;
    if (copied > 0U) {
        memcpy(response->body + response->used, event->data, copied);
        response->used += copied;
        response->body[response->used] = '\0';
    }
    if (copied != (size_t)event->data_len) response->truncated = true;
    return ESP_OK;
}

static bool response_is_success(const progress_sync_response_t *response)
{
    if (response->truncated || response->used == 0U) return false;
    cJSON *root = cJSON_ParseWithLength(response->body, response->used);
    if (root == NULL) return false;

    const cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *level_count = data == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(data, "levelCount");
    const bool valid = cJSON_IsTrue(success) && cJSON_IsNumber(level_count) &&
                       level_count->valueint == PROGRESS_SYNC_LEVEL_COUNT;
    cJSON_Delete(root);
    return valid;
}

static void log_error_response(const progress_sync_response_t *response)
{
    if (response->truncated || response->used == 0U) return;
    cJSON *root = cJSON_ParseWithLength(response->body, response->used);
    if (root == NULL) return;

    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    const cJSON *code = error == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(error, "code");
    const cJSON *message = error == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(error, "message");
    ESP_LOGW(TAG, "server response: code=%s message=%s",
             cJSON_IsString(code) ? code->valuestring : "-",
             cJSON_IsString(message) ? message->valuestring : "-");
    cJSON_Delete(root);
}

static esp_err_t build_request_body(const progress_sync_snapshot_t *snapshot, char **out_body)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *levels = cJSON_CreateArray();
    if (root == NULL || levels == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(levels);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "schemaVersion", 1);
    cJSON_AddItemToObject(root, "levels", levels);
    for (uint8_t index = 0U; index < PROGRESS_SYNC_LEVEL_COUNT; ++index) {
        cJSON *level = cJSON_CreateObject();
        if (level == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddNumberToObject(level, "levelId", s_levels[index].level_id);
        cJSON_AddStringToObject(level, "levelKey", s_levels[index].level_key);
        cJSON_AddBoolToObject(level, "completed", snapshot->completed[index]);
        cJSON_AddItemToArray(levels, level);
    }

    *out_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *out_body == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static esp_err_t upload_snapshot(const progress_sync_snapshot_t *snapshot,
                                 int *out_http_status, bool *out_valid_response)
{
    char *body = NULL;
    esp_err_t err = build_request_body(snapshot, &body);
    if (err != ESP_OK) return err;

    progress_sync_response_t *response = calloc(1U, sizeof(*response));
    if (response == NULL) {
        cJSON_free(body);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {
        .url = CONFIG_TUCO_PROGRESS_SYNC_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = PROGRESS_SYNC_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = progress_sync_http_event,
        .user_data = response,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .is_async = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response);
        cJSON_free(body);
        return ESP_ERR_NO_MEM;
    }

    char authorization[sizeof("Bearer ") + sizeof(CONFIG_TUCO_PROGRESS_SYNC_SECRET)] = {0};
    const int auth_length = snprintf(authorization, sizeof(authorization), "Bearer %s",
                                     CONFIG_TUCO_PROGRESS_SYNC_SECRET);
    if (auth_length < 0 || (size_t)auth_length >= sizeof(authorization)) {
        esp_http_client_cleanup(client);
        free(response);
        cJSON_free(body);
        return ESP_ERR_INVALID_SIZE;
    }

    err = esp_http_client_set_header(client, "Authorization", authorization);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) err = esp_http_client_set_post_field(client, body, strlen(body));
    if (err == ESP_OK) {
        const int64_t deadline_us = esp_timer_get_time() +
                                    (int64_t)PROGRESS_SYNC_TIMEOUT_MS * 1000LL;
        do {
            err = esp_http_client_perform(client);
            if (err == ESP_ERR_HTTP_EAGAIN) {
                vTaskDelay(pdMS_TO_TICKS(PROGRESS_SYNC_ASYNC_POLL_MS));
            }
        } while (err == ESP_ERR_HTTP_EAGAIN && esp_timer_get_time() < deadline_us);
        if (err == ESP_ERR_HTTP_EAGAIN) err = ESP_ERR_TIMEOUT;
    }
    *out_http_status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    *out_valid_response = err == ESP_OK && *out_http_status == 200 &&
                          response_is_success(response);
    ESP_LOGI(TAG, "HTTP result: err=%s status=%d response=%u truncated=%d valid=%d",
             esp_err_to_name(err), *out_http_status, (unsigned int)response->used,
             response->truncated, *out_valid_response);
    if (!*out_valid_response && response->used > 0U) log_error_response(response);
    esp_http_client_cleanup(client);
    free(response);
    cJSON_free(body);
    return err;
}

static void set_status(progress_sync_state_t state, uint8_t attempts, int http_status)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = state;
    s_status.attempt_count = attempts;
    s_status.http_status = http_status;
    xSemaphoreGive(s_mutex);
}

static void progress_sync_task(void *arg)
{
    (void)arg;
    static const uint32_t retry_delays_ms[] = {1000U, 3000U, 10000U};
    progress_sync_snapshot_t snapshot;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snapshot = s_snapshot;
    xSemaphoreGive(s_mutex);

    for (uint8_t attempt = 0U; attempt <= 3U; ++attempt) {
        int http_status = 0;
        bool valid_response = false;
        set_status(PROGRESS_SYNC_UPLOADING, attempt + 1U, http_status);
        const esp_err_t err = upload_snapshot(&snapshot, &http_status, &valid_response);
        ESP_LOGI(TAG,
                 "upload attempt %u: err=%s http=%d valid=%d",
                 attempt + 1U, esp_err_to_name(err), http_status, valid_response);
        if (err == ESP_OK && valid_response) {
            set_status(PROGRESS_SYNC_SUCCEEDED, attempt + 1U, http_status);
            break;
        }

        const bool retryable = err != ESP_OK || http_status == 500;
        if (!retryable) {
            set_status(http_status > 0 ? PROGRESS_SYNC_SERVER_REJECTED : PROGRESS_SYNC_FAILED,
                       attempt + 1U, http_status);
            break;
        }
        if (attempt == 3U) {
            set_status(PROGRESS_SYNC_FAILED, attempt + 1U, http_status);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[attempt]));
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_task = NULL;
    xSemaphoreGive(s_mutex);
    vTaskDelete(NULL);
}

esp_err_t progress_sync_init(void)
{
    if (s_mutex != NULL) return ESP_OK;
    s_mutex = xSemaphoreCreateMutex();
    return s_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t progress_sync_request(const campaign_node_t *nodes,
                                uint16_t node_count,
                                const bool *completed)
{
    if (nodes == NULL || completed == NULL || s_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (!CONFIG_TUCO_PROGRESS_SYNC_ENABLED || CONFIG_TUCO_PROGRESS_SYNC_SECRET[0] == '\0') {
        set_status(PROGRESS_SYNC_CONFIGURATION_ERROR, 0U, 0);
        return ESP_ERR_INVALID_STATE;
    }
    if (!c6_network_is_connected()) {
        set_status(PROGRESS_SYNC_NETWORK_UNAVAILABLE, 0U, 0);
        return ESP_ERR_INVALID_STATE;
    }

    progress_sync_snapshot_t snapshot = {0};
    for (uint8_t level = 0U; level < PROGRESS_SYNC_LEVEL_COUNT; ++level) {
        const int32_t node_index = find_node_index(nodes, node_count, s_levels[level].level_id);
        if (node_index < 0) return ESP_ERR_NOT_FOUND;
        snapshot.completed[level] = completed[node_index];
        if (snapshot.completed[level]) ++snapshot.completed_count;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_task != NULL) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_snapshot = snapshot;
    s_status = (progress_sync_status_t){
        .state = PROGRESS_SYNC_UPLOADING,
        .completed_count = snapshot.completed_count,
    };
    const BaseType_t created = xTaskCreate(progress_sync_task, "progress_sync", 8192,
                                           NULL, 3, &s_task);
    if (created != pdPASS) {
        s_task = NULL;
        s_status.state = PROGRESS_SYNC_FAILED;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "queued complete cloud snapshot: %u/%u levels completed",
             snapshot.completed_count, PROGRESS_SYNC_LEVEL_COUNT);
    return ESP_OK;
}

void progress_sync_get_status(progress_sync_status_t *out_status)
{
    if (out_status == NULL) return;
    if (s_mutex == NULL) {
        *out_status = s_status;
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out_status = s_status;
    xSemaphoreGive(s_mutex);
}
