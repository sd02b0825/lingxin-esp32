#include "lingxin_websocket_protocol.h"

#include "assets/lang_config.h"
#include "board.h"
#include "lingxin_signer.h"
#include "settings.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_log.h>
#include <sys/time.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#define TAG "LingxinWS"

static constexpr const char* LINGXIN_DEFAULT_V1_ENDPOINT = "wss://eagent.edu-aliyun.com/gw/ws/open/api/v1/agentChat";
static constexpr const char* LINGXIN_DEFAULT_V2_ENDPOINT = "wss://eagent.edu-aliyun.com/gw/ws/open/api/v2/agentChat";
static constexpr const char* LINGXIN_DEFAULT_FULL_DUPLEX_ENDPOINT = "wss://eagent.edu-aliyun.com/gw/ws/open/api/v1/unifiedAccess";
static constexpr int LINGXIN_RECONNECT_COOLDOWN_MS = 2000;

LingxinWebsocketProtocol::LingxinWebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
    LoadConfig();
    task_id_manager_ = std::make_unique<TaskIdManager>(sn_);
}

LingxinWebsocketProtocol::~LingxinWebsocketProtocol() {
    CloseAudioChannel(false);
    vEventGroupDelete(event_group_handle_);
}

bool LingxinWebsocketProtocol::Start() {
    return true;
}

