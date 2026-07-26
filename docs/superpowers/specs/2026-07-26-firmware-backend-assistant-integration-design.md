# 固件 AI 助教模式切换与后端接入设计

## 目标

在固件现有设置界面中提供“AI 助教：内置 / 后端”选项。选择后端模式时，设备保留本地 ASR、TTS、关卡会话管理和亮灯执行，仅将识别出的用户文本与 `tuco_circuit_v2` 电路快照发送给 AI 后端。

## 约束与非目标

- 后端地址固定在设备本地 `sdkconfig`，不提供设备端编辑界面。
- 不需要设备令牌或额外鉴权；API Key 只允许保留在未提交的本地 `sdkconfig`。
- 不改变 AI 后端的关卡提示词、LLM 配置或前端测试台。
- 网络失败时不自动回退到内置助教，避免用户不清楚当前模式；只给出明确错误提示。

## 设置界面

- 在现有设置列表增加 `AI 助教` 条目，展示当前值：`内置` 或 `后端`。
- 使用既有像素风、选中高亮、确认键交互和提示音；确认键在两个值之间切换。
- 设置值写入独立 NVS 命名空间，键为 `mode`：`0` 为内置、`1` 为后端。
- 若 NVS 中不存在值，使用内置助教作为默认模式。
- 切换在下一次提问生效；如果玩家正在关卡内，路由层立即结束旧模式会话并为新模式建立当前关卡会话。

## 固件配置

新增 Kconfig 配置：

- `CONFIG_TUCO_REMOTE_ASSISTANT_URL`：后端决策接口的完整 HTTPS URL，本地 `sdkconfig` 覆盖，默认留空。
- `CONFIG_TUCO_REMOTE_ASSISTANT_TIMEOUT_MS`：单次后端决策超时，默认 20 秒。

固定目标接口为 `POST /api/device/circuit-coach/decision`。请求 JSON 为：

```json
{
  "session_id": "fw-<device>-<level>-<session>",
  "user_text": "用户识别出的文本",
  "circuit_snapshot": { "schema": "tuco_circuit_v2", "...": "固件当前快照" }
}
```

不配置 URL 或 URL 非 HTTPS 时，后端模式不可用，并返回“后端助教尚未配置”。

## 路由与请求流程

新增统一助教路由层，供 `volcengine_voice.c` 和 `tuco_agent_serial.c` 共用：

1. `begin_level_session(level_id)`：根据当前模式建立内置或后端会话。
2. `submit(text)`：内置模式调用现有 `tuco_agent_submit()`；后端模式将文本、会话 ID 和 `build_circuit_context()` 生成的快照提交给专用 HTTP 任务。
3. `poll_result(request_id)` 与 `cancel(request_id)`：维持当前语音状态机和文字调试链路的异步语义。
4. `end_level_session()`：关闭本地或远端会话状态。

后端 HTTP 请求在独立 FreeRTOS 任务中执行，不能阻塞语音状态机。后端响应使用现有 `DecisionResponse` 结构：

- `assistant_text`：直接交给本地 TTS；文字调试链路直接返回。
- `tool_call.name = highlight_ports`：复用固件端口校验与一次一对端口的亮灯执行。
- `tool_call.name = highlight_empty_slot`：复用固件空槽、已解锁积木和每轮一次高亮校验。

为避免两套校验漂移，现有 `tuco_agent` 中的工具执行代码会抽取为共享的“应用助教决策”接口，内置助教和后端助教都调用它。

## 会话与切换语义

- 每次进入关卡创建会话；离开关卡关闭会话，与现有语音链路一致。
- 文本串口调试也通过同一路由层，因此和语音链路共享短期历史与工具执行，仅省略 ASR/TTS。
- 模式在关卡内切换时，旧模式请求会被取消；新模式使用新会话 ID，不混用历史。
- 后端会话 ID 由稳定设备标识、关卡 ID 和递增会话序号组成，满足后端 128 字符限制。

## 错误处理与日志

- HTTP、TLS、超时、非 2xx、JSON 解析失败和工具参数无效均记录原因与请求 ID。
- 语音链路统一提示“后端助教暂时无法连接”或“后端助教返回异常”。
- 文字链路返回相同的中文错误文本，便于调试。
- 不写入请求中的 API Key；日志仅记录 URL 主机、状态码、会话 ID、关卡和拓扑版本。

## 测试与验收

- 设置持久化自测：默认值、切换、重启读取和非法 NVS 值回退。
- 路由自测：两种模式均可创建、提交、轮询、取消和结束会话。
- 远端响应解析自测：文本、端口高亮、空槽高亮、错误响应和超时。
- 保持现有 `tuco_agent_context_self_test_run()`，并验证共享工具执行器仍拒绝无效端口/已连线端口/未解锁积木。
- `idf.py build` 成功；设备验收覆盖两种模式、模式切换、语音提问、串口文字提问和后端不可用提示。

## 实施边界

第一阶段只实现设备到当前 AI 后端的单次决策 HTTP 调用。后端现有 `/api/device/circuit-coach/decision` 已接受新版快照并返回文本或单个工具调用，因此不新增后端 API；如实施中发现模型字段不兼容，再在当前后端功能分支补充兼容性测试。
