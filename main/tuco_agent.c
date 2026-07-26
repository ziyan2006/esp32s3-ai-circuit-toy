#include "tuco_agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_ui.h"
#include "board_mapping.h"
#include "board_snapshot.h"
#include "c6_network_test.h"
#include "cJSON.h"
#include "claw_core.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "level_rules.h"
#include "play_mode.h"
#include "sdkconfig.h"
#include "tuco_port_highlight.h"

static const char *TAG = "tuco_agent";

#define TUCO_AGENT_REPLY_MAX 512U
#define TUCO_AGENT_REQUEST_TIMEOUT_MS 40000U
#define TUCO_AGENT_CIRCUIT_CONTEXT_MAX 8192U
#define TUCO_AGENT_MAX_REPLY_CHARS 40U

#define TUCO_AGENT_HIGHLIGHT_REPLY "把亮起的两个端口连起来就可以啦"
#define TUCO_AGENT_UNCLEAR_REPLY "我没明白，换个说法试试。"

static const char s_tools_json[] =
    "[{\"type\":\"function\",\"function\":{\"name\":\"highlight_ports\","
    "\"description\":\"仅当玩家明确想得到当前电路下一步接线提示时调用。"
    "依据circuit_snapshot中已放置模块的ports和edges，自行选择一对未连接的输出端到输入端；每轮只能调用一次。\","
    "\"parameters\":{\"type\":\"object\",\"additionalProperties\":false,"
    "\"properties\":{\"output_port\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":63},"
    "\"input_port\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":63}},"
    "\"required\":[\"output_port\",\"input_port\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"highlight_empty_slot\","
    "\"description\":\"仅当玩家明确想得到电路提示、没有可用未连接输出端到输入端组合，"
    "且原因是需要新增模块时调用；缺少INPUT或OUTPUT积木是典型场景。"
    "依据circuit_snapshot选择空槽及已解锁积木；每轮只能调用一次。\","
    "\"parameters\":{\"type\":\"object\",\"additionalProperties\":false,"
    "\"properties\":{\"slot\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":15},"
    "\"gate\":{\"type\":\"string\",\"enum\":[\"INPUT\",\"OUTPUT\",\"NOT\",\"AND\",\"OR\",\"NAND\",\"NOR\",\"XOR\",\"XNOR\"]}},"
    "\"required\":[\"slot\",\"gate\"]}}}]";

typedef struct {
    bool ready;
    bool failed;
    uint32_t request_id;
    char text[TUCO_AGENT_REPLY_MAX];
} tuco_agent_result_t;

static claw_core_handle_t s_core;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_result_task;
static uint32_t s_next_request_id;
static uint32_t s_active_request_id;
static uint32_t s_highlight_request_id;
static uint16_t s_active_level_id;
static uint16_t s_session_level_id;
static bool s_session_active;
static char s_active_user_text[TUCO_AGENT_REPLY_MAX];
static char *s_history_json;
static tuco_agent_result_t s_result;

static bool select_preferred_empty_slot(const board_snapshot_t *snapshot,
                                        ssd1315_gate_t gate,
                                        uint8_t requested_slot,
                                        uint8_t *out_slot);

static bool agent_credentials_configured(void)
{
#if CONFIG_TUCO_AGENT_ENABLED
    return CONFIG_TUCO_AGENT_DEEPSEEK_API_KEY[0] != '\0';
#else
    return false;
#endif
}

bool tuco_agent_is_configured(void)
{
    return agent_credentials_configured();
}

bool tuco_agent_is_busy(void)
{
    bool busy = false;
    if (s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    busy = s_active_request_id != 0U;
    xSemaphoreGive(s_lock);
    return busy;
}

static const char *port_side_name(board_port_side_t side)
{
    switch (side) {
    case BOARD_PORT_SIDE_UP: return "up";
    case BOARD_PORT_SIDE_DOWN: return "down";
    case BOARD_PORT_SIDE_LEFT: return "left";
    case BOARD_PORT_SIDE_RIGHT: return "right";
    default: return "unknown";
    }
}

static const char *port_role_name(board_port_role_t role)
{
    switch (role) {
    case BOARD_PORT_INPUT: return "input";
    case BOARD_PORT_OUTPUT: return "output";
    default: return "unused";
    }
}

static const char *link_status_name(const board_link_t *link)
{
    if (link == NULL) return "invalid_unknown";
    if (link->valid) return "valid";
    switch (link->error) {
    case BOARD_LINK_UNUSED_PORT: return "ignored_unused_port";
    case BOARD_LINK_UNKNOWN_ENDPOINT: return "invalid_unknown_endpoint";
    case BOARD_LINK_DIRECTION_ERROR: return "invalid_direction";
    case BOARD_LINK_MULTIPLE_CONNECTIONS: return "invalid_multiple_connections";
    default: return "invalid_unknown";
    }
}

static const char *gate_name_zh(ssd1315_gate_t gate)
{
    switch (gate) {
    case SSD1315_GATE_INPUT: return "输入";
    case SSD1315_GATE_OUTPUT: return "输出";
    case SSD1315_GATE_NOT: return "非门";
    case SSD1315_GATE_AND: return "与门";
    case SSD1315_GATE_OR: return "或门";
    case SSD1315_GATE_NAND: return "与非门";
    case SSD1315_GATE_NOR: return "或非门";
    case SSD1315_GATE_XOR: return "异或门";
    case SSD1315_GATE_XNOR: return "同或门";
    default: return NULL;
    }
}

static bool snapshot_port_is_connected(const board_snapshot_t *snapshot, uint8_t port)
{
    if (snapshot == NULL) return true;
    for (uint8_t index = 0; index < snapshot->link_count; ++index) {
        const board_link_t *link = &snapshot->links[index];
        if (link->first_port == port || link->second_port == port) return true;
    }
    return false;
}

static bool slot_has_any_link(const board_snapshot_t *snapshot, uint8_t slot)
{
    const uint8_t first = (uint8_t)(slot * BOARD_SNAPSHOT_PORTS_PER_SLOT);
    for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
        if (snapshot_port_is_connected(snapshot, (uint8_t)(first + local))) return true;
    }
    return false;
}

