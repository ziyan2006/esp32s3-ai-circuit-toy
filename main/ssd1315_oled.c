#include "ssd1315_oled.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "ssd1315_success_font_24.h"

#define OLED_WIDTH             128
#define OLED_HEIGHT            64
#define OLED_PAGE_COUNT        (OLED_HEIGHT / 8)
#define OLED_BUFFER_SIZE       (OLED_WIDTH * OLED_PAGE_COUNT)
#define OLED_DATA_CHUNK        128
#define OLED_TIMEOUT_TICKS     pdMS_TO_TICKS(100)

static void set_pixel(uint8_t *frame, int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    const size_t offset = (size_t)(y / 8) * OLED_WIDTH + (size_t)x;
    const uint8_t mask = (uint8_t)(1U << (y % 8));
    if (on) {
        frame[offset] |= mask;
    } else {
        frame[offset] &= (uint8_t)~mask;
    }
}

static void draw_line(uint8_t *frame, int x0, int y0, int x1, int y1)
{
    const int dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
    const int sx = (x0 < x1) ? 1 : -1;
    const int dy = (y0 < y1) ? (y1 - y0) : (y0 - y1);
    const int sy = (y0 < y1) ? 1 : -1;
    int error = dx - dy;

    for (;;) {
        set_pixel(frame, x0, y0, true);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice_error = error * 2;
        if (twice_error > -dy) {
            error -= dy;
            x0 += sx;
        }
        if (twice_error < dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_hline(uint8_t *frame, int x0, int x1, int y)
{
    for (int x = x0; x <= x1; ++x) {
        set_pixel(frame, x, y, true);
    }
}

static void draw_vline(uint8_t *frame, int x, int y0, int y1)
{
    for (int y = y0; y <= y1; ++y) {
        set_pixel(frame, x, y, true);
    }
}

static void draw_circle(uint8_t *frame, int cx, int cy, int radius)
{
    int x = radius;
    int y = 0;
    int error = 1 - radius;

    while (x >= y) {
        set_pixel(frame, cx + x, cy + y, true);
        set_pixel(frame, cx + y, cy + x, true);
        set_pixel(frame, cx - y, cy + x, true);
        set_pixel(frame, cx - x, cy + y, true);
        set_pixel(frame, cx - x, cy - y, true);
        set_pixel(frame, cx - y, cy - x, true);
        set_pixel(frame, cx + y, cy - x, true);
        set_pixel(frame, cx + x, cy - y, true);
        ++y;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            --x;
            error += 2 * (y - x) + 1;
        }
    }
}

static const uint8_t *glyph_for(char character)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t glyphs[][5] = {
        ['A' - 'A'] = {0x1E, 0x05, 0x05, 0x1E, 0x00},
        ['B' - 'A'] = {0x1F, 0x15, 0x15, 0x0A, 0x00},
        ['C' - 'A'] = {0x0E, 0x11, 0x11, 0x0A, 0x00},
        ['D' - 'A'] = {0x1F, 0x11, 0x11, 0x0E, 0x00},
        ['E' - 'A'] = {0x1F, 0x15, 0x15, 0x11, 0x00},
        ['I' - 'A'] = {0x11, 0x1F, 0x11, 0x00, 0x00},
        ['L' - 'A'] = {0x1F, 0x10, 0x10, 0x10, 0x00},
        ['N' - 'A'] = {0x1F, 0x02, 0x04, 0x1F, 0x00},
        ['O' - 'A'] = {0x0E, 0x11, 0x11, 0x0E, 0x00},
        ['P' - 'A'] = {0x1F, 0x05, 0x05, 0x02, 0x00},
        ['R' - 'A'] = {0x1F, 0x05, 0x0D, 0x12, 0x00},
        ['T' - 'A'] = {0x01, 0x1F, 0x01, 0x00, 0x00},
        ['U' - 'A'] = {0x0F, 0x10, 0x10, 0x0F, 0x00},
        ['X' - 'A'] = {0x1B, 0x04, 0x04, 0x1B, 0x00},
        ['Y' - 'A'] = {0x03, 0x04, 0x18, 0x04, 0x03},
    };
    static const uint8_t digit_glyphs[][5] = {
        {0x0E, 0x11, 0x13, 0x15, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x06, 0x08, 0x1F},
        {0x1F, 0x02, 0x06, 0x11, 0x0E},
    };

    if (character < 'A' || character > 'Z') {
        if (character >= '0' && character <= '3') {
            return digit_glyphs[character - '0'];
        }
        return blank;
    }
    return glyphs[character - 'A'];
}

static void draw_label(uint8_t *frame, const char *label)
{
    size_t length = strlen(label);
    const int text_width = (int)(length * 6U - (length > 0U ? 1U : 0U));
    int x = (OLED_WIDTH - text_width) / 2;

    for (size_t index = 0; index < length; ++index) {
        const uint8_t *glyph = glyph_for(label[index]);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 5; ++row) {
                if ((glyph[column] & (1U << row)) != 0U) {
                    set_pixel(frame, x + column, 54 + row, true);
                }
            }
        }
        x += 6;
    }
}

