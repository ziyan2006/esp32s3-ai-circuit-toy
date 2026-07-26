#include "app_fonts.h"
#include "app_ui_font_cn_16.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "themes/default/lv_theme_default.h"

static const char *TAG = "app_fonts";
static const lv_font_t *s_body_font;
static const lv_font_t *s_title_font;
static bool s_fonts_ready;

static void app_fonts_apply_theme(lv_display_t *disp, const lv_font_t *font)
{
    lv_theme_t *theme = lv_theme_default_init(
        disp,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        false,
        font);
    lv_display_set_theme(disp, theme);
}

void app_fonts_init(lv_display_t *disp)
{
    int64_t start_us;

    if (disp == NULL) {
        ESP_LOGW(TAG, "Display is NULL, skip font init");
        return;
    }

    if (s_fonts_ready) {
        app_fonts_apply_theme(disp, s_body_font);
        return;
    }

    start_us = esp_timer_get_time();
    s_body_font = &app_ui_font_cn_16;
    s_title_font = s_body_font;

    app_fonts_apply_theme(disp, s_body_font);
    s_fonts_ready = true;
    ESP_LOGI(TAG, "Static UI Chinese font ready in %lld ms", (esp_timer_get_time() - start_us) / 1000);
}

const lv_font_t *app_fonts_get_body_font(void)
{
    return s_body_font ? s_body_font : LV_FONT_DEFAULT;
}

bool app_fonts_are_ready(void)
{
    return s_fonts_ready;
}

const lv_font_t *app_fonts_get_title_font(void)
{
    return s_title_font ? s_title_font : app_fonts_get_body_font();
}
