#include "app_ui.h"

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "app_ui_font_cn_16.h"
#include "app_ui_font_cn_24.h"
#include "assistant_mode.h"
#include "assistant_router.h"
#include "audio_self_test.h"
#include "board_mapping.h"
#include "board_snapshot.h"
#include "circuit_debug.h"
#include "circuit_layout.h"
#include "c6_network_test.h"
#include "game_judge.h"
#include "level_rules.h"
#include "play_mode.h"
#include "campaign_content.h"
#include "campaign_progress.h"
#include "progress_sync.h"
#include "shooter_font_cn_16.h"
#include "shooter_game.h"
#include "volcengine_voice.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_display.h"
#include "lvgl.h"

#include "bsp/display.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp/touch.h"

#define UI_SCREEN_WIDTH            1024
#define UI_SCREEN_HEIGHT           600
#define UI_HEADER_HEIGHT           76
#define UI_MAX_NODES               24
#define UI_CAMPAIGN_SUMMARY_SIZE   768
#define UI_MAP_X                   18
#define UI_MAP_Y                   90
#define UI_MAP_WIDTH               724
#define UI_MAP_HEIGHT              500
#define UI_NODE_WIDTH              118
#define UI_NODE_HEIGHT             64
#define UI_MAP_GRID_SIZE           56

#define UI_COLOR_BG                0x0A0E27
#define UI_COLOR_SURFACE           0x0F1B3D
#define UI_COLOR_SURFACE_ALT       0x162451
#define UI_COLOR_SURFACE_HI        0x24315B
#define UI_COLOR_BORDER            0x294B78
#define UI_COLOR_GRID              0x1B2C55
#define UI_COLOR_CYAN              0x5EDCF4
#define UI_COLOR_CYAN_DIM          0x2C90B6
#define UI_COLOR_PINK              0xF0A6CA
#define UI_COLOR_YELLOW            0xF4D03F
#define UI_COLOR_AMBER             UI_COLOR_YELLOW
#define UI_COLOR_GREEN             0x75E6B0
#define UI_COLOR_TEXT              0xF4F0FF
#define UI_COLOR_MUTED             0xB9B7D5
#define UI_COLOR_RED               0xFF5B66
#define UI_COLOR_DANGER            0xFF7895

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_CAMPAIGN,
    UI_PAGE_DETAIL,
    UI_PAGE_PLAY,
    UI_PAGE_SUCCESS,
    UI_PAGE_SETTINGS,
    UI_PAGE_SETTINGS_VOLUME,
    UI_PAGE_SETTINGS_WIFI,
    UI_PAGE_SETTINGS_WIFI_PASSWORD,
    UI_PAGE_SETTINGS_DEBUG,
    UI_PAGE_DIAGNOSTICS,
    UI_PAGE_SHOOTER,
    UI_PAGE_COUNT,
} ui_page_t;

typedef enum {
    UI_HOME_CAMPAIGN = 0,
    UI_HOME_SETTINGS,
    UI_HOME_COUNT,
} ui_home_item_t;

typedef enum {
    UI_SETTINGS_VOLUME = 0,
    UI_SETTINGS_WIFI,
    UI_SETTINGS_PROGRESS_SYNC,
    UI_SETTINGS_ASSISTANT_MODE,
    UI_SETTINGS_DEBUG,
    UI_SETTINGS_COUNT,
} ui_settings_item_t;

typedef enum {
    UI_DEBUG_SPEAKER_TEST = 0,
    UI_DEBUG_UNLOCK_ALL,
    UI_DEBUG_INITIALIZE,
    UI_DEBUG_SETTINGS_COUNT,
} ui_debug_settings_item_t;

#define UI_WIFI_PRESET_COUNT 3U
#define UI_WIFI_VISIBLE_ROWS 4U
#define UI_WIFI_KEY_COUNT 40U
#define UI_WIFI_PASSWORD_MODE_CONTROL UI_WIFI_KEY_COUNT
#define UI_WIFI_PASSWORD_DELETE_CONTROL (UI_WIFI_KEY_COUNT + 1U)
#define UI_WIFI_PASSWORD_CONNECT_CONTROL (UI_WIFI_KEY_COUNT + 2U)

typedef struct {
    lv_obj_t *screen[UI_PAGE_COUNT];
    lv_obj_t *home_items[UI_HOME_COUNT];
    lv_obj_t *settings_items[UI_SETTINGS_COUNT];
    lv_obj_t *settings_wifi_items[UI_WIFI_VISIBLE_ROWS];
    lv_obj_t *settings_wifi_titles[UI_WIFI_VISIBLE_ROWS];
    lv_obj_t *settings_wifi_details[UI_WIFI_VISIBLE_ROWS];
    lv_obj_t *settings_debug_items[UI_DEBUG_SETTINGS_COUNT];
    lv_obj_t *map;
    lv_obj_t *map_nodes[UI_MAX_NODES];
    lv_obj_t *map_node_ids[UI_MAX_NODES];
    lv_obj_t *map_node_titles[UI_MAX_NODES];
    lv_obj_t *campaign_code;
    lv_obj_t *campaign_title;
    lv_obj_t *campaign_goal;
    lv_obj_t *campaign_gate_reward;
    lv_obj_t *campaign_ship_reward;
    lv_obj_t *detail_code;
    lv_obj_t *detail_title;
    lv_obj_t *detail_story;
    lv_obj_t *detail_availability;
    lv_obj_t *detail_availability_dot;
    lv_obj_t *detail_availability_label;
    lv_obj_t *detail_gate_reward;
    lv_obj_t *detail_ship_reward;
    lv_obj_t *detail_start;
    lv_obj_t *detail_start_label;
    lv_obj_t *settings_sync_status;
    lv_obj_t *settings_unlock_status;
    lv_obj_t *settings_initialize_status;
    lv_obj_t *settings_assistant_status;
    lv_obj_t *settings_volume_value;
    lv_obj_t *settings_volume_bar;
    lv_obj_t *settings_wifi_status;
    lv_obj_t *settings_wifi_list_title;
    lv_obj_t *settings_password_ssid;
    lv_obj_t *settings_password_value;
    lv_obj_t *settings_password_keys[UI_WIFI_KEY_COUNT];
    lv_obj_t *settings_password_key_labels[UI_WIFI_KEY_COUNT];
    lv_obj_t *settings_password_mode;
    lv_obj_t *settings_password_mode_label;
    lv_obj_t *settings_password_delete;
    lv_obj_t *settings_password_connect;
    lv_obj_t *play_circuit;
    lv_obj_t *play_code;
    lv_obj_t *play_title;
    lv_obj_t *play_goal;
    lv_obj_t *play_truth;
    lv_obj_t *play_status;
    lv_obj_t *play_action;
    lv_obj_t *play_action_label;
    lv_obj_t *play_debug_action;
    lv_obj_t *play_debug_action_label;
    lv_obj_t *success_code;
    lv_obj_t *success_title;
    lv_obj_t *success_status;
    lv_obj_t *debug_keys;
    lv_obj_t *debug_joystick;
    lv_obj_t *debug_switches;
    lv_obj_t *debug_ir;
    lv_obj_t *debug_mic;
    lv_obj_t *debug_tone;
    lv_obj_t *debug_mic_bar;
} ui_objects_t;

static const char *TAG = "app_ui";
static ui_objects_t s_ui;
static ui_page_t s_page = UI_PAGE_HOME;
static uint8_t s_home_selection = UI_HOME_CAMPAIGN;
static uint8_t s_settings_selection = UI_SETTINGS_VOLUME;
static uint8_t s_settings_wifi_selection;
static uint8_t s_settings_wifi_window;
static uint8_t s_settings_wifi_count;
static bool s_settings_wifi_scan_mode;
static bool s_settings_wifi_scan_pending;
static c6_network_scan_result_t s_settings_wifi_scan_results[C6_NETWORK_SCAN_MAX];
static char s_settings_wifi_selected_ssid[C6_NETWORK_SSID_MAX + 1U];
static char s_settings_wifi_password[C6_NETWORK_PASSWORD_MAX + 1U];
static uint8_t s_settings_wifi_keyboard_selection;
static uint8_t s_settings_wifi_keyboard_mode;
static uint8_t s_settings_debug_selection = UI_DEBUG_SPEAKER_TEST;
static uint16_t s_node_count;
static uint16_t s_selected_node;
static int32_t s_map_offset_x;
static int32_t s_map_offset_y;
static char s_campaign_summary[UI_CAMPAIGN_SUMMARY_SIZE];
static key_input_state_t s_previous_keys;
static joystick_input_state_t s_previous_joystick;
static bool s_input_initialized;
static bool s_completed_nodes[UI_MAX_NODES];
static bool s_unlock_all_nodes;
static bool s_initialize_error;
static progress_sync_status_t s_last_progress_sync_status;
static bool s_progress_sync_status_initialized;
static uint8_t s_play_action_selection;
static board_snapshot_t s_play_snapshot;
static circuit_layout_t s_play_layout;
static uint32_t s_play_snapshot_generation;
static uint32_t s_play_topology_revision;
static uint32_t s_play_judge_version;
static uint32_t s_play_debug_generation;
static uint32_t s_play_voice_generation;
static bool s_programmer_owns_input;
static char s_play_goal_text[384];
static int64_t s_last_play_health_log_us;
static bool s_play_invalid_link_prompt_active;
static int64_t s_play_last_prompt_us;

#define UI_GAMEPLAY_PROMPT_COOLDOWN_US 3000000LL

static const campaign_node_t *ui_nodes(void)
{
    return campaign_content_nodes(NULL);
}

static int32_t ui_find_node_index(const campaign_node_t *nodes,
                                  uint16_t count,
                                  uint16_t node_id)
{
    for (uint16_t index = 0; index < count; ++index) {
        if (nodes[index].id == node_id) {
            return (int32_t)index;
        }
    }
    return -1;
}

static uint16_t ui_gate_unlock_level(ssd1315_gate_t gate)
{
    switch (gate) {
    case SSD1315_GATE_NAND: return 101U;
    case SSD1315_GATE_NOT: return 103U;
    case SSD1315_GATE_AND: return 201U;
    case SSD1315_GATE_OR: return 202U;
    case SSD1315_GATE_NOR: return 203U;
    case SSD1315_GATE_XOR: return 301U;
    case SSD1315_GATE_XNOR: return 302U;
    default: return 0U;
    }
}

static bool ui_gate_is_unlocked_for_progress(ssd1315_gate_t gate,
                                              const bool *completed)
{
    if (gate == SSD1315_GATE_INPUT || gate == SSD1315_GATE_OUTPUT) return true;
    if (completed == NULL) return false;
    const uint16_t level_id = ui_gate_unlock_level(gate);
    if (level_id == 0U) return false;
    const int32_t index = ui_find_node_index(ui_nodes(), s_node_count, level_id);
    return index >= 0 && completed[index];
}

bool app_ui_gate_is_unlocked(ssd1315_gate_t gate)
{
    return ui_gate_is_unlocked_for_progress(gate, s_completed_nodes);
}

static bool ui_node_is_unlocked_for_progress(uint16_t index,
                                             const bool *completed,
                                             bool unlock_all)
{
    const campaign_node_t *nodes = ui_nodes();
    if (index >= s_node_count || completed == NULL) {
        return false;
    }
    if (unlock_all || completed[index]) {
        return true;
    }
    if (nodes[index].prerequisite_count == 0U) {
        return nodes[index].id == campaign_content_meta().initial_level_id;
    }
    for (uint8_t prerequisite = 0U;
         prerequisite < nodes[index].prerequisite_count;
         ++prerequisite) {
        const int32_t parent = ui_find_node_index(
            nodes, s_node_count, nodes[index].prerequisite_ids[prerequisite]);
        if (parent < 0 || !completed[parent]) {
            return false;
        }
    }
    return true;
}

static bool ui_node_is_unlocked(uint16_t index)
{
    return ui_node_is_unlocked_for_progress(index, s_completed_nodes, s_unlock_all_nodes);
}

static const int16_t s_star_points[][3] = {
    {42, 96, 3}, {132, 164, 2}, {220, 106, 4}, {318, 188, 2},
    {414, 102, 3}, {506, 158, 2}, {612, 94, 3}, {736, 148, 2},
    {842, 106, 4}, {956, 180, 2}, {70, 540, 2}, {176, 518, 3},
    {286, 566, 2}, {438, 532, 3}, {580, 548, 2}, {708, 524, 3},
    {888, 548, 2}, {980, 506, 3}, {522, 78, 2}, {764, 72, 2},
};
static const uint32_t s_star_colors[] = {UI_COLOR_CYAN, UI_COLOR_PINK, UI_COLOR_YELLOW};

static void ui_load_page(ui_page_t page);
static void ui_refresh_campaign(void);
static void ui_refresh_detail(void);
static void ui_refresh_play(void);
static void ui_refresh_success(void);
static void ui_refresh_volume_settings(void);
static void ui_refresh_wifi_selection(void);
static void ui_refresh_debug_selection(void);
static void ui_create_play_screen(void);
static void ui_create_success_screen(void);
static void ui_exit_shooter_to_campaign(void);
static void ui_record_node_completion(uint16_t index);

static void ui_set_font(lv_obj_t *object)
{
    lv_obj_set_style_text_font(object, &app_ui_font_cn_24, 0);
}

static void ui_set_compact_font(lv_obj_t *object)
{
    lv_obj_set_style_text_font(object, &app_ui_font_cn_16, 0);
}

static void ui_style_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static void ui_style_panel(lv_obj_t *panel, uint32_t background, uint32_t border)
{
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *ui_create_label(lv_obj_t *parent,
                                 const char *text,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    ui_set_font(label);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static lv_obj_t *ui_create_panel(lv_obj_t *parent,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    ui_style_panel(panel, UI_COLOR_SURFACE, UI_COLOR_BORDER);
    return panel;
}

static void ui_pixel_corners_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const uint32_t color = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    const int32_t arm = 18;
    const int32_t thick = 3;
    const lv_area_t areas[8] = {
        {area.x1, area.y1, area.x1 + arm, area.y1 + thick},
        {area.x1, area.y1, area.x1 + thick, area.y1 + arm},
        {area.x2 - arm, area.y2 - thick, area.x2, area.y2},
        {area.x2 - thick, area.y2 - arm, area.x2, area.y2},
        {area.x2 - arm, area.y1, area.x2, area.y1 + thick},
        {area.x2 - thick, area.y1, area.x2, area.y1 + arm},
        {area.x1, area.y2 - thick, area.x1 + arm, area.y2},
        {area.x1, area.y2 - arm, area.x1 + thick, area.y2},
    };
    lv_draw_rect_dsc_t draw;
    lv_draw_rect_dsc_init(&draw);
    draw.bg_color = lv_color_hex(color);
    draw.bg_opa = LV_OPA_COVER;
    draw.radius = 0;
    for (uint8_t index = 0; index < 8; ++index) {
        lv_draw_rect(layer, &draw, &areas[index]);
    }
}

static void ui_starfield_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_draw_rect_dsc_t draw;
    lv_draw_rect_dsc_init(&draw);
    draw.radius = 0;
    for (uint8_t index = 0; index < sizeof(s_star_points) / sizeof(s_star_points[0]); ++index) {
        draw.bg_color = lv_color_hex(s_star_colors[index % 3]);
        draw.bg_opa = index % 4 == 0 ? LV_OPA_COVER : LV_OPA_70;
        const lv_area_t star = {
            area.x1 + s_star_points[index][0], area.y1 + s_star_points[index][1],
            area.x1 + s_star_points[index][0] + s_star_points[index][2],
            area.y1 + s_star_points[index][1] + s_star_points[index][2],
        };
        lv_draw_rect(layer, &draw, &star);
    }
}

