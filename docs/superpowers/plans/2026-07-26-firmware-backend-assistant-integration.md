# 固件后端 AI 助教接入实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让设备设置页可持久化切换“内置 / 后端”AI 助教，并让语音和串口文字入口在后端模式下调用 Circuit Coach V2 决策接口。

**架构：** `assistant_mode` 负责 NVS 模式；`assistant_router` 成为文字和语音唯一异步入口；`remote_assistant` 在独立任务中请求后端；`assistant_tool_executor` 成为内置和远端共用的亮灯工具校验入口。

**技术栈：** ESP-IDF 5.5.4、FreeRTOS、NVS、`esp_http_client`、cJSON、LVGL、FastAPI `/api/device/circuit-coach/decision`。

---

## 文件结构

- 创建：`main/assistant_mode.h`、`main/assistant_mode.c` —— 模式 NVS 和无副作用自测。
- 创建：`main/assistant_tool_executor.h`、`main/assistant_tool_executor.c` —— 两种高亮工具的唯一执行点。
- 创建：`main/remote_assistant.h`、`main/remote_assistant.c` —— 异步 HTTP、请求/响应 JSON 与会话 ID。
- 创建：`main/assistant_router.h`、`main/assistant_router.c` —— 本地和远端模式路由。
- 修改：`main/Kconfig.projbuild`、`main/CMakeLists.txt`、`main/main.c` —— 配置、源文件、初始化和自测。
- 修改：`main/tuco_agent.h`、`main/tuco_agent.c` —— 导出快照、委托共享工具执行器。
- 修改：`main/volcengine_voice.c`、`main/tuco_agent_serial.c` —— 改走路由层。
- 修改：`main/app_ui.c` —— 增加像素风 AI 助教设置卡片。

### 任务 1：持久化助教模式

**文件：** 创建 `main/assistant_mode.h`、`main/assistant_mode.c`；修改 `main/CMakeLists.txt`、`main/main.c`。

- [ ] **步骤 1：先声明失败测试所需 API**

```c
typedef enum {
    ASSISTANT_MODE_LOCAL = 0,
    ASSISTANT_MODE_REMOTE = 1,
} assistant_mode_t;

esp_err_t assistant_mode_init(void);
assistant_mode_t assistant_mode_get(void);
esp_err_t assistant_mode_set(assistant_mode_t mode);
esp_err_t assistant_mode_self_test_run(void);
```

- [ ] **步骤 2：在启动自测中引用并构建失败**

在 `main/main.c` 的既有自测序列加入：

```c
ESP_RETURN_ON_ERROR(assistant_mode_self_test_run(), TAG, "assistant mode self-test");
```

运行：

```powershell
& 'E:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'; idf.py build
```

预期：FAIL，链接器报告 `assistant_mode_self_test_run` 未定义。

- [ ] **步骤 3：实现独立 NVS 命名空间**

在 `assistant_mode.c` 使用 `assistant` 命名空间和 `mode` 键，读取不到时用内置模式，非法值回退并覆盖保存：

```c
#define ASSISTANT_MODE_NAMESPACE "assistant"
#define ASSISTANT_MODE_KEY "mode"
static assistant_mode_t s_mode = ASSISTANT_MODE_LOCAL;

esp_err_t assistant_mode_set(assistant_mode_t mode)
{
    if (mode != ASSISTANT_MODE_LOCAL && mode != ASSISTANT_MODE_REMOTE) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_handle, ASSISTANT_MODE_KEY, (uint8_t)mode), TAG, "save mode");
    ESP_RETURN_ON_ERROR(nvs_commit(s_handle), TAG, "commit mode");
    s_mode = mode;
    return ESP_OK;
}
```

`assistant_mode_self_test_run()` 只测试转换 helper：`0 -> LOCAL`、`1 -> REMOTE`、`2 -> LOCAL`，不得覆盖玩家真实选择。

- [ ] **步骤 4：构建并验证**

运行同一 `idf.py build` 命令。预期：PASS；启动日志出现 `assistant mode self-test passed`。

- [ ] **步骤 5：提交**

```powershell
git add main/assistant_mode.h main/assistant_mode.c main/CMakeLists.txt main/main.c
git commit -m "feat: persist assistant mode setting"
```

### 任务 2：在设置页添加 AI 助教卡片

**文件：** 修改 `main/app_ui.c`；使用 `main/assistant_mode.h`、`main/assistant_router.h`。

- [ ] **步骤 1：增加枚举和状态标签**

