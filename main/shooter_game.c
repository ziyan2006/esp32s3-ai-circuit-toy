#include "shooter_game.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "audio_self_test.h"
#include "shooter_core.h"
#include "shooter_font_cn_16.h"
#include "shooter_renderer_lvgl.h"

#define SHOOTER_TICK_MS 16U

#define COLOR_BG       0x0A0E27
#define COLOR_PANEL    0x0F1B3D
#define COLOR_BORDER   0x294B78
#define COLOR_CYAN     0x5EDCF4
#define COLOR_PINK     0xF0A6CA
#define COLOR_YELLOW   0xF4D03F
#define COLOR_GREEN    0x75E6B0
#define COLOR_TEXT     0xF4F0FF
#define COLOR_MUTED    0xB9B7D5
#define COLOR_DANGER   0xFF7895

static const char *TAG = "shooter_game";
static shooter_core_t s_core;
static shooter_input_state_t s_input;
static shooter_renderer_lvgl_t s_renderer;
static lv_obj_t *s_root;
static lv_obj_t *s_stage_title;
static lv_obj_t *s_settlement;
static lv_timer_t *s_tick_timer;
static shooter_game_complete_cb_t s_complete_cb;
static shooter_game_exit_cb_t s_exit_cb;
static void *s_callback_context;
static bool s_wait_for_action_release;
static bool s_last_center_pressed;
static bool s_completion_reported;
static bool s_random_seeded;

static void log_lvgl_memory(const char *phase)
{
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    ESP_LOGI(TAG, "%s: lvgl=%u%% free=%u largest=%u frag=%u%%",
             phase, memory.used_pct, (unsigned)memory.free_size,
             (unsigned)memory.free_biggest_size, memory.frag_pct);
}

static void style_plain_object(lv_obj_t *object, uint32_t color)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_label(lv_obj_t *parent,
                              const char *text,
                              uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &shooter_font_cn_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static void start_stage(void)
{
    if (s_settlement != NULL) {
        lv_obj_delete(s_settlement);
        s_settlement = NULL;
    }
    shooter_core_init(&s_core, s_core.level);
    memset(&s_input, 0, sizeof(s_input));
    s_wait_for_action_release = true;
    s_completion_reported = false;
    if (s_stage_title != NULL) {
        lv_label_set_text(s_stage_title, shooter_core_stage_title(&s_core));
    }
    shooter_renderer_lvgl_update(&s_renderer, &s_core);
}

static void exit_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    audio_self_test_play_effect(AUDIO_EFFECT_BACK);
    shooter_game_exit_cb_t callback = s_exit_cb;
    void *context = s_callback_context;
    if (callback != NULL) {
        callback(context);
    }
}

static void replay_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    start_stage();
}

static lv_obj_t *create_command_button(lv_obj_t *parent,
                                       const char *text,
                                       uint32_t color,
                                       lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 154, 44);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = create_label(button, text, color);
    lv_obj_center(label);
    return button;
}

static void show_settlement(void)
{
    const bool won = s_core.phase == SHOOTER_PHASE_WON;

    if (s_settlement != NULL || s_renderer.arena == NULL) {
        return;
    }
    if (won && !s_completion_reported) {
        s_completion_reported = true;
        if (s_complete_cb != NULL) {
            s_complete_cb(s_core.level, s_callback_context);
        }
    }

    s_settlement = lv_obj_create(s_renderer.arena);
    lv_obj_set_size(s_settlement, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_settlement);
    lv_obj_set_style_bg_color(s_settlement, lv_color_hex(0x02040C), 0);
    lv_obj_set_style_bg_opa(s_settlement, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_settlement, 0, 0);
    lv_obj_set_style_radius(s_settlement, 12, 0);
    lv_obj_set_style_pad_all(s_settlement, 0, 0);
    lv_obj_clear_flag(s_settlement, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_settlement);
    lv_obj_set_size(card, 500, 256);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(won ? COLOR_GREEN : COLOR_DANGER), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(card, won ? "挑战成功！" : "飞船需要修整", won ? COLOR_GREEN : COLOR_DANGER);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *details = create_label(card, "", COLOR_TEXT);
    lv_label_set_text_fmt(details,
                          "%s\n击破 %u 架    受击 %u 次",
                          shooter_core_objective_text(&s_core),
                          (unsigned)s_core.total_destroyed,
                          (unsigned)s_core.total_hits_taken);
    lv_obj_set_width(details, 440);
    lv_obj_set_style_text_align(details, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(details, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t *actions = lv_obj_create(card);
    lv_obj_set_size(actions, 360, 48);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    create_command_button(actions, "再试一次", COLOR_YELLOW, replay_event_cb);
    create_command_button(actions, "返回航线", COLOR_CYAN, exit_event_cb);
}

static void tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_root == NULL) {
        return;
    }
    if (s_core.phase == SHOOTER_PHASE_RUNNING) {
        const shooter_phase_t phase_before = s_core.phase;
        const uint16_t destroyed_before = s_core.total_destroyed;
        const uint16_t hits_before = s_core.total_hits_taken;
        const uint16_t devices_before = s_core.total_device_activations;
        shooter_core_step(&s_core, &s_input, SHOOTER_TICK_MS);
        if (s_core.phase != phase_before) {
            audio_self_test_play_effect(s_core.phase == SHOOTER_PHASE_WON ?
                                        AUDIO_EFFECT_WIN : AUDIO_EFFECT_LOSE);
        } else if (s_core.total_hits_taken != hits_before) {
            audio_self_test_play_effect(AUDIO_EFFECT_PLAYER_HIT);
        } else if (s_core.total_device_activations != devices_before) {
            audio_self_test_play_effect(AUDIO_EFFECT_POWER);
        } else if (s_core.total_destroyed != destroyed_before) {
            audio_self_test_play_effect(AUDIO_EFFECT_ENEMY_DESTROYED);
        } else if (s_core.fire_edge_pressed &&
                   s_core.player.fire_cooldown_ms == s_core.upgrades.fire_interval_ms) {
            audio_self_test_play_effect(AUDIO_EFFECT_SHOOT);
        }
        shooter_renderer_lvgl_update(&s_renderer, &s_core);
    } else if (s_core.phase == SHOOTER_PHASE_WON || s_core.phase == SHOOTER_PHASE_LOST) {
        show_settlement();
    }
}