static void ui_create_starfield(lv_obj_t *screen)
{
    lv_obj_add_event_cb(screen, ui_starfield_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
}

static void ui_add_pixel_corners(lv_obj_t *object, uint32_t color)
{
    lv_obj_add_event_cb(object, ui_pixel_corners_draw_event_cb, LV_EVENT_DRAW_POST,
                        (void *)(uintptr_t)color);
}

static lv_obj_t *ui_create_accent(lv_obj_t *parent,
                                  int32_t x,
                                  int32_t y,
                                  int32_t width,
                                  uint32_t color)
{
    lv_obj_t *accent = lv_obj_create(parent);
    lv_obj_set_pos(accent, x, y);
    lv_obj_set_size(accent, width, 3);
    lv_obj_set_style_bg_color(accent, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);
    return accent;
}

static void ui_create_signal_icon(lv_obj_t *parent,
                                  int32_t x,
                                  int32_t y,
                                  uint32_t color,
                                  uint8_t variant)
{
    lv_obj_t *halo = lv_obj_create(parent);
    lv_obj_set_pos(halo, x, y);
    lv_obj_set_size(halo, 88, 88);
    lv_obj_set_style_bg_color(halo, lv_color_hex(0x0B2028), 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(halo, 1, 0);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(halo, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *core = lv_obj_create(halo);
    lv_obj_set_size(core, 54, 44);
    lv_obj_center(core);
    lv_obj_set_style_bg_color(core, lv_color_hex(UI_COLOR_SURFACE_ALT), 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(core, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(core, 2, 0);
    lv_obj_set_style_radius(core, 8, 0);
    lv_obj_clear_flag(core, LV_OBJ_FLAG_CLICKABLE);

    for (uint8_t index = 0; index < 2; ++index) {
        lv_obj_t *eye = lv_obj_create(core);
        lv_obj_set_pos(eye, 12 + index * 25, 11);
        lv_obj_set_size(eye, 7, 8);
        lv_obj_set_style_bg_color(eye, lv_color_hex(index == 0 ? UI_COLOR_CYAN : UI_COLOR_PINK), 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(eye, 0, 0);
        lv_obj_set_style_radius(eye, 0, 0);
        lv_obj_clear_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *mouth = lv_obj_create(core);
    lv_obj_set_pos(mouth, 17, 29);
    lv_obj_set_size(mouth, 20, 4);
    lv_obj_set_style_bg_color(mouth, lv_color_hex(UI_COLOR_YELLOW), 0);
    lv_obj_set_style_bg_opa(mouth, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mouth, 0, 0);
    lv_obj_set_style_radius(mouth, 0, 0);
    lv_obj_clear_flag(mouth, LV_OBJ_FLAG_CLICKABLE);

    const int16_t pixels[4][2] = {{7, 17}, {75, 22}, {16, 72}, {67, 68}};
    for (uint8_t index = 0; index < (variant == 0 ? 3 : 4); ++index) {
        lv_obj_t *pixel = lv_obj_create(halo);
        lv_obj_set_pos(pixel, pixels[index][0], pixels[index][1]);
        lv_obj_set_size(pixel, 6, 6);
        lv_obj_set_style_bg_color(pixel, lv_color_hex(index % 2 == 0 ? UI_COLOR_PINK : UI_COLOR_YELLOW), 0);
        lv_obj_set_style_bg_opa(pixel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pixel, 0, 0);
        lv_obj_set_style_radius(pixel, 0, 0);
        lv_obj_clear_flag(pixel, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void ui_back_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_page != UI_PAGE_HOME) {
        audio_self_test_play_effect(AUDIO_EFFECT_BACK);
    }
    if (s_page == UI_PAGE_SHOOTER) {
        ui_exit_shooter_to_campaign();
    } else if (s_page == UI_PAGE_SETTINGS_VOLUME ||
               s_page == UI_PAGE_SETTINGS_WIFI ||
               s_page == UI_PAGE_SETTINGS_WIFI_PASSWORD ||
               s_page == UI_PAGE_SETTINGS_DEBUG) {
        ui_load_page(s_page == UI_PAGE_SETTINGS_WIFI_PASSWORD ?
                     UI_PAGE_SETTINGS_WIFI : UI_PAGE_SETTINGS);
    } else if (s_page == UI_PAGE_SUCCESS) {
        ui_load_page(UI_PAGE_PLAY);
    } else if (s_page == UI_PAGE_PLAY || s_page == UI_PAGE_DETAIL) {
        ui_load_page(UI_PAGE_CAMPAIGN);
    } else if (s_page != UI_PAGE_HOME) {
        ui_load_page(UI_PAGE_HOME);
    }
}

static void ui_create_header(lv_obj_t *screen, const char *section, const char *title, bool show_back)
{
    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, UI_SCREEN_WIDTH, UI_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x071017), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_accent(header, 0, UI_HEADER_HEIGHT - 3, 220, UI_COLOR_CYAN);

    int32_t title_x = show_back ? 122 : 22;
    if (show_back) {
        lv_obj_t *back = lv_button_create(header);
        lv_obj_set_pos(back, 16, 13);
        lv_obj_set_size(back, 92, 46);
        lv_obj_set_style_bg_color(back, lv_color_hex(UI_COLOR_SURFACE_ALT), 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(back, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_set_style_border_width(back, 1, 0);
        lv_obj_set_style_radius(back, 5, 0);
        lv_obj_add_event_cb(back, ui_back_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(back);
        ui_set_font(label);
        lv_label_set_text(label, "< 返回");
        lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_CYAN), 0);
        lv_obj_center(label);
    }

    lv_obj_t *section_label = ui_create_label(header, section, title_x, 9, 420, UI_COLOR_CYAN);
    ui_set_compact_font(section_label);
    lv_obj_t *title_label = ui_create_label(header, title, title_x, 28, 520, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(title_label, &app_ui_font_cn_24, 0);

    lv_obj_t *status_pill = lv_obj_create(header);
    lv_obj_set_pos(status_pill, 836, 20);
    lv_obj_set_size(status_pill, 164, 36);
    lv_obj_set_style_bg_color(status_pill, lv_color_hex(0x0C211C), 0);
    lv_obj_set_style_bg_opa(status_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status_pill, lv_color_hex(0x276A50), 0);
    lv_obj_set_style_border_width(status_pill, 1, 0);
    lv_obj_set_style_radius(status_pill, 18, 0);
    /* Child coordinates are intentionally measured from the visible pill edge. */
    lv_obj_set_style_pad_all(status_pill, 0, 0);
    lv_obj_clear_flag(status_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *status_dot = lv_obj_create(status_pill);
    lv_obj_set_pos(status_dot, 12, 12);
    lv_obj_set_size(status_dot, 10, 10);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(UI_COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *status = ui_create_label(status_pill, "准备好了", 32, 7, 120, UI_COLOR_GREEN);
    ui_set_compact_font(status);
    lv_obj_set_height(status, 22);
    lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
}

static void ui_style_action_item(lv_obj_t *item, bool selected, bool enabled)
{
    const uint32_t border = selected ? UI_COLOR_CYAN : UI_COLOR_BORDER;
    const uint32_t background = selected ? UI_COLOR_SURFACE_HI : UI_COLOR_SURFACE;
    lv_obj_set_style_bg_color(item, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(item, enabled ? LV_OPA_COVER : LV_OPA_80, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(item, selected ? 2 : 1, 0);
    lv_obj_set_style_radius(item, 2, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(UI_COLOR_CYAN_DIM), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(item, lv_color_hex(UI_COLOR_CYAN), LV_STATE_PRESSED);
}

static lv_obj_t *ui_create_action_item(lv_obj_t *parent,
                                       int32_t x,
                                       int32_t y,
                                       int32_t width,
                                       int32_t height,
                                       const char *index,
                                       const char *title,
                                       const char *meta,
                                       lv_event_cb_t callback,
                                       void *user_data)
{
    lv_obj_t *item = lv_button_create(parent);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_size(item, width, height);
    lv_obj_set_style_pad_all(item, 0, 0);
    ui_style_action_item(item, false, true);
    if (callback != NULL) {
        lv_obj_add_event_cb(item, callback, LV_EVENT_CLICKED, user_data);
    }

    ui_create_label(item, index, 22, 18, 190, UI_COLOR_AMBER);
    const int32_t title_width = height > 220 ? width - 170 : width - 100;
    lv_obj_t *title_label = ui_create_label(item, title, 22, 49, title_width, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(title_label, &app_ui_font_cn_24, 0);
    ui_create_label(item, meta, 22, height - 38, width - 44, UI_COLOR_MUTED);
    ui_create_accent(item, 22, height - 58, width - 88, UI_COLOR_CYAN_DIM);

    if (height > 220) {
        ui_create_signal_icon(item, width - 122, 32, UI_COLOR_CYAN, 0);
    } else {
        lv_obj_t *arrow = ui_create_label(item, ">", width - 54, height / 2 - 16, 32, UI_COLOR_CYAN);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
    }
    return item;
}

static lv_obj_t *ui_create_settings_item(lv_obj_t *parent,
                                         int32_t x,
                                         int32_t y,
                                         int32_t width,
                                         int32_t height,
                                         const char *section,
                                         const char *title,
                                         lv_event_cb_t callback,
                                         void *user_data)
{
    lv_obj_t *item = lv_button_create(parent);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_size(item, width, height);
    lv_obj_set_style_pad_all(item, 0, 0);
    ui_style_action_item(item, false, true);
    if (callback != NULL) {
        lv_obj_add_event_cb(item, callback, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *section_label = ui_create_label(item, section, 22, 14, width - 84, UI_COLOR_AMBER);
    ui_set_compact_font(section_label);
    lv_obj_t *title_label = ui_create_label(item, title, 22, 38, width - 86, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(title_label, &app_ui_font_cn_24, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_height(title_label, 30);
    ui_create_accent(item, 22, 74, width - 86, UI_COLOR_CYAN_DIM);

    lv_obj_t *arrow = ui_create_label(item, ">", width - 54, 42, 32, UI_COLOR_CYAN);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
    return item;
}

static void ui_refresh_home_selection(void)
{
    for (uint8_t index = 0; index < UI_HOME_COUNT; ++index) {
        ui_style_action_item(s_ui.home_items[index], index == s_home_selection, true);
    }
}

static void ui_refresh_settings_selection(void)
{
    for (uint8_t index = 0; index < UI_SETTINGS_COUNT; ++index) {
        ui_style_action_item(s_ui.settings_items[index], index == s_settings_selection, true);
    }
    if (s_ui.settings_sync_status != NULL) {
        progress_sync_status_t status;
        progress_sync_get_status(&status);
        const char *text = "按中键上传 16 个关卡记录";
        uint32_t color = UI_COLOR_MUTED;
        switch (status.state) {
        case PROGRESS_SYNC_UPLOADING:
            text = "正在上传云存档...";
            color = UI_COLOR_CYAN;
            break;
        case PROGRESS_SYNC_SUCCEEDED:
            text = "云存档上传成功";
            color = UI_COLOR_GREEN;
            break;
        case PROGRESS_SYNC_NETWORK_UNAVAILABLE:
            text = "网络未连接";
            color = UI_COLOR_DANGER;
            break;
        case PROGRESS_SYNC_CONFIGURATION_ERROR:
            text = "同步密钥未配置";
            color = UI_COLOR_DANGER;
            break;
        case PROGRESS_SYNC_SERVER_REJECTED:
            text = "服务端拒绝上传";
            color = UI_COLOR_DANGER;
            break;
        case PROGRESS_SYNC_FAILED:
            text = "上传失败，请稍后重试";
            color = UI_COLOR_DANGER;
            break;
        case PROGRESS_SYNC_READY:
        default:
            break;
        }
        lv_label_set_text(s_ui.settings_sync_status, text);
        lv_obj_set_style_text_color(s_ui.settings_sync_status, lv_color_hex(color), 0);
        s_last_progress_sync_status = status;
        s_progress_sync_status_initialized = true;
    }
    if (s_ui.settings_assistant_status != NULL) {
        const bool remote = assistant_mode_get() == ASSISTANT_MODE_REMOTE;
        lv_label_set_text(s_ui.settings_assistant_status,
                          remote ? "当前：后端助教" : "当前：内置助教");
        lv_obj_set_style_text_color(s_ui.settings_assistant_status,
                                    lv_color_hex(remote ? UI_COLOR_GREEN : UI_COLOR_CYAN), 0);
    }
}

static void ui_refresh_volume_settings(void)
{
    if (s_ui.settings_volume_value == NULL || s_ui.settings_volume_bar == NULL) return;
    const uint8_t volume = audio_self_test_get_master_volume();
    lv_label_set_text_fmt(s_ui.settings_volume_value, "%u%%", (unsigned)volume);
    if (volume == 0U) {
        lv_obj_add_flag(s_ui.settings_volume_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_ui.settings_volume_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_ui.settings_volume_bar, (int32_t)(702U * volume / 100U));
    }
}

static void ui_refresh_wifi_selection(void)
{
    c6_network_status_t network;
    c6_network_get_status(&network);
    if (s_settings_wifi_scan_pending && network.state != C6_NETWORK_SCANNING) {
        s_settings_wifi_count = c6_network_get_scan_results(
            s_settings_wifi_scan_results, C6_NETWORK_SCAN_MAX);
        s_settings_wifi_scan_mode = true;
        s_settings_wifi_scan_pending = false;
        s_settings_wifi_selection = 0U;
        s_settings_wifi_window = 0U;
    }
    const uint8_t count = s_settings_wifi_scan_mode ? s_settings_wifi_count :
                          (uint8_t)(UI_WIFI_PRESET_COUNT + 1U);
    if (count > 0U && s_settings_wifi_selection >= count) s_settings_wifi_selection = count - 1U;
    if (s_settings_wifi_selection < s_settings_wifi_window ||
        s_settings_wifi_selection >= s_settings_wifi_window + UI_WIFI_VISIBLE_ROWS) {
        s_settings_wifi_window = (uint8_t)((s_settings_wifi_selection / UI_WIFI_VISIBLE_ROWS) *
                                           UI_WIFI_VISIBLE_ROWS);
    }
    if (s_ui.settings_wifi_list_title != NULL) {
        lv_label_set_text(s_ui.settings_wifi_list_title,
                          s_settings_wifi_scan_mode ? "附近网络" : "预设热点");
    }
    for (uint8_t row = 0U; row < UI_WIFI_VISIBLE_ROWS; ++row) {
        const uint8_t index = s_settings_wifi_window + row;
        if (index < count) {
            const char *title;
            const char *detail;
            char detail_text[64];
            if (s_settings_wifi_scan_mode) {
                const c6_network_scan_result_t *result = &s_settings_wifi_scan_results[index];
                title = result->ssid;
                snprintf(detail_text, sizeof(detail_text), "%ddBm  %s", result->rssi,
                         result->requires_password ? "需要密码" : "开放网络");
                detail = detail_text;
            } else if (index == 0U) {
                title = "扫描附近网络";
                detail = s_settings_wifi_scan_pending ? "正在扫描..." : "按中键开始扫描";
            } else {
                title = c6_network_get_preset_ssid(index - 1U);
                detail = title[0] == '\0' ? "未设置热点" : "使用预设密码连接";
            }
            lv_obj_remove_flag(s_ui.settings_wifi_items[row], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_ui.settings_wifi_titles[row], title);
            lv_label_set_text(s_ui.settings_wifi_details[row], detail);
            ui_style_action_item(s_ui.settings_wifi_items[row],
                                 index == s_settings_wifi_selection, true);
        } else {
            lv_obj_add_flag(s_ui.settings_wifi_items[row], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ui.settings_wifi_status != NULL) {
        const char *text = network.state == C6_NETWORK_SCANNING ? "正在扫描" :
                           network.state == C6_NETWORK_CONNECTING ? "正在连接" :
                           network.connected ? network.current_ssid :
                           network.state == C6_NETWORK_FAILED ? "连接失败" : "未连接";
        lv_label_set_text(s_ui.settings_wifi_status, text);
        lv_obj_set_style_text_color(s_ui.settings_wifi_status,
                                    lv_color_hex(network.connected ? UI_COLOR_GREEN :
                                                 network.state == C6_NETWORK_FAILED ? UI_COLOR_DANGER :
                                                 UI_COLOR_MUTED), 0);
    }
}

static void ui_refresh_wifi_password(void)
{
    if (s_ui.settings_password_ssid == NULL || s_ui.settings_password_value == NULL) return;
    char masked[C6_NETWORK_PASSWORD_MAX + 1U] = {0};
    const size_t length = strlen(s_settings_wifi_password);
    memset(masked, '*', length);
    lv_label_set_text(s_ui.settings_password_ssid, s_settings_wifi_selected_ssid);
    lv_label_set_text(s_ui.settings_password_value, masked);
    static const char *const keymaps[] = {
        "abcdefghijklmnopqrstuvwxyz0123456789-_@.",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_@.",
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~        ",
    };
    static const char *const mode_labels[] = {"abc", "ABC", "#+="};
    const char *keymap = keymaps[s_settings_wifi_keyboard_mode];
    for (uint8_t index = 0U; index < UI_WIFI_KEY_COUNT; ++index) {
        char key_label[3] = {keymap[index], '\0', '\0'};
        if (keymap[index] == ' ') strlcpy(key_label, "SP", sizeof(key_label));
        lv_label_set_text(s_ui.settings_password_key_labels[index], key_label);
        ui_style_action_item(s_ui.settings_password_keys[index],
                             s_settings_wifi_keyboard_selection == index, true);
    }
    lv_label_set_text(s_ui.settings_password_mode_label, mode_labels[s_settings_wifi_keyboard_mode]);
    ui_style_action_item(s_ui.settings_password_mode,
                         s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_MODE_CONTROL, true);
    ui_style_action_item(s_ui.settings_password_delete,
                         s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_DELETE_CONTROL, true);
    ui_style_action_item(s_ui.settings_password_connect,
                         s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_CONNECT_CONTROL, true);
}

static void ui_activate_wifi_selection(void)
{
    if (!s_settings_wifi_scan_mode) {
        if (s_settings_wifi_selection == 0U) {
            if (c6_network_request_scan() == ESP_OK) {
                s_settings_wifi_scan_pending = true;
                audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
            } else {
                audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
            }
        } else if (c6_network_connect_preset(s_settings_wifi_selection - 1U) == ESP_OK) {
            audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        } else {
            audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
        }
        ui_refresh_wifi_selection();
        return;
    }
    if (s_settings_wifi_selection >= s_settings_wifi_count) return;
    const c6_network_scan_result_t *result =
        &s_settings_wifi_scan_results[s_settings_wifi_selection];
    if (!result->requires_password) {
        if (c6_network_connect_scan_result(s_settings_wifi_selection, "") == ESP_OK) {
            audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        } else {
            audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
        }
        return;
    }
    strlcpy(s_settings_wifi_selected_ssid, result->ssid, sizeof(s_settings_wifi_selected_ssid));
    s_settings_wifi_password[0] = '\0';
    s_settings_wifi_keyboard_selection = 0U;
    s_settings_wifi_keyboard_mode = 0U;
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    ui_load_page(UI_PAGE_SETTINGS_WIFI_PASSWORD);
}

static void ui_refresh_debug_selection(void)
{
    for (uint8_t index = 0U; index < UI_DEBUG_SETTINGS_COUNT; ++index) {
        ui_style_action_item(s_ui.settings_debug_items[index],
                             index == s_settings_debug_selection, true);
    }
    if (s_ui.settings_unlock_status != NULL) {
        lv_label_set_text(s_ui.settings_unlock_status,
                          s_unlock_all_nodes ? "全部关卡已解锁" : "按中键解锁，仅本次上电");
        lv_obj_set_style_text_color(s_ui.settings_unlock_status,
                                    lv_color_hex(s_unlock_all_nodes ? UI_COLOR_GREEN : UI_COLOR_MUTED), 0);
    }
    if (s_ui.settings_initialize_status != NULL) {
        lv_label_set_text(s_ui.settings_initialize_status,
                          s_initialize_error ? "初始化失败，请稍后重试" : "按中键清空通关记录");
        lv_obj_set_style_text_color(s_ui.settings_initialize_status,
                                    lv_color_hex(s_initialize_error ? UI_COLOR_DANGER : UI_COLOR_MUTED), 0);
    }
}

static void ui_initialize_campaign_progress(void)
{
    const esp_err_t err = campaign_progress_clear();
    s_initialize_error = err != ESP_OK;
    if (err != ESP_OK) {
        audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
        ui_refresh_debug_selection();
        return;
    }

    memset(s_completed_nodes, 0, sizeof(s_completed_nodes));
    s_unlock_all_nodes = false;
    const campaign_content_meta_t meta = campaign_content_meta();
    const campaign_node_t *nodes = ui_nodes();
    s_selected_node = 0U;
    for (uint16_t index = 0U; index < s_node_count; ++index) {
        if (nodes[index].id == meta.initial_level_id) {
            s_selected_node = index;
            break;
        }
    }
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    ui_refresh_debug_selection();
    ui_refresh_campaign();
    ui_refresh_detail();
    ESP_LOGI(TAG, "campaign progress initialized from debug settings");
}

static void ui_activate_debug_selection(void)
{
    if (s_settings_debug_selection == UI_DEBUG_SPEAKER_TEST) return;
    if (s_settings_debug_selection == UI_DEBUG_UNLOCK_ALL) {
        if (!s_unlock_all_nodes) {
            s_unlock_all_nodes = true;
            audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
            ui_refresh_debug_selection();
            ui_refresh_campaign();
            ui_refresh_detail();
            ESP_LOGI(TAG, "all campaign nodes unlocked for this boot");
        }
        return;
    }
    ui_initialize_campaign_progress();
}

static void ui_activate_settings_selection(void)
{
    switch (s_settings_selection) {
    case UI_SETTINGS_VOLUME:
        audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        ui_load_page(UI_PAGE_SETTINGS_VOLUME);
        break;
    case UI_SETTINGS_WIFI:
        audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        ui_load_page(UI_PAGE_SETTINGS_WIFI);
        break;
    case UI_SETTINGS_PROGRESS_SYNC: {
        const esp_err_t err = progress_sync_request(ui_nodes(), s_node_count, s_completed_nodes);
        audio_self_test_play_effect(err == ESP_OK ? AUDIO_EFFECT_CONFIRM : AUDIO_EFFECT_ERROR);
        ui_refresh_settings_selection();
        break;
    }
    case UI_SETTINGS_ASSISTANT_MODE: {
        const assistant_mode_t next = assistant_mode_get() == ASSISTANT_MODE_LOCAL ?
            ASSISTANT_MODE_REMOTE : ASSISTANT_MODE_LOCAL;
        const esp_err_t err = assistant_mode_set(next);
        if (err == ESP_OK) {
            assistant_router_handle_mode_change();
            audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        } else {
            audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
        }
        ui_refresh_settings_selection();
        break;
    }
    case UI_SETTINGS_DEBUG:
        audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        ui_load_page(UI_PAGE_SETTINGS_DEBUG);
        break;
    default:
        break;
    }
}

static void ui_adjust_settings_volume(int32_t delta)
{
    int32_t volume = (int32_t)audio_self_test_get_master_volume() + delta;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    if (volume != audio_self_test_get_master_volume()) {
        audio_self_test_set_master_volume((uint8_t)volume);
        audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
    }
    ui_refresh_volume_settings();
}

static void ui_move_settings_selection(int32_t direction_x, int32_t direction_y)
{
    uint8_t next = s_settings_selection;
    if (direction_x < 0) {
        if (s_settings_selection == UI_SETTINGS_WIFI) next = UI_SETTINGS_VOLUME;
        else if (s_settings_selection == UI_SETTINGS_ASSISTANT_MODE) next = UI_SETTINGS_PROGRESS_SYNC;
        else if (s_settings_selection == UI_SETTINGS_DEBUG) next = UI_SETTINGS_PROGRESS_SYNC;
    } else if (direction_x > 0) {
        if (s_settings_selection == UI_SETTINGS_VOLUME) next = UI_SETTINGS_WIFI;
        else if (s_settings_selection == UI_SETTINGS_PROGRESS_SYNC) next = UI_SETTINGS_ASSISTANT_MODE;
        else if (s_settings_selection == UI_SETTINGS_DEBUG) next = UI_SETTINGS_ASSISTANT_MODE;
    } else if (direction_y < 0) {
        if (s_settings_selection == UI_SETTINGS_PROGRESS_SYNC) next = UI_SETTINGS_VOLUME;
        else if (s_settings_selection == UI_SETTINGS_ASSISTANT_MODE) next = UI_SETTINGS_WIFI;
        else if (s_settings_selection == UI_SETTINGS_DEBUG) next = UI_SETTINGS_PROGRESS_SYNC;
    } else if (direction_y > 0) {
        if (s_settings_selection == UI_SETTINGS_VOLUME) next = UI_SETTINGS_PROGRESS_SYNC;
        else if (s_settings_selection == UI_SETTINGS_WIFI) next = UI_SETTINGS_ASSISTANT_MODE;
        else if (s_settings_selection == UI_SETTINGS_PROGRESS_SYNC ||
                 s_settings_selection == UI_SETTINGS_ASSISTANT_MODE) next = UI_SETTINGS_DEBUG;
    }
    if (next != s_settings_selection) {
        s_settings_selection = next;
        audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
        ui_refresh_settings_selection();
    }
}

static void ui_home_item_event_cb(lv_event_t *event)
{
    const uintptr_t value = (uintptr_t)lv_event_get_user_data(event);
    s_home_selection = (uint8_t)(value - 1U);
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    ui_refresh_home_selection();
    ui_load_page(s_home_selection == UI_HOME_CAMPAIGN ? UI_PAGE_CAMPAIGN : UI_PAGE_SETTINGS);
}

static void ui_settings_item_event_cb(lv_event_t *event)
{
    const uintptr_t value = (uintptr_t)lv_event_get_user_data(event);
    s_settings_selection = (uint8_t)(value - 1U);
    audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
    ui_refresh_settings_selection();
    audio_self_test_set_tone_enabled(false);
    ui_activate_settings_selection();
}

static void ui_volume_adjust_event_cb(lv_event_t *event)
{
    const int32_t delta = (int32_t)(intptr_t)lv_event_get_user_data(event);
    if (delta == 0) {
        if (audio_self_test_get_master_volume() != 70U) {
            audio_self_test_set_master_volume(70U);
            audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
        }
        ui_refresh_volume_settings();
        return;
    }
    ui_adjust_settings_volume(delta);
}

static void ui_wifi_item_event_cb(lv_event_t *event)
{
    const uintptr_t value = (uintptr_t)lv_event_get_user_data(event);
    s_settings_wifi_selection = s_settings_wifi_window + (uint8_t)(value - 1U);
    audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
    ui_refresh_wifi_selection();
    ui_activate_wifi_selection();
}

static const char *ui_wifi_password_keymap(void)
{
    static const char *const keymaps[] = {
        "abcdefghijklmnopqrstuvwxyz0123456789-_@.",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_@.",
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~        ",
    };
    return keymaps[s_settings_wifi_keyboard_mode];
}

static void ui_wifi_password_insert(uint8_t index)
{
    const char *keymap = ui_wifi_password_keymap();
    const size_t length = strlen(s_settings_wifi_password);
    if (index < UI_WIFI_KEY_COUNT && length < C6_NETWORK_PASSWORD_MAX) {
        s_settings_wifi_password[length] = keymap[index];
        s_settings_wifi_password[length + 1U] = '\0';
        audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
        ui_refresh_wifi_password();
    }
}

static void ui_wifi_password_key_event_cb(lv_event_t *event)
{
    const uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    s_settings_wifi_keyboard_selection = (uint8_t)index;
    ui_wifi_password_insert((uint8_t)index);
}

static void ui_wifi_password_mode_event_cb(lv_event_t *event)
{
    (void)event;
    s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_MODE_CONTROL;
    s_settings_wifi_keyboard_mode = (s_settings_wifi_keyboard_mode + 1U) % 3U;
    audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
    ui_refresh_wifi_password();
}

static void ui_debug_item_event_cb(lv_event_t *event)
{
    const uintptr_t value = (uintptr_t)lv_event_get_user_data(event);
    s_settings_debug_selection = (uint8_t)(value - 1U);
    audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
    ui_refresh_debug_selection();
    ui_activate_debug_selection();
}

static void ui_create_home_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_HOME] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "图灵号", "关卡游玩", false);

    ui_create_label(screen, "选择你的冒险", 30, 102, 480, UI_COLOR_CYAN);
    lv_obj_t *headline = ui_create_label(screen, "小飞船要出发啦！", 30, 132, 520, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(headline, &app_ui_font_cn_24, 0);
    ui_create_label(screen, "请选择你想去的地方", 30, 166, 800, UI_COLOR_MUTED);
    ui_create_accent(screen, 30, 194, 964, UI_COLOR_BORDER);

    s_ui.home_items[UI_HOME_CAMPAIGN] = ui_create_action_item(
        screen, 30, 220, 466, 294, "第一站", "关卡游玩", "去星际地图闯关",
        ui_home_item_event_cb, (void *)(uintptr_t)(UI_HOME_CAMPAIGN + 1U));
    s_ui.home_items[UI_HOME_SETTINGS] = ui_create_action_item(
        screen, 528, 220, 466, 294, "第二站", "设置", "看看小飞船的状态",
        ui_home_item_event_cb, (void *)(uintptr_t)(UI_HOME_SETTINGS + 1U));
    ui_add_pixel_corners(s_ui.home_items[UI_HOME_CAMPAIGN], UI_COLOR_CYAN);
    ui_add_pixel_corners(s_ui.home_items[UI_HOME_SETTINGS], UI_COLOR_PINK);

    ui_create_label(screen, "用摇杆选择，按中间键进入", 30, 556, 500, UI_COLOR_MUTED);
    ui_refresh_home_selection();
}

static int32_t ui_positive_mod(int32_t value, int32_t divisor)
{
    int32_t result = value % divisor;
    return result < 0 ? result + divisor : result;
}

static void ui_map_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *map = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(map, &area);

    lv_draw_line_dsc_t grid;
    lv_draw_line_dsc_init(&grid);
    grid.color = lv_color_hex(UI_COLOR_GRID);
    grid.width = 1;
    grid.opa = LV_OPA_60;

    const int32_t grid_x = ui_positive_mod(s_map_offset_x, UI_MAP_GRID_SIZE);
    const int32_t grid_y = ui_positive_mod(s_map_offset_y, UI_MAP_GRID_SIZE);
    for (int32_t x = grid_x; x < UI_MAP_WIDTH; x += UI_MAP_GRID_SIZE) {
        grid.p1 = (lv_point_precise_t) {.x = area.x1 + x, .y = area.y1};
        grid.p2 = (lv_point_precise_t) {.x = area.x1 + x, .y = area.y2};
        lv_draw_line(layer, &grid);
    }
    for (int32_t y = grid_y; y < UI_MAP_HEIGHT; y += UI_MAP_GRID_SIZE) {
        grid.p1 = (lv_point_precise_t) {.x = area.x1, .y = area.y1 + y};
        grid.p2 = (lv_point_precise_t) {.x = area.x2, .y = area.y1 + y};
        lv_draw_line(layer, &grid);
    }

    const campaign_node_t *nodes = ui_nodes();
    for (uint16_t child = 0; child < s_node_count; ++child) {
        for (uint8_t prerequisite = 0; prerequisite < nodes[child].prerequisite_count; ++prerequisite) {
            uint16_t parent = 0;
            while (parent < s_node_count &&
                   nodes[parent].id != nodes[child].prerequisite_ids[prerequisite]) {
                ++parent;
            }
            if (parent == s_node_count) {
                continue;
            }
            lv_draw_line_dsc_t route;
            lv_draw_line_dsc_init(&route);
            const bool active = child == s_selected_node || parent == s_selected_node;
            route.color = lv_color_hex(active ? UI_COLOR_AMBER : UI_COLOR_CYAN_DIM);
            route.width = active ? 3 : 2;
            route.opa = active ? LV_OPA_COVER : LV_OPA_70;
            route.round_start = 1;
            route.round_end = 1;
            route.p1 = (lv_point_precise_t) {
                .x = area.x1 + nodes[parent].x + s_map_offset_x,
                .y = area.y1 + nodes[parent].y + s_map_offset_y,
            };
            route.p2 = (lv_point_precise_t) {
                .x = area.x1 + nodes[child].x + s_map_offset_x,
                .y = area.y1 + nodes[child].y + s_map_offset_y,
            };
            lv_draw_line(layer, &route);
        }
    }
}

static void ui_style_map_node(uint16_t index)
{
    const campaign_node_t *content = &ui_nodes()[index];
    const bool selected = index == s_selected_node;
    const bool reward = content->kind == CAMPAIGN_NODE_KIND_REWARD;
    const bool completed = s_completed_nodes[index];
    const bool unlocked = ui_node_is_unlocked(index);
    lv_obj_t *node = s_ui.map_nodes[index];
    const uint32_t background = !unlocked ? 0x080D15 :
                                (reward ? (selected ? 0x4A2949 : 0x241A35) :
                                 (selected ? 0x16434B : 0x0A171E));
    const uint32_t border = completed ? UI_COLOR_GREEN :
                            (!unlocked ? UI_COLOR_BORDER :
                             (reward ? (selected ? UI_COLOR_YELLOW : UI_COLOR_PINK) :
                              (selected ? UI_COLOR_AMBER : UI_COLOR_CYAN_DIM)));
    lv_obj_set_style_bg_color(node, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(node, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(node, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(node, completed ? 3 :
                                  (reward ? (selected ? 4 : 2) : (selected ? 3 : 1)), 0);
    lv_obj_set_style_radius(node, reward ? 4 : 14, 0);
    lv_obj_set_style_outline_color(node, lv_color_hex(reward ? UI_COLOR_PINK : UI_COLOR_CYAN_DIM), 0);
    lv_obj_set_style_outline_width(node, reward && unlocked ? 1 : 0, 0);
    lv_obj_set_style_outline_pad(node, reward && unlocked ? 3 : 0, 0);
    lv_obj_set_style_text_color(s_ui.map_node_ids[index],
                                lv_color_hex(!unlocked ? UI_COLOR_MUTED :
                                             (reward ? (selected ? UI_COLOR_YELLOW : UI_COLOR_PINK) :
                                              (selected ? UI_COLOR_AMBER : UI_COLOR_CYAN))), 0);
    lv_obj_set_style_text_color(s_ui.map_node_titles[index],
                                lv_color_hex(!unlocked ? UI_COLOR_MUTED :
                                             (reward ? UI_COLOR_YELLOW : UI_COLOR_TEXT)), 0);
}

static void ui_refresh_campaign_summary(void)
{
    const campaign_node_t *node = &ui_nodes()[s_selected_node];
    const lv_font_t *summary_font = node->kind == CAMPAIGN_NODE_KIND_REWARD ?
        &shooter_font_cn_16 : &app_ui_font_cn_16;
    if (node->kind == CAMPAIGN_NODE_KIND_REWARD) {
        lv_label_set_text(s_ui.campaign_code, "特别关卡");
    } else {
        lv_label_set_text_fmt(s_ui.campaign_code, "第 %03u 关", node->id);
    }
    lv_label_set_text(s_ui.campaign_title, node->title);
    lv_obj_set_style_text_font(s_ui.campaign_title,
                               node->kind == CAMPAIGN_NODE_KIND_REWARD ?
                                   &shooter_font_cn_16 : &app_ui_font_cn_24, 0);
    const char *encounter = strstr(node->goal, "【遭遇事件】");
    const char *challenge = strstr(node->goal, "【晶体挑战】");
    const char *action = strstr(node->goal, "【行动】");
    size_t summary_length = 0;

    /* The map summary is intentionally shorter; the detail page keeps node->goal intact. */
    if (encounter != NULL) {
        const char *encounter_end = challenge != NULL && challenge > encounter ? challenge : action;
        if (encounter_end == NULL || encounter_end < encounter) {
            encounter_end = node->goal + strlen(node->goal);
        }
        summary_length = (size_t)(encounter_end - encounter);
        if (summary_length >= sizeof(s_campaign_summary)) {
            summary_length = sizeof(s_campaign_summary) - 1U;
        }
        memcpy(s_campaign_summary, encounter, summary_length);
    }

    if (action != NULL && summary_length < sizeof(s_campaign_summary) - 1U) {
        if (summary_length > 0U && s_campaign_summary[summary_length - 1U] != '\n') {
            s_campaign_summary[summary_length++] = '\n';
        }
        size_t action_length = strlen(action);
        const size_t remaining = sizeof(s_campaign_summary) - 1U - summary_length;
        if (action_length > remaining) {
            action_length = remaining;
        }
        memcpy(s_campaign_summary + summary_length, action, action_length);
        summary_length += action_length;
    }
    s_campaign_summary[summary_length] = '\0';
    lv_label_set_text(s_ui.campaign_goal, s_campaign_summary);
    lv_obj_set_style_text_font(s_ui.campaign_goal, summary_font, 0);
    lv_label_set_text(s_ui.campaign_gate_reward, node->gate_reward);
    lv_label_set_text(s_ui.campaign_ship_reward, node->ship_reward);
    lv_obj_set_style_text_font(s_ui.campaign_gate_reward, summary_font, 0);
    lv_obj_set_style_text_font(s_ui.campaign_ship_reward, summary_font, 0);
    if (node->gate_reward[0] != '\0') {
        lv_obj_remove_flag(s_ui.campaign_gate_reward, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.campaign_gate_reward, LV_OBJ_FLAG_HIDDEN);
    }
    if (node->ship_reward[0] != '\0') {
        lv_obj_set_y(s_ui.campaign_ship_reward, node->gate_reward[0] != '\0' ? 400 : 372);
        lv_obj_remove_flag(s_ui.campaign_ship_reward, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.campaign_ship_reward, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_refresh_campaign(void)
{
    const campaign_node_t *nodes = ui_nodes();
    s_map_offset_x = UI_MAP_WIDTH / 2 - nodes[s_selected_node].x;
    s_map_offset_y = UI_MAP_HEIGHT / 2 - nodes[s_selected_node].y;

    for (uint16_t index = 0; index < s_node_count; ++index) {
        const int32_t x = nodes[index].x + s_map_offset_x - UI_NODE_WIDTH / 2;
        const int32_t y = nodes[index].y + s_map_offset_y - UI_NODE_HEIGHT / 2;
        const bool visible = x + UI_NODE_WIDTH >= 0 && x < UI_MAP_WIDTH &&
                             y + UI_NODE_HEIGHT >= 0 && y < UI_MAP_HEIGHT;
        lv_obj_set_pos(s_ui.map_nodes[index], x, y);
        if (visible) {
            lv_obj_remove_flag(s_ui.map_nodes[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.map_nodes[index], LV_OBJ_FLAG_HIDDEN);
        }
        ui_style_map_node(index);
    }
    ui_refresh_campaign_summary();
    lv_obj_invalidate(s_ui.map);
}

static void ui_map_node_event_cb(lv_event_t *event)
{
    const uintptr_t value = (uintptr_t)lv_event_get_user_data(event);
    const uint16_t index = (uint16_t)(value - 1U);
    if (index >= s_node_count) {
        return;
    }
    s_selected_node = index;
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    ui_refresh_campaign();
    ui_load_page(UI_PAGE_DETAIL);
}

static void ui_campaign_detail_event_cb(lv_event_t *event)
{
    (void)event;
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    ui_load_page(UI_PAGE_DETAIL);
}

static void ui_create_campaign_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_CAMPAIGN] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "星际冒险", "关卡航线", true);

    s_ui.map = ui_create_panel(screen, UI_MAP_X, UI_MAP_Y, UI_MAP_WIDTH, UI_MAP_HEIGHT);
    ui_add_pixel_corners(s_ui.map, UI_COLOR_CYAN);
    lv_obj_add_event_cb(s_ui.map, ui_map_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_clear_flag(s_ui.map, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_label(s_ui.map, "星际地图", 18, 12, 180, UI_COLOR_CYAN);
    ui_create_label(s_ui.map, "初始门电路：输入、输出", 184, 12, 320, UI_COLOR_MUTED);

    const campaign_node_t *nodes = campaign_content_nodes(&s_node_count);
    if (s_node_count > UI_MAX_NODES) {
        s_node_count = UI_MAX_NODES;
    }
    for (uint16_t index = 0; index < s_node_count; ++index) {
        lv_obj_t *node = lv_button_create(s_ui.map);
        s_ui.map_nodes[index] = node;
        lv_obj_set_size(node, UI_NODE_WIDTH, UI_NODE_HEIGHT);
        lv_obj_set_style_pad_all(node, 0, 0);
        lv_obj_set_style_radius(node, 14, 0);
        lv_obj_clear_flag(node, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(node, ui_map_node_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(index + 1U));

        s_ui.map_node_ids[index] = ui_create_label(node, "", 10, 7, UI_NODE_WIDTH - 20, UI_COLOR_CYAN);
        ui_set_compact_font(s_ui.map_node_ids[index]);
        if (nodes[index].kind == CAMPAIGN_NODE_KIND_REWARD) {
            lv_label_set_text(s_ui.map_node_ids[index], "特别关卡");
            ui_add_pixel_corners(node, UI_COLOR_PINK);
        } else {
            lv_label_set_text_fmt(s_ui.map_node_ids[index], "第 %03u 关", nodes[index].id);
        }
        s_ui.map_node_titles[index] = ui_create_label(
            node, nodes[index].title, 10, 33, UI_NODE_WIDTH - 20, UI_COLOR_TEXT);
        if (nodes[index].kind == CAMPAIGN_NODE_KIND_REWARD) {
            lv_obj_set_style_text_font(s_ui.map_node_titles[index], &shooter_font_cn_16, 0);
        } else {
            ui_set_compact_font(s_ui.map_node_titles[index]);
        }
        lv_obj_set_style_text_align(s_ui.map_node_titles[index], LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_obj_t *side = ui_create_panel(screen, 758, UI_MAP_Y, 248, UI_MAP_HEIGHT);
    ui_add_pixel_corners(side, UI_COLOR_PINK);
    ui_create_label(side, "当前选择", 18, 18, 212, UI_COLOR_CYAN);
    s_ui.campaign_code = ui_create_label(side, "", 18, 45, 212, UI_COLOR_AMBER);
    s_ui.campaign_title = ui_create_label(side, "", 18, 76, 212, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s_ui.campaign_title, &app_ui_font_cn_24, 0);
    s_ui.campaign_goal = ui_create_label(side, "", 18, 122, 212, UI_COLOR_MUTED);
    lv_obj_set_height(s_ui.campaign_goal, 226);
    ui_create_accent(side, 18, 356, 212, UI_COLOR_CYAN_DIM);
    s_ui.campaign_gate_reward = ui_create_label(side, "", 18, 372, 212, UI_COLOR_CYAN);
    ui_set_compact_font(s_ui.campaign_gate_reward);
    lv_obj_set_height(s_ui.campaign_gate_reward, 24);
    s_ui.campaign_ship_reward = ui_create_label(side, "", 18, 400, 212, UI_COLOR_PINK);
    ui_set_compact_font(s_ui.campaign_ship_reward);
    lv_obj_set_height(s_ui.campaign_ship_reward, 24);

    lv_obj_t *open = lv_button_create(side);
    lv_obj_set_pos(open, 18, 442);
    lv_obj_set_size(open, 212, 42);
    ui_style_action_item(open, true, true);
    lv_obj_add_event_cb(open, ui_campaign_detail_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *open_label = lv_label_create(open);
    ui_set_font(open_label);
    lv_label_set_text(open_label, "查看任务  >");
    lv_obj_set_style_text_color(open_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(open_label);
}

static void ui_refresh_detail(void)
{
    const campaign_node_t *node = &ui_nodes()[s_selected_node];
    const bool completed = s_completed_nodes[s_selected_node];
    const bool unlocked = ui_node_is_unlocked(s_selected_node);
    if (node->kind == CAMPAIGN_NODE_KIND_REWARD) {
        lv_label_set_text(s_ui.detail_code, completed ? "特别关卡  已完成" :
                          (unlocked ? "特别关卡  可以进入" : "特别关卡  未解锁"));
    } else {
        lv_label_set_text_fmt(s_ui.detail_code, completed ? "第 %03u 关  已完成" :
                              (unlocked ? "第 %03u 关  可以进入" : "第 %03u 关  未解锁"),
                              node->id);
    }
    lv_label_set_text(s_ui.detail_title, node->title);
    lv_label_set_text(s_ui.detail_story, node->goal);
    lv_label_set_text(s_ui.detail_gate_reward, node->gate_reward);
    lv_label_set_text(s_ui.detail_ship_reward, node->ship_reward);
    const lv_font_t *detail_font = node->kind == CAMPAIGN_NODE_KIND_REWARD ?
        &shooter_font_cn_16 : &app_ui_font_cn_24;
    lv_obj_set_style_text_font(s_ui.detail_title,
                               node->kind == CAMPAIGN_NODE_KIND_REWARD ?
                                   &shooter_font_cn_16 : &app_ui_font_cn_24, 0);
    lv_obj_set_style_text_font(s_ui.detail_story, detail_font, 0);
    lv_obj_set_style_text_font(s_ui.detail_gate_reward, detail_font, 0);
    lv_obj_set_style_text_font(s_ui.detail_ship_reward, detail_font, 0);
    const uint32_t status_color = completed || unlocked ? UI_COLOR_GREEN : UI_COLOR_PINK;
    lv_obj_set_style_bg_color(s_ui.detail_availability,
                              lv_color_hex(completed || unlocked ? 0x0C211C : 0x241A35), 0);
    lv_obj_set_style_border_color(s_ui.detail_availability, lv_color_hex(status_color), 0);
    lv_obj_set_style_bg_color(s_ui.detail_availability_dot, lv_color_hex(status_color), 0);
    lv_label_set_text(s_ui.detail_availability_label,
                      completed ? "已完成" : (unlocked ? "可进入" : "未解锁"));
    lv_obj_set_style_text_color(s_ui.detail_availability_label, lv_color_hex(status_color), 0);
    if (node->gate_reward[0] != '\0') {
        lv_obj_remove_flag(s_ui.detail_gate_reward, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.detail_gate_reward, LV_OBJ_FLAG_HIDDEN);
    }
    if (node->ship_reward[0] != '\0') {
        lv_obj_set_y(s_ui.detail_ship_reward, node->gate_reward[0] != '\0' ? 252 : 216);
        lv_obj_remove_flag(s_ui.detail_ship_reward, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.detail_ship_reward, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.detail_start != NULL) {
        if (unlocked) {
            lv_obj_remove_flag(s_ui.detail_start, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.detail_start, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_ui.detail_start_label != NULL) {
            lv_label_set_text(s_ui.detail_start_label,
                              node->kind == CAMPAIGN_NODE_KIND_REWARD ?
                                  "开始飞行  >" : "开始组装  >");
        }
    }
    lv_obj_scroll_to_y(lv_obj_get_parent(s_ui.detail_story), 0, LV_ANIM_OFF);
}

static uint32_t ui_reward_stage_id(uint16_t node_id)
{
    switch (node_id) {
    case 150: return 1U;
    case 250: return 2U;
    case 350: return 3U;
    case 450: return 4U;
    case 550: return 5U;
    case 650: return 6U;
    default: return 0U;
    }
}

static void ui_shooter_completed(uint32_t stage_id, void *context)
{
    const uintptr_t encoded_index = (uintptr_t)context;
    if (stage_id < 1U || stage_id > 6U || encoded_index == 0U) return;
    const uint16_t index = (uint16_t)(encoded_index - 1U);
    if (index >= s_node_count) return;
    ui_record_node_completion(index);
    ui_refresh_campaign();
    ESP_LOGI(TAG, "special stage %u completed at campaign node %u",
             (unsigned)stage_id, (unsigned)ui_nodes()[index].id);
}

static void ui_shooter_exit_requested(void *context)
{
    (void)context;
    ui_exit_shooter_to_campaign();
}

static void ui_enter_shooter(uint32_t stage_id)
{
    lv_obj_t *screen = shooter_game_create(
        stage_id, ui_shooter_completed, ui_shooter_exit_requested,
        (void *)(uintptr_t)(s_selected_node + 1U));
    if (screen == NULL) {
        ESP_LOGE(TAG, "failed to create special stage %u", (unsigned)stage_id);
        return;
    }
    s_page = UI_PAGE_SHOOTER;
    lv_screen_load(screen);
}

static void ui_exit_shooter_to_campaign(void)
{
    lv_obj_t *old_screen = shooter_game_stop();
    ui_load_page(UI_PAGE_CAMPAIGN);
    if (old_screen != NULL) {
        lv_obj_delete_async(old_screen);
    }
}

static void ui_start_play(void)
{
    const campaign_node_t *node = &ui_nodes()[s_selected_node];
    if (!ui_node_is_unlocked(s_selected_node)) {
        audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
        ESP_LOGW(TAG, "campaign node %u is still locked", (unsigned)node->id);
        return;
    }
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    if (node->kind == CAMPAIGN_NODE_KIND_STANDARD && level_rule_get(node->id) != NULL) {
        ui_load_page(UI_PAGE_PLAY);
    } else if (node->kind == CAMPAIGN_NODE_KIND_REWARD) {
        const uint32_t stage_id = ui_reward_stage_id(node->id);
        if (stage_id != 0U) ui_enter_shooter(stage_id);
    }
}

static void ui_start_play_event_cb(lv_event_t *event)
{
    (void)event;
    ui_start_play();
}

static void ui_create_detail_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_DETAIL] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "星际冒险", "关卡详情", true);

    lv_obj_t *story_panel = ui_create_panel(screen, 18, 90, 660, 492);
    ui_add_pixel_corners(story_panel, UI_COLOR_CYAN);
    ui_create_accent(story_panel, 0, 0, 180, UI_COLOR_AMBER);
    s_ui.detail_code = ui_create_label(story_panel, "", 22, 18, 610, UI_COLOR_AMBER);
    s_ui.detail_title = ui_create_label(story_panel, "", 22, 50, 610, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s_ui.detail_title, &app_ui_font_cn_24, 0);
    ui_create_accent(story_panel, 22, 80, 610, UI_COLOR_BORDER);

    lv_obj_t *story_scroll = lv_obj_create(story_panel);
    lv_obj_set_pos(story_scroll, 18, 98);
    lv_obj_set_size(story_scroll, 624, 372);
    lv_obj_set_style_bg_color(story_scroll, lv_color_hex(0x071016), 0);
    lv_obj_set_style_bg_opa(story_scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(story_scroll, 0, 0);
    lv_obj_set_style_radius(story_scroll, 6, 0);
    lv_obj_set_style_pad_all(story_scroll, 18, 0);
    lv_obj_set_scroll_dir(story_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(story_scroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(story_scroll, lv_color_hex(UI_COLOR_CYAN_DIM), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(story_scroll, LV_OPA_COVER, LV_PART_SCROLLBAR);
    s_ui.detail_story = ui_create_label(story_scroll, "", 0, 0, 570, UI_COLOR_TEXT);
    lv_obj_set_height(s_ui.detail_story, LV_SIZE_CONTENT);

    lv_obj_t *telemetry = ui_create_panel(screen, 694, 90, 312, 492);
    ui_add_pixel_corners(telemetry, UI_COLOR_YELLOW);
    ui_create_accent(telemetry, 0, 0, 120, UI_COLOR_CYAN);
    ui_create_label(telemetry, "任务状态", 20, 20, 272, UI_COLOR_CYAN);
    s_ui.detail_availability = lv_obj_create(telemetry);
    lv_obj_set_pos(s_ui.detail_availability, 20, 51);
    lv_obj_set_size(s_ui.detail_availability, 122, 34);
    lv_obj_set_style_bg_color(s_ui.detail_availability, lv_color_hex(0x0C211C), 0);
    lv_obj_set_style_bg_opa(s_ui.detail_availability, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_ui.detail_availability, lv_color_hex(0x276A50), 0);
    lv_obj_set_style_border_width(s_ui.detail_availability, 1, 0);
    lv_obj_set_style_radius(s_ui.detail_availability, 17, 0);
    /* Align the status dot and text to the visible capsule bounds. */
    lv_obj_set_style_pad_all(s_ui.detail_availability, 0, 0);
    lv_obj_clear_flag(s_ui.detail_availability, LV_OBJ_FLAG_CLICKABLE);
    s_ui.detail_availability_dot = lv_obj_create(s_ui.detail_availability);
    lv_obj_set_pos(s_ui.detail_availability_dot, 13, 12);
    lv_obj_set_size(s_ui.detail_availability_dot, 9, 9);
    lv_obj_set_style_bg_color(s_ui.detail_availability_dot, lv_color_hex(UI_COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(s_ui.detail_availability_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.detail_availability_dot, 0, 0);
    lv_obj_set_style_radius(s_ui.detail_availability_dot, LV_RADIUS_CIRCLE, 0);
    s_ui.detail_availability_label = ui_create_label(
        s_ui.detail_availability, "可进入", 31, 6, 76, UI_COLOR_GREEN);
    ui_create_label(telemetry, "任务目标", 20, 116, 272, UI_COLOR_CYAN);
    ui_create_label(telemetry, "完成这个任务，继续前进", 20, 148, 272, UI_COLOR_MUTED);
    ui_create_accent(telemetry, 20, 188, 272, UI_COLOR_BORDER);
    s_ui.detail_gate_reward = ui_create_label(telemetry, "", 20, 216, 272, UI_COLOR_CYAN);
    s_ui.detail_ship_reward = ui_create_label(telemetry, "", 20, 252, 272, UI_COLOR_PINK);

    s_ui.detail_start = lv_button_create(telemetry);
    lv_obj_set_pos(s_ui.detail_start, 20, 350);
    lv_obj_set_size(s_ui.detail_start, 272, 44);
    ui_style_action_item(s_ui.detail_start, true, true);
    lv_obj_add_event_cb(s_ui.detail_start, ui_start_play_event_cb, LV_EVENT_CLICKED, NULL);
    s_ui.detail_start_label = lv_label_create(s_ui.detail_start);
    ui_set_font(s_ui.detail_start_label);
    lv_label_set_text(s_ui.detail_start_label, "开始组装  >");
    lv_obj_set_style_text_color(s_ui.detail_start_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(s_ui.detail_start_label);

    ui_create_accent(telemetry, 20, 405, 272, UI_COLOR_AMBER);
    ui_create_label(telemetry, "图灵号逻辑实验室", 20, 427, 272, UI_COLOR_AMBER);
}

static const char *ui_gate_name(ssd1315_gate_t gate)
{
    static const char *names[] = {
        "输入", "输出", "非门", "与门", "或门", "与非", "或非", "异或", "同或", "未知",
    };
    return gate <= SSD1315_GATE_NULL ? names[gate] : names[SSD1315_GATE_NULL];
}

static void ui_draw_text(lv_layer_t *layer,
                         const char *text,
                         const lv_font_t *font,
                         uint32_t color,
                         const lv_area_t *area,
                         lv_text_align_t align)
{
    lv_draw_label_dsc_t draw;
    lv_draw_label_dsc_init(&draw);
    draw.text = text;
    draw.text_static = 1;
    draw.font = font;
    draw.color = lv_color_hex(color);
    draw.opa = LV_OPA_COVER;
    draw.align = align;
    draw.text_size.x = lv_area_get_width(area);
    draw.text_size.y = lv_area_get_height(area);
    lv_draw_label(layer, &draw, area);
}

static void ui_draw_line_segment(lv_layer_t *layer,
                                 int32_t x1,
                                 int32_t y1,
                                 int32_t x2,
                                 int32_t y2,
                                 uint32_t color,
                                 int32_t width,
                                 bool dashed)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.p1 = (lv_point_precise_t) {.x = x1, .y = y1};
    line.p2 = (lv_point_precise_t) {.x = x2, .y = y2};
    line.color = lv_color_hex(color);
    line.width = width;
    line.opa = LV_OPA_COVER;
    line.round_start = 1;
    line.round_end = 1;
    if (dashed) {
        line.dash_width = 7;
        line.dash_gap = 5;
    }
    lv_draw_line(layer, &line);
}

static void ui_draw_routed_link(lv_layer_t *layer,
                                const lv_area_t *parent,
                                const circuit_layout_route_t *route,
                                uint32_t color,
                                bool invalid)
{
    const uint32_t underlay = 0x071016;
    const int32_t under_width = 7;
    const int32_t color_width = invalid ? 4 : 3;
    for (uint8_t index = 0; index + 1U < route->point_count; ++index) {
        ui_draw_line_segment(layer,
                             parent->x1 + route->x[index], parent->y1 + route->y[index],
                             parent->x1 + route->x[index + 1U],
                             parent->y1 + route->y[index + 1U],
                             underlay, under_width, false);
    }
    for (uint8_t index = 0; index + 1U < route->point_count; ++index) {
        ui_draw_line_segment(layer,
                             parent->x1 + route->x[index], parent->y1 + route->y[index],
                             parent->x1 + route->x[index + 1U],
                             parent->y1 + route->y[index + 1U],
                             color, color_width, invalid);
    }
}

static void ui_draw_gate_symbol(lv_layer_t *layer,
                                const circuit_layout_node_t *node,
                                const lv_area_t *parent,
                                ssd1315_gate_t gate)
{
    const int32_t cx = parent->x1 + node->x + node->width / 2;
    const int32_t cy = parent->y1 + node->y + node->height / 2 + 8;
    const uint32_t color = gate == SSD1315_GATE_NULL ? UI_COLOR_DANGER : UI_COLOR_YELLOW;
    if (gate == SSD1315_GATE_INPUT) {
        ui_draw_line_segment(layer, cx - 15, cy, cx + 14, cy, color, 3, false);
        ui_draw_line_segment(layer, cx + 14, cy, cx + 6, cy - 7, color, 3, false);
        ui_draw_line_segment(layer, cx + 14, cy, cx + 6, cy + 7, color, 3, false);
    } else if (gate == SSD1315_GATE_OUTPUT) {
        lv_draw_rect_dsc_t lamp;
        lv_draw_rect_dsc_init(&lamp);
        lamp.bg_opa = LV_OPA_TRANSP;
        lamp.border_color = lv_color_hex(color);
        lamp.border_width = 3;
        lamp.radius = LV_RADIUS_CIRCLE;
        const lv_area_t lamp_area = {cx - 11, cy - 11, cx + 11, cy + 11};
        lv_draw_rect(layer, &lamp, &lamp_area);
    } else if (gate == SSD1315_GATE_NOT) {
        ui_draw_line_segment(layer, cx - 14, cy - 11, cx - 14, cy + 11, color, 3, false);
        ui_draw_line_segment(layer, cx - 14, cy - 11, cx + 8, cy, color, 3, false);
        ui_draw_line_segment(layer, cx + 8, cy, cx - 14, cy + 11, color, 3, false);
        lv_draw_rect_dsc_t bubble;
        lv_draw_rect_dsc_init(&bubble);
        bubble.bg_color = lv_color_hex(UI_COLOR_SURFACE_ALT);
        bubble.bg_opa = LV_OPA_COVER;
        bubble.border_color = lv_color_hex(color);
        bubble.border_width = 2;
        bubble.radius = LV_RADIUS_CIRCLE;
        const lv_area_t bubble_area = {cx + 8, cy - 4, cx + 16, cy + 4};
        lv_draw_rect(layer, &bubble, &bubble_area);
    } else {
        ui_draw_line_segment(layer, cx - 15, cy, cx, cy - 11, color, 2, false);
        ui_draw_line_segment(layer, cx, cy - 11, cx + 15, cy, color, 2, false);
        ui_draw_line_segment(layer, cx + 15, cy, cx, cy + 11, color, 2, false);
        ui_draw_line_segment(layer, cx, cy + 11, cx - 15, cy, color, 2, false);
        if (gate == SSD1315_GATE_NAND || gate == SSD1315_GATE_NOR || gate == SSD1315_GATE_XNOR) {
            lv_draw_rect_dsc_t bubble;
            lv_draw_rect_dsc_init(&bubble);
            bubble.bg_color = lv_color_hex(UI_COLOR_SURFACE_ALT);
            bubble.bg_opa = LV_OPA_COVER;
            bubble.border_color = lv_color_hex(color);
            bubble.border_width = 2;
            bubble.radius = LV_RADIUS_CIRCLE;
            const lv_area_t bubble_area = {cx + 13, cy - 4, cx + 21, cy + 4};
            lv_draw_rect(layer, &bubble, &bubble_area);
        }
        if (gate == SSD1315_GATE_XOR || gate == SSD1315_GATE_XNOR) {
            ui_draw_line_segment(layer, cx - 19, cy - 8, cx - 19, cy + 8, color, 2, false);
        }
    }
}

static void ui_circuit_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *circuit = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(circuit, &area);

    lv_draw_line_dsc_t grid;
    lv_draw_line_dsc_init(&grid);
    grid.color = lv_color_hex(UI_COLOR_GRID);
    grid.width = 1;
    grid.opa = LV_OPA_40;
    for (int32_t x = area.x1 + 28; x < area.x2; x += 56) {
        grid.p1 = (lv_point_precise_t) {.x = x, .y = area.y1};
        grid.p2 = (lv_point_precise_t) {.x = x, .y = area.y2};
        lv_draw_line(layer, &grid);
    }
    for (int32_t y = area.y1 + 28; y < area.y2; y += 56) {
        grid.p1 = (lv_point_precise_t) {.x = area.x1, .y = y};
        grid.p2 = (lv_point_precise_t) {.x = area.x2, .y = y};
        lv_draw_line(layer, &grid);
    }

    for (uint8_t index = 0; index < s_play_snapshot.link_count; ++index) {
        const board_link_t *link = &s_play_snapshot.links[index];
        if (board_link_is_ignored(link)) continue;
        const uint8_t first_slot = board_mapping_slot_for_port(link->first_port);
        const uint8_t second_slot = board_mapping_slot_for_port(link->second_port);
        const int8_t first_node_index = s_play_layout.node_for_slot[first_slot];
        const int8_t second_node_index = s_play_layout.node_for_slot[second_slot];
        if (first_node_index < 0 || second_node_index < 0) continue;
        const uint8_t first_local = board_mapping_local_port(link->first_port);
        const uint8_t second_local = board_mapping_local_port(link->second_port);
        const board_connection_color_t rgb = board_snapshot_get_color(
            link->valid ? link->color_index : BOARD_SNAPSHOT_INVALID_COLOR);
        const uint32_t color = ((uint32_t)rgb.red << 16) | ((uint32_t)rgb.green << 8) | rgb.blue;
        circuit_layout_route_t route;
        if (circuit_layout_route(&s_play_layout, 660, 492,
                                 (uint8_t)first_node_index, first_local,
                                 (uint8_t)second_node_index, second_local,
                                 index, &route)) {
            ui_draw_routed_link(layer, &area, &route, color, !link->valid);
        }
    }

    for (uint8_t index = 0; index < s_play_layout.node_count; ++index) {
        const circuit_layout_node_t *node = &s_play_layout.nodes[index];
        const board_slot_identity_t *slot = &s_play_snapshot.slots[node->slot];
        const uint32_t border = slot->id_valid ? UI_COLOR_CYAN : UI_COLOR_DANGER;
        lv_draw_rect_dsc_t body;
        lv_draw_rect_dsc_init(&body);
        body.bg_color = lv_color_hex(UI_COLOR_SURFACE_ALT);
        body.bg_opa = LV_OPA_COVER;
        body.border_color = lv_color_hex(border);
        body.border_width = 2;
        body.radius = 5;
        const lv_area_t body_area = {
            area.x1 + node->x, area.y1 + node->y,
            area.x1 + node->x + node->width, area.y1 + node->y + node->height,
        };
        lv_draw_rect(layer, &body, &body_area);

        const lv_area_t name_area = {
            body_area.x1 + 8, body_area.y1 + 5, body_area.x2 - 8, body_area.y1 + 25,
        };
        ui_draw_text(layer, ui_gate_name(slot->id_valid ? slot->gate : SSD1315_GATE_NULL),
                     &app_ui_font_cn_16, UI_COLOR_TEXT, &name_area, LV_TEXT_ALIGN_CENTER);
        if (node->height >= 52) {
            ui_draw_gate_symbol(layer, node, &area, slot->id_valid ? slot->gate : SSD1315_GATE_NULL);
        }

        for (uint8_t local = 0; local < BOARD_SNAPSHOT_PORTS_PER_SLOT; ++local) {
            const uint8_t port = node->slot * BOARD_SNAPSHOT_PORTS_PER_SLOT + local;
            const board_port_role_t role = s_play_snapshot.port_roles[port];
            if (role == BOARD_PORT_UNUSED) continue;
            const uint32_t port_color = role == BOARD_PORT_INPUT ? UI_COLOR_PINK :
                                        UI_COLOR_GREEN;
            lv_draw_rect_dsc_t dot;
            lv_draw_rect_dsc_init(&dot);
            dot.bg_color = lv_color_hex(port_color);
            dot.bg_opa = role == BOARD_PORT_UNUSED ? LV_OPA_50 : LV_OPA_COVER;
            dot.border_color = lv_color_hex(0x071016);
            dot.border_width = 1;
            dot.radius = LV_RADIUS_CIRCLE;
            const int32_t px = area.x1 + node->port_x[local];
            const int32_t py = area.y1 + node->port_y[local];
            const lv_area_t dot_area = {px - 5, py - 5, px + 5, py + 5};
            lv_draw_rect(layer, &dot, &dot_area);
        }
    }

    if (s_play_layout.node_count == 0U) {
        const lv_area_t empty = {area.x1 + 100, area.y1 + 205, area.x2 - 100, area.y1 + 250};
        ui_draw_text(layer, "把积木放到底板上吧", &app_ui_font_cn_24,
                     UI_COLOR_MUTED, &empty, LV_TEXT_ALIGN_CENTER);
    }
}

static void ui_draw_truth_bit(lv_layer_t *layer,
                              int32_t center_x,
                              int32_t center_y,
                              bool value,
                              bool available)
{
    lv_draw_rect_dsc_t bit;
    lv_draw_rect_dsc_init(&bit);
    bit.bg_color = lv_color_hex(value ? UI_COLOR_GREEN : UI_COLOR_RED);
    bit.bg_opa = available ? LV_OPA_COVER : LV_OPA_10;
    bit.border_color = lv_color_hex(available ? (value ? UI_COLOR_GREEN : UI_COLOR_RED) :
                                    UI_COLOR_MUTED);
    bit.border_width = 1;
    bit.radius = LV_RADIUS_CIRCLE;
    const int32_t radius = 4;
    const lv_area_t area = {
        center_x - radius, center_y - radius, center_x + radius, center_y + radius,
    };
    lv_draw_rect(layer, &bit, &area);
}

static void ui_draw_truth_bits(lv_layer_t *layer,
                               int32_t base_x,
                               int32_t center_y,
                               uint8_t value,
                               uint8_t count,
                               bool available)
{
    for (uint8_t bit = 0; bit < count; ++bit) {
        ui_draw_truth_bit(layer, base_x + bit * 11, center_y,
                          (value & (1U << bit)) != 0U, available);
    }
}

static void ui_truth_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *truth = lv_event_get_current_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(truth, &area);

    const campaign_node_t *node = &ui_nodes()[s_selected_node];
    const level_rule_t *rule = level_rule_get(node->id);
    game_judge_state_t judge;
    if (rule == NULL || !game_judge_get_state(&judge)) return;

    const int32_t width = lv_area_get_width(&area);
    const int32_t height = lv_area_get_height(&area);
    const int32_t header_height = 22;
    const uint8_t row_count = rule->case_count > 0U ? rule->case_count : 1U;
    const int32_t row_height = LV_MAX(8, (height - header_height) / row_count);

    const lv_area_t header_rule = {area.x1, area.y1 + header_height - 1,
                                   area.x2, area.y1 + header_height};
    lv_draw_rect_dsc_t separator;
    lv_draw_rect_dsc_init(&separator);
    separator.bg_color = lv_color_hex(UI_COLOR_BORDER);
    separator.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &separator, &header_rule);

    const lv_area_t input_header = {area.x1 + 2, area.y1, area.x1 + 80, area.y1 + 20};
    const lv_area_t expected_header = {area.x1 + 88, area.y1, area.x1 + 166, area.y1 + 20};
    const lv_area_t actual_header = {area.x1 + 174, area.y1, area.x1 + width - 4, area.y1 + 20};
    ui_draw_text(layer, "输入", &app_ui_font_cn_16, UI_COLOR_CYAN,
                 &input_header, LV_TEXT_ALIGN_LEFT);
    ui_draw_text(layer, "目标", &app_ui_font_cn_16, UI_COLOR_CYAN,
                 &expected_header, LV_TEXT_ALIGN_LEFT);
    ui_draw_text(layer, "实际", &app_ui_font_cn_16, UI_COLOR_CYAN,
                 &actual_header, LV_TEXT_ALIGN_LEFT);

    for (uint8_t row = 0; row < rule->case_count; ++row) {
        const int32_t row_top = area.y1 + header_height + row * row_height;
        const int32_t row_center = row_top + row_height / 2;
        const bool active = judge.active_row_valid && judge.active_row == row &&
                            judge.phase == GAME_JUDGE_RUNNING;
        if (active) {
            lv_draw_rect_dsc_t highlight;
            lv_draw_rect_dsc_init(&highlight);
            highlight.bg_color = lv_color_hex(UI_COLOR_CYAN_DIM);
            highlight.bg_opa = LV_OPA_60;
            highlight.border_color = lv_color_hex(UI_COLOR_CYAN);
            highlight.border_width = 1;
            highlight.radius = 2;
            const lv_area_t highlight_area = {
                area.x1, row_top, area.x2, row_top + row_height - 1,
            };
            lv_draw_rect(layer, &highlight, &highlight_area);
        }

        ui_draw_truth_bits(layer, area.x1 + 12, row_center, row,
                           rule->input_count, true);
        ui_draw_truth_bits(layer, area.x1 + 98, row_center,
                           rule->expected_outputs[row], rule->output_count, true);
        ui_draw_truth_bits(layer, area.x1 + 184, row_center,
                           judge.rows[row].actual, rule->output_count,
                           judge.rows[row].complete);

        if (judge.rows[row].complete) {
            lv_draw_rect_dsc_t result_mark;
            lv_draw_rect_dsc_init(&result_mark);
            result_mark.bg_color = lv_color_hex(judge.rows[row].passed ?
                                                 UI_COLOR_GREEN : UI_COLOR_DANGER);
            result_mark.bg_opa = LV_OPA_COVER;
            result_mark.radius = LV_RADIUS_CIRCLE;
            const lv_area_t mark = {area.x2 - 10, row_center - 2,
                                    area.x2 - 6, row_center + 2};
            lv_draw_rect(layer, &result_mark, &mark);
        }
    }
}

static void ui_start_judge(void)
{
    const level_rule_t *rule = level_rule_get(ui_nodes()[s_selected_node].id);
    board_snapshot_t snapshot;
    game_judge_state_t judge;
    if (rule != NULL && board_snapshot_get(&snapshot) && game_judge_get_state(&judge) &&
        judge.phase != GAME_JUDGE_RUNNING) {
        (void)game_judge_start(rule, &snapshot);
        audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        ui_refresh_play();
    } else {
        audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
    }
}

static void ui_start_judge_event_cb(lv_event_t *event)
{
    (void)event;
    ui_start_judge();
}

static void ui_toggle_debug(void)
{
    circuit_debug_set_enabled(!circuit_debug_is_enabled());
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    ui_refresh_play();
}

static void ui_toggle_debug_event_cb(lv_event_t *event)
{
    (void)event;
    ui_toggle_debug();
}

static void ui_record_node_completion(uint16_t index)
{
    if (index >= s_node_count || s_completed_nodes[index]) return;
    s_completed_nodes[index] = true;
    const esp_err_t err = campaign_progress_mark_completed(ui_nodes()[index].id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "level %u completed in RAM but persistent save failed: %s",
                 (unsigned)ui_nodes()[index].id, esp_err_to_name(err));
    }
}

static void ui_log_play_topology(const board_snapshot_t *snapshot,
                                 const circuit_layout_t *layout)
{
    ESP_LOGI(TAG, "play topology revision=%lu nodes=%u links=%u invalid=%u",
             (unsigned long)snapshot->topology_revision, layout->node_count,
             snapshot->link_count, snapshot->invalid_link_count);
    for (uint8_t index = 0; index < snapshot->link_count; ++index) {
        const board_link_t *link = &snapshot->links[index];
        const uint8_t first_slot = board_mapping_slot_for_port(link->first_port);
        const uint8_t second_slot = board_mapping_slot_for_port(link->second_port);
        const uint8_t first_local = board_mapping_local_port(link->first_port);
        const uint8_t second_local = board_mapping_local_port(link->second_port);
        const board_port_side_t first_side = board_mapping_port_side(first_slot, first_local);
        const board_port_side_t second_side = board_mapping_port_side(second_slot, second_local);
        ESP_LOGI(TAG,
                 "play link P%u(slot%u %s present=%u id=%u role=%u node=%d) "
                 "<-> P%u(slot%u %s present=%u id=%u role=%u node=%d) valid=%u error=%u",
                 link->first_port + 1U, first_slot + 1U,
                 board_mapping_side_name(first_side),
                 snapshot->slots[first_slot].present, snapshot->slots[first_slot].id_valid,
                 snapshot->port_roles[link->first_port], layout->node_for_slot[first_slot],
                 link->second_port + 1U, second_slot + 1U,
                 board_mapping_side_name(second_side),
                 snapshot->slots[second_slot].present, snapshot->slots[second_slot].id_valid,
                 snapshot->port_roles[link->second_port], layout->node_for_slot[second_slot],
                 link->valid, link->error);
    }
}

static void ui_request_gameplay_prompt(voice_gameplay_prompt_t prompt)
{
    const int64_t now_us = esp_timer_get_time();
    if (now_us - s_play_last_prompt_us < UI_GAMEPLAY_PROMPT_COOLDOWN_US) return;
    if (voice_assistant_request_gameplay_prompt(prompt) == ESP_OK) {
        s_play_last_prompt_us = now_us;
    }
}

static void ui_refresh_play(void)
{
    const campaign_node_t *node = &ui_nodes()[s_selected_node];
    const level_rule_t *rule = level_rule_get(node->id);
    board_snapshot_t latest;
    game_judge_state_t judge;
    circuit_debug_state_t debug;
    voice_assistant_status_t voice;
    if (rule == NULL || !board_snapshot_get(&latest) || !game_judge_get_state(&judge) ||
        !circuit_debug_get_state(&debug)) return;
    voice_assistant_get_status(&voice);

    const bool first_snapshot = s_play_snapshot_generation == UINT32_MAX;
    const bool snapshot_changed = latest.generation != s_play_snapshot_generation;
    const bool topology_changed = first_snapshot || latest.topology_revision != s_play_topology_revision;
    const bool judge_changed = judge.version != s_play_judge_version;
    const bool debug_changed = debug.generation != s_play_debug_generation;
    const bool voice_changed = voice.generation != s_play_voice_generation;
    if (!snapshot_changed && !judge_changed && !debug_changed && !voice_changed) return;

    if (snapshot_changed) {
        s_play_snapshot_generation = latest.generation;
    }
    if (topology_changed) {
        s_play_snapshot = latest;
        circuit_layout_compute(&s_play_snapshot, 660, 492, &s_play_layout);
        ui_log_play_topology(&s_play_snapshot, &s_play_layout);
        if (s_ui.play_circuit != NULL) lv_obj_invalidate(s_ui.play_circuit);
    }
    const bool invalid_links = latest.invalid_link_count > 0U || latest.link_overflow;
    if (topology_changed) {
        if (!first_snapshot && invalid_links && !s_play_invalid_link_prompt_active) {
            ui_request_gameplay_prompt(VOICE_GAMEPLAY_PROMPT_INVALID_LINK);
        }
        s_play_invalid_link_prompt_active = invalid_links;
    }
    if (s_play_topology_revision != 0U &&
        latest.topology_revision != s_play_topology_revision &&
        judge.phase != GAME_JUDGE_IDLE && judge.phase != GAME_JUDGE_RUNNING) {
        game_judge_reset();
        game_judge_get_state(&judge);
    }
    if (topology_changed) s_play_topology_revision = latest.topology_revision;
    if (!judge_changed && !debug_changed && !voice_changed && !first_snapshot &&
        judge.version == s_play_judge_version) return;
    s_play_judge_version = judge.version;
    s_play_debug_generation = debug.generation;
    s_play_voice_generation = voice.generation;

    lv_label_set_text_fmt(s_ui.play_code, "第 %03u 关", node->id);
    lv_label_set_text(s_ui.play_title, node->title);
    snprintf(s_play_goal_text, sizeof(s_play_goal_text), "%s\n输入：%s    输出：%s",
             rule->short_goal, rule->input_names, rule->output_names);
    lv_label_set_text(s_ui.play_goal, s_play_goal_text);
    if (s_ui.play_truth != NULL) lv_obj_invalidate(s_ui.play_truth);
    const bool judging = judge.phase == GAME_JUDGE_RUNNING;
    const bool misplaced_inputs = debug.enabled && debug.misplaced_input_mask != 0U;
    const char *status_text = s_programmer_owns_input ? "摇杆和按键正在修改本地积木" :
                              voice.visible ? voice.text :
                              judging ? judge.message :
                              misplaced_inputs ? "输入积木请放到最左侧一列" :
                              debug.enabled ? "用四个开关，让信号跑起来！" : judge.message;
    lv_label_set_text(s_ui.play_status, status_text);
    lv_label_set_text(s_ui.play_action_label,
                      judging ? "正在检查..." : "启动飞船检查");
    lv_label_set_text(s_ui.play_debug_action_label,
                      debug.enabled ? "关闭开关试玩" : "开启开关试玩");
    ui_style_action_item(s_ui.play_action, s_play_action_selection == 0U, true);
    ui_style_action_item(s_ui.play_debug_action, s_play_action_selection == 1U, true);
    if (judging) {
        lv_obj_add_state(s_ui.play_action, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_ui.play_action, LV_STATE_DISABLED);
    }
    lv_obj_set_style_text_color(s_ui.play_status,
                                lv_color_hex(s_programmer_owns_input ? UI_COLOR_CYAN :
                                             (voice.visible ?
                                              (voice.error ? UI_COLOR_DANGER : UI_COLOR_CYAN) :
                                             (misplaced_inputs ? UI_COLOR_DANGER :
                                             (debug.enabled && !judging ? UI_COLOR_GREEN :
                                             (judge.phase == GAME_JUDGE_PASSED ? UI_COLOR_GREEN :
                                             ((judge.phase == GAME_JUDGE_FAILED ||
                                               judge.phase == GAME_JUDGE_PRECHECK_ERROR ||
                                               judge.phase == GAME_JUDGE_CANCELLED) ?
                                              UI_COLOR_DANGER : UI_COLOR_MUTED)))))), 0);

    const bool pass_just_completed = judge_changed &&
                                     judge.phase == GAME_JUDGE_PASSED &&
                                     judge.level_id == node->id;
    if (judge_changed && judge.level_id == node->id) {
        if (judge.phase == GAME_JUDGE_PASSED) {
            audio_self_test_play_effect(AUDIO_EFFECT_WIN);
            ui_request_gameplay_prompt(VOICE_GAMEPLAY_PROMPT_CHECK_PASSED);
        } else if (judge.phase == GAME_JUDGE_FAILED ||
                   judge.phase == GAME_JUDGE_PRECHECK_ERROR) {
            audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
            if (judge.phase == GAME_JUDGE_FAILED) {
                ui_request_gameplay_prompt(VOICE_GAMEPLAY_PROMPT_CHECK_FAILED);
            } else if (judge.precheck_code == CIRCUIT_CHECK_WRONG_INPUT_COUNT ||
                       judge.precheck_code == CIRCUIT_CHECK_WRONG_OUTPUT_COUNT) {
                ui_request_gameplay_prompt(VOICE_GAMEPLAY_PROMPT_WRONG_BLOCK_COUNT);
            } else if (judge.precheck_code == CIRCUIT_CHECK_INCOMPLETE) {
                ui_request_gameplay_prompt(VOICE_GAMEPLAY_PROMPT_INCOMPLETE_CIRCUIT);
            }
        } else if (judge.phase == GAME_JUDGE_CANCELLED && s_page == UI_PAGE_PLAY) {
            ui_request_gameplay_prompt(VOICE_GAMEPLAY_PROMPT_CHECK_CANCELLED);
        }
    }
    if (pass_just_completed && s_page == UI_PAGE_PLAY) {
        ui_record_node_completion(s_selected_node);
        s_initialize_error = false;
        ui_refresh_campaign();
        ui_refresh_success();
        ui_load_page(UI_PAGE_SUCCESS);
    }
}

static void ui_log_play_health(void)
{
    const int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_play_health_log_us < 30000000LL) return;
    s_last_play_health_log_us = now_us;
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    const lv_result_t integrity = lv_mem_test();
    ESP_LOGI(TAG, "play health: lvgl=%u%% free=%u largest=%u frag=%u%% integrity=%s ui_stack=%u",
             memory.used_pct, (unsigned)memory.free_size, (unsigned)memory.free_biggest_size,
             memory.frag_pct, integrity == LV_RESULT_OK ? "ok" : "failed",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void ui_create_play_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_PLAY] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "星际冒险", "关卡游玩", true);

    s_ui.play_circuit = ui_create_panel(screen, 18, 90, 660, 492);
    ui_add_pixel_corners(s_ui.play_circuit, UI_COLOR_CYAN);
    lv_obj_add_event_cb(s_ui.play_circuit, ui_circuit_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    ui_create_label(s_ui.play_circuit, "实时电路", 18, 12, 180, UI_COLOR_CYAN);

    lv_obj_t *side = ui_create_panel(screen, 694, 90, 312, 492);
    ui_add_pixel_corners(side, UI_COLOR_YELLOW);
    ui_create_accent(side, 0, 0, 124, UI_COLOR_CYAN);
    s_ui.play_code = ui_create_label(side, "", 20, 16, 272, UI_COLOR_AMBER);
    ui_set_compact_font(s_ui.play_code);
    s_ui.play_title = ui_create_label(side, "", 20, 43, 272, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s_ui.play_title, &app_ui_font_cn_24, 0);
    s_ui.play_goal = ui_create_label(side, "", 20, 78, 272, UI_COLOR_MUTED);
    lv_obj_set_height(s_ui.play_goal, 64);
    ui_create_accent(side, 20, 145, 272, UI_COLOR_BORDER);
    s_ui.play_truth = lv_obj_create(side);
    lv_obj_set_pos(s_ui.play_truth, 20, 158);
    lv_obj_set_size(s_ui.play_truth, 272, 196);
    lv_obj_set_style_bg_opa(s_ui.play_truth, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.play_truth, 0, 0);
    lv_obj_set_style_radius(s_ui.play_truth, 0, 0);
    lv_obj_set_style_pad_all(s_ui.play_truth, 0, 0);
    lv_obj_clear_flag(s_ui.play_truth, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.play_truth, ui_truth_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    s_ui.play_status = ui_create_label(side, "", 20, 358, 272, UI_COLOR_MUTED);
    lv_obj_set_height(s_ui.play_status, 50);

    s_ui.play_action = lv_button_create(side);
    lv_obj_set_pos(s_ui.play_action, 20, 414);
    lv_obj_set_size(s_ui.play_action, 272, 36);
    ui_style_action_item(s_ui.play_action, true, true);
    lv_obj_add_event_cb(s_ui.play_action, ui_start_judge_event_cb, LV_EVENT_CLICKED, NULL);
    s_ui.play_action_label = lv_label_create(s_ui.play_action);
    ui_set_font(s_ui.play_action_label);
    lv_label_set_text(s_ui.play_action_label, "启动飞船检查");
    lv_obj_set_style_text_color(s_ui.play_action_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(s_ui.play_action_label);

    s_ui.play_debug_action = lv_button_create(side);
    lv_obj_set_pos(s_ui.play_debug_action, 20, 454);
    lv_obj_set_size(s_ui.play_debug_action, 272, 36);
    ui_style_action_item(s_ui.play_debug_action, false, true);
    lv_obj_add_event_cb(s_ui.play_debug_action, ui_toggle_debug_event_cb, LV_EVENT_CLICKED, NULL);
    s_ui.play_debug_action_label = lv_label_create(s_ui.play_debug_action);
    ui_set_font(s_ui.play_debug_action_label);
    lv_label_set_text(s_ui.play_debug_action_label, "开启开关试玩");
    lv_obj_set_style_text_color(s_ui.play_debug_action_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(s_ui.play_debug_action_label);
}

static void ui_refresh_success(void)
{
    if (s_ui.success_code == NULL) return;
    const campaign_node_t *node = &ui_nodes()[s_selected_node];
    lv_label_set_text_fmt(s_ui.success_code, "第 %03u 关", node->id);
    lv_label_set_text(s_ui.success_title, node->title);
    lv_label_set_text(s_ui.success_status, "飞船检查通过！\n\n任务已经完成\n返回后还可以继续尝试其他电路");
}

static void ui_create_success_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_SUCCESS] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "星际冒险", "关卡游玩", true);

    lv_obj_t *panel = ui_create_panel(screen, 132, 108, 760, 430);
    ui_add_pixel_corners(panel, UI_COLOR_GREEN);
    ui_create_accent(panel, 0, 0, 220, UI_COLOR_YELLOW);
    ui_create_signal_icon(panel, 72, 118, UI_COLOR_GREEN, 1);
    s_ui.success_code = ui_create_label(panel, "", 210, 68, 500, UI_COLOR_AMBER);
    s_ui.success_title = ui_create_label(panel, "", 210, 106, 500, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s_ui.success_title, &app_ui_font_cn_24, 0);
    s_ui.success_status = ui_create_label(panel, "", 210, 158, 500, UI_COLOR_GREEN);
    lv_obj_set_height(s_ui.success_status, 160);
    ui_create_accent(panel, 210, 330, 470, UI_COLOR_CYAN_DIM);
    ui_create_label(panel, "按左键或触摸返回，继续组装", 210, 352, 470, UI_COLOR_MUTED);
}

static void ui_create_settings_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_SETTINGS] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "小飞船设置", "设置", true);

    lv_obj_t *side = ui_create_panel(screen, 18, 90, 244, 492);
    ui_add_pixel_corners(side, UI_COLOR_CYAN);
    ui_create_accent(side, 0, 0, 112, UI_COLOR_CYAN);
    lv_obj_t *side_title = ui_create_label(side, "图灵号", 22, 24, 200, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(side_title, &app_ui_font_cn_24, 0);
    ui_create_label(side, "小飞船状态", 22, 58, 200, UI_COLOR_CYAN);
    ui_create_signal_icon(side, 78, 94, UI_COLOR_GREEN, 1);
    ui_create_accent(side, 22, 205, 200, UI_COLOR_BORDER);
    ui_create_label(side,
        "飞船状态\n正常\n\n连接接口\n64 个\n\n屏幕\n已连接\n\n控制器\n已连接",
        22, 226, 200, UI_COLOR_MUTED);

    ui_create_label(screen, "选择功能", 282, 98, 500, UI_COLOR_CYAN);
    s_ui.settings_items[UI_SETTINGS_VOLUME] = ui_create_settings_item(
        screen, 282, 122, 350, 126, "声音输出", "音量调整",
        ui_settings_item_event_cb, (void *)(uintptr_t)(UI_SETTINGS_VOLUME + 1U));
    lv_obj_t *volume_status = ui_create_label(
        s_ui.settings_items[UI_SETTINGS_VOLUME], "调节本次开机音量", 22, 90, 306, UI_COLOR_MUTED);
    lv_obj_set_height(volume_status, 32);
    s_ui.settings_items[UI_SETTINGS_WIFI] = ui_create_settings_item(
        screen, 656, 122, 350, 126, "网络连接", "Wi-Fi 选择",
        ui_settings_item_event_cb, (void *)(uintptr_t)(UI_SETTINGS_WIFI + 1U));
    lv_obj_t *wifi_status = ui_create_label(
        s_ui.settings_items[UI_SETTINGS_WIFI], "预设热点与连接状态", 22, 90, 306, UI_COLOR_MUTED);
    lv_obj_set_height(wifi_status, 32);
    s_ui.settings_items[UI_SETTINGS_PROGRESS_SYNC] = ui_create_settings_item(
        screen, 282, 264, 350, 126, "云端同步", "上传云存档",
        ui_settings_item_event_cb, (void *)(uintptr_t)(UI_SETTINGS_PROGRESS_SYNC + 1U));
    s_ui.settings_sync_status = ui_create_label(
        s_ui.settings_items[UI_SETTINGS_PROGRESS_SYNC], "", 22, 90, 306, UI_COLOR_MUTED);
    lv_obj_set_height(s_ui.settings_sync_status, 32);
    s_ui.settings_items[UI_SETTINGS_ASSISTANT_MODE] = ui_create_settings_item(
        screen, 656, 264, 350, 126, "AI 助教", "内置 / 后端",
        ui_settings_item_event_cb, (void *)(uintptr_t)(UI_SETTINGS_ASSISTANT_MODE + 1U));
    s_ui.settings_assistant_status = ui_create_label(
        s_ui.settings_items[UI_SETTINGS_ASSISTANT_MODE], "", 22, 90, 306, UI_COLOR_CYAN);
    lv_obj_set_height(s_ui.settings_assistant_status, 32);
    s_ui.settings_items[UI_SETTINGS_DEBUG] = ui_create_settings_item(
        screen, 282, 406, 724, 162, "开发工具", "调试设置",
        ui_settings_item_event_cb, (void *)(uintptr_t)(UI_SETTINGS_DEBUG + 1U));
    lv_obj_t *debug_status = ui_create_label(
        s_ui.settings_items[UI_SETTINGS_DEBUG], "扬声器测试 / 解锁所有关卡 / 初始化", 22, 90, 640, UI_COLOR_MUTED);
    lv_obj_set_height(debug_status, 32);
    ui_add_pixel_corners(s_ui.settings_items[UI_SETTINGS_VOLUME], UI_COLOR_CYAN);
    ui_add_pixel_corners(s_ui.settings_items[UI_SETTINGS_WIFI], UI_COLOR_GREEN);
    ui_add_pixel_corners(s_ui.settings_items[UI_SETTINGS_PROGRESS_SYNC], UI_COLOR_GREEN);
    ui_add_pixel_corners(s_ui.settings_items[UI_SETTINGS_ASSISTANT_MODE], UI_COLOR_PINK);
    ui_add_pixel_corners(s_ui.settings_items[UI_SETTINGS_DEBUG], UI_COLOR_YELLOW);
    ui_refresh_settings_selection();
}

static lv_obj_t *ui_create_volume_button(lv_obj_t *parent,
                                         int32_t x,
                                         const char *label,
                                         int32_t delta)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, 232);
    lv_obj_set_size(button, 206, 52);
    ui_style_action_item(button, false, true);
    lv_obj_add_event_cb(button, ui_volume_adjust_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)delta);
    lv_obj_t *text = lv_label_create(button);
    ui_set_font(text);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_color(text, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(text);
    return button;
}

static void ui_create_settings_volume_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_SETTINGS_VOLUME] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "小飞船设置", "音量调整", true);

    lv_obj_t *panel = ui_create_panel(screen, 132, 118, 760, 370);
    ui_add_pixel_corners(panel, UI_COLOR_CYAN);
    ui_create_accent(panel, 0, 0, 220, UI_COLOR_CYAN);
    ui_create_label(panel, "扬声器输出", 28, 28, 280, UI_COLOR_CYAN);
    s_ui.settings_volume_value = ui_create_label(panel, "", 556, 20, 170, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s_ui.settings_volume_value, &app_ui_font_cn_24, 0);
    lv_obj_set_style_text_align(s_ui.settings_volume_value, LV_TEXT_ALIGN_RIGHT, 0);
    ui_create_label(panel, "左右摇杆每次调整 5%", 28, 68, 480, UI_COLOR_MUTED);
    ui_create_accent(panel, 28, 102, 704, UI_COLOR_BORDER);

    lv_obj_t *volume_track = ui_create_panel(panel, 28, 132, 704, 30);
    lv_obj_set_style_bg_color(volume_track, lv_color_hex(UI_COLOR_SURFACE_ALT), 0);
    lv_obj_set_style_border_color(volume_track, lv_color_hex(UI_COLOR_BORDER), 0);
    s_ui.settings_volume_bar = lv_obj_create(volume_track);
    lv_obj_set_pos(s_ui.settings_volume_bar, 0, 0);
    lv_obj_set_height(s_ui.settings_volume_bar, 28);
    lv_obj_set_style_bg_color(s_ui.settings_volume_bar, lv_color_hex(UI_COLOR_CYAN), 0);
    lv_obj_set_style_bg_opa(s_ui.settings_volume_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.settings_volume_bar, 0, 0);
    lv_obj_set_style_radius(s_ui.settings_volume_bar, 1, 0);
    lv_obj_clear_flag(s_ui.settings_volume_bar, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_volume_button(panel, 28, "- 5%", -5);
    ui_create_volume_button(panel, 277, "70%", 0);
    ui_create_volume_button(panel, 526, "+ 5%", 5);
    ui_create_label(panel, "当前设置仅本次开机有效", 28, 314, 704, UI_COLOR_MUTED);
    ui_refresh_volume_settings();
}

static void ui_create_settings_wifi_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_SETTINGS_WIFI] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "小飞船设置", "Wi-Fi 选择", true);

    lv_obj_t *side = ui_create_panel(screen, 18, 90, 244, 492);
    ui_add_pixel_corners(side, UI_COLOR_GREEN);
    ui_create_accent(side, 0, 0, 112, UI_COLOR_GREEN);
    lv_obj_t *side_title = ui_create_label(side, "网络状态", 22, 24, 200, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(side_title, &app_ui_font_cn_24, 0);
    s_ui.settings_wifi_status = ui_create_label(side, "", 22, 70, 200, UI_COLOR_GREEN);
    lv_obj_set_height(s_ui.settings_wifi_status, 52);
    ui_create_accent(side, 22, 152, 200, UI_COLOR_BORDER);
    ui_create_label(side, "自动连接预设热点\n\n扫描后可输入密码\n\n临时连接不保存", 22, 180, 200, UI_COLOR_MUTED);

    s_ui.settings_wifi_list_title = ui_create_label(screen, "", 282, 98, 500, UI_COLOR_CYAN);
    for (uint8_t row = 0U; row < UI_WIFI_VISIBLE_ROWS; ++row) {
        s_ui.settings_wifi_items[row] = ui_create_settings_item(
            screen, 282, 122 + row * 108, 724, 100, "", "",
            ui_wifi_item_event_cb, (void *)(uintptr_t)(row + 1U));
        s_ui.settings_wifi_titles[row] = ui_create_label(s_ui.settings_wifi_items[row], "",
                                                         22, 36, 640, UI_COLOR_TEXT);
        lv_obj_set_style_text_font(s_ui.settings_wifi_titles[row], &app_ui_font_cn_24, 0);
        lv_obj_set_height(s_ui.settings_wifi_titles[row], 30);
        s_ui.settings_wifi_details[row] = ui_create_label(s_ui.settings_wifi_items[row], "",
                                                          22, 78, 640, UI_COLOR_MUTED);
        lv_obj_set_height(s_ui.settings_wifi_details[row], 32);
        ui_add_pixel_corners(s_ui.settings_wifi_items[row], UI_COLOR_GREEN);
    }
    ui_refresh_wifi_selection();
}

static void ui_wifi_password_delete(void)
{
    const size_t length = strlen(s_settings_wifi_password);
    if (length > 0U) {
        s_settings_wifi_password[length - 1U] = '\0';
        audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
        ui_refresh_wifi_password();
    }
}

static void ui_wifi_password_submit(void)
{
    if (s_settings_wifi_password[0] == '\0') {
        audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
        return;
    }
    if (c6_network_connect_scan_result(s_settings_wifi_selection, s_settings_wifi_password) == ESP_OK) {
        audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
        ui_load_page(UI_PAGE_SETTINGS_WIFI);
    } else {
        audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
    }
}

static void ui_wifi_password_control_event_cb(lv_event_t *event)
{
    const uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if (action == 0U) {
        s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_DELETE_CONTROL;
        ui_wifi_password_delete();
    } else {
        s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_CONNECT_CONTROL;
        ui_wifi_password_submit();
    }
}

static void ui_create_settings_wifi_password_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_SETTINGS_WIFI_PASSWORD] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "小飞船设置", "输入密码", true);

    lv_obj_t *panel = ui_create_panel(screen, 18, 90, 988, 138);
    ui_add_pixel_corners(panel, UI_COLOR_GREEN);
    ui_create_label(panel, "网络", 22, 18, 120, UI_COLOR_CYAN);
    s_ui.settings_password_ssid = ui_create_label(panel, "", 118, 18, 830, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s_ui.settings_password_ssid, &app_ui_font_cn_24, 0);
    ui_create_label(panel, "密码", 22, 76, 120, UI_COLOR_CYAN);
    s_ui.settings_password_value = ui_create_label(panel, "", 118, 76, 830, UI_COLOR_TEXT);
    lv_label_set_long_mode(s_ui.settings_password_value, LV_LABEL_LONG_CLIP);
    ui_create_label(screen, "摇杆选择，中键输入，右键删除，左键取消", 18, 238, 650, UI_COLOR_MUTED);
    s_ui.settings_password_mode = lv_button_create(screen);
    lv_obj_set_pos(s_ui.settings_password_mode, 708, 234);
    lv_obj_set_size(s_ui.settings_password_mode, 298, 28);
    ui_style_action_item(s_ui.settings_password_mode, false, true);
    lv_obj_add_event_cb(s_ui.settings_password_mode, ui_wifi_password_mode_event_cb,
                        LV_EVENT_CLICKED, NULL);
    s_ui.settings_password_mode_label = lv_label_create(s_ui.settings_password_mode);
    lv_label_set_text(s_ui.settings_password_mode_label, "abc");
    lv_obj_set_style_text_font(s_ui.settings_password_mode_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_ui.settings_password_mode_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(s_ui.settings_password_mode_label);

    for (uint8_t index = 0U; index < UI_WIFI_KEY_COUNT; ++index) {
        const uint8_t column = index % 10U;
        const uint8_t row = index / 10U;
        lv_obj_t *key = lv_button_create(screen);
        lv_obj_set_pos(key, 18 + column * 99, 268 + row * 58);
        lv_obj_set_size(key, 88, 48);
        ui_style_action_item(key, false, true);
        lv_obj_add_event_cb(key, ui_wifi_password_key_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
        s_ui.settings_password_key_labels[index] = lv_label_create(key);
        lv_obj_set_style_text_font(s_ui.settings_password_key_labels[index], &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(s_ui.settings_password_key_labels[index],
                                    lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_center(s_ui.settings_password_key_labels[index]);
        s_ui.settings_password_keys[index] = key;
    }
    for (uint8_t index = 0U; index < 2U; ++index) {
        lv_obj_t *button = lv_button_create(screen);
        lv_obj_set_pos(button, 18 + index * 500, 510);
        lv_obj_set_size(button, 480, 58);
        ui_style_action_item(button, false, true);
        lv_obj_add_event_cb(button, ui_wifi_password_control_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
        lv_obj_t *text = lv_label_create(button);
        ui_set_font(text);
        lv_label_set_text(text, index == 0U ? "删除" : "连接");
        lv_obj_set_style_text_color(text, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_center(text);
        if (index == 0U) s_ui.settings_password_delete = button;
        else s_ui.settings_password_connect = button;
    }
    ui_refresh_wifi_password();
}

static void ui_create_settings_debug_screen(void)
{
    static const char *const sections[UI_DEBUG_SETTINGS_COUNT] = {
        "声音测试", "辅助功能", "游玩记录",
    };
    static const char *const titles[UI_DEBUG_SETTINGS_COUNT] = {
        "扬声器测试", "解锁所有关卡", "初始化",
    };
    static const uint32_t colors[UI_DEBUG_SETTINGS_COUNT] = {
        UI_COLOR_CYAN, UI_COLOR_PINK, UI_COLOR_YELLOW,
    };

    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_SETTINGS_DEBUG] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "小飞船设置", "调试设置", true);
    ui_create_label(screen, "开发工具", 150, 98, 500, UI_COLOR_CYAN);

    for (uint8_t index = 0U; index < UI_DEBUG_SETTINGS_COUNT; ++index) {
        s_ui.settings_debug_items[index] = ui_create_settings_item(
            screen, 150, 122 + index * 142, 724, 126, sections[index], titles[index],
            ui_debug_item_event_cb, (void *)(uintptr_t)(index + 1U));
        if (index == UI_DEBUG_SPEAKER_TEST) {
            lv_obj_t *status = ui_create_label(s_ui.settings_debug_items[index],
                                               "按住中键播放 1 kHz", 22, 90, 640, UI_COLOR_MUTED);
            lv_obj_set_height(status, 32);
        } else if (index == UI_DEBUG_UNLOCK_ALL) {
            s_ui.settings_unlock_status = ui_create_label(s_ui.settings_debug_items[index], "",
                                                          22, 90, 640, UI_COLOR_MUTED);
            lv_obj_set_height(s_ui.settings_unlock_status, 32);
        } else {
            s_ui.settings_initialize_status = ui_create_label(s_ui.settings_debug_items[index], "",
                                                              22, 90, 640, UI_COLOR_MUTED);
            lv_obj_set_height(s_ui.settings_initialize_status, 32);
        }
        ui_add_pixel_corners(s_ui.settings_debug_items[index], colors[index]);
    }
    ui_refresh_debug_selection();
}

static lv_obj_t *ui_create_diagnostic_value(lv_obj_t *panel, const char *heading)
{
    ui_create_label(panel, heading, 18, 16, 430, UI_COLOR_CYAN);
    return ui_create_label(panel, "", 18, 48, 430, UI_COLOR_TEXT);
}

static void ui_create_diagnostics_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_ui.screen[UI_PAGE_DIAGNOSTICS] = screen;
    ui_style_screen(screen);
    ui_create_starfield(screen);
    ui_create_header(screen, "小飞船检查", "调试界面", true);

    lv_obj_t *keys = ui_create_panel(screen, 18, 90, 300, 148);
    ui_add_pixel_corners(keys, UI_COLOR_CYAN);
    ui_create_accent(keys, 0, 0, 88, UI_COLOR_CYAN);
    s_ui.debug_keys = ui_create_diagnostic_value(keys, "按键状态");
    lv_obj_t *joystick = ui_create_panel(screen, 336, 90, 340, 148);
    ui_add_pixel_corners(joystick, UI_COLOR_YELLOW);
    ui_create_accent(joystick, 0, 0, 88, UI_COLOR_AMBER);
    s_ui.debug_joystick = ui_create_diagnostic_value(joystick, "摇杆状态");
    lv_obj_t *switches = ui_create_panel(screen, 694, 90, 312, 148);
    ui_add_pixel_corners(switches, UI_COLOR_GREEN);
    ui_create_accent(switches, 0, 0, 88, UI_COLOR_GREEN);
    s_ui.debug_switches = ui_create_diagnostic_value(switches, "开关状态");

    lv_obj_t *ir = ui_create_panel(screen, 18, 256, 300, 144);
    ui_add_pixel_corners(ir, UI_COLOR_PINK);
    ui_create_accent(ir, 0, 0, 88, UI_COLOR_AMBER);
    s_ui.debug_ir = ui_create_diagnostic_value(ir, "积木连接");
    lv_obj_t *microphone = ui_create_panel(screen, 336, 256, 670, 144);
    ui_add_pixel_corners(microphone, UI_COLOR_GREEN);
    ui_create_accent(microphone, 0, 0, 112, UI_COLOR_GREEN);
    s_ui.debug_mic = ui_create_diagnostic_value(microphone, "麦克风音量");
    lv_obj_set_style_text_font(s_ui.debug_mic, &lv_font_montserrat_22, 0);
    s_ui.debug_mic_bar = lv_bar_create(microphone);
    lv_obj_set_pos(s_ui.debug_mic_bar, 18, 98);
    lv_obj_set_size(s_ui.debug_mic_bar, 634, 18);
    lv_bar_set_range(s_ui.debug_mic_bar, 0, 2500);
    lv_bar_set_value(s_ui.debug_mic_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.debug_mic_bar, lv_color_hex(UI_COLOR_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.debug_mic_bar, lv_color_hex(UI_COLOR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.debug_mic_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.debug_mic_bar, 1, LV_PART_INDICATOR);

    lv_obj_t *audio = ui_create_panel(screen, 18, 420, 988, 162);
    ui_add_pixel_corners(audio, UI_COLOR_CYAN);
    ui_create_accent(audio, 0, 0, 140, UI_COLOR_CYAN);
    ui_create_label(audio, "声音测试（按住右键）", 18, 16, 500, UI_COLOR_AMBER);
    s_ui.debug_tone = ui_create_label(audio, "", 18, 51, 950, UI_COLOR_TEXT);
    ui_create_label(audio, "播放 1000 赫兹测试音", 18, 96, 950, UI_COLOR_MUTED);
    ui_create_signal_icon(audio, 870, 36, UI_COLOR_CYAN, 0);
}

static void ui_update_diagnostics(const key_input_state_t *keys,
                                  const joystick_input_state_t *joystick,
                                  uint32_t scans,
                                  uint16_t links)
{
    float rms = 0.0f;
    float tone_ratio = 0.0f;
    audio_self_test_get_microphone_level(&rms, &tone_ratio);

    lv_label_set_text_fmt(s_ui.debug_keys, "左键 %s\n中间键 %s\n右键 %s",
                          keys->key3_pressed ? "按下" : "松开",
                          keys->key1_pressed ? "按下" : "松开",
                          keys->key0_pressed ? "按下" : "松开");
    lv_label_set_text_fmt(s_ui.debug_joystick, "上 %s   下 %s\n左 %s   右 %s",
                          joystick->up_pressed ? "按下" : "松开",
                          joystick->down_pressed ? "按下" : "松开",
                          joystick->left_pressed ? "按下" : "松开",
                          joystick->right_pressed ? "按下" : "松开");
    lv_label_set_text_fmt(s_ui.debug_switches, "开关1 %s   开关2 %s\n开关3 %s   开关4 %s",
                          keys->sw1_on ? "开启" : "关闭", keys->sw2_on ? "开启" : "关闭",
                          keys->sw3_on ? "开启" : "关闭", keys->sw4_on ? "开启" : "关闭");
    lv_label_set_text_fmt(s_ui.debug_ir, "连接数量  %u\n扫描次数  %lu",
                          links, (unsigned long)scans);
    lv_label_set_text_fmt(s_ui.debug_mic, "音量  %.0f", (double)rms);
    lv_label_set_text_fmt(s_ui.debug_tone, "扬声器  %s        声音匹配  %.3f",
                          keys->key0_pressed ? "开启" : "关闭", (double)tone_ratio);
    lv_obj_set_style_text_color(s_ui.debug_keys,
                                lv_color_hex((keys->key0_pressed || keys->key1_pressed || keys->key3_pressed) ?
                                             UI_COLOR_CYAN : UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_ui.debug_joystick,
                                lv_color_hex((joystick->up_pressed || joystick->down_pressed ||
                                              joystick->left_pressed || joystick->right_pressed) ?
                                             UI_COLOR_AMBER : UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_ui.debug_switches,
                                lv_color_hex((keys->sw1_on || keys->sw2_on || keys->sw3_on || keys->sw4_on) ?
                                             UI_COLOR_GREEN : UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_ui.debug_tone,
                                lv_color_hex(keys->key0_pressed ? UI_COLOR_AMBER : UI_COLOR_TEXT), 0);
    lv_bar_set_value(s_ui.debug_mic_bar, rms > 2500.0f ? 2500 : (int32_t)rms, LV_ANIM_OFF);
}

static void ui_load_page(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT || s_ui.screen[page] == NULL) {
        return;
    }
    const bool old_play_page = s_page == UI_PAGE_PLAY || s_page == UI_PAGE_SUCCESS;
    const bool new_play_page = page == UI_PAGE_PLAY || page == UI_PAGE_SUCCESS;
    if (!old_play_page && new_play_page) {
        circuit_debug_reset();
        board_snapshot_reset(true);
        play_mode_set_active(true);
        game_judge_reset();
        s_play_snapshot_generation = UINT32_MAX;
        s_play_judge_version = UINT32_MAX;
        s_play_debug_generation = UINT32_MAX;
        s_play_voice_generation = UINT32_MAX;
        s_play_topology_revision = 0;
        s_play_action_selection = 0;
        s_play_invalid_link_prompt_active = false;
        s_play_last_prompt_us = 0;
    } else if (s_page == UI_PAGE_SUCCESS && page == UI_PAGE_PLAY) {
        game_judge_reset();
        s_play_judge_version = UINT32_MAX;
    } else if (old_play_page && !new_play_page) {
        circuit_debug_reset();
        game_judge_cancel("检查已经停止");
        play_mode_set_active(false);
        board_snapshot_reset(false);
    }
    if ((s_page == UI_PAGE_DIAGNOSTICS || s_page == UI_PAGE_SETTINGS ||
         s_page == UI_PAGE_SETTINGS_DEBUG) && page != s_page) {
        audio_self_test_set_tone_enabled(false);
    }
    if (page == UI_PAGE_CAMPAIGN) {
        ui_refresh_campaign();
    } else if (page == UI_PAGE_DETAIL) {
        ui_refresh_detail();
    } else if (page == UI_PAGE_PLAY) {
        ui_refresh_play();
    } else if (page == UI_PAGE_SUCCESS) {
        ui_refresh_success();
    } else if (page == UI_PAGE_SETTINGS) {
        ui_refresh_settings_selection();
    } else if (page == UI_PAGE_SETTINGS_VOLUME) {
        ui_refresh_volume_settings();
    } else if (page == UI_PAGE_SETTINGS_WIFI) {
        ui_refresh_wifi_selection();
    } else if (page == UI_PAGE_SETTINGS_WIFI_PASSWORD) {
        ui_refresh_wifi_password();
    } else if (page == UI_PAGE_SETTINGS_DEBUG) {
        ui_refresh_debug_selection();
    }
    s_page = page;
    lv_screen_load(s_ui.screen[page]);
}

static void ui_activate_home_selection(void)
{
    audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    ui_load_page(s_home_selection == UI_HOME_CAMPAIGN ? UI_PAGE_CAMPAIGN : UI_PAGE_SETTINGS);
}

static void ui_select_nearest_node(int32_t direction_x, int32_t direction_y)
{
    const campaign_node_t *nodes = ui_nodes();
    const campaign_node_t *current = &nodes[s_selected_node];
    uint16_t best = s_selected_node;
    int64_t best_score = INT64_MAX;

    for (uint16_t index = 0; index < s_node_count; ++index) {
        if (index == s_selected_node) {
            continue;
        }
        const int32_t dx = nodes[index].x - current->x;
        const int32_t dy = nodes[index].y - current->y;
        int32_t primary;
        int32_t perpendicular;
        if (direction_x > 0 && dx > 0) {
            primary = dx;
            perpendicular = abs(dy);
        } else if (direction_x < 0 && dx < 0) {
            primary = -dx;
            perpendicular = abs(dy);
        } else if (direction_y > 0 && dy > 0) {
            primary = dy;
            perpendicular = abs(dx);
        } else if (direction_y < 0 && dy < 0) {
            primary = -dy;
            perpendicular = abs(dx);
        } else {
            continue;
        }
        /* Keep diagonal candidates out of a cardinal move at branch points. */
        if ((int64_t)perpendicular * 4 > (int64_t)primary * 5) {
            continue;
        }
        const int64_t score = (int64_t)primary * 4 + perpendicular;
        if (score < best_score) {
            best_score = score;
            best = index;
        }
    }
    if (best != s_selected_node) {
        s_selected_node = best;
        audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
        ui_refresh_campaign();
    }
}

static bool ui_pressed_edge(bool current, bool previous)
{
    return current && !previous;
}

static esp_err_t ui_campaign_progression_self_test(void)
{
    const campaign_node_t *nodes = ui_nodes();
    bool completed[UI_MAX_NODES] = {0};
    const int32_t level101 = ui_find_node_index(nodes, s_node_count, 101U);
    const int32_t level102 = ui_find_node_index(nodes, s_node_count, 102U);
    const int32_t level103 = ui_find_node_index(nodes, s_node_count, 103U);
    const int32_t level150 = ui_find_node_index(nodes, s_node_count, 150U);
    const int32_t level201 = ui_find_node_index(nodes, s_node_count, 201U);
    const int32_t level202 = ui_find_node_index(nodes, s_node_count, 202U);
    const int32_t level203 = ui_find_node_index(nodes, s_node_count, 203U);
    const int32_t level301 = ui_find_node_index(nodes, s_node_count, 301U);
    const int32_t level302 = ui_find_node_index(nodes, s_node_count, 302U);
    const int32_t level550 = ui_find_node_index(nodes, s_node_count, 550U);
    const int32_t level602 = ui_find_node_index(nodes, s_node_count, 602U);
    const int32_t level650 = ui_find_node_index(nodes, s_node_count, 650U);

    if (s_node_count > UI_MAX_NODES || level101 < 0 || level102 < 0 || level103 < 0 ||
        level150 < 0 || level201 < 0 || level202 < 0 || level203 < 0 ||
        level301 < 0 || level302 < 0 ||
        level550 < 0 || level602 < 0 || level650 < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ui_gate_is_unlocked_for_progress(SSD1315_GATE_INPUT, completed) ||
        !ui_gate_is_unlocked_for_progress(SSD1315_GATE_OUTPUT, completed) ||
        ui_gate_is_unlocked_for_progress(SSD1315_GATE_NAND, completed) ||
        ui_gate_is_unlocked_for_progress(SSD1315_GATE_NOT, completed) ||
        ui_gate_is_unlocked_for_progress(SSD1315_GATE_AND, completed) ||
        ui_gate_is_unlocked_for_progress(SSD1315_GATE_XNOR, completed)) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint16_t index = 0U; index < s_node_count; ++index) {
        const bool initially_unlocked = ui_node_is_unlocked_for_progress(index, completed, false);
        if (initially_unlocked != (index == (uint16_t)level101)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!ui_node_is_unlocked_for_progress(index, completed, true)) {
            return ESP_ERR_INVALID_STATE;
        }
        for (uint8_t prerequisite = 0U;
             prerequisite < nodes[index].prerequisite_count;
             ++prerequisite) {
            if (ui_find_node_index(nodes, s_node_count,
                                   nodes[index].prerequisite_ids[prerequisite]) < 0) {
                return ESP_ERR_INVALID_STATE;
            }
        }
    }

    completed[level101] = true;
    if (!ui_gate_is_unlocked_for_progress(SSD1315_GATE_NAND, completed) ||
        ui_gate_is_unlocked_for_progress(SSD1315_GATE_NOT, completed)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ui_node_is_unlocked_for_progress((uint16_t)level102, completed, false)) {
        return ESP_ERR_INVALID_STATE;
    }
    completed[level102] = true;
    completed[level103] = true;
    if (!ui_gate_is_unlocked_for_progress(SSD1315_GATE_NOT, completed)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ui_node_is_unlocked_for_progress((uint16_t)level150, completed, false)) {
        return ESP_ERR_INVALID_STATE;
    }
    completed[level201] = true;
    if (!ui_gate_is_unlocked_for_progress(SSD1315_GATE_AND, completed)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ui_node_is_unlocked_for_progress((uint16_t)level203, completed, false)) {
        return ESP_ERR_INVALID_STATE;
    }
    completed[level202] = true;
    if (!ui_gate_is_unlocked_for_progress(SSD1315_GATE_OR, completed)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ui_node_is_unlocked_for_progress((uint16_t)level203, completed, false)) {
        return ESP_ERR_INVALID_STATE;
    }
    completed[level203] = true;
    if (!ui_gate_is_unlocked_for_progress(SSD1315_GATE_NOR, completed)) {
        return ESP_ERR_INVALID_STATE;
    }
    completed[level301] = true;
    completed[level302] = true;
    if (!ui_gate_is_unlocked_for_progress(SSD1315_GATE_XOR, completed) ||
        !ui_gate_is_unlocked_for_progress(SSD1315_GATE_XNOR, completed)) {
        return ESP_ERR_INVALID_STATE;
    }
    completed[level602] = true;
    if (ui_node_is_unlocked_for_progress((uint16_t)level650, completed, false)) {
        return ESP_ERR_INVALID_STATE;
    }
    completed[level550] = true;
    if (!ui_node_is_unlocked_for_progress((uint16_t)level650, completed, false) ||
        !ui_node_is_unlocked_for_progress((uint16_t)level650, completed, true)) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "campaign progression self-test passed: initial 101, branches, final dual prerequisite");
    return ESP_OK;
}

static esp_err_t ui_shooter_lifecycle_self_test(void)
{
    lv_mem_monitor_t initial;
    lv_mem_monitor_t warmed;
    lv_mem_monitor_t after;
    lv_mem_monitor(&initial);
    lv_obj_t *screen = shooter_game_create(1U, NULL, NULL, NULL);
    if (screen == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_t *stopped_screen = shooter_game_stop();
    if (stopped_screen != screen) {
        return ESP_FAIL;
    }
    lv_obj_delete(screen);
    if (lv_mem_test() != LV_RESULT_OK) {
        return ESP_FAIL;
    }
    lv_mem_monitor(&warmed);

    screen = shooter_game_create(1U, NULL, NULL, NULL);
    if (screen == NULL) {
        return ESP_ERR_NO_MEM;
    }
    stopped_screen = shooter_game_stop();
    if (stopped_screen != screen) {
        return ESP_FAIL;
    }
    lv_obj_delete(screen);
    if (lv_mem_test() != LV_RESULT_OK) {
        return ESP_FAIL;
    }
    lv_mem_monitor(&after);
    ESP_LOGI(TAG,
             "shooter lifecycle self-test: initial=%u warmed=%u after=%u largest=%u frag=%u%%",
             (unsigned)initial.free_size, (unsigned)warmed.free_size, (unsigned)after.free_size,
             (unsigned)after.free_biggest_size, after.frag_pct);
    return warmed.free_size == after.free_size ? ESP_OK : ESP_FAIL;
}

esp_err_t app_ui_init(void)
{
    const bsp_display_config_t hardware_config = {
        .hdmi_resolution = BSP_HDMI_RES_NONE,
        .dsi_bus = {
            .phy_clk_src = 0,
            .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        },
    };
    bsp_lcd_handles_t lcd_handles = {0};
    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 16U * 1024U;
    adapter_config.stack_in_psram = true;

    ESP_RETURN_ON_ERROR(bsp_display_new_with_handles(&hardware_config, &lcd_handles), TAG,
                        "initialize LCD panel");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG, "initialize LVGL adapter");
    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
        lcd_handles.panel, lcd_handles.io, BSP_LCD_H_RES, BSP_LCD_V_RES, ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = 48U;
    display_config.profile.use_psram = true;
    lv_display_t *display = esp_lv_adapter_register_display(&display_config);
    if (display == NULL) {
        return ESP_FAIL;
    }
    esp_lcd_touch_handle_t touch = NULL;
    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &touch), TAG, "initialize GT911 touch");
    const esp_lv_adapter_touch_config_t touch_config =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);
    if (esp_lv_adapter_register_touch(&touch_config) == NULL) {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "start LVGL task");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "enable LCD backlight");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(-1), TAG, "lock LVGL");

    const campaign_content_meta_t meta = campaign_content_meta();
    const campaign_node_t *nodes = campaign_content_nodes(&s_node_count);
    if (s_node_count > UI_MAX_NODES) {
        esp_lv_adapter_unlock();
        return ESP_ERR_INVALID_SIZE;
    }
    memset(s_completed_nodes, 0, sizeof(s_completed_nodes));
    const esp_err_t progress_err = campaign_progress_load(nodes, s_node_count, s_completed_nodes);
    if (progress_err != ESP_OK) {
        ESP_LOGW(TAG, "campaign progress load failed; starting with empty progress: %s",
                 esp_err_to_name(progress_err));
        memset(s_completed_nodes, 0, sizeof(s_completed_nodes));
    } else {
        ESP_LOGI(TAG, "campaign progress restored from flash");
    }
    for (s_selected_node = 0; s_selected_node < s_node_count; ++s_selected_node) {
        if (nodes[s_selected_node].id == meta.initial_level_id) {
            break;
        }
    }
    if (s_selected_node >= s_node_count) {
        s_selected_node = 0;
    }
    ESP_RETURN_ON_ERROR(ui_campaign_progression_self_test(), TAG,
                        "validate campaign unlock progression");

    ui_create_home_screen();
    ui_create_campaign_screen();
    ui_create_detail_screen();
    ui_create_play_screen();
    ui_create_success_screen();
    ui_create_settings_screen();
    ui_create_settings_volume_screen();
    ui_create_settings_wifi_screen();
    ui_create_settings_wifi_password_screen();
    ui_create_settings_debug_screen();
    ui_create_diagnostics_screen();
    ESP_RETURN_ON_ERROR(ui_shooter_lifecycle_self_test(), TAG,
                        "validate dynamic shooter screen lifecycle");
    ui_refresh_campaign();
    ui_refresh_detail();
    ui_load_page(UI_PAGE_HOME);

    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);

    esp_lv_adapter_unlock();
    ESP_LOGI(TAG, "sci-fi UI ready: %u campaign nodes, ten persistent screens; "
             "LVGL memory=%u%% free=%u largest=%u frag=%u%%",
             s_node_count, memory.used_pct, (unsigned)memory.free_size,
             (unsigned)memory.free_biggest_size, memory.frag_pct);
    return ESP_OK;
}

esp_err_t app_ui_update(const key_input_state_t *keys,
                        const joystick_input_state_t *joystick,
                        uint32_t completed_ir_scans,
                        uint16_t ir_link_pairs,
                        bool programmer_owns_input)
{
    if (keys == NULL || joystick == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    voice_assistant_update(s_page == UI_PAGE_PLAY,
                           programmer_owns_input,
                           keys->key0_pressed,
                           ui_nodes()[s_selected_node].id);
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(-1), TAG, "lock LVGL");

    if (s_programmer_owns_input != programmer_owns_input) {
        s_programmer_owns_input = programmer_owns_input;
        s_play_judge_version = UINT32_MAX;
    }

    if (!s_input_initialized) {
        s_previous_keys = *keys;
        s_previous_joystick = *joystick;
        s_input_initialized = true;
    } else {
        const bool key0_edge = ui_pressed_edge(keys->key0_pressed, s_previous_keys.key0_pressed);
        const bool key1_edge = ui_pressed_edge(keys->key1_pressed, s_previous_keys.key1_pressed);
        const bool key3_edge = ui_pressed_edge(keys->key3_pressed, s_previous_keys.key3_pressed);
        const bool up_edge = ui_pressed_edge(joystick->up_pressed, s_previous_joystick.up_pressed);
        const bool down_edge = ui_pressed_edge(joystick->down_pressed, s_previous_joystick.down_pressed);
        const bool left_edge = ui_pressed_edge(joystick->left_pressed, s_previous_joystick.left_pressed);
        const bool right_edge = ui_pressed_edge(joystick->right_pressed, s_previous_joystick.right_pressed);

        if (!programmer_owns_input && key3_edge) {
            ui_back_event_cb(NULL);
        } else if (!programmer_owns_input && s_page == UI_PAGE_HOME) {
            if (left_edge && s_home_selection != UI_HOME_CAMPAIGN) {
                s_home_selection = UI_HOME_CAMPAIGN;
                audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                ui_refresh_home_selection();
            } else if (right_edge && s_home_selection != UI_HOME_SETTINGS) {
                s_home_selection = UI_HOME_SETTINGS;
                audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                ui_refresh_home_selection();
            }
            if (key1_edge) {
                ui_activate_home_selection();
            }
        } else if (!programmer_owns_input && s_page == UI_PAGE_SETTINGS) {
            progress_sync_status_t progress_status;
            progress_sync_get_status(&progress_status);
            if (!s_progress_sync_status_initialized ||
                memcmp(&progress_status, &s_last_progress_sync_status, sizeof(progress_status)) != 0) {
                ui_refresh_settings_selection();
            }
            if (left_edge) ui_move_settings_selection(-1, 0);
            if (right_edge) ui_move_settings_selection(1, 0);
            if (up_edge) ui_move_settings_selection(0, -1);
            if (down_edge) ui_move_settings_selection(0, 1);
            if (key1_edge) ui_activate_settings_selection();
        } else if (!programmer_owns_input && s_page == UI_PAGE_SETTINGS_VOLUME) {
            if (left_edge) ui_adjust_settings_volume(-5);
            if (right_edge) ui_adjust_settings_volume(5);
            if (key1_edge && audio_self_test_get_master_volume() != 70U) {
                audio_self_test_set_master_volume(70U);
                audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                ui_refresh_volume_settings();
            }
        } else if (!programmer_owns_input && s_page == UI_PAGE_SETTINGS_WIFI) {
            ui_refresh_wifi_selection();
            const uint8_t count = s_settings_wifi_scan_mode ? s_settings_wifi_count :
                                  (uint8_t)(UI_WIFI_PRESET_COUNT + 1U);
            if (up_edge || down_edge) {
                if (count == 0U) {
                    s_settings_wifi_selection = 0U;
                } else if (up_edge) {
                    s_settings_wifi_selection = s_settings_wifi_selection == 0U ?
                        count - 1U : s_settings_wifi_selection - 1U;
                } else {
                    s_settings_wifi_selection =
                        (s_settings_wifi_selection + 1U) % count;
                }
                audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                ui_refresh_wifi_selection();
            }
            if (left_edge && s_settings_wifi_scan_mode) {
                s_settings_wifi_scan_mode = false;
                s_settings_wifi_selection = 0U;
                s_settings_wifi_window = 0U;
                ui_refresh_wifi_selection();
            }
            if (key1_edge) ui_activate_wifi_selection();
        } else if (!programmer_owns_input && s_page == UI_PAGE_SETTINGS_WIFI_PASSWORD) {
            bool refresh_password = false;
            if (left_edge) {
                if (s_settings_wifi_keyboard_selection < UI_WIFI_KEY_COUNT) {
                    s_settings_wifi_keyboard_selection = s_settings_wifi_keyboard_selection % 10U == 0U ?
                        s_settings_wifi_keyboard_selection + 9U : s_settings_wifi_keyboard_selection - 1U;
                } else if (s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_DELETE_CONTROL) {
                    s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_MODE_CONTROL;
                } else if (s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_CONNECT_CONTROL) {
                    s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_DELETE_CONTROL;
                } else {
                    s_settings_wifi_keyboard_mode = (s_settings_wifi_keyboard_mode + 2U) % 3U;
                }
                refresh_password = true;
            }
            if (right_edge) {
                if (s_settings_wifi_keyboard_selection < UI_WIFI_KEY_COUNT) {
                    s_settings_wifi_keyboard_selection = s_settings_wifi_keyboard_selection % 10U == 9U ?
                        s_settings_wifi_keyboard_selection - 9U : s_settings_wifi_keyboard_selection + 1U;
                } else if (s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_MODE_CONTROL) {
                    s_settings_wifi_keyboard_mode = (s_settings_wifi_keyboard_mode + 1U) % 3U;
                } else if (s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_DELETE_CONTROL) {
                    s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_CONNECT_CONTROL;
                } else {
                    s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_MODE_CONTROL;
                }
                refresh_password = true;
            }
            if (up_edge) {
                if (s_settings_wifi_keyboard_selection < 10U) {
                    s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_MODE_CONTROL;
                } else if (s_settings_wifi_keyboard_selection < UI_WIFI_KEY_COUNT) {
                    s_settings_wifi_keyboard_selection -= 10U;
                } else {
                    s_settings_wifi_keyboard_selection = 30U;
                }
                refresh_password = true;
            }
            if (down_edge) {
                if (s_settings_wifi_keyboard_selection < 30U) {
                    s_settings_wifi_keyboard_selection += 10U;
                    refresh_password = true;
                } else if (s_settings_wifi_keyboard_selection < UI_WIFI_KEY_COUNT) {
                    s_settings_wifi_keyboard_selection = UI_WIFI_PASSWORD_CONNECT_CONTROL;
                    refresh_password = true;
                } else if (s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_MODE_CONTROL) {
                    s_settings_wifi_keyboard_selection = 0U;
                    refresh_password = true;
                } else {
                    s_settings_wifi_keyboard_selection = 30U;
                    refresh_password = true;
                }
            }
            if (refresh_password) {
                audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                ui_refresh_wifi_password();
            }
            if (key0_edge) ui_wifi_password_delete();
            if (key1_edge) {
                if (s_settings_wifi_keyboard_selection < UI_WIFI_KEY_COUNT) {
                    ui_wifi_password_insert(s_settings_wifi_keyboard_selection);
                } else if (s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_MODE_CONTROL) {
                    s_settings_wifi_keyboard_mode = (s_settings_wifi_keyboard_mode + 1U) % 3U;
                    audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                    ui_refresh_wifi_password();
                } else if (s_settings_wifi_keyboard_selection == UI_WIFI_PASSWORD_DELETE_CONTROL) {
                    ui_wifi_password_delete();
                } else {
                    ui_wifi_password_submit();
                }
            }
        } else if (!programmer_owns_input && s_page == UI_PAGE_SETTINGS_DEBUG) {
            if (up_edge || down_edge) {
                if (up_edge) {
                    s_settings_debug_selection = s_settings_debug_selection == 0U ?
                        UI_DEBUG_SETTINGS_COUNT - 1U : s_settings_debug_selection - 1U;
                } else {
                    s_settings_debug_selection =
                        (s_settings_debug_selection + 1U) % UI_DEBUG_SETTINGS_COUNT;
                }
                audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                ui_refresh_debug_selection();
            }
            if (key1_edge) ui_activate_debug_selection();
        } else if (!programmer_owns_input && s_page == UI_PAGE_CAMPAIGN) {
            if (up_edge) ui_select_nearest_node(0, -1);
            if (down_edge) ui_select_nearest_node(0, 1);
            if (left_edge) ui_select_nearest_node(-1, 0);
            if (right_edge) ui_select_nearest_node(1, 0);
            if (key1_edge) {
                audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
                ui_load_page(UI_PAGE_DETAIL);
            }
        } else if (!programmer_owns_input && s_page == UI_PAGE_DETAIL) {
            if (key1_edge) ui_start_play();
        } else if (!programmer_owns_input && s_page == UI_PAGE_PLAY) {
            if (up_edge || down_edge) {
                s_play_action_selection = s_play_action_selection == 0U ? 1U : 0U;
                audio_self_test_play_effect(AUDIO_EFFECT_SELECT);
                ui_style_action_item(s_ui.play_action, s_play_action_selection == 0U, true);
                ui_style_action_item(s_ui.play_debug_action, s_play_action_selection == 1U, true);
            }
            if (key1_edge) {
                if (s_play_action_selection == 0U) {
                    ui_start_judge();
                } else {
                    ui_toggle_debug();
                }
            }
        }
    }

    const bool settings_tone = s_page == UI_PAGE_SETTINGS_DEBUG &&
                               s_settings_debug_selection == UI_DEBUG_SPEAKER_TEST &&
                               keys->key1_pressed;
    if (s_page == UI_PAGE_DIAGNOSTICS) {
        audio_self_test_set_tone_enabled(keys->key0_pressed);
        ui_update_diagnostics(keys, joystick, completed_ir_scans, ir_link_pairs);
    } else {
        audio_self_test_set_tone_enabled(settings_tone);
    }
    if (s_page == UI_PAGE_PLAY) {
        ui_refresh_play();
        ui_log_play_health();
    } else if (s_page == UI_PAGE_SHOOTER) {
        shooter_game_set_input(keys, joystick);
    }

    s_previous_keys = *keys;
    s_previous_joystick = *joystick;
    esp_lv_adapter_unlock();
    return ESP_OK;
}
