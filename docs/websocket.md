# Lingxin WebSocket Cloud Integration

This branch uses the Lingxin WebSocket protocol for conversations. The self-hosted cloud remains responsible for OTA, asset updates, time sync, and delivering the `lingxin` configuration block. The previous MQTT+UDP path, legacy WebSocket `hello/type` protocol, and `BinaryProtocol2/3` audio wrapping have been retired.

## Connection And Authentication

Default endpoint:

```text
wss://eagent.edu-aliyun.com/gw/ws/open/api/v1/agentChat
```

The WebSocket handshake sends these headers:

- `app_id`: Lingxin enterprise application id.
- `sn`: device serial number, also used as the `task_id` prefix.
- `timestamp`: millisecond timestamp, expected to be corrected by OTA time sync or SNTP at startup.
- `signature`: HMAC-SHA1 + Base64 over `app_id=<urlencode>&sn=<urlencode>&timestamp=<ms>`.
- `sdk_name` / `sdk_version`: device-side client identification.

Configuration priority is: OTA-written `lingxin.*` values in NVS > existing NVS values > Kconfig defaults.

## Task Lifecycle

The existing `Protocol` interface is mapped to Lingxin task commands:

- `OpenAudioChannel()` opens the Lingxin WebSocket and sets auth headers. It does not send the old `hello` message.
- `SendStartListening()` persists a monotonic `task_id`, sends `start_task`, and waits for `task_started` before enabling local recording.
- `SendAudio()` sends raw uplink binary audio frames, defaulting to `16 kHz Opus`.
- `SendStopListening()` sends `end_audio`.
- `SendAbortSpeaking()` sends `terminate_task`.
- `CloseAudioChannel()` sends `end_task` or `terminate_task` when needed, then closes the socket.

`task_id` format:

```text
sn^conversation_id^turn_id
```

Counters are persisted in NVS keys `lingxin.conv_id`, `lingxin.turn_id`, and `lingxin.task_inflight` before `start_task` is sent to avoid server state-machine and license risk-control rejection.

## Messages

Example `start_task`:

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

Handled server `header.action` values:

- `task_started`: audio upload may begin.
- `vad_end`: automatically sends `end_audio`.
- `vad_exit`: sends `end_audio` and returns to idle.
- `audio_response_start`: enters Speaking and accepts downlink PCM.
- `audio_response_end` / `task_ended`: returns to Listening or Idle depending on the listening mode.
- `task_terminated`: clears playback and returns to Idle.
- `text_output`: updates UI or dispatches action payloads by `payload.type`.

Handled `text_output` subtypes:

- `asr_final_text`: shows user text.
- `agent_response_text` / `llm_sentence_text`: shows assistant text.
- `sentiment_analysis_result`: updates the displayed emotion.
- `action_list` / `tool_call`: logs the action payload and passes object payloads to local MCP when possible.

## Audio Defaults

- Uplink: `16 kHz Opus`, raw binary frames.
- Downlink: `16 kHz PCM`, raw binary frames.
- Frame duration: `60 ms` by default, matching the local Opus frame duration.

Kconfig and OTA `lingxin` overrides can set `audio_up_sample_rate`, `audio_down_sample_rate`, `audio_up_codec`, `audio_down_codec`, and `audio_frame_ms`. Keep the defaults unless the Lingxin service capability is known to match the override.

## Retired Behavior

- MQTT+UDP conversation transport.
- Legacy WebSocket `hello/type` protocol.
- Legacy `BinaryProtocol2/3` audio wrapping.
- Server-side AEC config and old custom cloud-message switch.

Local wake word, AFE, VAD, device-side AEC, Opus, buttons, display, LED, power management, and self-hosted OTA remain in place.
