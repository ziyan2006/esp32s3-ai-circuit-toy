#include "shooter_screen.h"

#include <string.h>

#include "app_fonts.h"
#include "shooter_renderer_lvgl.h"

#if !defined(LVGL_PC_PREVIEW)
#include "ui_app.h"
#endif

#define SHOOTER_TICK_MS 16U

static shooter_core_t s_core;
static shooter_input_state_t s_pending_input;
static shooter_renderer_lvgl_t s_renderer;
static lv_timer_t *s_tick_timer;
static lv_obj_t *s_settlement_overlay;
static lv_obj_t *s_subhead;
static lv_obj_t *s_device_button;
static bool s_fire_click_triggered;
static bool s_device_click_triggered;
static uint32_t s_current_level = 1U;

static void refresh_header(void)
{
    if (s_subhead != NULL) {
        lv_label_set_text_fmt(s_subhead, "%s\n目标：%s",
                              shooter_core_stage_title(&s_core),
                              shooter_core_objective_text(&s_core));
    }
}

static void refresh_device_button(void)
{
    if (s_device_button == NULL) {
        return;
    }
    if (s_core.upgrades.shield_unlocked) {
        lv_obj_remove_flag(s_device_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_device_button, LV_OBJ_FLAG_HIDDEN);
    }
}

static void start_level(uint32_t level)
{
    if (s_settlement_overlay != NULL) {
        lv_obj_delete(s_settlement_overlay);
        s_settlement_overlay = NULL;
    }

    s_current_level = shooter_stage_get(level)->level_id;
    shooter_core_init(&s_core, s_current_level);
    memset(&s_pending_input, 0, sizeof(s_pending_input));
    s_fire_click_triggered = false;
    s_device_click_triggered = false;
    refresh_header();
    refresh_device_button();
    shooter_renderer_lvgl_update(&s_renderer, &s_core);
}

void shooter_screen_exit_to_home(void)
{
    if (s_tick_timer != NULL) {
        lv_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }
#if defined(LVGL_PC_PREVIEW)
    extern void show_pc_preview_homepage(void);
    show_pc_preview_homepage();
#else
    ui_app_show_campaign();
#endif
}

static void exit_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    shooter_screen_exit_to_home();
}

static void replay_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    start_level(s_current_level);
}

static void next_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    start_level(s_current_level + 1U);
}

static void fire_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_fire_click_triggered = true;
}

static void device_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_device_click_triggered = true;
}

static lv_obj_t *create_action_button(lv_obj_t *parent,
                                      const char *text,
                                      lv_coord_t right_offset,
                                      lv_color_t color,
                                      lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 132, 46);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, right_offset, -18);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, app_fonts_get_body_font(), 0);
    lv_obj_center(label);
    return button;
}

static void show_settlement(void)
{
    lv_obj_t *root = lv_screen_active();
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *details;
    lv_obj_t *button_row;

    s_settlement_overlay = lv_obj_create(root);
    lv_obj_set_size(s_settlement_overlay, 880, 420);
    lv_obj_align(s_settlement_overlay, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(s_settlement_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_settlement_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_settlement_overlay, 0, 0);
    lv_obj_set_style_radius(s_settlement_overlay, 16, 0);
    lv_obj_clear_flag(s_settlement_overlay, LV_OBJ_FLAG_SCROLLABLE);

    card = lv_obj_create(s_settlement_overlay);
    lv_obj_set_size(card, 460, 260);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0F223D), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x00D2D3), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(card);
    lv_label_set_text(title, s_core.phase == SHOOTER_PHASE_WON ? "任务完成" : "任务失败");
    lv_obj_set_style_text_color(title,
                                s_core.phase == SHOOTER_PHASE_WON
                                    ? lv_color_hex(0x2ECC71)
                                    : lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_text_font(title, app_fonts_get_title_font(), 0);

    details = lv_label_create(card);
    lv_label_set_text_fmt(details, "%s\n击破：%u  受击：%u  装置：%u\n关卡：%u",
                          shooter_core_objective_text(&s_core),
                          (unsigned)s_core.total_destroyed,
                          (unsigned)s_core.total_hits_taken,
                          (unsigned)s_core.total_device_activations,
                          (unsigned)s_current_level);
    lv_obj_set_style_text_color(details, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(details, app_fonts_get_body_font(), 0);

    button_row = lv_obj_create(card);
    lv_obj_set_size(button_row, 400, 60);
    lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_row, 0, 0);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);

    const char *texts[] = {"退出", "重玩", "下一关"};
    lv_event_cb_t callbacks[] = {exit_event_cb, replay_event_cb, next_event_cb};
    const uint32_t colors[] = {0xE74C3C, 0x34495E, 0x00D2D3};
    for (uint8_t i = 0U; i < 3U; ++i) {
        lv_obj_t *button = lv_button_create(button_row);
        lv_obj_set_size(button, 110, 40);
        lv_obj_set_style_bg_color(button, lv_color_hex(colors[i]), 0);
        if (i == 2U && (s_core.phase != SHOOTER_PHASE_WON || s_current_level >= 6U)) {
            lv_obj_set_style_bg_color(button, lv_color_hex(0x7F8C8D), 0);
            lv_obj_add_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_event_cb(button, callbacks[i], LV_EVENT_CLICKED, NULL);
        }
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, texts[i]);
        lv_obj_set_style_text_font(label, app_fonts_get_body_font(), 0);
        lv_obj_center(label);
    }
}