static const char *gate_port_role_name(ssd1315_gate_t gate, board_port_side_t side)
{
    if (gate == SSD1315_GATE_INPUT) return "output";
    if (gate == SSD1315_GATE_OUTPUT) return side == BOARD_PORT_SIDE_LEFT ? "input" : "unused";
    if (gate == SSD1315_GATE_NOT) {
        return side == BOARD_PORT_SIDE_LEFT ? "input" :
               side == BOARD_PORT_SIDE_RIGHT ? "output" : "unused";
    }
    if (gate >= SSD1315_GATE_AND && gate <= SSD1315_GATE_XNOR) {
        return (side == BOARD_PORT_SIDE_UP || side == BOARD_PORT_SIDE_DOWN) ? "input" :
               side == BOARD_PORT_SIDE_RIGHT ? "output" : "unused";
    }
    return "unused";
}

static uint16_t unlocked_gate_mask(void)
{
    uint16_t mask = 0U;
    for (ssd1315_gate_t gate = SSD1315_GATE_INPUT; gate < SSD1315_GATE_NULL; ++gate) {
        if (app_ui_gate_is_unlocked(gate)) mask |= (uint16_t)(1U << gate);
    }
    return mask;
}

static cJSON *make_port_records(const board_snapshot_t *snapshot, uint8_t slot)
{
    cJSON *ports = cJSON_CreateArray();
    if (ports == NULL) return NULL;
    for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
        cJSON *port = cJSON_CreateArray();
        const uint8_t port_id = (uint8_t)(slot * BOARD_SNAPSHOT_PORTS_PER_SLOT + local);
        const char *role = "unassigned";
        if (snapshot->slots[slot].present) {
            role = snapshot->slots[slot].id_valid ? port_role_name(snapshot->port_roles[port_id]) : "unknown";
        }
        if (port == NULL) {
            cJSON_Delete(ports);
            return NULL;
        }
        cJSON_AddItemToArray(port, cJSON_CreateNumber(port_id));
        cJSON_AddItemToArray(port, cJSON_CreateString(
            port_side_name(board_mapping_port_side(slot, local))));
        cJSON_AddItemToArray(port, cJSON_CreateString(role));
        cJSON_AddItemToArray(ports, port);
    }
    return ports;
}

static bool add_gate_templates(cJSON *root)
{
    cJSON *templates = cJSON_AddArrayToObject(root, "gate_templates");
    if (templates == NULL) return false;
    for (ssd1315_gate_t gate = SSD1315_GATE_INPUT; gate < SSD1315_GATE_NULL; ++gate) {
        cJSON *entry = cJSON_CreateArray();
        if (entry == NULL) return false;
        cJSON_AddItemToArray(entry, cJSON_CreateString(ssd1315_gate_name(gate)));
        for (board_port_side_t side = BOARD_PORT_SIDE_UP; side <= BOARD_PORT_SIDE_RIGHT; ++side) {
            cJSON *port = cJSON_CreateArray();
            if (port == NULL) {
                cJSON_Delete(entry);
                return false;
            }
            cJSON_AddItemToArray(port, cJSON_CreateString(port_side_name(side)));
            cJSON_AddItemToArray(port, cJSON_CreateString(gate_port_role_name(gate, side)));
            cJSON_AddItemToArray(entry, port);
        }
        cJSON_AddItemToArray(templates, entry);
    }
    return true;
}

