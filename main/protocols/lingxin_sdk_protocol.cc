/**
 * lingxin_sdk_protocol.cc - LingXin SDK Protocol implementation
 *
 * Implements Protocol interface using LingXin SDK's voice_chat C API.
 */

#include "lingxin_sdk_protocol.h"
#include "lingxin_sdk_bridge.h"
#include "audio_service.h"
#include "application.h"
#include "boards/common/board.h"
#include "settings.h"
#include "esp_log.h"
#include <cJSON.h>
#include <string>

static const char *TAG = "LingxinSdkProtocol";

LingxinSdkProtocol* LingxinSdkProtocol::instance_ = nullptr;

/* ---- Auth callback implementations ---- */

char* LingxinSdkProtocol::GetAppId() {
    static std::string app_id;
#ifdef CONFIG_LINGXIN_APP_ID
    app_id = CONFIG_LINGXIN_APP_ID;
#else
    Settings settings("lingxin", true);
    app_id = settings.GetString("app_id", "");
#endif
    return const_cast<char*>(app_id.c_str());
}

char* LingxinSdkProtocol::GetLicense() {
    static std::string license;
#ifdef CONFIG_LINGXIN_APP_KEY
    license = CONFIG_LINGXIN_APP_KEY;
#else
    Settings settings("lingxin", true);
    license = settings.GetString("app_key", "");
#endif
    return const_cast<char*>(license.c_str());
}

char* LingxinSdkProtocol::GetSn() {
    static std::string sn;
#ifdef CONFIG_LINGXIN_SN
    sn = CONFIG_LINGXIN_SN;
#else
    Settings settings("lingxin", true);
    sn = settings.GetString("sn", "");
    if (sn.empty()) {
        sn = Board::GetInstance().GetUuid();
    }
#endif
    return const_cast<char*>(sn.c_str());
}

char* LingxinSdkProtocol::GetAppCode() {
    static std::string app_code;
#ifdef CONFIG_LINGXIN_AI_APP_CODE
    app_code = CONFIG_LINGXIN_AI_APP_CODE;
#else
    Settings settings("lingxin", true);
    app_code = settings.GetString("ai_app_code", "");
#endif
    return const_cast<char*>(app_code.c_str());
}

char* LingxinSdkProtocol::GetDeviceCode() {
    static std::string device_code;
#ifdef CONFIG_LINGXIN_DEVICE_CODE
    device_code = CONFIG_LINGXIN_DEVICE_CODE;
#else
    Settings settings("lingxin", true);
    device_code = settings.GetString("device_code", "");
#endif
    return const_cast<char*>(device_code.c_str());
}

/* ---- SDK Lifecycle callback (runs on SDK thread) ---- */

