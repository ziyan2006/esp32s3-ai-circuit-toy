#pragma once

#include <stdint.h>

#define CAMPAIGN_NODE_MAX_PREREQUISITES 4

typedef enum {
    CAMPAIGN_NODE_COMPLETE = 0,
    CAMPAIGN_NODE_AVAILABLE,
    CAMPAIGN_NODE_LOCKED,
} campaign_node_state_t;

typedef enum {
    CAMPAIGN_NODE_KIND_STANDARD = 0,
    CAMPAIGN_NODE_KIND_REWARD,
} campaign_node_kind_t;

typedef struct {
    uint16_t id;
    uint8_t prerequisite_count;
    uint16_t prerequisite_ids[CAMPAIGN_NODE_MAX_PREREQUISITES];
    int16_t x;
    int16_t y;
    campaign_node_state_t state;
    const char *title;
    const char *goal;
    const char *gate_reward;
    const char *ship_reward;
    campaign_node_kind_t kind;
} campaign_node_t;

typedef struct {
    int32_t world_width;
    int32_t world_height;
    uint16_t initial_level_id;
} campaign_content_meta_t;

const campaign_node_t *campaign_content_nodes(uint16_t *out_count);
campaign_content_meta_t campaign_content_meta(void);
