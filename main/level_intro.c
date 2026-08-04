#include "level_intro.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "level_intro";

static const level_intro_page_t s_level_101_pages[] = {
    {
        .speaker = "图灵号AI",
        .text = "嗨！我是图灵号的机载 AI 助手。刚才的电磁风暴让我们的飞船停在太空里。",
    },
    {
        .speaker = "图灵号AI",
        .text = "不过别担心，我还醒着！从现在起，你就是我的小小维修员。",
    },
    {
        .speaker = "图灵号AI",
        .text = "第一项任务很简单：放一块输入积木和一块输出积木，再把 A 和 Y 接起来。信号能从 A 走到 Y，主控台就有电啦！",
    },
};

static const level_intro_page_t s_level_401_pages[] = {
    {
        .speaker = "图灵号AI",
        .text = "飞船计算机只认识一种数字：二进制。它只认识 0 和 1。",
    },
    {
        .speaker = "图灵号AI",
        .text = "先看最上方四个槽位：空槽表示 0，放进任意积木表示 1。",
    },
    {
        .speaker = "图灵号AI",
        .text = "从右往左，它们代表 1、2、4、8。先去训练区读出几个数字，再开始做小计算器吧！",
    },
};

static const level_intro_page_t s_level_403_pages[] = {
    {
        .speaker = "图灵号AI",
        .text = "两个二进制数字相加时，答案有两个位置：个位和进位。",
    },
    {
        .speaker = "图灵号AI",
        .text = "左列从上到下是 A、B，右列从上到下是个位、进位。空槽是 0，放入积木是 1。",
    },
    {
        .speaker = "图灵号AI",
        .text = "先放好 A 和 B，再放好答案。看看 1 加 1：个位是 0、进位是 1。",
    },
};

static const level_intro_page_t s_level_501_pages[] = {
    {
        .speaker = "图灵号AI",
        .text = "半加器能把两个二进制数字相加。现在还要把前一位传来的 1 一起算进去。",
    },
    {
        .speaker = "图灵号AI",
        .text = "先只看个位：三个输入里有 1 个或 3 个 1，个位就是 1；有 0 个或 2 个 1，个位就是 0。",
    },
    {
        .speaker = "图灵号AI",
        .text = "左边三个槽位就是三个输入。空槽是 0，放入积木是 1。先去训练区数数有几个 1，再开始组装吧！",
    },
};

static const level_intro_page_t s_level_504_pages[] = {
    {
        .speaker = "图灵号AI",
        .text = "全加器是一个小计算器，可以把三个二进制数字一起相加。",
    },
    {
        .speaker = "图灵号AI",
        .text = "左列从上到下是 A、B、进位输入，右列从上到下是个位、进位输出。空槽是 0，放入积木是 1。",
    },
    {
        .speaker = "图灵号AI",
        .text = "看看 1 加 1 加 1：答案是 11，个位和进位输出都是 1。",
    },
};

uint8_t level_intro_page_count(uint16_t level_id)
{
    switch (level_id) {
    case 101U:
        return (uint8_t)(sizeof(s_level_101_pages) / sizeof(s_level_101_pages[0]));
    case 401U:
        return (uint8_t)(sizeof(s_level_401_pages) / sizeof(s_level_401_pages[0]));
    case 403U:
        return (uint8_t)(sizeof(s_level_403_pages) / sizeof(s_level_403_pages[0]));
    case 501U:
        return (uint8_t)(sizeof(s_level_501_pages) / sizeof(s_level_501_pages[0]));
    case 504U:
        return (uint8_t)(sizeof(s_level_504_pages) / sizeof(s_level_504_pages[0]));
    default:
        return 0U;
    }
}

const level_intro_page_t *level_intro_page_get(uint16_t level_id, uint8_t page_index)
{
    if (page_index >= level_intro_page_count(level_id)) return NULL;
    switch (level_id) {
    case 101U:
        return &s_level_101_pages[page_index];
    case 401U:
        return &s_level_401_pages[page_index];
    case 403U:
        return &s_level_403_pages[page_index];
    case 501U:
        return &s_level_501_pages[page_index];
    case 504U:
        return &s_level_504_pages[page_index];
    default:
        return NULL;
    }
}

