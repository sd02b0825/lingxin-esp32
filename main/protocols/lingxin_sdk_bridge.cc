/**
 * lingxin_sdk_bridge.cc - C/C++ bridge implementation
 *
 * Implements extern "C" functions declared in lingxin_sdk_bridge.h,
 * providing access from SDK adapter C code to v2.6.6 AudioService/Board C++ objects.
 */

#include "lingxin_sdk_bridge.h"
#include "audio_service.h"
#include "application.h"
#include "boards/common/board.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string>

/* Fallback if Kconfig is not configured */
#ifndef CONFIG_LINGXIN_AUDIO_DOWN_CODEC
#define CONFIG_LINGXIN_AUDIO_DOWN_CODEC "pcm"
#endif

static const char *TAG = "lx_sdk_bridge";

/* ---- AudioService record bridge ---- */

static bool g_sdk_record_mode = false;

void audio_service_start_record_to_sdk(void)
{
    ESP_LOGI(TAG, "SDK record mode: START");
    g_sdk_record_mode = true;
}

void audio_service_stop_record_to_sdk(void)
{
    ESP_LOGI(TAG, "SDK record mode: STOP");
    g_sdk_record_mode = false;
}

int lingxin_sdk_is_record_mode(void)
{
    return g_sdk_record_mode ? 1 : 0;
}

/* ---- AudioService playback bridge ---- */

void audio_service_push_decode_packet(const uint8_t *data, int len, const char *codec, int sample_rate)
{
    auto &audio_service = Application::GetInstance().GetAudioService();

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

    /* SDK passes filesystem paths like "/spiffs/welcome.mp3".
     * v2.6.6 AudioService::PlaySound expects an OGG sound name from embedded assets.
     * Since we set welcome/terminate/continue_audio_path to NULL in voice_chat_init,
     * this should only be called if SDK needs ad-hoc local playback.
     * We map known paths to v2.6.6 sounds or read and push the file.
     */
    ESP_LOGI(TAG, "SDK local sound request: %s", audio_path);

    /* Try to play via AudioService's PlaySound if it's a known sound name */
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
    /* Use compile-time version macro from IDF */
#ifdef CONFIG_LINGXIN_DEVICE_VERSION
    return CONFIG_LINGXIN_DEVICE_VERSION;
#else
    return "1.3.0";
#endif
}
