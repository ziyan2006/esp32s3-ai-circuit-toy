# 单击切换录音按键实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将 BOOT 键改为第一次按下开始录音、第二次按下停止录音，并在 AI 回复期间忽略按键。

**架构：** 在现有 `talk_control_task` 中增加按键下降沿检测和显式交互状态。WebSocket 模块暴露会话是否仍活动，主任务据此从等待回复恢复为空闲。

**技术栈：** ESP-IDF 5.5.4、FreeRTOS、GPIO、Volcengine WebSocket、C

---

## 文件结构

- 修改 `main/main.c`：按键下降沿、交互状态机和录音启停逻辑。
- 修改 `main/volcengine_ws.h`：声明会话活动状态查询接口。
- 修改 `main/volcengine_ws.c`：根据内部会话状态返回是否正在处理本轮交互。

### 任务 1：暴露 AI 会话活动状态

**文件：**
- 修改：`main/volcengine_ws.h`
- 修改：`main/volcengine_ws.c`

- [ ] **步骤 1：声明状态查询接口**

```c
bool volcengine_ws_is_session_active(void);
```

- [ ] **步骤 2：实现状态查询**

```c
bool volcengine_ws_is_session_active(void)
{
    return s_session_starting || s_session_ready || s_session_closing;
}
```

- [ ] **步骤 3：编译验证接口签名**

运行：`idf.py build`
预期：编译器不报告声明或链接错误。

### 任务 2：实现下降沿切换状态机

**文件：**
- 修改：`main/main.c`

- [ ] **步骤 1：定义交互状态**

```c
typedef enum {
    TALK_STATE_READY,
    TALK_STATE_RECORDING,
    TALK_STATE_WAITING_RESPONSE,
} talk_state_t;
```

- [ ] **步骤 2：检测稳定下降沿**

保存 `previous_button_pressed`，只在 `button_pressed && !previous_button_pressed` 时执行切换；每轮循环末尾更新前一状态。

- [ ] **步骤 3：第一次按下开始录音**

在 `TALK_STATE_READY` 中调用 `volcengine_ws_prepare_session()`，成功后清零录音统计并切换为 `TALK_STATE_RECORDING`。

- [ ] **步骤 4：第二次按下停止录音**

在 `TALK_STATE_RECORDING` 中执行短录音和发送错误检查，有效时调用 `volcengine_ws_commit_and_respond()` 并切换为 `TALK_STATE_WAITING_RESPONSE`。

- [ ] **步骤 5：等待期间忽略按键**

在 `TALK_STATE_WAITING_RESPONSE` 中不处理下降沿；当 `volcengine_ws_is_session_active()` 返回 `false` 时恢复 `TALK_STATE_READY`。

### 任务 3：构建与设备验证

**文件：**
- 验证：`main/main.c`
- 验证：`main/volcengine_ws.c`

- [ ] **步骤 1：运行协议测试**

运行：`$env:PYTHONPATH=(Resolve-Path scratch).Path; python -m pytest scratch/tests -q`
预期：`3 passed`。

- [ ] **步骤 2：构建固件**

运行：`. E:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1; idf.py build`
预期：生成 `build/esp32_volcengine_voice.bin`。

- [ ] **步骤 3：烧录设备**

运行：`idf.py -p COM14 flash`
预期：校验成功并硬复位设备。

- [ ] **步骤 4：验证交互**

第一次短按后松开并等待两秒，串口应持续输出录音帧；第二次短按后应出现 `Recording stopped` 和 `Sending EndASR`。AI 回复期间按键不得出现新的 `Recording started`。