```c
typedef enum {
    UI_SETTINGS_SPEAKER_TEST = 0,
    UI_SETTINGS_PROGRESS_SYNC,
    UI_SETTINGS_UNLOCK_ALL,
    UI_SETTINGS_INITIALIZE,
    UI_SETTINGS_ASSISTANT_MODE,
    UI_SETTINGS_COUNT,
} ui_settings_item_t;
```

在 `s_ui` 增加 `lv_obj_t *settings_assistant_status;`；在 `ui_refresh_settings_selection()` 写入：

```c
lv_label_set_text(s_ui.settings_assistant_status,
    assistant_mode_get() == ASSISTANT_MODE_REMOTE ?
    "当前使用后端助教\n按中键切换" : "当前使用内置助教\n按中键切换");
```

- [ ] **步骤 2：实现不改变风格的五卡片布局**

把当前 `350 x 190` 的两行卡片改为前两行各两个 `350 x 126` 卡片；最后一行创建横向 `724 x 126` 卡片：

```c
s_ui.settings_items[UI_SETTINGS_ASSISTANT_MODE] = ui_create_action_item(
    screen, 282, 426, 724, 126, "AI 助教", "内置 / 后端",
    "按中键切换，后端地址由本机配置", ui_settings_item_event_cb,
    (void *)(uintptr_t)(UI_SETTINGS_ASSISTANT_MODE + 1U));
```

- [ ] **步骤 3：实现确认键切换与取消旧请求**

在设置页中键分支加入：

```c
case UI_SETTINGS_ASSISTANT_MODE: {
    assistant_mode_t next = assistant_mode_get() == ASSISTANT_MODE_LOCAL ?
        ASSISTANT_MODE_REMOTE : ASSISTANT_MODE_LOCAL;
    if (assistant_mode_set(next) == ESP_OK) {
        assistant_router_handle_mode_change();
        audio_self_test_play_effect(AUDIO_EFFECT_CONFIRM);
    } else {
        audio_self_test_play_effect(AUDIO_EFFECT_ERROR);
    }
    ui_refresh_settings_selection();
    break;
}
```

- [ ] **步骤 4：构建与设备验证**

运行：

```powershell
& 'E:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'; idf.py build
idf.py -p COM14 flash monitor
```

预期：第五张卡片可选，切换后重启仍保持，现有四张卡片功能不变。

- [ ] **步骤 5：提交**

```powershell
git add main/app_ui.c main/assistant_mode.h main/assistant_router.h
git commit -m "feat: add assistant mode setting"
```

### 任务 3：抽取共享亮灯执行器

**文件：** 创建 `main/assistant_tool_executor.h`、`main/assistant_tool_executor.c`；修改 `main/tuco_agent.c`、`main/tuco_agent.h`、`main/CMakeLists.txt`、`main/main.c`。

- [ ] **步骤 1：声明工具执行器与失败自测**

```c
esp_err_t assistant_tool_executor_begin(uint32_t request_id);
void assistant_tool_executor_cancel(uint32_t request_id);
esp_err_t assistant_tool_executor_apply(uint32_t request_id, const char *name,
                                        const char *arguments_json, char *out_reply,
                                        size_t out_reply_size);
esp_err_t assistant_tool_executor_self_test_run(void);
```

先在 `main/main.c` 调用自测，构建预期因未定义函数 FAIL。

- [ ] **步骤 2：移动现有工具校验逻辑**

从 `tuco_agent.c` 移动 `parse_tool_uint`、`parse_tool_gate`、连线检查、空槽选择和 `tuco_port_highlight_show*` 调用。共享入口必须先检查：

```c
if (request_id == 0U || name == NULL || arguments_json == NULL ||
    out_reply == NULL || out_reply_size == 0U) return ESP_ERR_INVALID_ARG;
if (!play_mode_is_active() || !board_snapshot_get(&snapshot)) return ESP_ERR_INVALID_STATE;
if (already_highlighted_for_request(request_id)) return ESP_ERR_INVALID_STATE;
```

端口只接受未连线的输出到输入；空槽要求空且无连线、积木已解锁，但不再因存在其它可连端口而拒绝。

- [ ] **步骤 3：让内置助教委托共享入口**

将 `tuco_agent_call_cap()` 变为：

```c
char reply[TUCO_AGENT_REPLY_MAX];
esp_err_t err = assistant_tool_executor_apply(request->request_id, cap_name,
                                               input_json, reply, sizeof(reply));
return tool_result(out_output, err, err == ESP_OK ? reply : "工具提示不可用。");
```