static void draw_bitmap_16x16(uint8_t *frame, const uint16_t bitmap[16], int x, int y)
{
    for (int row = 0; row < 16; ++row) {
        for (int column = 0; column < 16; ++column) {
            if ((bitmap[row] & (uint16_t)(1U << (15 - column))) != 0U) {
                set_pixel(frame, x + column, y + row, true);
            }
        }
    }
}

static void draw_role_ascii(uint8_t *frame, char character)
{
    const uint8_t *glyph = glyph_for(character);
    for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 5; ++row) {
            if ((glyph[column] & (1U << row)) == 0U) continue;
            const int x = 2 + column * 2;
            const int y = 2 + row * 2;
            set_pixel(frame, x, y, true);
            set_pixel(frame, x + 1, y, true);
            set_pixel(frame, x, y + 1, true);
            set_pixel(frame, x + 1, y + 1, true);
        }
    }
}

static void draw_role_label(uint8_t *frame, const char *role_label)
{
    if (role_label == NULL || role_label[0] == '\0') return;

    static const uint16_t ge_bitmap[16] = {
        0x0000, 0x0000, 0x0100, 0x0280,
        0x0640, 0x0C20, 0x3010, 0x610C,
        0x0100, 0x0100, 0x0100, 0x0100,
        0x0100, 0x0100, 0x0000, 0x0000,
    };
    static const uint16_t jin_bitmap[16] = {
        0x0000, 0x0000, 0x2120, 0x1120,
        0x17F8, 0x0120, 0x7120, 0x1120,
        0x17F8, 0x1120, 0x1320, 0x1620,
        0x2800, 0x4FF8, 0x0000, 0x0000,
    };
    static const uint16_t xuan_bitmap[16] = {
        0x0000, 0x0000, 0x2140, 0x1340,
        0x0BF8, 0x0440, 0x7040, 0x17FC,
        0x10A0, 0x10A4, 0x11A4, 0x133C,
        0x2800, 0x47FC, 0x0000, 0x0000,
    };

    if (strcmp(role_label, "个") == 0) {
        draw_bitmap_16x16(frame, ge_bitmap, 0, 0);
    } else if (strcmp(role_label, "进") == 0) {
        draw_bitmap_16x16(frame, jin_bitmap, 0, 0);
    } else if (strcmp(role_label, "选") == 0) {
        draw_bitmap_16x16(frame, xuan_bitmap, 0, 0);
    } else {
        draw_role_ascii(frame, role_label[0]);
    }
}