void LingxinWebsocketProtocol::LoadConfig() {
    configured_ws_url_ = GetConfigString("ws_url", "");
    ws_url_ = configured_ws_url_.empty() ? CONFIG_LINGXIN_WS_URL : configured_ws_url_;
    app_id_ = GetConfigString("app_id", CONFIG_LINGXIN_APP_ID);
    app_key_ = GetConfigString("app_key", CONFIG_LINGXIN_APP_KEY);
    sn_ = GetConfigString("sn", CONFIG_LINGXIN_SN);
    ai_app_code_ = GetConfigString("ai_app_code", CONFIG_LINGXIN_AI_APP_CODE);
    device_code_ = GetConfigString("device_code", Board::GetInstance().GetUuid().c_str());
    chat_mode_ = GetConfigString("mode", CONFIG_LINGXIN_CHAT_MODE);
    flow_control_strategy_ = GetConfigString("flow_control_strategy", CONFIG_LINGXIN_FLOW_CONTROL_STRATEGY);
    audio_up_codec_ = GetConfigString("audio_up_codec", CONFIG_LINGXIN_AUDIO_UP_CODEC);
    audio_down_codec_ = GetConfigString("audio_down_codec", CONFIG_LINGXIN_AUDIO_DOWN_CODEC);
    flow_control_max_size_ = GetConfigInt("flow_control_max_size", CONFIG_LINGXIN_FLOW_CONTROL_MAX_SIZE);
    max_sentence_silence_ms_ = GetConfigInt("max_sentence_silence_ms", CONFIG_LINGXIN_MAX_SENTENCE_SILENCE_MS);
    audio_up_sample_rate_ = GetConfigInt("audio_up_sample_rate", CONFIG_LINGXIN_AUDIO_UP_SAMPLE_RATE);
    audio_down_sample_rate_ = GetConfigInt("audio_down_sample_rate", CONFIG_LINGXIN_AUDIO_DOWN_SAMPLE_RATE);
    audio_channels_ = GetConfigInt("audio_channels", CONFIG_LINGXIN_AUDIO_CHANNELS);
    audio_bits_per_sample_ = GetConfigInt("audio_bits_per_sample", CONFIG_LINGXIN_AUDIO_BITS_PER_SAMPLE);
    audio_frame_ms_ = GetConfigInt("audio_frame_ms", CONFIG_LINGXIN_AUDIO_FRAME_MS);

    if (sn_.empty()) {
        sn_ = Board::GetInstance().GetUuid();
    }
    std::transform(chat_mode_.begin(), chat_mode_.end(), chat_mode_.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (chat_mode_ != "voice" && chat_mode_ != "cloud_vad" && chat_mode_ != "text_to_voice" && chat_mode_ != "full_duplex") {
        ESP_LOGW(TAG, "Unsupported chat mode %s, fallback to cloud_vad", chat_mode_.c_str());
        chat_mode_ = "cloud_vad";
    }
    ResolveEndpoint();
    ValidateAudioConfig();
    server_sample_rate_ = audio_down_sample_rate_;
    server_frame_duration_ = audio_frame_ms_;

    ESP_LOGI(TAG, "Lingxin config mode=%s url=%s sn=%s up=%d/%s down=%d/%s channels=%d bits=%d frame=%d",
        chat_mode_.c_str(), ws_url_.c_str(), sn_.c_str(), audio_up_sample_rate_, audio_up_codec_.c_str(),
        audio_down_sample_rate_, audio_down_codec_.c_str(), audio_channels_, audio_bits_per_sample_, audio_frame_ms_);
}

void LingxinWebsocketProtocol::ResolveEndpoint() {
    if (!configured_ws_url_.empty() || std::string(CONFIG_LINGXIN_WS_URL) != LINGXIN_DEFAULT_V1_ENDPOINT) {
        return;
    }

    if (chat_mode_ == "cloud_vad") {
        ws_url_ = LINGXIN_DEFAULT_V2_ENDPOINT;
    } else if (chat_mode_ == "full_duplex") {
        ws_url_ = LINGXIN_DEFAULT_FULL_DUPLEX_ENDPOINT;
    } else {
        ws_url_ = LINGXIN_DEFAULT_V1_ENDPOINT;
    }
}

std::string LingxinWebsocketProtocol::ResolveRuntimeMode(ListeningMode mode) const {
    if (chat_mode_ == "full_duplex" && mode != kListeningModeRealtime) {
        ESP_LOGE(TAG, "full_duplex requires realtime listening and device-side AEC");
        return "";
    }
    return chat_mode_;
}

bool LingxinWebsocketProtocol::ValidateAudioConfig() {
    auto normalize_codec = [](std::string& codec, const char* fallback) {
        std::transform(codec.begin(), codec.end(), codec.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (codec == "raw_opus") {
            codec = "opus";
        } else if (codec == "raw_pcm") {
            codec = "pcm";
        }
        if (codec != "opus" && codec != "pcm" && codec != "wav" && codec != "mp3") {
            ESP_LOGW(TAG, "Unsupported audio codec %s, fallback to %s", codec.c_str(), fallback);
            codec = fallback;
        }
    };

    normalize_codec(audio_up_codec_, "opus");
    normalize_codec(audio_down_codec_, "pcm");
    if (audio_up_sample_rate_ <= 0) {
        ESP_LOGW(TAG, "Invalid uplink sample rate %d, fallback to 16000", audio_up_sample_rate_);
        audio_up_sample_rate_ = 16000;
    }
    if (audio_down_sample_rate_ <= 0) {
        ESP_LOGW(TAG, "Invalid downlink sample rate %d, fallback to 16000", audio_down_sample_rate_);
        audio_down_sample_rate_ = 16000;
    }
    if (audio_channels_ != 1 && audio_channels_ != 2) {
        ESP_LOGW(TAG, "Invalid channel count %d, fallback to mono", audio_channels_);
        audio_channels_ = 1;
    }
    if (audio_bits_per_sample_ != 16) {
        ESP_LOGW(TAG, "Unsupported bit depth %d, fallback to 16", audio_bits_per_sample_);
        audio_bits_per_sample_ = 16;
    }
    if (audio_frame_ms_ <= 0) {
        ESP_LOGW(TAG, "Invalid frame duration %d, fallback to 60", audio_frame_ms_);
        audio_frame_ms_ = 60;
    }
    return true;
}

std::string LingxinWebsocketProtocol::GetConfigString(const std::string& key, const char* default_value) {
    Settings settings("lingxin", false);
    return settings.GetString(key, default_value == nullptr ? "" : default_value);
}

int LingxinWebsocketProtocol::GetConfigInt(const std::string& key, int default_value) {
    Settings settings("lingxin", false);
    return settings.GetInt(key, default_value);
}

std::string LingxinWebsocketProtocol::CurrentTimestampMs() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t timestamp_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return std::to_string(timestamp_ms);
}

bool LingxinWebsocketProtocol::OpenAudioChannel() {
    if (IsAudioChannelOpened()) {
        return true;
    }
    if (last_channel_closed_time_.time_since_epoch().count() > 0) {
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_channel_closed_time_).count();
        if (elapsed_ms < LINGXIN_RECONNECT_COOLDOWN_MS) {
            ESP_LOGW(TAG, "Reconnect is cooling down to avoid duplicate Lingxin websocket connections");
            SetError(Lang::Strings::SERVER_NOT_CONNECTED);
            return false;
        }
    }
    if (ws_url_.empty() || app_id_.empty() || app_key_.empty() || sn_.empty()) {
        ESP_LOGE(TAG, "Missing Lingxin websocket auth config");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < 1700000000) {
        ESP_LOGE(TAG, "System time is not synchronized, refuse Lingxin connection to avoid signature rejection");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    error_occurred_ = false;
    task_started_ = false;
    task_inflight_ = false;
    request_id_.clear();
    xEventGroupClearBits(event_group_handle_, LINGXIN_TASK_STARTED_EVENT);
    websocket_.reset();

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    std::string timestamp = CurrentTimestampMs();
    std::string signature = LingxinSigner::GenerateSignature(sn_, app_key_, app_id_, timestamp);
    if (signature.empty()) {
        ESP_LOGE(TAG, "Failed to generate Lingxin signature");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    websocket_->SetHeader("app_id", app_id_.c_str());
    websocket_->SetHeader("sn", sn_.c_str());
    websocket_->SetHeader("timestamp", timestamp.c_str());
    websocket_->SetHeader("signature", signature.c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        last_incoming_time_ = std::chrono::steady_clock::now();
        if (binary) {
            if (on_incoming_audio_ != nullptr) {
                on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                    .sample_rate = server_sample_rate_,
                    .frame_duration = server_frame_duration_,
                    .channels = audio_channels_,
                    .bits_per_sample = audio_bits_per_sample_,
                    .timestamp = 0,
                    .codec = audio_down_codec_,
                    .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                }));
            }
            return;
        }

        cJSON* root = cJSON_ParseWithLength(data, len);
        if (root == nullptr) {
            ESP_LOGE(TAG, "Failed to parse Lingxin JSON: %.*s", static_cast<int>(len), data);
            return;
        }
        HandleJsonMessage(root);
        cJSON_Delete(root);
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Lingxin websocket disconnected");
        task_started_ = false;
        task_inflight_ = false;
        last_channel_closed_time_ = std::chrono::steady_clock::now();
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
        if (on_disconnected_ != nullptr) {
            on_disconnected_();
        }
    });

    ESP_LOGI(TAG, "Connecting to Lingxin websocket: %s", ws_url_.c_str());
    if (!websocket_->Connect(ws_url_.c_str())) {
        ESP_LOGE(TAG, "Failed to connect Lingxin websocket, code=%d", websocket_->GetLastError());
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    last_incoming_time_ = std::chrono::steady_clock::now();
    if (on_connected_ != nullptr) {
        on_connected_();
    }
    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

void LingxinWebsocketProtocol::CloseAudioChannel(bool send_goodbye) {
    if (send_goodbye && websocket_ != nullptr && websocket_->IsConnected() && task_inflight_) {
        SendTaskCommand(task_started_ ? "end_task" : "terminate_task");
    }
    websocket_.reset();
    task_started_ = false;
    task_inflight_ = false;
    last_channel_closed_time_ = std::chrono::steady_clock::now();
}

bool LingxinWebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

bool LingxinWebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (!task_started_) {
        return true;
    }
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }
    if (audio_up_codec_ != "opus") {
        ESP_LOGE(TAG, "Uplink codec %s is configured but the current capture path only produces Opus frames", audio_up_codec_.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
}

bool LingxinWebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }
    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send Lingxin text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return true;
}