`tuco_agent_submit()` 成功后调用 `assistant_tool_executor_begin(request_id)`；取消和结果结束时调用 `assistant_tool_executor_cancel(request_id)`。

- [ ] **步骤 4：实现自测并构建**

自测输入错误 JSON、未知工具、同一请求二次高亮和有未连线输入/输出时的空槽工具。运行 `idf.py build`，预期 PASS 和启动日志 `assistant tool executor self-test passed`。

- [ ] **步骤 5：提交**

```powershell
git add main/assistant_tool_executor.* main/tuco_agent.c main/tuco_agent.h main/CMakeLists.txt main/main.c
git commit -m "refactor: share assistant tool execution"
```

### 任务 4：实现异步远端 Circuit Coach 客户端

**文件：** 创建 `main/remote_assistant.h`、`main/remote_assistant.c`；修改 `main/Kconfig.projbuild`、`main/tuco_agent.h`、`main/tuco_agent.c`、`main/CMakeLists.txt`、`main/main.c`。

- [ ] **步骤 1：增加本机配置和 API**

在 `Kconfig.projbuild` 增加：

```kconfig
config TUCO_REMOTE_ASSISTANT_URL
    string "Remote assistant decision URL"
    default ""
config TUCO_REMOTE_ASSISTANT_TIMEOUT_MS
    int "Remote assistant timeout (ms)"
    range 1000 60000
    default 20000
```

在 `remote_assistant.h` 声明：

```c
esp_err_t remote_assistant_init(void);
bool remote_assistant_is_configured(void);
void remote_assistant_begin_level_session(uint16_t level_id);
void remote_assistant_end_level_session(void);
esp_err_t remote_assistant_submit(const char *text, uint16_t level_id, uint32_t *out_request_id);
esp_err_t remote_assistant_take_result(uint32_t request_id, char *output, size_t output_size);
void remote_assistant_cancel(uint32_t request_id);
esp_err_t remote_assistant_response_self_test_run(void);
```

- [ ] **步骤 2：先写响应解析自测并确认失败**

声明内部函数：

```c
static esp_err_t parse_decision_response(const char *body, uint32_t expected_revision,
    char *out_text, size_t out_text_size, char *out_tool_name, size_t out_tool_name_size,
    char *out_tool_arguments, size_t out_tool_arguments_size);
```

自测样本为纯文本响应、`highlight_ports` 响应、同时存在文本/工具的拒绝响应和拓扑版本不一致响应。先构建，预期 FAIL。

- [ ] **步骤 3：导出当前快照并构建 HTTP 请求**

在 `tuco_agent.h`/`.c` 新增：

```c
esp_err_t tuco_agent_build_circuit_snapshot(uint16_t level_id, char **out_json);
```

它复用现有快照、关卡规则和解锁积木序列化逻辑。远端任务用 cJSON 解析返回的 JSON，并请求：

```json
{
  "session_id": "fw-<mac>-<level>-<counter>",
  "user_text": "用户识别文本",
  "circuit_snapshot": { "schema": "tuco_circuit_v2" }
}
```

- [ ] **步骤 4：实现单请求队列与 HTTP 任务**

HTTP 任务必须不阻塞语音状态机：

```c
build_remote_request_json(session_id, text, level_id, &json);
esp_http_client_perform(client);
parse_decision_response(body, topology_revision, text, sizeof(text), tool_name,
                        sizeof(tool_name), tool_args, sizeof(tool_args));
if (tool_name[0] != '\0') {
    result = assistant_tool_executor_apply(request_id, tool_name, tool_args,
                                           text, sizeof(text));
}
store_result(request_id, result, text);
```

只接受 `https://` URL。URL 为空/非 HTTPS 时返回 `ESP_ERR_INVALID_STATE` 和“后端助教尚未配置”；HTTP、TLS、超时、非 2xx 与 JSON 错误记录请求 ID、关卡、拓扑版本和状态码，不记录密钥。

- [ ] **步骤 5：构建验证与提交**

运行 `idf.py build`，预期 PASS 和启动日志 `remote assistant response self-test passed`；然后：

```powershell
git add main/remote_assistant.* main/Kconfig.projbuild main/tuco_agent.* main/CMakeLists.txt main/main.c
git commit -m "feat: add remote circuit coach client"
```

### 任务 5：实现路由并替换语音、串口入口