esp_err_t level_intro_self_test_run(void)
{
    const level_intro_page_t *first = level_intro_page_get(101U, 0U);
    const level_intro_page_t *last = level_intro_page_get(101U, 2U);
    const level_intro_page_t *binary_first = level_intro_page_get(401U, 0U);
    const level_intro_page_t *binary_last = level_intro_page_get(401U, 2U);
    const level_intro_page_t *half_first = level_intro_page_get(403U, 0U);
    const level_intro_page_t *half_last = level_intro_page_get(403U, 2U);
    const level_intro_page_t *parity_first = level_intro_page_get(501U, 0U);
    const level_intro_page_t *parity_last = level_intro_page_get(501U, 2U);
    const level_intro_page_t *full_first = level_intro_page_get(504U, 0U);
    const level_intro_page_t *full_last = level_intro_page_get(504U, 2U);

    ESP_RETURN_ON_FALSE(level_intro_page_count(101U) == 3U, ESP_FAIL, TAG,
                        "level 101 intro page count mismatch");
    ESP_RETURN_ON_FALSE(level_intro_page_count(102U) == 0U, ESP_FAIL, TAG,
                        "unexpected intro on level 102");
    ESP_RETURN_ON_FALSE(level_intro_page_count(401U) == 3U, ESP_FAIL, TAG,
                        "level 401 intro page count mismatch");
    ESP_RETURN_ON_FALSE(level_intro_page_count(403U) == 3U, ESP_FAIL, TAG,
                        "level 403 intro page count mismatch");
    ESP_RETURN_ON_FALSE(level_intro_page_count(501U) == 3U, ESP_FAIL, TAG,
                        "level 501 intro page count mismatch");
    ESP_RETURN_ON_FALSE(level_intro_page_count(504U) == 3U, ESP_FAIL, TAG,
                        "level 504 intro page count mismatch");
    ESP_RETURN_ON_FALSE(binary_first != NULL && strstr(binary_first->text, "二进制") != NULL,
                        ESP_FAIL, TAG, "level 401 binary introduction mismatch");
    ESP_RETURN_ON_FALSE(binary_last != NULL && strstr(binary_last->text, "1、2、4、8") != NULL,
                        ESP_FAIL, TAG, "level 401 weight guidance mismatch");
    ESP_RETURN_ON_FALSE(half_first != NULL && strstr(half_first->text, "个位和进位") != NULL,
                        ESP_FAIL, TAG, "level 403 half adder introduction mismatch");
    ESP_RETURN_ON_FALSE(half_last != NULL && strstr(half_last->text, "1 加 1") != NULL,
                        ESP_FAIL, TAG, "level 403 carry guidance mismatch");
    ESP_RETURN_ON_FALSE(parity_first != NULL && strstr(parity_first->text, "前一位传来的 1") != NULL,
                        ESP_FAIL, TAG, "level 501 parity introduction mismatch");
    ESP_RETURN_ON_FALSE(parity_last != NULL && strstr(parity_last->text, "数数有几个 1") != NULL,
                        ESP_FAIL, TAG, "level 501 parity activity guidance mismatch");
    ESP_RETURN_ON_FALSE(full_first != NULL && strstr(full_first->text, "三个二进制数字") != NULL,
                        ESP_FAIL, TAG, "level 504 full adder introduction mismatch");
    ESP_RETURN_ON_FALSE(full_last != NULL && strstr(full_last->text, "1 加 1 加 1") != NULL,
                        ESP_FAIL, TAG, "level 504 carry guidance mismatch");
    ESP_RETURN_ON_FALSE(first != NULL && strstr(first->text, "图灵号的机载 AI 助手") != NULL,
                        ESP_FAIL, TAG, "level 101 intro greeting mismatch");
    ESP_RETURN_ON_FALSE(last != NULL && strstr(last->text, "A 和 Y") != NULL,
                        ESP_FAIL, TAG, "level 101 task guidance mismatch");
    ESP_RETURN_ON_FALSE(level_intro_page_get(403U, 3U) == NULL, ESP_FAIL, TAG,
                        "out-of-range level 403 intro page returned");
    ESP_RETURN_ON_FALSE(level_intro_page_get(501U, 3U) == NULL, ESP_FAIL, TAG,
                        "out-of-range level 501 intro page returned");
    ESP_RETURN_ON_FALSE(level_intro_page_get(504U, 3U) == NULL, ESP_FAIL, TAG,
                        "out-of-range level 504 intro page returned");

    ESP_LOGI(TAG, "level intro self-test passed");
    return ESP_OK;
}