static void tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_core.phase == SHOOTER_PHASE_RUNNING) {
        shooter_input_state_t input = s_pending_input;
        if (s_fire_click_triggered) {
            input.fire_pressed = true;
        }
        if (s_device_click_triggered) {
            input.device_pressed = true;
        }
        shooter_core_step(&s_core, &input, SHOOTER_TICK_MS);
        s_fire_click_triggered = false;
        s_device_click_triggered = false;
        shooter_renderer_lvgl_update(&s_renderer, &s_core);
    } else if ((s_core.phase == SHOOTER_PHASE_WON || s_core.phase == SHOOTER_PHASE_LOST) &&
               s_settlement_overlay == NULL) {
        show_settlement();
    }
}

lv_obj_t *shooter_screen_create(lv_display_t *display, uint32_t level_id)
{
    LV_UNUSED(display);

    if (s_tick_timer != NULL) {
        lv_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }
    s_settlement_overlay = NULL;
    s_subhead = NULL;
    s_device_button = NULL;
    memset(&s_renderer, 0, sizeof(s_renderer));

    lv_obj_t *root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(root, lv_color_hex(0xF5F1E8), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(0xE8F0FA), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 24, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_label_create(root);
    lv_label_set_text(header, "图灵号飞行挑战");
    lv_obj_set_style_text_color(header, lv_color_hex(0x24466B), 0);
    lv_obj_set_style_text_font(header, app_fonts_get_title_font(), 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);

    s_subhead = lv_label_create(root);
    lv_obj_set_style_text_color(s_subhead, lv_color_hex(0x4E6785), 0);
    lv_obj_set_style_text_font(s_subhead, app_fonts_get_body_font(), 0);
    lv_obj_align_to(s_subhead, header, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    shooter_renderer_lvgl_create(&s_renderer, root);
    lv_obj_align(s_renderer.arena, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *back = lv_button_create(root);
    lv_obj_set_size(back, 130, 36);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0xE74C3C), 0);
    lv_obj_add_event_cb(back, exit_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_font(back_label, app_fonts_get_body_font(), 0);
    lv_obj_center(back_label);

    (void)create_action_button(root, "发射  J", -145, lv_color_hex(0xE67E22), fire_event_cb);
    s_device_button = create_action_button(root, "启动装置  K", 0,
                                           lv_color_hex(0x9B59B6), device_event_cb);

    start_level(level_id);
    s_tick_timer = lv_timer_create(tick_cb, SHOOTER_TICK_MS, NULL);
    lv_screen_load_anim(root, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    return root;
}

void shooter_screen_set_input(const shooter_input_state_t *input)
{
    if (input == NULL) {
        memset(&s_pending_input, 0, sizeof(s_pending_input));
    } else {
        s_pending_input = *input;
    }
}

void shooter_screen_force_start_level(uint32_t level)
{
    start_level(level);
}
