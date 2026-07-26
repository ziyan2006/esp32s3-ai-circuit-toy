#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "campaign_content.h"
#include "esp_err.h"

/* Persistent campaign data is intentionally separate from system settings. */
esp_err_t campaign_progress_init(void);
esp_err_t campaign_progress_load(const campaign_node_t *nodes,
                                 uint16_t node_count,
                                 bool *completed);
esp_err_t campaign_progress_mark_completed(uint16_t level_id);
esp_err_t campaign_progress_clear(void);
