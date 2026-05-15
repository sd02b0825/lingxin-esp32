/**
 * lingxin_buffer_play.c - v2.6.6 adapter
 *
 * 重写适配层：SDK 下行音频通过此模块推入 v2.6.6 AudioService 的 DecodeQueue。
 * 不再依赖 ESP-GMF (lingxin_gmf)。
 *
 * SDK 调用 module_bufferPlay_data(buf, rlen) 时，数据为下行音频流（可能是 mp3/pcm/opus），
 * 我们将其封装为 AudioStreamPacket 并推入 AudioService::PushPacketToDecodeQueue。
 */

#include "audio_buffer_play.h"
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "lingxin_log.h"
#include "lingxin_memory.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Fallback if Kconfig is not configured (e.g. standalone build) */
#ifndef CONFIG_LINGXIN_AUDIO_DOWN_CODEC
#define CONFIG_LINGXIN_AUDIO_DOWN_CODEC "pcm"
#endif

static const char *TAG = "lx_adapter_buffer_play";

static PlaybackEventHandler g_callback = NULL;
static void *g_user_data = NULL;
static int is_terminating = 0;
static SemaphoreHandle_t g_terminate_mutex = NULL;

/* Current codec type for downstream audio, initialized from Kconfig */
static char g_current_codec[16] = CONFIG_LINGXIN_AUDIO_DOWN_CODEC;
static int g_current_sample_rate = 16000;

/**
 * Bridge functions implemented in lingxin_sdk_bridge.cc
 * These call into AudioService C++ methods.
 */
extern void audio_service_push_decode_packet(const uint8_t *data, int len, const char *codec, int sample_rate);
extern void audio_service_reset_decoder(void);

void module_bufferPlay_audioInit(PlaybackEventHandler callback, void *user_data)
{
    g_callback = callback;
    g_user_data = user_data;

    if (g_terminate_mutex == NULL) {
        g_terminate_mutex = xSemaphoreCreateMutex();
        if (g_terminate_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create terminate mutex");
            return;
        }
    }

    /* Codec is set from Kconfig at compile time */
    strncpy(g_current_codec, CONFIG_LINGXIN_AUDIO_DOWN_CODEC, sizeof(g_current_codec) - 1);
    g_current_sample_rate = 16000;

    lingxin_log_debug("bufferPlay audioInit, codec=%s, sample_rate=%d", g_current_codec, g_current_sample_rate);

    if (g_callback) {
        g_callback(Lingxin_Download_Audio_InitEnd, g_user_data);
    } else {
        lingxin_log_error("No callback registered");
    }
}

void module_bufferPlay_data(void *buf, int rlen)
{
    if (buf == NULL || rlen <= 0) {
        return;
    }

    if (is_terminating) {
        lingxin_log_debug("bufferplay is terminating, skip data");
        return;
    }

    /* Push audio data to AudioService decode queue via bridge */
    audio_service_push_decode_packet((const uint8_t *)buf, rlen, g_current_codec, g_current_sample_rate);
}

void module_bufferPlay_audioEnd()
{
    if (is_terminating) {
        lingxin_log_debug("bufferplay is terminating.");
        return;
    }
    lingxin_log_debug("audio buffer send finish");

    if (g_callback) {
        g_callback(Lingxin_Download_Audio_PlayEnd, g_user_data);
    } else {
        lingxin_log_error("No callback registered");
    }
}

void module_bufferPlay_terminate()
{
    if (g_terminate_mutex == NULL) {
        ESP_LOGE(TAG, "Terminate mutex not initialized");
        return;
    }

    if (xSemaphoreTake(g_terminate_mutex, portMAX_DELAY) == pdTRUE) {
        is_terminating = 1;
        /* Reset AudioService decoder to stop current playback */
        audio_service_reset_decoder();
        is_terminating = 0;
        xSemaphoreGive(g_terminate_mutex);
    }

    if (g_callback) {
        g_callback(Lingxin_Download_Audio_TerminateEnd, g_user_data);
    } else {
        lingxin_log_error("No callback registered");
    }
}

void module_bufferPlay_setVolume(int volume)
{
    /* Volume is managed by AudioService/Board in v2.6.6 */
    lingxin_log_debug("Volume set request: %d (managed by AudioService)", volume);
}

bool module_bufferPlay_formatCheck(char *format)
{
    if (format) {
        if (strcmp(format, "mp3") == 0 || strcmp(format, "raw_opus") == 0
            || strcmp(format, "opus") == 0 || strcmp(format, "pcm") == 0
            || strcmp(format, "wav") == 0) {
            return true;
        }
    }
    return false;
}
