#ifndef APP_FONTS_H
#define APP_FONTS_H

#include "lvgl.h"

void app_fonts_init(lv_display_t *disp);
bool app_fonts_are_ready(void);
const lv_font_t *app_fonts_get_body_font(void);
const lv_font_t *app_fonts_get_title_font(void);

#endif
