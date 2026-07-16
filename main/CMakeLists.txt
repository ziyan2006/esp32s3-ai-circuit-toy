idf_component_register(SRCS "main.c"
                            "wifi.c"
                            "audio_hal.c"
                            "volcengine_ws.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_websocket_client nvs_flash esp_wifi esp_event esp_http_client driver json esp-box-3 lvgl esp_codec_dev)
