# 单击切换录音按键设计

## 目标

将 ESP32-S3-BOX-3 的 BOOT 键从“按住录音、松开发送”改为“第一次按下开始录音、第二次按下停止并发送”。

## 交互规则

- `READY` 状态第一次检测到 BOOT 键下降沿时，创建 AI 会话并开始录音。
- 松开 BOOT 键不会停止录音。
- `RECORDING` 状态第二次检测到 BOOT 键下降沿时，停止录音并发送 `EndASR`。
- `THINKING`、`SPEAKING`、重连和错误恢复期间忽略 BOOT 键。
- AI 会话结束或失败后恢复到 `READY`。
- 长按只产生一次下降沿，不得同时触发开始和停止。

## 实现方案

继续使用现有 FreeRTOS 轮询任务，不增加 GPIO 中断或新组件。任务保存上一次稳定按键电平，并在高电平到低电平时产生一次按键事件。使用显式交互状态控制开始、录音和等待回复，录音循环仍以约 32 ms 的 PCM 帧运行，因此第二次按键的响应延迟保持在可接受范围内。

## 状态机

```text
READY --按下--> STARTING --会话成功--> RECORDING
STARTING --失败--> READY
RECORDING --按下--> VALIDATING
VALIDATING --有效录音--> WAITING_RESPONSE
VALIDATING --无效录音--> READY
WAITING_RESPONSE --会话结束/错误--> READY
```

## 屏幕提示

- 空闲：`Ready! Press BOOT to start recording.`
- 录音：`Recording... Press BOOT again to stop.`
- 提交后：`<<< Thinking...`
- 等待回复期间再次按键：保持当前提示，不启动新录音。

## 保留行为

- 保留短录音拒绝、音频发送错误、ASR 超时、WebSocket 自动重连和播放缓冲处理。
- 保留每轮录音统计日志。
- 不改变音量滑条行为。

## 验收标准

1. 第一次短按后持续录音，松开不停止。
2. 第二次短按后停止录音并进入 `Thinking`。
3. 长按不产生重复切换。
4. AI 回复期间按键不启动录音。
5. 回复结束后下一次按键可以开始新一轮录音。
