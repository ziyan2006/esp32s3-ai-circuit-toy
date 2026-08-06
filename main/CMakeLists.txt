set(SHOOTER_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../shooter_simulator/src")

idf_component_register(
    SRCS "main.c" "key_input.c" "joystick_input.c" "app_ui.c" "campaign_content.c" "campaign_progress.c" "progress_sync.c" "assistant_mode.c" "assistant_diagnostics.c" "assistant_error_latch.c" "assistant_router.c" "remote_assistant.c" "tuco_agent.c" "tuco_agent_serial.c" "tuco_port_highlight.c" "app_ui_font_cn_16.c" "app_ui_font_cn_24.c" "shooter_font_cn_16.c" "shooter_assets.c" "shooter_game.c" "baseboard_ir.c" "block_i2c.c" "ssd1315_oled.c" "audio_self_test.c" "play_mode.c" "board_mapping.c" "board_snapshot.c" "level_rules.c" "level_intro.c" "learning_activity.c" "circuit_logic.c" "circuit_debug.c" "circuit_layout.c" "game_judge.c" "game_logic_self_test.c" "c6_network_test.c" "volcengine_voice.c"
         "${SHOOTER_SRC_DIR}/shooter_boss.c"
         "${SHOOTER_SRC_DIR}/shooter_core.c"
         "${SHOOTER_SRC_DIR}/shooter_core_self_test.c"
         "${SHOOTER_SRC_DIR}/shooter_renderer_lvgl.c"
         "${SHOOTER_SRC_DIR}/shooter_stage.c"
         "${SHOOTER_SRC_DIR}/shooter_wave.c"
    INCLUDE_DIRS "." "${SHOOTER_SRC_DIR}"
    REQUIRES driver esp_hw_support esp_timer esp_event esp_netif esp_wifi nvs_flash esp_partition json mbedtls esp_http_client lvgl espressif__esp_lvgl_adapter espressif__esp32_p4_function_ev_board_noglib espressif__esp_codec_dev espressif__esp_wifi_remote espressif__esp_hosted espressif__esp_websocket_client espressif__zlib claw_core
    EMBED_FILES
        "assets/shooter/player_ship.bin"
        "assets/shooter/enemy_ship.bin"
        "assets/shooter/laser_bullet.bin"
        "assets/shooter/planet_00.bin"
        "assets/shooter/planet_01.bin"
        "assets/shooter/planet_02.bin"
        "assets/shooter/planet_03.bin"
        "assets/shooter/planet_04.bin"
        "assets/shooter/planet_05.bin"
        "assets/shooter/boss_ship.bin"
        "assets/shooter/elite_bruiser.bin"
        "assets/shooter/laser_drone.bin"
)

# ESP32-P4 Function EV Board uses its on-board ESP32-C6 over 4-bit SDIO.
# Stop the build if menuconfig silently selects an incompatible Hosted target.
if(NOT CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6
   OR NOT CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD
   OR NOT CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
   OR CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE)
    message(FATAL_ERROR
        "Invalid ESP-Hosted configuration: this project requires ESP32-C6 + "
        "ESP32-P4 Function EV Board + SDIO (SPI must be disabled).")
endif()

if(NOT "${CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH}" STREQUAL "4"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ}" STREQUAL "40000"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_CMD}" STREQUAL "19"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_CLK}" STREQUAL "18"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D0}" STREQUAL "14"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D1}" STREQUAL "15"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D2}" STREQUAL "16"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_PIN_D3}" STREQUAL "17"
   OR NOT "${CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE}" STREQUAL "54")
    message(FATAL_ERROR
        "Invalid ESP-Hosted SDIO pins/settings for ESP32-P4 Function EV Board. "
        "Expected 4-bit/40MHz, CMD=19 CLK=18 D0..D3=14..17 RESET=54.")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE SHOOTER_EMBEDDED=1 SHOOTER_PLANET_COUNT=6)
