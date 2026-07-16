#include <stdio.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP32-S3-BOX-3 BSP and UI library
#include "bsp/esp-box-3.h"
#include "lvgl.h"

#include "config.h"
#include "wifi.h"
#include "audio_hal.h"
#include "volcengine_ws.h"

static const char *TAG = "MAIN_APP";

#define TALK_BUTTON_GPIO GPIO_NUM_0  // ESP32-S3-BOX-3 BOOT Button
#define DEFAULT_VOLUME_PERCENT 70
#define VOLUME_STEP_PERCENT 5
#define MIN_SPEECH_FRAMES 10
#define ASR_ACTIVITY_TIMEOUT_MS 4000

// LVGL Label handle to show statuses on screen
static lv_obj_t *s_wifi_status_label = NULL;
static lv_obj_t *s_volume_slider = NULL;
static lv_obj_t *s_volume_value_label = NULL;

static int normalize_volume(int value)
{
    int normalized = ((value + VOLUME_STEP_PERCENT / 2) / VOLUME_STEP_PERCENT) * VOLUME_STEP_PERCENT;
    if (normalized < 0) {
        return 0;
    }
    if (normalized > 100) {
        return 100;
    }
    return normalized;
}

static void volume_slider_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_obj_t *slider = lv_event_get_target(event);
    int volume = normalize_volume(lv_slider_get_value(slider));
    lv_slider_set_value(slider, volume, LV_ANIM_OFF);

    if (s_volume_value_label != NULL) {
        char value_text[8];
        snprintf(value_text, sizeof(value_text), "%d%%", volume);
        lv_label_set_text(s_volume_value_label, value_text);
    }

    esp_err_t ret = audio_hal_set_volume(volume);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set output volume to %d%%: %s", volume, esp_err_to_name(ret));
    }
}

