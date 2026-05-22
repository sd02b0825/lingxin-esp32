/**
 * lingxin_sdk_bridge.cc - C/C++ bridge implementation
 *
 * Implements extern "C" functions declared in lingxin_sdk_bridge.h,
 * providing access from SDK adapter C code to v2.6.6 AudioService/Board C++ objects.
 */

#include "lingxin_sdk_bridge.h"
#include "lingxin_sdk_protocol.h"
#include "audio_service.h"
#include "application.h"
#include "device_state.h"
#include "boards/common/board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <string>

#if CONFIG_USE_AUDIO_PROCESSOR
#include "processors/afe_audio_processor.h"
#endif

/* Fallback if Kconfig is not configured */
#ifndef CONFIG_LINGXIN_AUDIO_DOWN_CODEC
#define CONFIG_LINGXIN_AUDIO_DOWN_CODEC "pcm"
#endif

static const char *TAG = "lx_sdk_bridge";

/* ---- AudioService record bridge ---- */

static bool g_sdk_record_mode = false;
static bool g_sdk_uplink_active = false;

extern "C" void lingxin_recorder_finish_open(void *recorder_hdl);

void audio_service_start_record_to_sdk(void)
{
    ESP_LOGI(TAG, "SDK record mode: START");
    g_sdk_record_mode = true;
}

void audio_service_stop_record_to_sdk(void)
{
    ESP_LOGI(TAG, "SDK record mode: STOP");
    g_sdk_record_mode = false;
#if CONFIG_USE_AUDIO_PROCESSOR
    Application::GetInstance().GetAudioService().SetProcessorTaskPriority(3);
#endif
}

int lingxin_sdk_is_record_mode(void)
{
    return g_sdk_record_mode ? 1 : 0;
}

int audio_service_is_sdk_uplink_active(void)
{
    return g_sdk_uplink_active ? 1 : 0;
}

void audio_service_wait_playback_idle(void)
{
    int64_t t0 = esp_timer_get_time();
    Application::GetInstance().GetAudioService().WaitForPlaybackQueueEmpty();
    int wait_ms = static_cast<int>((esp_timer_get_time() - t0) / 1000);
    ESP_LOGI(TAG, "wait_playback_idle done in %d ms", wait_ms);
}

int audio_service_is_playback_busy(void)
{
    return Application::GetInstance().GetAudioService().IsPlaybackBusy() ? 1 : 0;
}

void audio_service_schedule_recorder_uplink_begin(void *recorder_hdl)
{
    if (recorder_hdl == nullptr) {
        return;
    }

    Application::GetInstance().Schedule([recorder_hdl]() {
        int64_t t0 = esp_timer_get_time();
        auto &audio_service = Application::GetInstance().GetAudioService();
        audio_service.WaitForPlaybackQueueEmpty();
        int wait_ms = static_cast<int>((esp_timer_get_time() - t0) / 1000);
        ESP_LOGI(TAG, "recorder uplink: wait_playback %d ms", wait_ms);

        audio_service_start_record_to_sdk();
        g_sdk_uplink_active = true;
        audio_service.EnableVoiceProcessing(true);
#if CONFIG_USE_AUDIO_PROCESSOR
        audio_service.SetProcessorTaskPriority(5);
#endif

        auto &app = Application::GetInstance();
        if (app.GetDeviceState() != kDeviceStateListening &&
            app.GetDeviceState() != kDeviceStateConnecting) {
            app.SetDeviceState(kDeviceStateListening);
        }

        lingxin_recorder_finish_open(recorder_hdl);
    });
}

void audio_service_schedule_recorder_uplink_end(void)
{
    Application::GetInstance().Schedule([]() {
        g_sdk_uplink_active = false;
        g_sdk_record_mode = false;
#if CONFIG_USE_AUDIO_PROCESSOR
        Application::GetInstance().GetAudioService().SetProcessorTaskPriority(3);
#endif
        if (!g_sdk_record_mode) {
            Application::GetInstance().GetAudioService().EnableVoiceProcessing(false);
        }
        ESP_LOGI(TAG, "recorder uplink: deactivated");
    });
}

/* ---- AudioService playback bridge ---- */

void audio_service_push_decode_packet(const uint8_t *data, int len, const char *codec, int sample_rate)
{
    auto &audio_service = Application::GetInstance().GetAudioService();


    if (LingxinSdkProtocol::GetInstance()) {
        LingxinSdkProtocol::GetInstance()->OnDownlinkStarted();
    }

    auto packet = std::make_unique<AudioStreamPacket>();
    packet->codec = codec ? codec : CONFIG_LINGXIN_AUDIO_DOWN_CODEC;
    packet->sample_rate = sample_rate;
    packet->channels = 1;
    packet->bits_per_sample = 16;
    packet->frame_duration = 60;
    packet->payload.assign(data, data + len);

    if (!audio_service.PushPacketToDecodeQueue(std::move(packet))) {
        ESP_LOGW(TAG, "Decode queue full, dropping packet (%d bytes)", len);
    }
}

void audio_service_reset_decoder(void)
{
    auto &audio_service = Application::GetInstance().GetAudioService();
    audio_service.ResetDecoder();
}

/* ---- AudioService local sound bridge ---- */

void audio_service_play_local_sound(const char *audio_path)
{
    if (!audio_path) return;

    auto &audio_service = Application::GetInstance().GetAudioService();

    ESP_LOGI(TAG, "SDK local sound request: %s", audio_path);
    audio_service.PlaySound(audio_path);
}

/* ---- Device info bridge ---- */

const char *lingxin_bridge_get_device_name(void)
{
    static std::string device_name;
    if (device_name.empty()) {
#ifdef CONFIG_LINGXIN_DEVICE_NAME
        device_name = CONFIG_LINGXIN_DEVICE_NAME;
#else
        return "LINGXIN_ESP32S3";
#endif
    }
    return device_name.c_str();
}

const char *lingxin_bridge_get_device_version(void)
{
#ifdef CONFIG_LINGXIN_DEVICE_VERSION
    return CONFIG_LINGXIN_DEVICE_VERSION;
#else
    return "1.3.0";
#endif
}