static void draw_gate_symbol(uint8_t *frame, ssd1315_gate_t gate)
{
    const int input_x = 16;
    const int left_x = 42;
    const int right_x = 88;
    const int top_y = 7;
    const int middle_y = 28;
    const int bottom_y = 49;

    if (gate == SSD1315_GATE_NULL) {
        return;
    }
    if (gate == SSD1315_GATE_INPUT) {
        draw_hline(frame, 25, 80, middle_y);
        draw_line(frame, 80, middle_y, 72, middle_y - 5);
        draw_line(frame, 80, middle_y, 72, middle_y + 5);
        draw_vline(frame, 23, 20, 36);
        draw_hline(frame, 19, 27, 20);
        draw_hline(frame, 19, 27, 36);
        draw_vline(frame, 19, 20, 36);
        return;
    }
    if (gate == SSD1315_GATE_OUTPUT) {
        draw_hline(frame, 23, 50, middle_y);
        draw_line(frame, 50, middle_y, 68, top_y + 4);
        draw_line(frame, 50, middle_y, 68, bottom_y - 4);
        draw_line(frame, 68, top_y + 4, 86, middle_y);
        draw_line(frame, 86, middle_y, 68, bottom_y - 4);
        draw_hline(frame, 86, 104, middle_y);
        draw_circle(frame, 109, middle_y, 5);
        return;
    }

    if (gate == SSD1315_GATE_NOT) {
        draw_hline(frame, input_x, left_x, middle_y);
        draw_line(frame, left_x, top_y, left_x, bottom_y);
        draw_line(frame, left_x, top_y, right_x - 7, middle_y);
        draw_line(frame, left_x, bottom_y, right_x - 7, middle_y);
        draw_circle(frame, right_x, middle_y, 4);
        draw_hline(frame, right_x + 5, 109, middle_y);
        return;
    }

    draw_hline(frame, input_x, left_x, 18);
    draw_hline(frame, input_x, left_x, 38);

    if (gate == SSD1315_GATE_AND || gate == SSD1315_GATE_NAND) {
        draw_vline(frame, left_x, top_y, bottom_y);
        draw_hline(frame, left_x, 65, top_y);
        draw_hline(frame, left_x, 65, bottom_y);
        for (int y = top_y; y <= bottom_y; ++y) {
            const int dy = y - middle_y;
            const int x = 65 + (int)(20.0f - (float)(dy * dy) / 44.0f);
            set_pixel(frame, x, y, true);
        }
        if (gate == SSD1315_GATE_NAND) {
            draw_circle(frame, 88, middle_y, 4);
            draw_hline(frame, 93, 109, middle_y);
        } else {
            draw_hline(frame, 86, 109, middle_y);
        }
        return;
    }

    const int curve_offset = (gate == SSD1315_GATE_XOR || gate == SSD1315_GATE_XNOR) ? 5 : 0;
    if (curve_offset != 0) {
        for (int y = top_y; y <= bottom_y; ++y) {
            const int dy = y - middle_y;
            set_pixel(frame, left_x - 6 - (dy * dy) / 80, y, true);
        }
    }
    for (int y = top_y; y <= bottom_y; ++y) {
        const int dy = y - middle_y;
        const int left_curve = left_x + 4 - (dy * dy) / 80;
        const int right_curve = 76 + (dy * dy) / 70;
        set_pixel(frame, left_curve, y, true);
        set_pixel(frame, right_curve, y, true);
    }
    draw_line(frame, left_x + 4, top_y, 68, top_y);
    draw_line(frame, 68, top_y, 86, middle_y);
    draw_line(frame, 86, middle_y, 68, bottom_y);
    draw_line(frame, 68, bottom_y, left_x + 4, bottom_y);
    if (gate == SSD1315_GATE_NOR || gate == SSD1315_GATE_XNOR) {
        draw_circle(frame, 91, middle_y, 4);
        draw_hline(frame, 96, 109, middle_y);
    } else {
        draw_hline(frame, 87, 109, middle_y);
    }
}

static void draw_success_glyphs(uint8_t *frame)
{
    const int start_x = 4;
    const int start_y = (OLED_HEIGHT - SSD1315_SUCCESS_FONT_HEIGHT) / 2;

    for (uint8_t glyph = 0; glyph < SSD1315_SUCCESS_FONT_GLYPH_COUNT; ++glyph) {
        for (uint8_t row = 0; row < SSD1315_SUCCESS_FONT_HEIGHT; ++row) {
            for (uint8_t column = 0; column < SSD1315_SUCCESS_FONT_WIDTH; ++column) {
                const size_t offset = (size_t)row * (SSD1315_SUCCESS_FONT_WIDTH / 8U) + (column / 8U);
                const uint8_t bit = (uint8_t)(1U << (7U - (column % 8U)));
                if ((ssd1315_success_font_24[glyph][offset] & bit) != 0U) {
                    set_pixel(frame, start_x + glyph * SSD1315_SUCCESS_FONT_WIDTH + column,
                              start_y + row, true);
                }
            }
        }
    }
}