static esp_err_t build_circuit_context(const board_snapshot_t *snapshot,
                                       uint16_t level_id,
                                       const level_rule_t *rule,
                                       uint16_t gate_mask,
                                       char **out_context)
{
    cJSON *root = NULL;
    cJSON *level = NULL;
    cJSON *unlocked = NULL;
    cJSON *board = NULL;
    cJSON *slots = NULL;
    cJSON *edges = NULL;
    char *encoded = NULL;

    if (out_context != NULL) *out_context = NULL;
    if (snapshot == NULL || out_context == NULL) return ESP_ERR_INVALID_ARG;

    root = cJSON_CreateObject();
    if (root == NULL) goto no_mem;
    cJSON_AddStringToObject(root, "schema", "tuco_circuit_v2");
    level = cJSON_AddObjectToObject(root, "level");
    if (level == NULL) goto no_mem;
    cJSON_AddNumberToObject(level, "id", level_id);
    cJSON_AddStringToObject(level, "goal", rule ? rule->short_goal : "unknown");
    cJSON_AddStringToObject(level, "inputs", rule ? rule->input_names : "");
    cJSON_AddStringToObject(level, "outputs", rule ? rule->output_names : "");
    cJSON_AddNumberToObject(level, "input_count", rule ? rule->input_count : 0U);
    cJSON_AddNumberToObject(level, "output_count", rule ? rule->output_count : 0U);

    unlocked = cJSON_AddArrayToObject(root, "unlocked_gates");
    if (unlocked == NULL) goto no_mem;
    for (ssd1315_gate_t gate = SSD1315_GATE_INPUT; gate < SSD1315_GATE_NULL; ++gate) {
        if ((gate_mask & (uint16_t)(1U << gate)) != 0U) {
            cJSON_AddItemToArray(unlocked, cJSON_CreateString(ssd1315_gate_name(gate)));
        }
    }
    if (!add_gate_templates(root)) goto no_mem;

    board = cJSON_AddObjectToObject(root, "board");
    if (board == NULL) goto no_mem;
    cJSON_AddNumberToObject(board, "topology_revision", snapshot->topology_revision);
    cJSON_AddNumberToObject(board, "link_count", snapshot->link_count);
    cJSON_AddNumberToObject(board, "invalid_link_count", snapshot->invalid_link_count);
    cJSON_AddNumberToObject(board, "ignored_link_count", snapshot->ignored_link_count);
    cJSON_AddBoolToObject(board, "link_overflow", snapshot->link_overflow);
    slots = cJSON_AddArrayToObject(board, "slots");
    edges = cJSON_AddArrayToObject(board, "edges");
    if (slots == NULL || edges == NULL) goto no_mem;

    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        const board_slot_mapping_t *mapping = board_mapping_slot(slot);
        cJSON *entry = cJSON_CreateArray();
        cJSON *ports = make_port_records(snapshot, slot);
        if (entry == NULL || ports == NULL || mapping == NULL) {
            cJSON_Delete(entry);
            cJSON_Delete(ports);
            goto no_mem;
        }
        cJSON_AddItemToArray(entry, cJSON_CreateNumber(slot));
        cJSON_AddItemToArray(entry, cJSON_CreateNumber(mapping->grid_row));
        cJSON_AddItemToArray(entry, cJSON_CreateNumber(mapping->grid_column));
        if (!snapshot->slots[slot].present) {
            cJSON_AddItemToArray(entry, cJSON_CreateString("empty"));
            cJSON_AddItemToArray(entry, cJSON_CreateNull());
        } else if (!snapshot->slots[slot].id_valid) {
            cJSON_AddItemToArray(entry, cJSON_CreateString("unidentified"));
            cJSON_AddItemToArray(entry, cJSON_CreateNumber(snapshot->slots[slot].raw_id));
        } else {
            cJSON_AddItemToArray(entry, cJSON_CreateString("present"));
            cJSON_AddItemToArray(entry, cJSON_CreateString(ssd1315_gate_name(snapshot->slots[slot].gate)));
        }
        cJSON_AddItemToArray(entry, ports);
        cJSON_AddItemToArray(slots, entry);
    }

    for (uint8_t index = 0; index < snapshot->link_count; ++index) {
        const board_link_t *link = &snapshot->links[index];
        cJSON *edge = cJSON_CreateArray();
        if (edge == NULL) goto no_mem;
        cJSON_AddItemToArray(edge, cJSON_CreateNumber(link->first_port));
        cJSON_AddItemToArray(edge, cJSON_CreateNumber(link->second_port));
        cJSON_AddItemToArray(edge, cJSON_CreateString(link_status_name(link)));
        cJSON_AddItemToArray(edges, edge);
    }

    encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (encoded == NULL) return ESP_ERR_NO_MEM;
    if (strlen(encoded) >= TUCO_AGENT_CIRCUIT_CONTEXT_MAX) {
        cJSON_free(encoded);
        return ESP_ERR_INVALID_SIZE;
    }
    *out_context = encoded;
    return ESP_OK;

no_mem:
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
}

static void context_self_test_set_slot(board_snapshot_t *snapshot, uint8_t slot, ssd1315_gate_t gate)
{
    snapshot->slots[slot] = (board_slot_identity_t) {
        .present = true,
        .id_valid = true,
        .gate = gate,
    };
    for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
        const uint8_t port = (uint8_t)(slot * BOARD_SNAPSHOT_PORTS_PER_SLOT + local);
        snapshot->port_roles[port] = board_snapshot_gate_port_role(gate, slot, local);
    }
}

esp_err_t tuco_agent_context_self_test_run(void)
{
    board_snapshot_t snapshot = {0};
    const level_rule_t *rule = level_rule_get(101U);
    const uint16_t all_gates = (uint16_t)((1U << SSD1315_GATE_NULL) - 1U);
    char *context = NULL;
    uint8_t selected_slot;

    ESP_RETURN_ON_FALSE(rule != NULL, ESP_FAIL, TAG, "missing level rule for agent context");
    context_self_test_set_slot(&snapshot, 0U, SSD1315_GATE_INPUT);
    context_self_test_set_slot(&snapshot, 1U, SSD1315_GATE_OUTPUT);
    snapshot.topology_revision = 7U;
    snapshot.link_count = 2U;
    snapshot.invalid_link_count = 1U;
    snapshot.links[0] = (board_link_t) {
        .first_port = 0U, .second_port = 4U, .valid = true, .error = BOARD_LINK_OK,
    };
    snapshot.links[1] = (board_link_t) {
        .first_port = 1U, .second_port = 8U, .valid = false, .error = BOARD_LINK_DIRECTION_ERROR,
    };
    ESP_RETURN_ON_ERROR(build_circuit_context(&snapshot, 101U, rule, all_gates, &context), TAG,
                        "serialize direct circuit context");
    const bool basic_ok =
        strstr(context, "\"schema\":\"tuco_circuit_v2\"") != NULL &&
        strstr(context, "\"unlocked_gates\":[\"INPUT\",\"OUTPUT\"") != NULL &&
        strstr(context, "\"slots\":[[0,0,3,\"present\",\"INPUT\"") != NULL &&
        strstr(context, "[0,\"right\",\"output\"]") != NULL &&
        strstr(context, "[4,\"left\",\"input\"]") != NULL &&
        strstr(context, "\"edges\":[[0,4,\"valid\"],[1,8,\"invalid_direction\"]]") != NULL &&
        strstr(context, "candidate_pairs") == NULL;
    cJSON_free(context);
    context = NULL;
    ESP_RETURN_ON_FALSE(basic_ok, ESP_FAIL, TAG, "circuit context content");

    memset(&snapshot, 0, sizeof(snapshot));
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        context_self_test_set_slot(&snapshot, slot, SSD1315_GATE_INPUT);
    }
    snapshot.link_count = BOARD_SNAPSHOT_MAX_LINKS;
    for (uint8_t index = 0; index < snapshot.link_count; ++index) {
        snapshot.links[index] = (board_link_t) {
            .first_port = index,
            .second_port = (uint8_t)((index + 1U) % BOARD_SNAPSHOT_PORT_COUNT),
            .valid = true,
            .error = BOARD_LINK_OK,
        };
    }
    ESP_RETURN_ON_ERROR(build_circuit_context(&snapshot, 101U, rule, all_gates, &context), TAG,
                        "serialize maximum circuit context");
    const bool max_ok = strlen(context) < TUCO_AGENT_CIRCUIT_CONTEXT_MAX &&
                        strstr(context, "[63,0,\"valid\"]") != NULL;
    cJSON_free(context);
    ESP_RETURN_ON_FALSE(max_ok, ESP_FAIL, TAG, "maximum circuit context size");

    memset(&snapshot, 0, sizeof(snapshot));
    selected_slot = BOARD_SNAPSHOT_SLOT_COUNT;
    ESP_RETURN_ON_FALSE(select_preferred_empty_slot(&snapshot, SSD1315_GATE_OUTPUT, 9U, &selected_slot) &&
                            selected_slot == 0U,
                        ESP_FAIL, TAG, "output empty-slot preference");
    selected_slot = BOARD_SNAPSHOT_SLOT_COUNT;
    ESP_RETURN_ON_FALSE(select_preferred_empty_slot(&snapshot, SSD1315_GATE_INPUT, 0U, &selected_slot) &&
                            selected_slot == 6U,
                        ESP_FAIL, TAG, "input empty-slot preference");
    selected_slot = BOARD_SNAPSHOT_SLOT_COUNT;
    ESP_RETURN_ON_FALSE(select_preferred_empty_slot(&snapshot, SSD1315_GATE_AND, 0U, &selected_slot) &&
                            selected_slot == 2U,
                        ESP_FAIL, TAG, "gate empty-slot preference");
    context_self_test_set_slot(&snapshot, 0U, SSD1315_GATE_INPUT);
    context_self_test_set_slot(&snapshot, 1U, SSD1315_GATE_OUTPUT);
    selected_slot = BOARD_SNAPSHOT_SLOT_COUNT;
    ESP_RETURN_ON_FALSE(select_preferred_empty_slot(&snapshot, SSD1315_GATE_NAND, 5U, &selected_slot) &&
                            selected_slot == 5U,
                        ESP_FAIL, TAG, "empty-slot prompt with unconnected input and output");
    ESP_LOGI(TAG, "circuit context self-test passed");
    return ESP_OK;
}

