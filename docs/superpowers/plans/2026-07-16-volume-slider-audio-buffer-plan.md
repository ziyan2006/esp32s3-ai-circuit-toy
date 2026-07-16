# 音量滑条与连续音频播放实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 ESP32-S3-BOX-3 主界面增加底部音量滑条，并消除火山引擎 TTS 音频突发下发造成的播放断续。

**架构：** UI 使用 LVGL `lv_slider` 直接调用现有 `audio_hal_set_volume()`；音频播放仍保留现有生产者/消费者任务，但将 32 KB 内部 RAM 环形缓冲改为 256 KB PSRAM 缓冲，以容纳约 5–8 秒的 16 kHz 单声道 PCM 突发数据。WebSocket 接收逻辑不改变协议，只延长入队等待并保留丢帧日志。

**技术栈：** ESP-IDF 5.5.4、FreeRTOS Ringbuffer、ESP PSRAM、LVGL 8、ESP-BOX-3 BSP、ES8311 Codec。

---

## 文件结构

- 修改 `main/audio_hal.c`：创建 PSRAM 播放缓冲、提高容量、记录播放写入错误。
- 修改 `main/main.c`：创建底部音量滑条、百分比标签和事件回调。
- 保持 `main/audio_hal.h` 的现有公开接口不变。

### 任务 1：修复 TTS 播放缓冲不足

**文件：**
- 修改：`main/audio_hal.c:15`
- 修改：`main/audio_hal.c:78`
- 修改：`main/audio_hal.c:103`

- [ ] **步骤 1：保留失败证据**

确认串口日志包含以下稳定复现信息：

```text
Audio playback queue rejected 12288 bytes: ESP_ERR_TIMEOUT
Audio playback queue rejected 14336 bytes: ESP_ERR_TIMEOUT
```

这证明 32 KB `RINGBUF_TYPE_NOSPLIT` 缓冲无法吸收服务端突发音频。

- [ ] **步骤 2：将播放缓冲移至 PSRAM**

在 `audio_hal.c` 引入 `esp_heap_caps.h`，并将创建逻辑改为：

```c
#define AUDIO_PLAY_RINGBUF_SIZE (256 * 1024)

s_play_ringbuf = xRingbufferCreateWithCaps(
    AUDIO_PLAY_RINGBUF_SIZE,
    RINGBUF_TYPE_NOSPLIT,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

如果 PSRAM 分配失败，记录错误并返回 `ESP_ERR_NO_MEM`，不静默回退到容易丢帧的 32 KB 缓冲。

- [ ] **步骤 3：检查 Codec 写入结果**

将播放任务中的写入改为：

```c
esp_err_t result = esp_codec_dev_write(s_speaker_dev, data, item_size);
if (result != ESP_OK) {
    ESP_LOGE(TAG, "Speaker write failed: %s", esp_err_to_name(result));
}
```

无论写入是否成功，都必须调用 `vRingbufferReturnItem()` 归还项目。

- [ ] **步骤 4：延长突发入队等待**

将 `audio_play_queue_push()` 的等待时间从 100 ms 调整为 500 ms：

```c
BaseType_t result = xRingbufferSend(
    s_play_ringbuf, (void *)buffer, size, pdMS_TO_TICKS(500));
```

仍然返回 `ESP_ERR_TIMEOUT`，让 WebSocket 层保留诊断能力。

- [ ] **步骤 5：构建验证**

运行：

```powershell
. E:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
idf.py build
```

预期：`Project build complete`，无新增编译错误。

### 任务 2：增加底部音量滑条

**文件：**
- 修改：`main/main.c:23`
- 修改：`main/main.c:25`
- 修改：`main/main.c:148`

- [ ] **步骤 1：定义音量状态与归一化函数**

增加默认音量和 5% 步进归一化：

```c
#define DEFAULT_VOLUME_PERCENT 70
#define VOLUME_STEP_PERCENT 5

static int normalize_volume(int value)
{
    return ((value + VOLUME_STEP_PERCENT / 2) / VOLUME_STEP_PERCENT) * VOLUME_STEP_PERCENT;
}
```

输入来自范围固定为 `0–100` 的 LVGL Slider，因此输出保持在该范围内。

- [ ] **步骤 2：增加滑条事件回调**

回调读取滑条值、按 5% 归一化、更新标签并调用音频 HAL：

```c
static void volume_slider_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    int volume = normalize_volume(lv_slider_get_value(slider));
    lv_slider_set_value(slider, volume, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_volume_value_label, "%d%%", volume);

    esp_err_t result = audio_hal_set_volume(volume);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set volume: %s", esp_err_to_name(result));
    }
}
```

- [ ] **步骤 3：创建底部控件**

在音频 HAL 初始化成功后、WebSocket 初始化前持有 `bsp_display_lock(0)` 创建：

```c
lv_obj_t *volume_label = lv_label_create(lv_scr_act());
lv_label_set_text(volume_label, "VOL");
lv_obj_align(volume_label, LV_ALIGN_BOTTOM_LEFT, 16, -18);

s_volume_slider = lv_slider_create(lv_scr_act());
lv_obj_set_size(s_volume_slider, 210, 16);
lv_obj_align(s_volume_slider, LV_ALIGN_BOTTOM_MID, -5, -18);
lv_slider_set_range(s_volume_slider, 0, 100);
lv_slider_set_value(s_volume_slider, DEFAULT_VOLUME_PERCENT, LV_ANIM_OFF);
lv_obj_add_event_cb(s_volume_slider, volume_slider_event_cb,
                    LV_EVENT_VALUE_CHANGED, NULL);

s_volume_value_label = lv_label_create(lv_scr_act());
lv_label_set_text_fmt(s_volume_value_label, "%d%%", DEFAULT_VOLUME_PERCENT);
lv_obj_align(s_volume_value_label, LV_ALIGN_BOTTOM_RIGHT, -12, -18);
```

为主轨道、Indicator 和 Knob 设置深灰、蓝色、浅色圆形样式；Knob 触摸尺寸不小于 24 px。

- [ ] **步骤 4：同步默认 Codec 音量**

在创建控件前调用：

```c
ESP_ERROR_CHECK(audio_hal_set_volume(DEFAULT_VOLUME_PERCENT));
```

确保 UI 初始值与扬声器实际输出一致。

- [ ] **步骤 5：构建与烧录**

运行：

```powershell
. E:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
idf.py build
idf.py -p COM14 flash
```

预期：构建和烧录成功，屏幕底部出现 `VOL`、滑条和 `70%`。

### 任务 3：设备回归验证

**文件：**
- 不修改生产文件。

- [ ] **步骤 1：验证音量交互**

将滑条依次拖到 `20%`、`70%`、`100%`，确认标签按 5% 步进更新，串口无 `Failed to set volume`。

- [ ] **步骤 2：验证连续播放**

按住 BOOT 说一句可产生至少 5 秒回复的问题，等待 TTS 播放完成。

预期：串口不再出现：

```text
Audio playback queue rejected ... ESP_ERR_TIMEOUT
```

听感上没有明显缺字、停顿或断裂。

- [ ] **步骤 3：验证对话回归**

确认仍能看到 `SessionStarted`、`ASRResponse`、`ChatResponse`、`TTSEnded` 和 `SessionFinished`，屏幕状态最终恢复 Ready。

- [ ] **步骤 4：清理临时连接**

关闭串口监控连接，确保 `COM14` 不被后台进程持续占用。