void LingxinSdkProtocol::SdkLifeCycleHandler(ChatLifeCycleEvent event, void *payload) {
    auto *self = GetInstance();
    if (!self) {
        ESP_LOGE(TAG, "SdkLifeCycleHandler called but no instance");
        return;
    }

    /* All callbacks are dispatched to the main thread via Application::GetInstance().Schedule.
     * Payload data that may be freed by the SDK after this callback returns
     * must be copied (strdup for strings, by-value for enums). */

    switch (event) {
    case CHAT_LIFE_CYCLE_EVENT_CHAT_PHASE_CHANGE: {
        if (payload) {
            ChatPhaseChangePayload *phase_payload = (ChatPhaseChangePayload *)payload;
            ChatPhaseCode phase = phase_payload->phase_code;
            ESP_LOGI(TAG, "SDK ChatPhase: %d (scheduling to main thread)", phase);
            Application::GetInstance().Schedule([self, phase]() {
                self->HandleChatPhaseChange(phase);
            });
        }
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_EXIT: {
        if (payload) {
            ExitPayload *exit_payload = (ExitPayload *)payload;
            ExitCode exit_code = exit_payload->exit_code;
            char *reason_copy = exit_payload->reason ? strdup(exit_payload->reason) : nullptr;
            ESP_LOGI(TAG, "SDK Exit: code=%d, reason=%s (scheduling to main thread)",
                     exit_code, exit_payload->reason ? exit_payload->reason : "null");
            Application::GetInstance().Schedule([self, exit_code, reason_copy]() {
                self->HandleExit(exit_code, reason_copy);
            });
        }
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_TEXT_OUT: {
        if (payload) {
            char *text_copy = strdup((const char *)payload);
            Application::GetInstance().Schedule([self, text_copy]() {
                self->HandleTextOut(text_copy);
            });
        }
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_PLAY_END: {
        ESP_LOGI(TAG, "SDK play end event (scheduling to main thread)");
        Application::GetInstance().Schedule([self]() {
            self->HandlePlayEnd();
        });
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_ERROR: {
        ESP_LOGE(TAG, "SDK error event (scheduling to main thread)");
        Application::GetInstance().Schedule([self]() {
            self->HandleError();
        });
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_SCHEDULE_EMIT: {
        ESP_LOGI(TAG, "SDK schedule event (ignored)");
        break;
    }
    }
}

/* ---- Handle methods (called on main thread via Schedule) ---- */

void LingxinSdkProtocol::HandleChatPhaseChange(ChatPhaseCode phase) {
    switch (phase) {
    case CHAT_PHASE_INPUTING:
        audio_channel_opened_ = true;
        if (on_audio_channel_opened_) {
            on_audio_channel_opened_();
        }
        break;
    case CHAT_PHASE_THINKING:
        /* No specific Protocol callback for thinking; handled by Application state */
        break;
    case CHAT_PHASE_OUTPUTING:
        /* AI is speaking; audio comes through buffer_play adapter */
        break;
    case CHAT_PHASE_INTERRUPTING:
        /* User interrupted AI */
        break;
    case CHAT_PHASE_EXITING: {
        bool notify_close = chat_session_active_ || audio_channel_opened_;
        ClearChatSessionFlags();
        if (notify_close && on_audio_channel_closed_) {
            on_audio_channel_closed_();
        }
        break;
    }
    default:
        break;
    }
}

void LingxinSdkProtocol::HandleTextOut(char *text) {
    /* text was strdup'd in SdkLifeCycleHandler, we must free it */
    if (text && on_incoming_json_) {
        cJSON *root = cJSON_Parse(text);
        if (root) {
            on_incoming_json_(root);
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "HandleTextOut: JSON parse failed, raw text: %.128s", text);
        }
    }
    free(text);
}

void LingxinSdkProtocol::HandleExit(ExitCode exit_code, char *reason) {
    /* reason was strdup'd in SdkLifeCycleHandler, we must free it */
    ESP_LOGI(TAG, "HandleExit: code=%d, reason=%s", exit_code, reason ? reason : "null");
    bool notify_close = chat_session_active_ || audio_channel_opened_;
    ClearChatSessionFlags();
    if (notify_close && on_audio_channel_closed_) {
        on_audio_channel_closed_();
    }
    free(reason);
}

void LingxinSdkProtocol::HandlePlayEnd() {
    ESP_LOGI(TAG, "HandlePlayEnd");
    /* Currently no specific callback for play end in Protocol interface */
}

void LingxinSdkProtocol::HandleError() {
    ESP_LOGE(TAG, "HandleError");
    if (on_network_error_) {
        on_network_error_("SDK internal error");
    }
}

/* ---- Constructor / Destructor ---- */

LingxinSdkProtocol::LingxinSdkProtocol() {
    instance_ = this;
}

LingxinSdkProtocol::~LingxinSdkProtocol() {
    instance_ = nullptr;
}

void LingxinSdkProtocol::ClearChatSessionFlags() {
    chat_session_active_ = false;
    audio_channel_opened_ = false;
}

/* ---- Protocol interface ---- */

bool LingxinSdkProtocol::Start() {
    ESP_LOGI(TAG, "Initializing LingXin SDK voice_chat");

    VoiceChatInitProps props = get_voice_chat_init_default_props();
    props.auth_app_id_get_func = GetAppId;
    props.auth_license_get_func = GetLicense;
    props.auth_sn_get_func = GetSn;
    props.auth_app_code_get_func = GetAppCode;
    props.device_code_get_func = GetDeviceCode;
    props.chat_life_cycle_event_listener = SdkLifeCycleHandler;

    /* Audio format configuration from Kconfig */
    props.send_uni_size = 0;  /* Use SDK default */
    props.send_cbuf_scale = 0; /* Use SDK default */

    /* No local prompt sounds - v2.6.6 handles its own sounds */
    props.welcome_audio_path = NULL;
    props.terminate_audio_path = NULL;
    props.continue_audio_path = NULL;

    props.is_schedule_task_on = 0;
    props.is_log_upload_on = 0;

    int ret = voice_chat_init(&props);
    if (ret != 0) {
        ESP_LOGE(TAG, "voice_chat_init failed: %d", ret);
        return false;
    }

    sdk_initialized_ = true;
    ESP_LOGI(TAG, "LingXin SDK initialized successfully");
    return true;
}

bool LingxinSdkProtocol::OpenAudioChannel() {
    /* In SDK mode, OpenAudioChannel starts a new chat session.
     * The actual WS connection is managed by SDK internally. */
    if (!sdk_initialized_) {
        ESP_LOGE(TAG, "SDK not initialized");
        return false;
    }

    if (chat_session_active_) {
        ESP_LOGD(TAG, "Chat session already active, skip start_new_chat");
        return true;
    }

    ESP_LOGI(TAG, "Opening audio channel (start_new_chat)");

    StartNewChatProps start_props = get_start_new_chat_default_props();
    start_props.disable_welcome_audio = true;  /* v2.6.6 plays its own sounds */
    start_props.single_round = false;          /* Multi-turn conversation */

    int ret = start_new_chat(&start_props);
    if (ret != 0) {
        ESP_LOGE(TAG, "start_new_chat failed: %d", ret);
        return false;
    }

    chat_session_active_ = true;
    return true;
}

void LingxinSdkProtocol::CloseAudioChannel(bool send_goodbye) {
    ESP_LOGI(TAG, "Closing audio channel (exit_chat)");

    if (!sdk_initialized_) return;

    ExitChatProps exit_props = get_exit_chat_default_props();
    exit_props.disable_close_ws_immediately = false;

    int ret = exit_chat(&exit_props);
    if (ret != 0) {
        ESP_LOGE(TAG, "exit_chat failed: %d", ret);
    }
    ClearChatSessionFlags();
}

bool LingxinSdkProtocol::IsAudioChannelOpened() const {
    return chat_session_active_ || audio_channel_opened_;
}

bool LingxinSdkProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    /* In SDK mode, audio is sent through the ringbuf bridge,
     * not through this method. This is a no-op. */
    return true;
}

void LingxinSdkProtocol::SendWakeWordDetected(const std::string& wake_word) {
    current_wake_word_ = wake_word;
    /* In SDK mode, wake word triggers start_new_chat via OpenAudioChannel */
}

void LingxinSdkProtocol::SendStartListening(ListeningMode mode) {
    (void)mode;
    /* SDK session is started in OpenAudioChannel; v2.6.6 enables local capture separately. */
    if (!chat_session_active_ && !audio_channel_opened_) {
        ESP_LOGW(TAG, "SendStartListening without active session, opening channel");
        OpenAudioChannel();
    }
}

void LingxinSdkProtocol::SendStopListening() {
    /* In SDK mode, stop recording is handled by SDK's internal VAD */
    StopChatRecordProps stop_props;
    stop_chat_record(&stop_props);
}

void LingxinSdkProtocol::SendAbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "SendAbortSpeaking, reason=%d", reason);
    /* Abort = exit current chat */
    CloseAudioChannel(false);
}

void LingxinSdkProtocol::SendMcpMessage(const std::string& message) {
    /* Send text as user input to SDK */
    if (!sdk_initialized_) return;

    /* Copy message to member buffer so the pointer remains valid
     * through start_new_chat() call (avoids dangling pointer to
     * temporary std::string) */
    mcp_message_buffer_ = message;

    StartNewChatProps start_props = get_start_new_chat_default_props();
    start_props.disable_welcome_audio = true;
    start_props.user_input = const_cast<char*>(mcp_message_buffer_.c_str());

    int ret = start_new_chat(&start_props);
    if (ret != 0) {
        ESP_LOGE(TAG, "start_new_chat with text input failed: %d", ret);
    } else {
        chat_session_active_ = true;
    }
}

bool LingxinSdkProtocol::SendText(const std::string& text) {
    /* Not used in SDK mode - text is sent via SendMcpMessage */
    return true;
}