static esp_err_t collect_tuco_context(const claw_core_request_t *request,
                                      claw_core_context_t *out_context,
                                      void *user_ctx)
{
    board_snapshot_t snapshot;
    uint16_t level_id;
    const level_rule_t *rule;
    esp_err_t err;

    (void)request;
    (void)user_ctx;
    if (out_context == NULL || s_lock == NULL || !board_snapshot_get(&snapshot)) return ESP_FAIL;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    level_id = s_active_level_id;
    xSemaphoreGive(s_lock);
    rule = level_rule_get(level_id);
    err = build_circuit_context(&snapshot, level_id, rule, unlocked_gate_mask(), &out_context->content);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "circuit snapshot failed request=%lu topology=%lu links=%u err=%s",
                 (unsigned long)request->request_id, (unsigned long)snapshot.topology_revision,
                 (unsigned)snapshot.link_count, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "circuit snapshot request=%lu topology=%lu links=%u invalid=%u bytes=%u",
             (unsigned long)request->request_id, (unsigned long)snapshot.topology_revision,
             (unsigned)snapshot.link_count, (unsigned)snapshot.invalid_link_count,
             (unsigned)strlen(out_context->content));
    out_context->kind = CLAW_CORE_CONTEXT_KIND_SYSTEM_PROMPT;
    return ESP_OK;
}

static esp_err_t collect_tuco_history(const claw_core_request_t *request,
                                      claw_core_context_t *out_context,
                                      void *user_ctx)
{
    (void)request;
    (void)user_ctx;
    if (out_context == NULL || s_lock == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out_context->content = s_history_json ? strdup(s_history_json) : NULL;
    xSemaphoreGive(s_lock);
    if (out_context->content == NULL) return ESP_ERR_NOT_FOUND;
    out_context->kind = CLAW_CORE_CONTEXT_KIND_MESSAGES;
    return ESP_OK;
}

static esp_err_t collect_tuco_tools(const claw_core_request_t *request,
                                    claw_core_context_t *out_context,
                                    void *user_ctx)
{
    (void)request;
    (void)user_ctx;
    if (out_context == NULL) return ESP_ERR_INVALID_ARG;
    out_context->kind = CLAW_CORE_CONTEXT_KIND_TOOLS;
    out_context->content = strdup(s_tools_json);
    return out_context->content == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

static esp_err_t tool_result(char **out_output, esp_err_t result, const char *message)
{
    if (out_output != NULL) *out_output = strdup(message);
    return result;
}

static bool parse_tool_uint(cJSON *root, const char *name, uint8_t maximum, uint8_t *out_value)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(value) || value->valuedouble != (double)value->valueint ||
        value->valueint < 0 || value->valueint > maximum) return false;
    *out_value = (uint8_t)value->valueint;
    return true;
}

static bool parse_tool_gate(cJSON *root, ssd1315_gate_t *out_gate)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "gate");
    if (!cJSON_IsString(value) || value->valuestring == NULL) return false;
    for (ssd1315_gate_t gate = SSD1315_GATE_INPUT; gate < SSD1315_GATE_NULL; ++gate) {
        if (strcmp(value->valuestring, ssd1315_gate_name(gate)) == 0) {
            *out_gate = gate;
            return true;
        }
    }
    return false;
}

static bool take_highlight_turn(const claw_core_request_t *request)
{
    bool already_highlighted;
    bool request_active;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    already_highlighted = s_highlight_request_id == request->request_id;
    request_active = s_active_request_id == request->request_id;
    if (!already_highlighted && request_active) s_highlight_request_id = request->request_id;
    xSemaphoreGive(s_lock);
    return request_active && !already_highlighted;
}

static bool empty_slot_is_available(const board_snapshot_t *snapshot, uint8_t slot)
{
    return snapshot != NULL && slot < BOARD_SNAPSHOT_SLOT_COUNT && !snapshot->slots[slot].present &&
           !slot_has_any_link(snapshot, slot);
}