std::string LingxinWebsocketProtocol::BuildTaskCommand(const std::string& action) const {
    cJSON* root = cJSON_CreateObject();
    cJSON* header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "action", action.c_str());
    cJSON_AddStringToObject(header, "task_id", task_id_manager_->Current().c_str());
    if (!request_id_.empty() && action != "start_task") {
        cJSON_AddStringToObject(header, "request_id", request_id_.c_str());
    }
    cJSON_AddItemToObject(root, "header", header);
    cJSON_AddItemToObject(root, "payload", cJSON_CreateObject());

    char* json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str == nullptr ? "" : json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

std::string LingxinWebsocketProtocol::BuildStartTaskMessage(ListeningMode mode) {
    std::string runtime_mode = ResolveRuntimeMode(mode);
    if (runtime_mode.empty()) {
        SetError(Lang::Strings::SERVER_ERROR);
        return "";
    }
    if (runtime_mode == "text_to_voice" && pending_text_input_.empty()) {
        ESP_LOGE(TAG, "text_to_voice requires pending user input");
        SetError(Lang::Strings::SERVER_ERROR);
        return "";
    }
    std::string task_id = (mode == kListeningModeManualStop || task_id_manager_->Current().empty())
        ? task_id_manager_->BeginNewConversation()
        : task_id_manager_->BeginNextTurn();

    cJSON* root = cJSON_CreateObject();
    cJSON* header = cJSON_CreateObject();
    cJSON_AddStringToObject(header, "action", "start_task");
    cJSON_AddStringToObject(header, "task_id", task_id.c_str());
    cJSON_AddItemToObject(root, "header", header);

    cJSON* payload = cJSON_CreateObject();
    const bool text_to_voice = runtime_mode == "text_to_voice";
    const bool full_duplex = runtime_mode == "full_duplex";
    cJSON_AddStringToObject(payload, "input_mode", text_to_voice ? "no_voice" : "voice");
    cJSON_AddStringToObject(payload, "output_mode", "voice");
    if (full_duplex) {
        cJSON_AddStringToObject(payload, "task", "chat_full_duplex");
    }
    cJSON_AddStringToObject(payload, "ai_app_code", ai_app_code_.c_str());
    cJSON* agent_basic_inputs = cJSON_CreateObject();
    if (text_to_voice) {
        cJSON_AddStringToObject(agent_basic_inputs, "user_input", pending_text_input_.c_str());
    }
    cJSON_AddItemToObject(payload, "agent_basic_inputs", agent_basic_inputs);

    cJSON* agent_ext_inputs = cJSON_CreateObject();
    cJSON_AddBoolToObject(agent_ext_inputs, "play_prologue", false);
    cJSON_AddBoolToObject(agent_ext_inputs, "show_asr_output", true);
    cJSON_AddItemToObject(payload, "agent_ext_inputs", agent_ext_inputs);

    cJSON* flow_control = cJSON_CreateObject();
    if (!flow_control_strategy_.empty() && flow_control_strategy_ != "none") {
        cJSON_AddStringToObject(flow_control, "flow_control_strategy", flow_control_strategy_.c_str());
        cJSON_AddNumberToObject(flow_control, "max_size", flow_control_max_size_);
    }
    cJSON_AddItemToObject(payload, "flow_control_parameters", flow_control);

    cJSON* biz_parameters = cJSON_CreateObject();
    cJSON_AddStringToObject(biz_parameters, "device_code", device_code_.c_str());
    cJSON_AddItemToObject(payload, "biz_parameters", biz_parameters);

    // 灵芯 Cloud VAD 用 max_sentence_silence 控制自动断句 / Cloud VAD uses this to end utterances.
    if (runtime_mode == "cloud_vad" && max_sentence_silence_ms_ > 0) {
        cJSON_AddNumberToObject(payload, "max_sentence_silence", max_sentence_silence_ms_);
    }
    cJSON_AddStringToObject(payload, "input_format", audio_up_codec_.c_str());
    cJSON_AddStringToObject(payload, "output_format", audio_down_codec_.c_str());
    cJSON_AddNumberToObject(payload, "input_sample_rate", audio_up_sample_rate_);
    cJSON_AddNumberToObject(payload, "output_sample_rate", audio_down_sample_rate_);
    cJSON_AddItemToObject(root, "payload", payload);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str == nullptr ? "" : json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

bool LingxinWebsocketProtocol::SendTaskCommand(const std::string& action) {
    std::string message = BuildTaskCommand(action);
    if (message.empty()) {
        return false;
    }
    return SendText(message);
}

void LingxinWebsocketProtocol::SendStartListening(ListeningMode mode) {
    if (!IsAudioChannelOpened()) {
        return;
    }

    task_started_ = false;
    task_inflight_ = true;
    request_id_.clear();
    xEventGroupClearBits(event_group_handle_, LINGXIN_TASK_STARTED_EVENT);
    std::string message = BuildStartTaskMessage(mode);
    if (message.empty()) {
        task_inflight_ = false;
        return;
    }
    if (!SendText(message)) {
        return;
    }

    // 先等待 task_started 再启用本地录音，避免音频早于服务端状态机 / Wait before recording so audio cannot arrive before server is ready.
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, LINGXIN_TASK_STARTED_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & LINGXIN_TASK_STARTED_EVENT)) {
        ESP_LOGE(TAG, "Timed out waiting for task_started");
        SetError(Lang::Strings::SERVER_TIMEOUT);
    }
}

