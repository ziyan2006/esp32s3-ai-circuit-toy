#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Starts ESP-Hosted/C6 Wi-Fi diagnostics and keeps the remote interface available. */
esp_err_t c6_network_test_start(void);

/* The remote Wi-Fi interface remains active after the one-shot diagnostics task exits. */
bool c6_network_is_connected(void);