static uint8_t preferred_column_for_gate(ssd1315_gate_t gate)
{
    if (gate == SSD1315_GATE_INPUT) return 0U;
    if (gate == SSD1315_GATE_OUTPUT) return 3U;
    return 1U;
}

static bool select_preferred_empty_slot(const board_snapshot_t *snapshot,
                                        ssd1315_gate_t gate,
                                        uint8_t requested_slot,
                                        uint8_t *out_slot)
{
    const uint8_t preferred_column = preferred_column_for_gate(gate);
    bool middle_gate = gate != SSD1315_GATE_INPUT && gate != SSD1315_GATE_OUTPUT;

    if (snapshot == NULL || out_slot == NULL || requested_slot >= BOARD_SNAPSHOT_SLOT_COUNT) return false;
    for (uint8_t slot = 0; slot < BOARD_SNAPSHOT_SLOT_COUNT; ++slot) {
        const board_slot_mapping_t *mapping = board_mapping_slot(slot);
        if (!empty_slot_is_available(snapshot, slot) || mapping == NULL) continue;
        const bool in_preferred_column = middle_gate ?
            (mapping->grid_column == 1U || mapping->grid_column == 2U) :
            mapping->grid_column == preferred_column;
        if (!in_preferred_column) continue;
        if (slot == requested_slot) {
            *out_slot = slot;
            return true;
        }
        if (*out_slot >= BOARD_SNAPSHOT_SLOT_COUNT) *out_slot = slot;
    }
    if (*out_slot < BOARD_SNAPSHOT_SLOT_COUNT) return true;
    if (!empty_slot_is_available(snapshot, requested_slot)) return false;
    *out_slot = requested_slot;
    return true;
}

static esp_err_t tuco_agent_call_cap(const char *cap_name,
                                     const char *input_json,
                                     const claw_core_request_t *request,
                                     char **out_output,
                                     void *user_ctx)
{
    board_snapshot_t snapshot;
    cJSON *root = NULL;
    uint8_t first;
    uint8_t second;
    ssd1315_gate_t gate;

    (void)user_ctx;
    if (out_output != NULL) *out_output = NULL;
    if (cap_name == NULL || input_json == NULL || request == NULL || out_output == NULL) {
        return tool_result(out_output, ESP_ERR_INVALID_ARG, "工具参数无效。");
    }
    root = cJSON_Parse(input_json);
    if (!cJSON_IsObject(root) || cJSON_GetArraySize(root) != 2) {
        ESP_LOGW(TAG, "tool rejected request=%lu cap=%s reason=bad_json args=%s",
                 (unsigned long)request->request_id, cap_name, input_json);
        cJSON_Delete(root);
        return tool_result(out_output, ESP_ERR_INVALID_ARG, "工具参数无效。");
    }
    if (!play_mode_is_active() || !board_snapshot_get(&snapshot)) {
        cJSON_Delete(root);
        return tool_result(out_output, ESP_ERR_INVALID_STATE, "当前不在游玩状态。");
    }
    if (strcmp(cap_name, "highlight_ports") == 0) {
        const bool parsed = parse_tool_uint(root, "output_port", BOARD_SNAPSHOT_PORT_COUNT - 1U, &first) &&
                            parse_tool_uint(root, "input_port", BOARD_SNAPSHOT_PORT_COUNT - 1U, &second);
        cJSON_Delete(root);
        if (!parsed || first == second || board_mapping_slot_for_port(first) == board_mapping_slot_for_port(second) ||
            !snapshot.slots[board_mapping_slot_for_port(first)].present ||
            !snapshot.slots[board_mapping_slot_for_port(first)].id_valid ||
            !snapshot.slots[board_mapping_slot_for_port(second)].present ||
            !snapshot.slots[board_mapping_slot_for_port(second)].id_valid ||
            snapshot.port_roles[first] != BOARD_PORT_OUTPUT || snapshot.port_roles[second] != BOARD_PORT_INPUT ||
            snapshot_port_is_connected(&snapshot, first) || snapshot_port_is_connected(&snapshot, second)) {
            if (parsed) {
                ESP_LOGW(TAG,
                         "highlight ports rejected request=%lu output=%u input=%u topology=%lu "
                         "source(slot=%u present=%d valid=%d role=%d linked=%d) "
                         "target(slot=%u present=%d valid=%d role=%d linked=%d)",
                         (unsigned long)request->request_id, first, second,
                         (unsigned long)snapshot.topology_revision, board_mapping_slot_for_port(first),
                         snapshot.slots[board_mapping_slot_for_port(first)].present,
                         snapshot.slots[board_mapping_slot_for_port(first)].id_valid,
                         snapshot.port_roles[first], snapshot_port_is_connected(&snapshot, first),
                         board_mapping_slot_for_port(second),
                         snapshot.slots[board_mapping_slot_for_port(second)].present,
                         snapshot.slots[board_mapping_slot_for_port(second)].id_valid,
                         snapshot.port_roles[second], snapshot_port_is_connected(&snapshot, second));
            } else {
                ESP_LOGW(TAG, "highlight ports rejected request=%lu reason=bad_args args=%s",
                         (unsigned long)request->request_id, input_json);
            }
            return tool_result(out_output, ESP_ERR_INVALID_ARG, "端口不是可用的未连线输出端到输入端组合。");
        }
        if (!take_highlight_turn(request)) {
            return tool_result(out_output, ESP_ERR_INVALID_STATE, "本轮不能再高亮端口。");
        }
        tuco_port_highlight_show(first, second);
        ESP_LOGI(TAG, "highlight request=%lu output=%u input=%u", (unsigned long)request->request_id, first, second);
        return tool_result(out_output, ESP_OK, TUCO_AGENT_HIGHLIGHT_REPLY);
    }
    if (strcmp(cap_name, "highlight_empty_slot") == 0) {
        uint8_t selected_slot = BOARD_SNAPSHOT_SLOT_COUNT;
        const bool parsed = parse_tool_uint(root, "slot", BOARD_SNAPSHOT_SLOT_COUNT - 1U, &first) &&
                            parse_tool_gate(root, &gate);
        cJSON_Delete(root);
        if (!parsed || gate_name_zh(gate) == NULL || !app_ui_gate_is_unlocked(gate)) {
            if (parsed) {
                ESP_LOGW(TAG,
                         "highlight empty slot rejected request=%lu slot=%u gate=%s topology=%lu "
                         "present=%d linked=%d unlocked=%d",
                         (unsigned long)request->request_id, first, ssd1315_gate_name(gate),
                         (unsigned long)snapshot.topology_revision, snapshot.slots[first].present,
                         slot_has_any_link(&snapshot, first),
                         app_ui_gate_is_unlocked(gate));
            } else {
                ESP_LOGW(TAG, "highlight empty slot rejected request=%lu reason=bad_args args=%s",
                         (unsigned long)request->request_id, input_json);
            }
            return tool_result(out_output, ESP_ERR_INVALID_ARG, "槽位或逻辑门不是当前可用提示。");
        }
        if (!select_preferred_empty_slot(&snapshot, gate, first, &selected_slot)) {
            ESP_LOGW(TAG, "highlight empty slot rejected request=%lu slot=%u gate=%s topology=%lu reason=no_empty_slot",
                     (unsigned long)request->request_id, first, ssd1315_gate_name(gate),
                     (unsigned long)snapshot.topology_revision);
            return tool_result(out_output, ESP_ERR_INVALID_ARG, "没有可用的空槽位。");
        }
        if (!take_highlight_turn(request)) {
            return tool_result(out_output, ESP_ERR_INVALID_STATE, "本轮不能再高亮槽位。");
        }
        if (selected_slot != first) {
            const board_slot_mapping_t *requested_mapping = board_mapping_slot(first);
            const board_slot_mapping_t *selected_mapping = board_mapping_slot(selected_slot);
            ESP_LOGI(TAG,
                     "highlight empty slot normalized request=%lu gate=%s requested=%u(row=%u col=%u) selected=%u(row=%u col=%u)",
                     (unsigned long)request->request_id, ssd1315_gate_name(gate), first,
                     requested_mapping->grid_row, requested_mapping->grid_column, selected_slot,
                     selected_mapping->grid_row, selected_mapping->grid_column);
        }
        tuco_port_highlight_show_slot(selected_slot);
        char reply[TUCO_AGENT_REPLY_MAX];
        snprintf(reply, sizeof(reply), "试试在这里放下一个%s积木呢", gate_name_zh(gate));
        ESP_LOGI(TAG, "highlight empty slot request=%lu slot=%u gate=%s", (unsigned long)request->request_id,
                 selected_slot, ssd1315_gate_name(gate));
        return tool_result(out_output, ESP_OK, reply);
    }
    cJSON_Delete(root);
    return tool_result(out_output, ESP_ERR_NOT_SUPPORTED, "不支持的工具调用。");
}

