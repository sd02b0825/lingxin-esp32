/**
 * lingxin_sdk_protocol.h - LingXin SDK Protocol for v2.6.6
 *
 * Wraps LingXin SDK's voice_chat API (chat_api.h) as a Protocol implementation.
 * Replaces LingxinWebsocketProtocol with SDK-managed WebSocket, state machine,
 * and audio lifecycle.
 */

#ifndef LINGXIN_SDK_PROTOCOL_H
#define LINGXIN_SDK_PROTOCOL_H

#include "protocol.h"
#include "chat_api.h"

class LingxinSdkProtocol : public Protocol {
public:
    LingxinSdkProtocol();
    ~LingxinSdkProtocol() override;

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

    /**
     * Called from SDK's ChatLifeCycleEventListener (which runs on SDK's thread).
     * Dispatches events to the main thread via Application::Schedule.
     */
    static void SdkLifeCycleHandler(ChatLifeCycleEvent event, void *payload);

    /**
     * Get the singleton instance (used by static callback).
     */
    static LingxinSdkProtocol* GetInstance() { return instance_; }

    /**
     * Check if SDK has been initialized.
     */
    bool IsInitialized() const { return sdk_initialized_; }

private:
    bool SendText(const std::string& text) override;

    bool sdk_initialized_ = false;
    bool audio_channel_opened_ = false;
    /** Set synchronously after start_new_chat succeeds; cleared on exit. */
    bool chat_session_active_ = false;
    std::string current_wake_word_;

    void ClearChatSessionFlags();

    static LingxinSdkProtocol* instance_;

    /* Auth function callbacks (static, called by SDK) */
    static char* GetAppId();
    static char* GetLicense();
    static char* GetSn();
    static char* GetAppCode();
    static char* GetDeviceCode();

    /* Internal event handlers called on main thread via Schedule */
    void HandleChatPhaseChange(ChatPhaseCode phase);
    void HandleTextOut(char *text);  /* takes ownership (strdup'd), caller must free */
    void HandleExit(ExitCode exit_code, char *reason);  /* takes ownership (strdup'd) */
    void HandlePlayEnd();
    void HandleError();

    /* Buffer for SendMcpMessage - avoids dangling pointer to temporary string */
    std::string mcp_message_buffer_;
};

#endif /* LINGXIN_SDK_PROTOCOL_H */
