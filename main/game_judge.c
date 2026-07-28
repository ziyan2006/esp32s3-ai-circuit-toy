#include "game_judge.h"

#include <string.h>

#include "circuit_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "play_mode.h"

#define JUDGE_ROW_TIME_MS 400U

typedef struct {
    board_snapshot_t snapshot;
    level_rule_t rule;
    uint32_t token;
} judge_command_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_queue;
static game_judge_state_t s_state;
static uint32_t s_token;

static void set_message_locked(const char *message)
{
    if (message == NULL) message = "";
    strlcpy(s_state.message, message, sizeof(s_state.message));
}

static void set_phase(game_judge_phase_t phase, const char *message)
{
    portENTER_CRITICAL(&s_lock);
    s_state.phase = phase;
    s_state.active_row_valid = false;
    set_message_locked(message);
    ++s_state.version;
    portEXIT_CRITICAL(&s_lock);
}

static bool command_is_current(uint32_t token, uint32_t topology_revision)
{
    board_snapshot_t current;
    board_snapshot_get(&current);
    bool valid;
    portENTER_CRITICAL(&s_lock);
    valid = token == s_token;
    portEXIT_CRITICAL(&s_lock);
    return valid && play_mode_is_active() && current.topology_revision == topology_revision;
}

static bool token_is_current(uint32_t token)
{
    bool current;
    portENTER_CRITICAL(&s_lock);
    current = token == s_token;
    portEXIT_CRITICAL(&s_lock);
    return current;
}

static void judge_task(void *arg)
{
    judge_command_t command;
    (void)arg;
    for (;;) {
        if (xQueueReceive(s_queue, &command, portMAX_DELAY) != pdPASS) continue;
        bool all_passed = true;
        for (uint8_t row = 0; row < command.rule.case_count; ++row) {
            if (!command_is_current(command.token, command.snapshot.topology_revision)) {
                if (token_is_current(command.token)) {
                    set_phase(GAME_JUDGE_CANCELLED, "电路变动啦，请再检查一次");
                }
                all_passed = false;
                break;
            }
            uint8_t actual = 0;
            const circuit_check_result_t checked = circuit_logic_evaluate(
                &command.snapshot, &command.rule, row, &actual);
            const bool passed = checked.code == CIRCUIT_CHECK_OK &&
                                actual == command.rule.expected_outputs[row];
            portENTER_CRITICAL(&s_lock);
            s_state.active_row = row;
            s_state.active_row_valid = true;
            s_state.rows[row] = (game_judge_row_t) {
                .inputs = row,
                .expected = command.rule.expected_outputs[row],
                .actual = actual,
                .complete = true,
                .passed = passed,
            };
            s_state.completed_rows = row + 1U;
            set_message_locked(passed ? "这一行通过啦" : "这一行还不一样");
            ++s_state.version;
            portEXIT_CRITICAL(&s_lock);
            if (!passed) all_passed = false;
            vTaskDelay(pdMS_TO_TICKS(JUDGE_ROW_TIME_MS));
        }
        if (!command_is_current(command.token, command.snapshot.topology_revision)) continue;
        set_phase(all_passed ? GAME_JUDGE_PASSED : GAME_JUDGE_FAILED,
                  all_passed ? "全部通过，飞船可以出发啦！" : "还有几行不一样，再试一次吧");
    }
}

esp_err_t game_judge_init(void)
{
    s_queue = xQueueCreate(1, sizeof(judge_command_t));
    if (s_queue == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(judge_task, "game_judge", 4096, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    game_judge_reset();
    return ESP_OK;
}

esp_err_t game_judge_start(const level_rule_t *rule, const board_snapshot_t *snapshot)
{
    if (rule == NULL || snapshot == NULL || s_queue == NULL) return ESP_ERR_INVALID_ARG;
    const circuit_check_result_t checked = circuit_logic_precheck(snapshot, rule);
    if (checked.code != CIRCUIT_CHECK_OK) {
        portENTER_CRITICAL(&s_lock);
        const uint32_t version = s_state.version + 1U;
        ++s_token;
        memset(&s_state, 0, sizeof(s_state));
        s_state.phase = GAME_JUDGE_PRECHECK_ERROR;
        s_state.level_id = rule->level_id;
        s_state.topology_revision = snapshot->topology_revision;
        s_state.precheck_code = checked.code;
        set_message_locked(checked.message);
        s_state.version = version;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    judge_command_t command = {.snapshot = *snapshot, .rule = *rule};
    portENTER_CRITICAL(&s_lock);
    const uint32_t version = s_state.version + 1U;
    command.token = ++s_token;
    memset(&s_state, 0, sizeof(s_state));
    s_state.phase = GAME_JUDGE_RUNNING;
    s_state.level_id = rule->level_id;
    s_state.topology_revision = snapshot->topology_revision;
    s_state.input_count = rule->input_count;
    s_state.output_count = rule->output_count;
    s_state.row_count = rule->case_count;
    s_state.active_row_valid = false;
    set_message_locked("飞船正在一行一行检查");
    s_state.version = version;
    portEXIT_CRITICAL(&s_lock);
    xQueueOverwrite(s_queue, &command);
    return ESP_OK;
}

void game_judge_cancel(const char *message)
{
    portENTER_CRITICAL(&s_lock);
    ++s_token;
    s_state.phase = GAME_JUDGE_CANCELLED;
    s_state.active_row_valid = false;
    set_message_locked(message == NULL ? "检查已经停止" : message);
    ++s_state.version;
    portEXIT_CRITICAL(&s_lock);
}

void game_judge_reset(void)
{
    portENTER_CRITICAL(&s_lock);
    ++s_token;
    const uint32_t version = s_state.version + 1U;
    memset(&s_state, 0, sizeof(s_state));
    s_state.phase = GAME_JUDGE_IDLE;
    s_state.active_row_valid = false;
    s_state.version = version;
    set_message_locked("准备好后启动飞船检查");
    portEXIT_CRITICAL(&s_lock);
}

bool game_judge_get_state(game_judge_state_t *state)
{
    if (state == NULL) return false;
    portENTER_CRITICAL(&s_lock);
    *state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return true;
}