static char *limited_reply(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    size_t bytes = 0U;
    size_t characters = 0U;
    size_t last_sentence_end = 0U;
    if (text == NULL || text[0] == '\0') return NULL;
    while (cursor[bytes] != '\0' && characters < TUCO_AGENT_MAX_REPLY_CHARS) {
        const size_t start = bytes;
        const unsigned char first = cursor[bytes];
        size_t length = (first < 0x80U) ? 1U : ((first & 0xE0U) == 0xC0U) ? 2U :
                        ((first & 0xF0U) == 0xE0U) ? 3U : ((first & 0xF8U) == 0xF0U) ? 4U : 1U;
        if (length > 1U) {
            for (size_t index = 1U; index < length; ++index) {
                if ((cursor[bytes + index] & 0xC0U) != 0x80U) {
                    length = 1U;
                    break;
                }
            }
        }
        bytes += length;
        ++characters;
        if ((length == 1U && (cursor[start] == '.' || cursor[start] == '!' || cursor[start] == '?')) ||
            (length == 3U && cursor[start] == 0xE3U && cursor[start + 1U] == 0x80U &&
             (cursor[start + 2U] == 0x82U || cursor[start + 2U] == 0x81U || cursor[start + 2U] == 0x9FU))) {
            last_sentence_end = bytes;
        }
    }
    if (cursor[bytes] != '\0' && last_sentence_end != 0U) bytes = last_sentence_end;
    char *result = calloc(1, bytes + 1U);
    if (result != NULL) memcpy(result, text, bytes);
    return result;
}

static esp_err_t tuco_agent_first_round_reply(const char *cap_name,
                                              const char *input_json,
                                              const char *model_text,
                                              const claw_core_request_t *request,
                                              char **out_final_text,
                                              void *user_ctx)
{
    char *tool_output = NULL;
    esp_err_t tool_err;

    (void)user_ctx;
    if (out_final_text == NULL) return ESP_ERR_INVALID_ARG;
    *out_final_text = NULL;
    if (cap_name == NULL || input_json == NULL) {
        if (model_text != NULL && model_text[0] != '\0') {
            *out_final_text = limited_reply(model_text);
            return *out_final_text != NULL ? ESP_OK : ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "single_turn invalid request=%lu reason=no_text_or_multiple_tools",
                 (unsigned long)(request ? request->request_id : 0U));
        return tool_result(out_final_text, ESP_OK, TUCO_AGENT_UNCLEAR_REPLY);
    }
    tool_err = tuco_agent_call_cap(cap_name, input_json, request, &tool_output, NULL);
    if (tool_err != ESP_OK) {
        free(tool_output);
        ESP_LOGW(TAG, "single_turn invalid tool request=%lu err=%s",
                 (unsigned long)(request ? request->request_id : 0U), esp_err_to_name(tool_err));
        return tool_result(out_final_text, ESP_OK, TUCO_AGENT_UNCLEAR_REPLY);
    }
    *out_final_text = tool_output;
    return ESP_OK;
}

