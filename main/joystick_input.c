#include "joystick_input.h"

#include "driver/gpio.h"
#include "esp_log.h"

/* Current board wiring: right=GPIO2, with the other directions unchanged. */
#define JOYSTICK_UP_GPIO       GPIO_NUM_36
#define JOYSTICK_DOWN_GPIO     GPIO_NUM_48
#define JOYSTICK_LEFT_GPIO     GPIO_NUM_23
#define JOYSTICK_RIGHT_GPIO    GPIO_NUM_2

static const char *TAG = "joystick_input";

esp_err_t joystick_input_init(void)
{
    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << JOYSTICK_UP_GPIO) |
                        (1ULL << JOYSTICK_DOWN_GPIO) |
                        (1ULL << JOYSTICK_LEFT_GPIO) |
                        (1ULL << JOYSTICK_RIGHT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&input_config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Logical directions: UP=%d DOWN=%d LEFT=%d RIGHT=%d",
                 JOYSTICK_UP_GPIO,
                 JOYSTICK_DOWN_GPIO,
                 JOYSTICK_LEFT_GPIO,
                 JOYSTICK_RIGHT_GPIO);
    }
    return err;
}

joystick_input_state_t joystick_input_read(void)
{
    return (joystick_input_state_t) {
        .up_pressed = gpio_get_level(JOYSTICK_UP_GPIO) == 0,
        .down_pressed = gpio_get_level(JOYSTICK_DOWN_GPIO) == 0,
        .left_pressed = gpio_get_level(JOYSTICK_LEFT_GPIO) == 0,
        .right_pressed = gpio_get_level(JOYSTICK_RIGHT_GPIO) == 0,
    };
}
