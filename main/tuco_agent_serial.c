#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tuco_agent.h"

static const char *TAG = "tuco_agent_serial";

#if CONFIG_TUCO_AGENT_SERIAL_CONSOLE
static void serial_agent_task(void *arg)
{
    char line[384];
    size_t line_len = 0U;
    (void)arg;
    ESP_LOGI(TAG, "serial test ready; type: /agent <Chinese question>");
    for (;;) {
        uint8_t input[64];
        const int read = usb_serial_jtag_read_bytes(input, sizeof(input), pdMS_TO_TICKS(100));
        for (int index = 0; index < read; ++index) {
            const char ch = (char)input[index];
            if (ch == '\r' || ch == '\n') {
                if (line_len == 0U) continue;
                line[line_len] = '\0';
                line_len = 0U;
            } else if (line_len + 1U < sizeof(line)) {
                line[line_len++] = ch;
                continue;
            } else {
                line_len = 0U;
                ESP_LOGW(TAG, "serial command too long; discarded");
                continue;
            }
            if (line[0] == '\0') continue;
            if (strncmp(line, "/agent ", 7U) != 0 || line[7] == '\0') continue;

            uint32_t request_id = 0U;
            const esp_err_t submit = tuco_agent_submit(line + 7U, 101U, &request_id);
            if (submit != ESP_OK) {
                ESP_LOGW(TAG, "serial request rejected: %s", esp_err_to_name(submit));
                continue;
            }
            ESP_LOGI(TAG, "serial request=%lu submitted", (unsigned long)request_id);
            const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(50000);
            char reply[512];
            for (;;) {
                const esp_err_t result = tuco_agent_take_result(request_id, reply, sizeof(reply));
                if (result != ESP_ERR_NOT_FOUND) {
                    ESP_LOGI(TAG, "serial reply request=%lu result=%s text=%s",
                             (unsigned long)request_id, esp_err_to_name(result), reply);
                    break;
                }
                if (xTaskGetTickCount() >= deadline) {
                    tuco_agent_cancel(request_id);
                    ESP_LOGW(TAG, "serial request=%lu timed out", (unsigned long)request_id);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}
#endif

void tuco_agent_serial_start(void)
{
#if CONFIG_TUCO_AGENT_SERIAL_CONSOLE
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (!usb_serial_jtag_is_driver_installed() &&
        usb_serial_jtag_driver_install(&config) != ESP_OK) {
        ESP_LOGW(TAG, "could not initialize USB Serial/JTAG input");
        return;
    }
    if (tuco_agent_is_configured() &&
        xTaskCreate(serial_agent_task, "tuco_agent_cli", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "could not create serial test task");
    }
#endif
}