static void append_history_locked(const char *user_text, const char *assistant_text)
{
    cJSON *history = s_history_json ? cJSON_Parse(s_history_json) : cJSON_CreateArray();
    cJSON *user;
    cJSON *assistant;
    char *encoded;
    if (!cJSON_IsArray(history)) {
        cJSON_Delete(history);
        history = cJSON_CreateArray();
    }
    if (history == NULL) return;
    user = cJSON_CreateObject();
    assistant = cJSON_CreateObject();
    if (user == NULL || assistant == NULL) {
        cJSON_Delete(user);
        cJSON_Delete(assistant);
        cJSON_Delete(history);
        return;
    }
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", user_text);
    cJSON_AddStringToObject(assistant, "role", "assistant");
    cJSON_AddStringToObject(assistant, "content", assistant_text);
    cJSON_AddItemToArray(history, user);
    cJSON_AddItemToArray(history, assistant);
    encoded = cJSON_PrintUnformatted(history);
    cJSON_Delete(history);
    if (encoded == NULL) return;
    free(s_history_json);
    s_history_json = NULL;
    if (strlen(encoded) > CONFIG_TUCO_AGENT_CONTEXT_MAX_BYTES) {
        ESP_LOGI(TAG, "level conversation cleared: history=%u max=%u", (unsigned)strlen(encoded),
                 (unsigned)CONFIG_TUCO_AGENT_CONTEXT_MAX_BYTES);
        cJSON_free(encoded);
        return;
    }
    s_history_json = encoded;
}

static void agent_result_task(void *arg)
{
    (void)arg;
    for (;;) {
        claw_core_response_t response = {0};
        const esp_err_t err = claw_core_receive(s_core, &response, pdMS_TO_TICKS(1000));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Agent result receive failed: %s", esp_err_to_name(err));
            continue;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (response.request_id == s_active_request_id) {
            s_result.ready = true;
            s_result.failed = response.status != CLAW_CORE_RESPONSE_STATUS_OK || response.text == NULL ||
                              response.text[0] == '\0';
            s_result.request_id = response.request_id;
            strlcpy(s_result.text, s_result.failed ? (response.error_message ? response.error_message : "Agent reply failed") :
                    response.text, sizeof(s_result.text));
            if (!s_result.failed && s_session_active) append_history_locked(s_active_user_text, s_result.text);
            s_active_request_id = 0U;
            s_active_user_text[0] = '\0';
            ESP_LOGI(TAG, "reply request=%lu status=%s text=%s", (unsigned long)response.request_id,
                     s_result.failed ? "error" : "ok", s_result.text);
        }
        xSemaphoreGive(s_lock);
        claw_core_response_free(&response);
    }
}

void tuco_agent_begin_level_session(uint16_t level_id)
{
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_session_active || s_session_level_id != level_id) {
        free(s_history_json);
        s_history_json = NULL;
        s_session_active = true;
        s_session_level_id = level_id;
        ESP_LOGI(TAG, "level conversation started level=%u", (unsigned)level_id);
    }
    xSemaphoreGive(s_lock);
}

void tuco_agent_end_level_session(void)
{
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_session_active || s_history_json != NULL) ESP_LOGI(TAG, "level conversation cleared");
    free(s_history_json);
    s_history_json = NULL;
    s_session_active = false;
    xSemaphoreGive(s_lock);
}

