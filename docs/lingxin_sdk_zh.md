# 灵芯 LingXin SDK 云端接入（v2.6.6）

本分支固件仅通过 **LingXin SDK `voice_chat` API** 接入灵芯云端。应用层不再实现 `LingxinWebsocketProtocol`；WebSocket 建链、鉴权、任务状态机由 SDK 内部管理（`adapter_LingXin/lingxin_websocket.c`）。

自建云仍负责 OTA、资产升级、时间同步，以及向 NVS `lingxin` 命名空间下发配置。

## 架构

```text
Application (DeviceState)
    ↑ ChatPhase 映射
LingxinSdkProtocol
    ↑ 生命周期回调
LingXin SDK (chat_state_machine)
    ↕
lingxin_sdk_bridge ↔ AudioService (PCM 上行 ringbuf / 下行 decode 队列)
```

- **会话阶段**：以 SDK `ChatPhase` 为主源驱动 `Connecting` / `Listening` / `Speaking`。
- **展示与 MCP**：SDK `TEXT_OUT` → `HandleSdkTextOutput()`（ASR/助手文本、表情、tool_call）。
- **上行音频**：AFE → `lingxin_record_write_pcm()` → SDK；不经 Opus 发送队列。
- **下行音频**：SDK → `audio_service_push_decode_packet()` → 播放。

## 配置优先级

所有灵芯认证与业务参数按 **NVS `lingxin.*` > Kconfig > fallback** 读取：

| NVS 键 | Kconfig | 说明 |
|--------|---------|------|
| `app_id` | `CONFIG_LINGXIN_APP_ID` | 企业 app_id |
| `app_key` | `CONFIG_LINGXIN_APP_KEY` | license |
| `sn` | `CONFIG_LINGXIN_SN` | 设备 SN，空则使用 `Board::GetUuid()` |
| `ai_app_code` | `CONFIG_LINGXIN_AI_APP_CODE` | 应用/智能体 code |
| `device_code` | `CONFIG_LINGXIN_DEVICE_CODE` | 设备型号 |
| `mode` | `CONFIG_LINGXIN_CHAT_MODE` | `cloud_vad` / `voice` / `text_to_voice` |
| `fc_strategy` 等 | 流控 Kconfig | 下行流控 JSON |

## 对话模式（StartNewChatProps）

| mode | SDK `task` | 说明 |
|------|------------|------|
| `cloud_vad` | `chat_vad` | 云端 VAD（默认） |
| `voice` | `chat` | 端侧结束收音 |
| `text_to_voice` | `chat_vad` + `user_input` | 触发 SDK 内部 `no_voice` |
| `full_duplex` | 降级为 `chat_vad` | 未确认前不使用 `chat_full_duplex` |

## ChatPhase → DeviceState

| ChatPhase | DeviceState | 备注 |
|-----------|-------------|------|
| STANDBY | 不变 | 非会话态不误切 Activation/WifiConfig |
| STARTING | Connecting | 仅活跃会话 |
| INPUTING | Listening | 触发 `on_audio_channel_opened` |
| THINKING | 保持 Listening | 可选 UI |
| OUTPUTING | Speaking | `pending_outputing` + 首包下行同步；`opus_codec` 优先级临时提升 |
| INTERRUPTING | Listening | 软打断：`wait_playback_idle` 后 `ResetDecoder` |
| EXITING | Idle | `on_audio_channel_closed` |

## 打断

`SendAbortSpeaking()` 调用 SDK `state_machine_run_event(State_Event_Wakeup_Detected)` 进入 `State_Terminate`，并 `module_bufferPlay_terminate()` 停止本地 TTS。**不再**使用 `exit_chat()` 冒充打断。

Speaking 中唤醒词：Application → `AbortSpeaking` → SDK terminate → `CHAT_PHASE_INTERRUPTING` → Listening。

## MCP

`TEXT_OUT` 中 `tool_call` / `action_list` 由 `McpServer::ParseMessage` 本地执行。工具结果通过 `SendMcpMessage()` 以 `user_input` 调用 `start_new_chat`；**不保证**回到同一 SDK task 上下文（降级能力）。

## 唤醒词

检测链路在 `main/audio/`，与协议实现无关。`ContinueWakeWordInvoke()` 保留 `OpenAudioChannel` → `SetListeningMode`；已移除 `CONFIG_SEND_WAKE_WORD_DATA` 下的 Opus 预发送（SDK 模式下无效）。

## 测试建议

- 冷启动 STANDBY 不误切非会话状态
- NVS 覆盖 Kconfig 空字符串
- 默认/自定义唤醒词：Idle → Connecting → Listening
- Speaking 中唤醒打断 → Listening 可继续说话
- OUTPUTING 早于首包仍能进入 Speaking
- NetworkConfiguring 音频测试仍可用
- OTA/激活流程回归（未改 `ota.cc`）

已在 **bread-compact-wifi** 板级验证 SDK 适配路径。



**相关 Kconfig（menu Lingxin Cloud）**

| 配置项 | 默认 | 说明 |
|--------|------|------|
| `LINGXIN_PLAYBACK_TASKS_IN_QUEUE` | 8 | 待播放 PCM 段缓冲深度，过小易欠载 |
| `LINGXIN_DECODE_PUSH_WAIT_MS` | 100 | 入队等待毫秒，超时丢包 |
| `LINGXIN_FLOW_CONTROL_MAX_SIZE` | 16 | 流控单包 KB（原 32），减小可降低包间空档感 |
| `LINGXIN_FLOW_CONTROL_SPACE_TIME_MS` | 100 | 定间隔流控毫秒（原 120） |

NVS `lingxin.fc_max_size` / `lingxin.fc_sp_time_ms` 可覆盖流控。SRAM 紧张时将播放队列深度改为 4~6。