static esp_err_t send_commands(i2c_master_dev_handle_t device, const uint8_t *commands, size_t length)
{
    uint8_t buffer[32];
    if (length + 1U > sizeof(buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[0] = 0x00;
    memcpy(&buffer[1], commands, length);
    return i2c_master_transmit(device, buffer, length + 1U, OLED_TIMEOUT_TICKS);
}

static esp_err_t send_data(i2c_master_dev_handle_t device, const uint8_t *data, size_t length)
{
    uint8_t buffer[1 + OLED_DATA_CHUNK];
    if (length > OLED_DATA_CHUNK) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[0] = 0x40;
    memcpy(&buffer[1], data, length);
    return i2c_master_transmit(device, buffer, length + 1U, OLED_TIMEOUT_TICKS);
}

static esp_err_t write_frame(i2c_master_dev_handle_t device, const uint8_t *frame)
{
    const uint8_t position[] = {0x21, 0x00, 0x7F, 0x22, 0x00, OLED_PAGE_COUNT - 1U};
    esp_err_t err = send_commands(device, position, sizeof(position));
    if (err != ESP_OK) return err;

    for (size_t offset = 0; offset < OLED_BUFFER_SIZE; offset += OLED_DATA_CHUNK) {
        err = send_data(device, &frame[offset], OLED_DATA_CHUNK);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ssd1315_oled_init(i2c_master_dev_handle_t device)
{
    static const uint8_t init_sequence[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F,
        0xD9, 0xF1, 0xDB, 0x40, 0x8D, 0x14, 0xA4, 0xA6,
        0xAF,
    };
    return send_commands(device, init_sequence, sizeof(init_sequence));
}

esp_err_t ssd1315_oled_show_gate(i2c_master_dev_handle_t device, ssd1315_gate_t gate)
{
    return ssd1315_oled_show_gate_with_role(device, gate, NULL);
}

esp_err_t ssd1315_oled_show_gate_with_role(i2c_master_dev_handle_t device,
                                           ssd1315_gate_t gate,
                                           const char *role_label)
{
    uint8_t frame[OLED_BUFFER_SIZE] = {0};
    draw_gate_symbol(frame, gate);
    draw_label(frame, ssd1315_gate_name(gate));
    draw_role_label(frame, role_label);
    return write_frame(device, frame);
}

esp_err_t ssd1315_oled_show_success(i2c_master_dev_handle_t device)
{
    uint8_t frame[OLED_BUFFER_SIZE] = {0};
    draw_success_glyphs(frame);
    return write_frame(device, frame);
}

bool ssd1315_gate_from_eeprom_id(uint8_t id, ssd1315_gate_t *gate)
{
    const ssd1315_gate_t decoded = (id == 0xF0U) ? SSD1315_GATE_INPUT :
                                        (id == 0xF1U) ? SSD1315_GATE_OUTPUT :
                                        (id == 4U) ? SSD1315_GATE_NOT :
                                        (id == 5U) ? SSD1315_GATE_AND :
                                        (id == 6U) ? SSD1315_GATE_OR :
                                        (id == 7U) ? SSD1315_GATE_NAND :
                                        (id == 8U) ? SSD1315_GATE_NOR :
                                        (id == 9U) ? SSD1315_GATE_XOR :
                                        (id == 10U) ? SSD1315_GATE_XNOR : SSD1315_GATE_NULL;
    if (decoded == SSD1315_GATE_NULL) {
        return false;
    }
    if (gate != NULL) {
        *gate = decoded;
    }
    return true;
}

uint8_t ssd1315_gate_to_eeprom_id(ssd1315_gate_t gate)
{
    static const uint8_t ids[] = {0xF0U, 0xF1U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    return gate < SSD1315_GATE_NULL ? ids[gate] : 0xFFU;
}

ssd1315_gate_t ssd1315_gate_next(ssd1315_gate_t gate, int direction)
{
    static const ssd1315_gate_t order[] = {
        SSD1315_GATE_INPUT,
        SSD1315_GATE_OUTPUT,
        SSD1315_GATE_NAND,
        SSD1315_GATE_NOT,
        SSD1315_GATE_AND,
        SSD1315_GATE_OR,
        SSD1315_GATE_NOR,
        SSD1315_GATE_XOR,
        SSD1315_GATE_XNOR,
    };
    const int count = (int)(sizeof(order) / sizeof(order[0]));
    int index = 0;
    for (int candidate = 0; candidate < count; ++candidate) {
        if (order[candidate] == gate) {
            index = candidate;
            break;
        }
    }
    index = (index + (direction < 0 ? count - 1 : 1)) % count;
    return order[index];
}

const char *ssd1315_gate_name(ssd1315_gate_t gate)
{
    static const char *const names[] = {
        "INPUT", "OUTPUT", "NOT", "AND", "OR", "NAND", "NOR", "XOR", "XNOR", "NULL",
    };
    return gate <= SSD1315_GATE_NULL ? names[gate] : names[SSD1315_GATE_NULL];
}