esp_err_t tuco_agent_init(void)
{
    if (s_core != NULL || s_lock != NULL) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    if (!agent_credentials_configured()) {
        ESP_LOGW(TAG, "ESP-Claw agent disabled until the local DeepSeek key is configured");
        return ESP_OK;
    }
    const claw_core_config_t config = {
        .instance_id = 1U,
        .api_key = CONFIG_TUCO_AGENT_DEEPSEEK_API_KEY,
        .backend_type = "openai_compatible",
        .model = CONFIG_TUCO_AGENT_MODEL,
        .base_url = CONFIG_TUCO_AGENT_DEEPSEEK_BASE_URL,
        .auth_type = "bearer",
        .max_tokens_field = "max_tokens",
        .timeout_ms = TUCO_AGENT_REQUEST_TIMEOUT_MS,
        .max_tokens = CONFIG_TUCO_AGENT_MAX_TOKENS,
        .disable_thinking = true,
        .supports_tools = true,
        .supports_vision = false,
        .image_remote_url_only = true,
        .system_prompt = "你是图灵号电路指导员，帮助儿童完成当前电路关卡。"
                         "只能依据circuit_snapshot和历史对话，不能编造硬件、连线或积木。"
                         "circuit_snapshot使用tuco_circuit_v2：board.slots每项是"
                         "[slot_id,row,column,state,gate_or_raw_id,ports]，ports每项是[port_id,side,role]；"
                         "state为empty、unidentified或present，role为unassigned、unknown、unused、input或output。"
                         "board.edges每项是[port_a,port_b,status]，列出所有实际检测到的连线；"
                         "端口只要出现在任一edge中即视为已连接，不得再次选择。"
                         "gate_templates给出所有门的方位规则，unlocked_gates是当前唯一可放置的门集合。"
                         "快照不提供候选端口对；需要你基于完整拓扑自行判断。"
                         "仅当快照中已存在所需INPUT和OUTPUT积木，且它们有未连接的可用端口时，"
                         "单个输入与单个输出保持相同的关卡才应直接连接输入和输出，不需要逻辑门。"
                         "若缺少INPUT或OUTPUT积木，必须视为需要新增模块，不能直接建议连线；"
                         "玩家明确索取下一步提示时必须调用highlight_empty_slot，缺少OUTPUT时gate必须为OUTPUT，"
                         "缺少INPUT时gate必须为INPUT，调用时正文必须为空。"
                         "严格按以下互斥规则输出，任何一轮都不得同时没有正文且没有工具调用。"
                         "一，玩家文本无意义或无法理解：不调用工具，只回复我没明白，换个说法试试。"
                         "二，玩家明确索取当前电路下一步接线提示，且快照中存在未连接的输出端和不同槽位的未连接输入端："
                         "必须且只能调用一次highlight_ports，参数必须是快照中实际存在、未出现在edges内的输出端到输入端；调用时正文必须为空。"
                         "三，玩家明确索取当前电路提示，且不存在上述端口组合，原因是需要新增模块："
                         "必须且只能调用一次highlight_empty_slot，slot必须是state为empty且四个端口均未出现在edges内的slot_id，"
                         "gate必须在unlocked_gates中；在多个合法空槽之间，新增INPUT优先选择column为0的最左列，"
                         "新增OUTPUT优先选择column为3的最右列，新增NOT、AND、OR、NAND、NOR、XOR或XNOR优先选择column为1或2的中间两列；"
                         "调用时正文必须为空。"
                         "四，其他所有情况，包括电路已接好但结果不对、信息不足、知识问题和闲聊：不调用工具，"
                         "必须返回一条完整、自然、适合儿童听的中文口语，正文不超过24个汉字。"
                         "不得复述或解释circuit_snapshot、快照、JSON、topology、slots、ports、edges、槽位、端口编号，"
                         "不得说INPUT、OUTPUT或其他英文门名；应改说输入积木、输出积木或对应中文门名。"
                         "不得用让我看看、根据快照、当前电路里已有等过程性话术开头。"
                         "不能确定工具参数时属于第四种，绝不能返回空正文。",
        .call_cap = tuco_agent_call_cap,
        .first_round_reply = tuco_agent_first_round_reply,
        .task_stack_size = 12288U,
        .task_priority = 4U,
        .task_core = tskNO_AFFINITY,
        .max_tool_iterations = 1U,
        .request_queue_len = 1U,
        .response_queue_len = 1U,
        .max_context_providers = 3U,
    };
    esp_err_t err = claw_core_create(&config, &s_core);
    if (err != ESP_OK) return err;
    const claw_core_context_provider_t context_provider = {
        .name = "tuco_circuit_snapshot", .collect = collect_tuco_context,
        .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY,
    };
    const claw_core_context_provider_t history_provider = {
        .name = "tuco_level_history", .collect = collect_tuco_history,
    };
    const claw_core_context_provider_t tool_provider = {
        .name = "tuco_hint_tools", .collect = collect_tuco_tools,
        .flags = CLAW_CORE_CONTEXT_PROVIDER_FLAG_REQUEST_START_ONLY,
    };
    if ((err = claw_core_add_context_provider(s_core, &context_provider)) != ESP_OK ||
        (err = claw_core_add_context_provider(s_core, &history_provider)) != ESP_OK ||
        (err = claw_core_add_context_provider(s_core, &tool_provider)) != ESP_OK ||
        (err = claw_core_start(s_core)) != ESP_OK ||
        xTaskCreate(agent_result_task, "tuco_agent_rx", 4096, NULL, 4, &s_result_task) != pdPASS) {
        ESP_LOGE(TAG, "ESP-Claw agent startup failed: %s", esp_err_to_name(err));
        return err == ESP_OK ? ESP_ERR_NO_MEM : err;
    }
    ESP_LOGI(TAG, "ESP-Claw agent ready model=%s endpoint=%s key=configured", CONFIG_TUCO_AGENT_MODEL,
             CONFIG_TUCO_AGENT_DEEPSEEK_BASE_URL);
    return ESP_OK;
}

esp_err_t tuco_agent_submit(const char *text, uint16_t level_id, uint32_t *out_request_id)
{
    claw_core_request_t request = {0};
    uint32_t request_id;
    esp_err_t err;
    if (out_request_id != NULL) *out_request_id = 0U;
    if (s_core == NULL || text == NULL || text[0] == '\0') return ESP_ERR_INVALID_STATE;
    if (!c6_network_is_connected()) return ESP_ERR_INVALID_STATE;
    tuco_agent_begin_level_session(level_id);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active_request_id != 0U || s_result.ready) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    request_id = ++s_next_request_id;
    if (request_id == 0U) request_id = ++s_next_request_id;
    s_active_request_id = request_id;
    s_active_level_id = level_id;
    s_highlight_request_id = 0U;
    strlcpy(s_active_user_text, text, sizeof(s_active_user_text));
    memset(&s_result, 0, sizeof(s_result));
    xSemaphoreGive(s_lock);
    tuco_port_highlight_clear();
    request = (claw_core_request_t) {
        .request_id = request_id, .session_id = "tuco-level", .user_text = text, .source_cap = "tuco_voice",
    };
    err = claw_core_submit(s_core, &request, 0U);
    if (err != ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_active_request_id == request_id) {
            s_active_request_id = 0U;
            s_active_user_text[0] = '\0';
        }
        xSemaphoreGive(s_lock);
        return err;
    }
    ESP_LOGI(TAG, "submit request=%lu source=%s", (unsigned long)request_id, request.source_cap);
    if (out_request_id != NULL) *out_request_id = request_id;
    return ESP_OK;
}

esp_err_t tuco_agent_take_result(uint32_t request_id, char *output, size_t output_size)
{
    esp_err_t result = ESP_ERR_NOT_FOUND;
    if (output == NULL || output_size == 0U || s_lock == NULL || request_id == 0U) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_result.ready && s_result.request_id == request_id) {
        strlcpy(output, s_result.text, output_size);
        result = s_result.failed ? ESP_FAIL : ESP_OK;
        memset(&s_result, 0, sizeof(s_result));
    }
    xSemaphoreGive(s_lock);
    return result;
}

void tuco_agent_cancel(uint32_t request_id)
{
    if (s_core == NULL || request_id == 0U) return;
    (void)claw_core_cancel_request(s_core, request_id);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active_request_id == request_id) {
        s_active_request_id = 0U;
        s_highlight_request_id = 0U;
        s_active_user_text[0] = '\0';
        memset(&s_result, 0, sizeof(s_result));
    }
    xSemaphoreGive(s_lock);
}
