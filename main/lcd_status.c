#include "lcd_status.h"

#include "esp_check.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_display.h"
#include "esp_log.h"
#include "lvgl.h"

#include "bsp/display.h"
#include "bsp/esp32_p4_function_ev_board.h"

static const char *TAG = "lcd_status";
static lv_obj_t *s_key_status_label;
static lv_obj_t *s_joystick_status_label;
static lv_obj_t *s_switch_status_label;
static lv_obj_t *s_ir_status_label;
static uint32_t s_last_ir_scan = UINT32_MAX;

static const char *lcd_status_text(bool pressed)
{
    return pressed ? "PRESSED" : "RELEASED";
}

esp_err_t lcd_status_init(void)
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

    ESP_RETURN_ON_ERROR(bsp_display_new_with_handles(&hardware_config, &lcd_handles), TAG, "initialize LCD panel");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), TAG, "initialize LVGL adapter");

    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
        lcd_handles.panel,
        lcd_handles.io,
        BSP_LCD_H_RES,
        BSP_LCD_V_RES,
        ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.buffer_height = 48U;
    display_config.profile.use_psram = true;

    if (esp_lv_adapter_register_display(&display_config) == NULL) {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "start LVGL task");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "enable LCD backlight");
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(-1), TAG, "lock LVGL");

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x102030), 0);
    lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(screen, 28, 0);
    lv_obj_set_style_pad_bottom(screen, 20, 0);
    lv_obj_set_style_pad_row(screen, 14, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Input Status");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    s_key_status_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_key_status_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_key_status_label, lv_color_hex(0x9FE7FF), 0);
    lv_label_set_text(s_key_status_label,
                      "KEY0 (RIGHT): RELEASED\nKEY1 (CENTER): RELEASED\nKEY3 (LEFT): RELEASED");

    s_joystick_status_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_joystick_status_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_joystick_status_label, lv_color_hex(0xB8F5C1), 0);
    lv_label_set_text(s_joystick_status_label,
                      "JOYSTICK\nUP: RELEASED\nDOWN: RELEASED\nLEFT: RELEASED\nRIGHT: RELEASED");

    s_switch_status_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_switch_status_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_switch_status_label, lv_color_hex(0xFFD59A), 0);
    lv_label_set_text(s_switch_status_label, "SWITCHES\nSW1: OFF\nSW2: OFF\nSW3: OFF\nSW4: OFF");

    s_ir_status_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_ir_status_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_ir_status_label, lv_color_hex(0xF7A8FF), 0);
    lv_label_set_text(s_ir_status_label, "IR LINKS: 0");

    esp_lv_adapter_unlock();
    ESP_LOGI(TAG, "LCD status screen ready");
    return ESP_OK;
}

esp_err_t lcd_status_show_inputs(const key_input_state_t *key_state,
                                 const joystick_input_state_t *joystick_state,
                                 uint32_t completed_ir_scans,
                                 uint16_t link_pairs)
{
    if (key_state == NULL || joystick_state == NULL || s_key_status_label == NULL ||
        s_joystick_status_label == NULL || s_switch_status_label == NULL || s_ir_status_label == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_lock(100), TAG, "lock LVGL");
    lv_label_set_text_fmt(s_key_status_label,
                          "KEY0 (RIGHT): %s\nKEY1 (CENTER): %s\nKEY3 (LEFT): %s",
                          lcd_status_text(key_state->key0_pressed),
                          lcd_status_text(key_state->key1_pressed),
                          lcd_status_text(key_state->key3_pressed));
    lv_label_set_text_fmt(s_joystick_status_label,
                          "JOYSTICK\nUP: %s\nDOWN: %s\nLEFT: %s\nRIGHT: %s",
                          lcd_status_text(joystick_state->up_pressed),
                          lcd_status_text(joystick_state->down_pressed),
                          lcd_status_text(joystick_state->left_pressed),
                          lcd_status_text(joystick_state->right_pressed));
    lv_label_set_text_fmt(s_switch_status_label,
                          "SWITCHES\nSW1: %s\nSW2: %s\nSW3: %s\nSW4: %s",
                          key_state->sw1_on ? "ON" : "OFF",
                          key_state->sw2_on ? "ON" : "OFF",
                          key_state->sw3_on ? "ON" : "OFF",
                          key_state->sw4_on ? "ON" : "OFF");
    if (completed_ir_scans != s_last_ir_scan) {
        lv_label_set_text_fmt(s_ir_status_label, "IR LINKS: %u", link_pairs);
        s_last_ir_scan = completed_ir_scans;
    }
    esp_lv_adapter_unlock();
    return ESP_OK;
}
