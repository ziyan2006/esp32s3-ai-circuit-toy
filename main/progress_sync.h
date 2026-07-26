#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "campaign_content.h"
#include "esp_err.h"

typedef enum {
    PROGRESS_SYNC_READY = 0,
    PROGRESS_SYNC_UPLOADING,
    PROGRESS_SYNC_SUCCEEDED,
    PROGRESS_SYNC_NETWORK_UNAVAILABLE,
    PROGRESS_SYNC_CONFIGURATION_ERROR,
    PROGRESS_SYNC_SERVER_REJECTED,
    PROGRESS_SYNC_FAILED,
} progress_sync_state_t;

typedef struct {
    progress_sync_state_t state;
    uint8_t attempt_count;
    uint8_t completed_count;
    int http_status;
} progress_sync_status_t;

esp_err_t progress_sync_init(void);
esp_err_t progress_sync_request(const campaign_node_t *nodes,
                                uint16_t node_count,
                                const bool *completed);
void progress_sync_get_status(progress_sync_status_t *out_status);