void LingxinWebsocketProtocol::SendStopListening() {
    if (task_inflight_) {
        SendTaskCommand("end_audio");
        task_started_ = false;
    }
}

void LingxinWebsocketProtocol::SendAbortSpeaking(AbortReason reason) {
    (void)reason;
    if (task_inflight_) {
        SendTaskCommand("terminate_task");
    }
}

void LingxinWebsocketProtocol::SendWakeWordDetected(const std::string& wake_word) {
    (void)wake_word;
}

void LingxinWebsocketProtocol::SendMcpMessage(const std::string& message) {
    if (chat_mode_ != "text_to_voice") {
        ESP_LOGW(TAG, "Ignoring MCP message because Lingxin chat mode is %s", chat_mode_.c_str());
        return;
    }
    pending_text_input_ = message;
    SendStartListening(kListeningModeManualStop);
}

void LingxinWebsocketProtocol::HandleJsonMessage(const cJSON* root) {
    const cJSON* header = cJSON_GetObjectItem(root, "header");
    const cJSON* action = cJSON_IsObject(header) ? cJSON_GetObjectItem(header, "action") : nullptr;
    if (!cJSON_IsString(action)) {
        ESP_LOGW(TAG, "Missing Lingxin header.action");
        return;
    }

    const cJSON* request_id = cJSON_GetObjectItem(header, "request_id");
    if (cJSON_IsString(request_id)) {
        request_id_ = request_id->valuestring;
    }
    const cJSON* code = cJSON_GetObjectItem(header, "code");
    if (cJSON_IsString(code) && strcmp(code->valuestring, "20000001") != 0) {
        const cJSON* err_msg = cJSON_GetObjectItem(header, "err_msg");
        const cJSON* task_id = cJSON_GetObjectItem(header, "task_id");
        std::string message = cJSON_IsString(err_msg) ? err_msg->valuestring : Lang::Strings::SERVER_ERROR;
        ESP_LOGE(TAG, "Lingxin error action=%s code=%s request_id=%s task_id=%s message=%s",
            action->valuestring,
            code->valuestring,
            request_id_.empty() ? "" : request_id_.c_str(),
            cJSON_IsString(task_id) ? task_id->valuestring : task_id_manager_->Current().c_str(),
            message.c_str());
        SetError(message);
    }

    const char* action_value = action->valuestring;
    ESP_LOGI(TAG, "Lingxin action: %s", action_value);

    if (strcmp(action_value, "task_started") == 0) {
        task_started_ = true;
        xEventGroupSetBits(event_group_handle_, LINGXIN_TASK_STARTED_EVENT);
    } else if (strcmp(action_value, "audio_response_end") == 0) {
        task_started_ = false;
        if (task_inflight_) {
            SendTaskCommand("end_task");
        }
    } else if (strcmp(action_value, "task_ended") == 0 || strcmp(action_value, "task_terminated") == 0) {
        task_started_ = false;
        task_inflight_ = false;
        task_id_manager_->MarkTaskClosed();
    } else if (strcmp(action_value, "vad_end") == 0 || strcmp(action_value, "vad_exit") == 0) {
        task_started_ = false;
    }

    if (on_incoming_json_ != nullptr) {
        on_incoming_json_(root);
    }
}
