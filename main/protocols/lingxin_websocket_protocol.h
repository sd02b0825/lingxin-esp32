#ifndef LINGXIN_WEBSOCKET_PROTOCOL_H
#define LINGXIN_WEBSOCKET_PROTOCOL_H

#include "protocol.h"
#include "task_id_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <web_socket.h>

#include <memory>
#include <string>
#include <chrono>

#define LINGXIN_TASK_STARTED_EVENT (1 << 0)

class LingxinWebsocketProtocol : public Protocol {
public:
    LingxinWebsocketProtocol();
    ~LingxinWebsocketProtocol();

    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    void SendWakeWordDetected(const std::string& wake_word) override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;
    void SendAbortSpeaking(AbortReason reason) override;
    void SendMcpMessage(const std::string& message) override;

private:
    EventGroupHandle_t event_group_handle_;
    std::unique_ptr<WebSocket> websocket_;
    std::unique_ptr<TaskIdManager> task_id_manager_;

    std::string ws_url_;
    std::string app_id_;
    std::string app_key_;
    std::string sn_;
    std::string ai_app_code_;
    std::string device_code_;
    std::string chat_mode_;
    std::string configured_ws_url_;
    std::string flow_control_strategy_;
    std::string audio_up_codec_;
    std::string audio_down_codec_;
    int flow_control_max_size_ = 32;
    int flow_control_space_time_ms_ = 120;
    int max_sentence_silence_ms_ = 800;
    int audio_up_sample_rate_ = 16000;
    int audio_down_sample_rate_ = 16000;
    int audio_channels_ = 1;
    int audio_bits_per_sample_ = 16;
    int audio_frame_ms_ = 60;
    bool task_started_ = false;
    bool task_inflight_ = false;
    bool end_task_sent_ = false;
    std::string request_id_;
    std::string pending_text_input_;
    std::chrono::time_point<std::chrono::steady_clock> last_channel_closed_time_;

    void LoadConfig();
    bool ValidateAudioConfig();
    void ResolveEndpoint();
    std::string ResolveRuntimeMode(ListeningMode mode) const;
    std::string GetConfigString(const std::string& key, const char* default_value);
    int GetConfigInt(const std::string& key, int default_value);
    std::string CurrentTimestampMs() const;
    std::string BuildTaskCommand(const std::string& action) const;
    std::string BuildStartTaskMessage(ListeningMode mode);
    bool SendText(const std::string& text) override;
    bool SendTaskCommand(const std::string& action);
    void HandleJsonMessage(const cJSON* root);
};

#endif // LINGXIN_WEBSOCKET_PROTOCOL_H
