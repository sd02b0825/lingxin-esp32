# 灵芯 WebSocket 云端接入

本分支已将对话链路切换为灵芯 WebSocket 协议。自建云仍只负责 OTA、资产升级、时间同步和下发 `lingxin` 配置块；旧 MQTT+UDP、旧 WebSocket `hello/type` 协议和 `BinaryProtocol2/3` 音频封装已下线。

## 连接与鉴权

默认端点会随 `lingxin.mode` 选择：

```text
voice / text_to_voice: wss://eagent.edu-aliyun.com/gw/ws/open/api/v1/agentChat
cloud_vad: wss://eagent.edu-aliyun.com/gw/ws/open/api/v2/agentChat
full_duplex: wss://eagent.edu-aliyun.com/gw/ws/open/api/v1/unifiedAccess
```

建链时通过 WebSocket header 携带：

- `app_id`：灵芯企业应用标识。
- `sn`：设备序列号，也是 `task_id` 前缀。
- `timestamp`：毫秒时间戳，依赖启动阶段 OTA 或 SNTP 校时。
- `signature`：按 `app_id=<urlencode>&sn=<urlencode>&timestamp=<ms>` 生成 HMAC-SHA1，再 Base64。
- `sdk_name` / `sdk_version`：端侧标识。

配置优先级为：OTA 写入 NVS 的 `lingxin.*` > 已存在 NVS > Kconfig 默认值。OTA 写入会做字段白名单和取值校验，非法 codec、采样率、endpoint 和流控参数会被忽略。

## 接入模式

- `voice`：普通语音输入语音输出，端侧结束收音后发送 `end_audio`。
- `cloud_vad`：云端 VAD 语音输入语音输出，收到 `vad_end` 或 `vad_exit` 后发送 `end_audio`，支持 `max_sentence_silence`。
- `text_to_voice`：文本输入语音输出，`input_mode=no_voice`，`agent_basic_inputs.user_input` 必填，不发送上行音频。
- `full_duplex`：全双工，使用 `unifiedAccess` 和 `task=chat_full_duplex`，仅建议端侧 AEC/实时监听可用时启用。

## 任务生命周期

端侧通过 `Protocol` 接口映射到灵芯任务协议：

- `OpenAudioChannel()`：建立灵芯 WebSocket，设置鉴权 header，不发送旧 `hello`。
- `SendStartListening()`：持久化递增 `task_id`，发送 `start_task`，等待 `task_started` 后再打开本地录音。
- `SendAudio()`：发送裸二进制上行音频帧，默认 `16 kHz Opus`；当前采集链路只产生 Opus，上行配置为其它格式时会拒绝发送并报错。
- `SendStopListening()`：发送 `end_audio`。
- `SendAbortSpeaking()`：发送 `terminate_task`。
- `CloseAudioChannel()`：必要时发送 `end_task` 或 `terminate_task` 后关闭连接。

`task_id` 格式为：

```text
sn^conversation_id^turn_id
```

计数保存在 NVS `lingxin.conv_id`、`lingxin.turn_id`、`lingxin.task_inflight`，发送 `start_task` 前先落库，避免服务端状态机和 license 风控拒绝。

## 消息格式

发送 `start_task` 示例：

```json
{
  "header": {
    "action": "start_task",
    "task_id": "SN0001^1^1"
  },
  "payload": {
    "input_mode": "voice",
    "output_mode": "voice",
    "ai_app_code": "app_xxx",
    "agent_basic_inputs": {},
    "agent_ext_inputs": {
      "play_prologue": false,
      "show_asr_output": true
    },
    "flow_control_parameters": {
      "flow_control_strategy": "fixed_byte_rate",
      "max_size": 32
    },
    "biz_parameters": {
      "device_code": "device-uuid"
    }
  }
}
```

端侧处理的服务端 `header.action`：

- `task_started`：允许开始发送音频。
- `vad_end`：自动发送 `end_audio`。
- `vad_exit`：发送 `end_audio` 并回到待机。
- `audio_response_start`：进入 Speaking，按 `audio_down_codec` 接收下行音频。
- `audio_response_end` / `task_ended`：按监听模式回到 Listening 或 Idle。
- `task_terminated`：清空播放队列并回到 Idle。
- `text_output`：按 `payload.type` 更新 UI 或分发动作。

`text_output` 子类型：

- `asr_final_text`：显示用户文本。
- `agent_response_text` / `llm_sentence_text`：显示助手文本。
- `sentiment_analysis_result`：更新表情，支持字符串、对象和文档中的数组对象结构。
- `action_list` / `tool_call`：记录动作载荷，若为对象或数组则尝试交给本地 MCP。

## 音频参数

默认参数：

- 上行：`16 kHz Opus`，裸二进制帧。
- 下行：`16 kHz PCM`，裸二进制帧。
- 可配置格式：`opus`、`pcm`、`wav`、`mp3`。
- 帧长：默认 `60 ms`，与本地 Opus 编码帧长保持一致。

可通过 Kconfig 或 OTA `lingxin` 配置覆盖 `audio_up_sample_rate`、`audio_down_sample_rate`、`audio_up_codec`、`audio_down_codec`、`audio_channels`、`audio_bits_per_sample`、`audio_frame_ms`。当前运行能力边界：

- 下行 `opus`：走现有 Opus 解码。
- 下行 `pcm`：按 16-bit PCM 直通/重采样后播放。
- 下行 `wav`：解析 PCM WAV 头后播放。
- 下行 `mp3`：允许配置和下发，但当前 `AudioService` 未集成 MP3 解码器，会明确拒绝播放并记录日志。
- 上行：现有麦克风链路只输出 Opus 包，其它格式需后续扩展采集/编码路径。

## 已废弃能力

- MQTT+UDP 对话链路。
- 旧 WebSocket `hello/type` 协议。
- 旧 `BinaryProtocol2/3` 音频封装。
- 服务端 AEC 配置项和旧云自定义消息开关。

端侧唤醒词、AFE、VAD、设备侧 AEC、Opus 编解码、按键、显示、灯效、电源管理和自建 OTA 继续保留。