static void create_header(void)
{
    lv_obj_t *header = lv_obj_create(s_root);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 1024, 76);
    style_plain_object(header, 0x071017);
    lv_obj_set_style_border_color(header, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t *accent = lv_obj_create(header);
    lv_obj_set_pos(accent, 0, 73);
    lv_obj_set_size(accent, 240, 3);
    style_plain_object(accent, COLOR_PINK);

    lv_obj_t *back = lv_button_create(header);
    lv_obj_set_pos(back, 16, 13);
    lv_obj_set_size(back, 92, 46);
    lv_obj_set_style_bg_color(back, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_radius(back, 5, 0);
    lv_obj_add_event_cb(back, exit_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = create_label(back, "< 返回", COLOR_CYAN);
    lv_obj_center(back_label);

    lv_obj_t *section = create_label(header, "特别关卡", COLOR_PINK);
    lv_obj_set_pos(section, 122, 9);
    s_stage_title = create_label(header, shooter_core_stage_title(&s_core), COLOR_TEXT);
    lv_obj_set_pos(s_stage_title, 122, 34);

    lv_obj_t *status = lv_obj_create(header);
    lv_obj_set_pos(status, 858, 20);
    lv_obj_set_size(status, 142, 36);
    lv_obj_set_style_bg_color(status, lv_color_hex(0x0C211C), 0);
    lv_obj_set_style_border_color(status, lv_color_hex(0x276A50), 0);
    lv_obj_set_style_border_width(status, 1, 0);
    lv_obj_set_style_radius(status, 18, 0);
    lv_obj_set_style_pad_all(status, 0, 0);
    lv_obj_t *status_label = create_label(status, "飞行中", COLOR_GREEN);
    lv_obj_center(status_label);
}

lv_obj_t *shooter_game_create(uint32_t stage_id,
                              shooter_game_complete_cb_t complete_cb,
                              shooter_game_exit_cb_t exit_cb,
                              void *context)
{
    if (s_root != NULL || stage_id < 1U || stage_id > 6U) {
        return NULL;
    }
    if (!s_random_seeded) {
        srand(esp_random());
        s_random_seeded = true;
    }

    memset(&s_renderer, 0, sizeof(s_renderer));
    memset(&s_input, 0, sizeof(s_input));
    shooter_core_init(&s_core, stage_id);
    s_complete_cb = complete_cb;
    s_exit_cb = exit_cb;
    s_callback_context = context;
    s_wait_for_action_release = true;
    s_last_center_pressed = true;
    s_completion_reported = false;

    s_root = lv_obj_create(NULL);
    style_plain_object(s_root, COLOR_BG);
    lv_obj_set_style_text_font(s_root, &shooter_font_cn_16, 0);
    create_header();

    shooter_renderer_lvgl_create(&s_renderer, s_root);
    lv_obj_set_pos(s_renderer.arena, 72, 96);
    lv_obj_set_style_text_font(s_renderer.arena, &shooter_font_cn_16, 0);
    shooter_renderer_lvgl_update(&s_renderer, &s_core);

    s_tick_timer = lv_timer_create(tick_cb, SHOOTER_TICK_MS, NULL);
    ESP_LOGI(TAG, "stage %u created with dynamic LVGL object pools", (unsigned)stage_id);
    log_lvgl_memory("stage created");
    return s_root;
}

lv_obj_t *shooter_game_stop(void)
{
    lv_obj_t *screen = s_root;
    if (s_tick_timer != NULL) {
        lv_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }
    s_root = NULL;
    s_stage_title = NULL;
    s_settlement = NULL;
    s_complete_cb = NULL;
    s_exit_cb = NULL;
    s_callback_context = NULL;
    memset(&s_renderer, 0, sizeof(s_renderer));
    memset(&s_input, 0, sizeof(s_input));
    ESP_LOGI(TAG, "dynamic stage stopped");
    return screen;
}

void shooter_game_set_input(const key_input_state_t *keys,
                            const joystick_input_state_t *joystick)
{
    if (s_root == NULL || keys == NULL || joystick == NULL) {
        return;
    }

    const bool center_edge = keys->key1_pressed && !s_last_center_pressed;
    s_last_center_pressed = keys->key1_pressed;
    if ((s_core.phase == SHOOTER_PHASE_WON || s_core.phase == SHOOTER_PHASE_LOST) && center_edge) {
        audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        start_stage();
        return;
    }

    s_input.move_x = (int16_t)((joystick->right_pressed ? 1 : 0) -
                               (joystick->left_pressed ? 1 : 0));
    s_input.move_y = (int16_t)((joystick->down_pressed ? 1 : 0) -
                               (joystick->up_pressed ? 1 : 0));

    if (s_wait_for_action_release) {
        s_input.fire_pressed = false;
        s_input.device_pressed = false;
        if (!keys->key1_pressed && !keys->key0_pressed) {
            s_wait_for_action_release = false;
        }
    } else {
        s_input.fire_pressed = keys->key1_pressed;
        s_input.device_pressed = keys->key0_pressed;
    }
}

bool shooter_game_is_active(void)
{
    return s_root != NULL;
}