**文件：** 创建 `main/assistant_router.h`、`main/assistant_router.c`；修改 `main/volcengine_voice.c`、`main/tuco_agent_serial.c`、`main/main.c`、`main/CMakeLists.txt`。

- [ ] **步骤 1：声明统一异步 API 与选择 helper 自测**

```c
esp_err_t assistant_router_init(void);
void assistant_router_begin_level_session(uint16_t level_id);
void assistant_router_end_level_session(void);
esp_err_t assistant_router_submit(const char *text, uint16_t level_id, uint32_t *out_request_id);
esp_err_t assistant_router_take_result(uint32_t request_id, char *output, size_t output_size);
void assistant_router_cancel(uint32_t request_id);
void assistant_router_handle_mode_change(void);
esp_err_t assistant_router_self_test_run(void);
```

自测确认 `LOCAL` 选内置实现、`REMOTE` 选远端实现、未知值返回 `ESP_ERR_INVALID_ARG`。

- [ ] **步骤 2：先替换串口入口并观察未实现失败**

在 `tuco_agent_serial.c` 将 `tuco_agent_submit`、`tuco_agent_take_result`、`tuco_agent_cancel` 替换为相同签名的 `assistant_router_*`；在缺失实现时构建预期 FAIL。

- [ ] **步骤 3：实现路由和切换取消语义**

`assistant_router_submit()` 读取 `assistant_mode_get()` 并调用对应实现；成功后保存 `s_active_mode`，保证模式切换后仍能正确取消旧实现。

```c
if (s_active_request_id != 0U) {
    dispatch_cancel(s_active_mode, s_active_request_id);
    s_active_request_id = 0U;
}
dispatch_end_session(s_active_mode);
if (s_play_active) dispatch_begin_session(assistant_mode_get(), s_level_id);
```

- [ ] **步骤 4：替换语音入口且不改变音频状态机**

在 `volcengine_voice.c` 用 `assistant_router_begin_level_session`、`assistant_router_end_level_session`、`assistant_router_submit`、`assistant_router_take_result` 与 `assistant_router_cancel` 替换所有直接 `tuco_agent_*` 调用。保留 ASR、等待提示、TTS 和现有错误状态机。

- [ ] **步骤 5：构建、设备回归和提交**

运行 `idf.py build`。设备验收：内置模式语音/串口正常；后端模式两入口共享后端会话；中途切换不播放旧回复。

```powershell
git add main/assistant_router.* main/volcengine_voice.c main/tuco_agent_serial.c main/main.c main/CMakeLists.txt
git commit -m "feat: route assistant requests by mode"
```

### 任务 6：后端兼容性与最终验收

**文件：** 必要时修改 `E:\3.6bench\tests\test_api.py` 和 `E:\3.6bench\src\tuco_ai_backend\models.py`；修改设计说明记录验收。

- [ ] **步骤 1：增加固件等价请求测试**

```python
payload = {
    "session_id": "fw-test-101-1",
    "user_text": "接下来怎么做？",
    "circuit_snapshot": firmware_v2_snapshot,
}
response = client.post("/api/device/circuit-coach/decision", json=payload)
assert response.status_code == 200
assert response.json()["topology_revision"] == firmware_v2_snapshot["board"]["topology_revision"]
```

- [ ] **步骤 2：运行后端和固件最终验证**

```powershell
# E:\3.6bench
pytest -q
# E:\emb_agent_new
& 'E:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'; idf.py build
```

预期：两者 PASS；固件生成 `build/tuco_baseboard_firmware.bin` 且小于最小 app 分区。

- [ ] **步骤 3：记录验收和提交**

在设计说明追加内置模式、后端模式文字、后端模式语音、端口高亮、空槽高亮、断网提示、重启后保持的实际结果。提交：

```powershell
git add docs/superpowers/specs/2026-07-26-firmware-backend-assistant-integration-design.md
git commit -m "docs: record assistant integration verification"
```

## 计划自检

- 规格覆盖：设置界面、NVS、固定 SDK 配置、双入口路由、HTTP、会话、工具复用、错误、后端兼容性和验收分别落在任务 1–6。
- 占位符：每项包含具体文件、函数、命令和可观察结果；没有待定行为。
- 类型一致性：`assistant_mode_t`、`assistant_router_*`、`assistant_tool_executor_*` 和 `remote_assistant_*` 在所有任务中使用相同签名；后端请求固定为 `session_id`、`user_text`、`circuit_snapshot`。