static void ui_create_volume_control(void)
{
    if (!bsp_display_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock display while creating volume control");
        return;
    }

    lv_obj_t *volume_label = lv_label_create(lv_scr_act());
    lv_label_set_text(volume_label, "VOL");
    lv_obj_align(volume_label, LV_ALIGN_BOTTOM_LEFT, 12, -15);
    lv_obj_set_style_text_color(volume_label, lv_color_make(190, 195, 205), 0);
    lv_obj_set_style_text_font(volume_label, &lv_font_montserrat_14, 0);

    s_volume_slider = lv_slider_create(lv_scr_act());
    lv_obj_set_size(s_volume_slider, 210, 10);
    lv_obj_align(s_volume_slider, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_slider_set_range(s_volume_slider, 0, 100);
    lv_slider_set_value(s_volume_slider, DEFAULT_VOLUME_PERCENT, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_make(65, 68, 78), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_volume_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_volume_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_make(48, 142, 255), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_make(238, 244, 255), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_volume_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_volume_slider, 7, LV_PART_KNOB);
    lv_obj_add_event_cb(s_volume_slider, volume_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_volume_value_label = lv_label_create(lv_scr_act());
    lv_label_set_text(s_volume_value_label, "70%");
    lv_obj_set_width(s_volume_value_label, 42);
    lv_obj_align(s_volume_value_label, LV_ALIGN_BOTTOM_RIGHT, -8, -15);
    lv_obj_set_style_text_align(s_volume_value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_volume_value_label, lv_color_make(238, 244, 255), 0);
    lv_obj_set_style_text_font(s_volume_value_label, &lv_font_montserrat_14, 0);

    bsp_display_unlock();
}

void ui_update_status(const char *status)
{
    if (s_wifi_status_label != NULL) {
        bsp_display_lock(0);
        lv_label_set_text(s_wifi_status_label, status);
        bsp_display_unlock();
    }
}

static void talk_control_task(void *pvParameters)
{
    // Allocate 1024 bytes buffer (approx. 32ms of mono 16kHz 16-bit PCM)
    uint8_t *rec_buffer = malloc(1024);
    if (rec_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer.");
        vTaskDelete(NULL);
    }

    // Configure BOOT button (GPIO 0)
    gpio_config_t btn_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << TALK_BUTTON_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&btn_conf);

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Talk Control Task Started.");
    ESP_LOGI(TAG, " Hold BOOT button (GPIO 0) to talk, release to send.");
    ESP_LOGI(TAG, "==================================================");

    bool is_recording = false;
    size_t recorded_bytes = 0;
    size_t sent_bytes = 0;
    uint32_t recorded_frames = 0;
    esp_err_t first_send_error = ESP_OK;
    uint64_t sample_abs_sum = 0;
    uint32_t sample_count = 0;
    uint32_t sample_peak = 0;
    bool awaiting_asr_activity = false;
    TickType_t asr_wait_started = 0;

    while (1) {
        if (awaiting_asr_activity) {
            if (volcengine_ws_has_asr_activity()) {
                awaiting_asr_activity = false;
            } else if ((xTaskGetTickCount() - asr_wait_started) >= pdMS_TO_TICKS(ASR_ACTIVITY_TIMEOUT_MS)) {
                awaiting_asr_activity = false;
                ESP_LOGW(TAG, "No ASR activity received within %d ms", ASR_ACTIVITY_TIMEOUT_MS);
                esp_err_t cancel_ret = volcengine_ws_cancel_session();
                if (cancel_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to cancel ASR timeout session: %s",
                             esp_err_to_name(cancel_ret));
                }
                ui_update_status("No speech recognized.\nPlease try again.");
            }
        }

        // BOOT Button is active low
        if (gpio_get_level(TALK_BUTTON_GPIO) == 0) {
            if (awaiting_asr_activity) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            if (!is_recording) {
                ui_update_status("Starting AI session...");
                esp_err_t session_ret = volcengine_ws_prepare_session();
                if (session_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to prepare AI session: %s", esp_err_to_name(session_ret));
                    ui_update_status("AI session start failed.\nRelease and try again.");
                    vTaskDelay(pdMS_TO_TICKS(200));
                    continue;
                }
                is_recording = true;
                recorded_bytes = 0;
                sent_bytes = 0;
                recorded_frames = 0;
                first_send_error = ESP_OK;
                sample_abs_sum = 0;
                sample_count = 0;
                sample_peak = 0;
                ui_update_status(">>> Recording...");
                ESP_LOGI(TAG, "[PTT] >>> Recording started... Speak now!");
            }

            size_t bytes_read = 0;
            // Capture audio frame from Codec
            esp_err_t ret = audio_record_frame(rec_buffer, 1024, &bytes_read, pdMS_TO_TICKS(100));
            if (ret == ESP_OK && bytes_read > 0) {
                recorded_frames++;
                recorded_bytes += bytes_read;
                const int16_t *samples = (const int16_t *)rec_buffer;
                size_t frame_sample_count = bytes_read / sizeof(int16_t);
                for (size_t sample_index = 0; sample_index < frame_sample_count; sample_index++) {
                    int32_t sample = samples[sample_index];
                    uint32_t magnitude = sample < 0 ? (uint32_t)(-sample) : (uint32_t)sample;
                    sample_abs_sum += magnitude;
                    sample_count++;
                    if (magnitude > sample_peak) {
                        sample_peak = magnitude;
                    }
                }
                if (volcengine_ws_is_connected()) {
                    esp_err_t send_ret = volcengine_ws_send_audio(rec_buffer, bytes_read);
                    if (send_ret == ESP_OK) {
                        sent_bytes += bytes_read;
                    } else if (first_send_error == ESP_OK) {
                        first_send_error = send_ret;
                        ESP_LOGE(TAG, "First audio send failed: %s", esp_err_to_name(send_ret));
                    }
                }
            } else if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Audio recording failed: %s", esp_err_to_name(ret));
            }
        } 
        else {
            if (is_recording) {
                is_recording = false;
                uint32_t mean_abs = sample_count > 0 ? (uint32_t)(sample_abs_sum / sample_count) : 0;
                ESP_LOGI(TAG,
                         "[PTT] <<< Recording stopped. frames=%lu recorded=%u sent=%u mean_abs=%lu peak=%lu first_send_error=%s",
                         (unsigned long)recorded_frames,
                         (unsigned int)recorded_bytes,
                         (unsigned int)sent_bytes,
                         (unsigned long)mean_abs,
                         (unsigned long)sample_peak,
                         esp_err_to_name(first_send_error));

                bool recording_too_short = recorded_frames < MIN_SPEECH_FRAMES;
                bool audio_send_failed = sent_bytes == 0 || first_send_error != ESP_OK;
                if (recording_too_short || audio_send_failed) {
                    ESP_LOGW(TAG,
                             "Discarding invalid speech turn: too_short=%d send_failed=%d",
                             recording_too_short, audio_send_failed);
                    esp_err_t cancel_ret = volcengine_ws_cancel_session();
                    if (cancel_ret != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to cancel invalid speech session: %s",
                                 esp_err_to_name(cancel_ret));
                    }
                    ui_update_status(recording_too_short
                                         ? "Too short.\nHold BOOT longer and try again."
                                         : "No clear speech detected.\nPlease try again.");
                } else if (volcengine_ws_is_connected()) {
                    esp_err_t commit_ret = volcengine_ws_commit_and_respond();
                    if (commit_ret == ESP_OK) {
                        ui_update_status("<<< Thinking...");
                        if (!volcengine_ws_has_asr_activity()) {
                            awaiting_asr_activity = true;
                            asr_wait_started = xTaskGetTickCount();
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed to request AI response: %s", esp_err_to_name(commit_ret));
                        ui_update_status("Request failed.\nPlease try again.");
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(500)); // debounce delay
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // poll rate
        }
    }

    free(rec_buffer);
}

void app_main(void)
{
    // Initialize the display using the esp-box-3 Board Support Package
    bsp_display_start();
    
    // Set backlight brightness to 100%
    bsp_display_brightness_set(100);
    
    bsp_display_lock(0);
    // Set a premium dark theme background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(24, 24, 32), 0);
    
    // Create status label
    s_wifi_status_label = lv_label_create(lv_scr_act());
    lv_obj_align(s_wifi_status_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(s_wifi_status_label, lv_color_make(240, 240, 240), 0);
    
    // Wrap text and center align
    lv_label_set_long_mode(s_wifi_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_status_label, 300);
    lv_obj_set_style_text_align(s_wifi_status_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Use larger Montserrat font
    lv_obj_set_style_text_font(s_wifi_status_label, &lv_font_montserrat_14, 0);
    
    lv_label_set_text(s_wifi_status_label, "Initializing System...");
    bsp_display_unlock();

    // 1. Initialize NVS (required for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize default Event Loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Connect to Wi-Fi
    char status_buf[128];
    snprintf(status_buf, sizeof(status_buf), "SSID: %s\nConnecting to Wi-Fi...", WIFI_SSID);
    ui_update_status(status_buf);
    
    wifi_init_sta();

    if (wifi_is_connected()) {
        ui_update_status("Wi-Fi Connected!\nConnecting to Volcano Engine...");
    } else {
        ui_update_status("Wi-Fi Connection Failed!\nPlease check 2.4GHz band / SSID.");
        return;
    }

    // 4. Initialize Audio Codec and I2S Channels
    ret = audio_hal_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio HAL initialization failed!");
        ui_update_status("Audio Init Failed!");
        return;
    }

    ret = audio_hal_set_volume(DEFAULT_VOLUME_PERCENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial output volume: %s", esp_err_to_name(ret));
    }
    ui_create_volume_control();

    // 5. Initialize and Connect Volcano Engine WebSocket
    ret = volcengine_ws_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket client initialization failed!");
        ui_update_status("WebSocket Init Failed!");
        return;
    }

    ret = volcengine_ws_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket connection failed!");
        ui_update_status("WebSocket Connect Failed!");
        return;
    }

    // Wait for connection to establish
    int retries = 0;
    while (!volcengine_ws_is_connected() && retries < 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retries++;
    }

    if (volcengine_ws_is_connected()) {
        ui_update_status("Ready!\nHold BOOT button to speak.");
    } else {
        ui_update_status("Volcano Engine Connection Timeout!");
        return;
    }

    // 6. Spawn the Push-to-Talk task
    xTaskCreatePinnedToCore(talk_control_task, "talk_control_task", 4096, NULL, 5, NULL, 0);
}
