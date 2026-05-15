/**
 * lingxin_local_play.c - v2.6.6 adapter
 *
 * 重写适配层：SDK 本地播放（welcome/terminate/continue 提示音）
 * 通过 v2.6.6 AudioService 的 PlaySound 接口播放。
 * 不再依赖 ESP-GMF 和 esp_audio_simple_player。
 */

#include "lingxin_local_player.h"
#include "lingxin_log.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "lx_adapter_local_player";

typedef struct {
    int dummy;  /* Placeholder - no state needed */
} local_audio_handle_t;

static local_audio_handle_t local_audio_player;

/**
 * Bridge function implemented in lingxin_sdk_bridge.cc
 * Maps SDK audio path to AudioService::PlaySound or direct playback.
 */
extern void audio_service_play_local_sound(const char *audio_path);

lingxin_local_player_t lingxin_local_player_create()
{
    lingxin_log_debug("lingxin_local_player_create (v2.6.6 adapter)");
    return &local_audio_player;
}

void lingxin_local_player_play(lingxin_local_player_t player, lingxin_local_player_play_param_t *param, lingxin_local_player_callback_t callback)
{
    if (!player || !param || !callback) {
        ESP_LOGW(TAG, "invalid param");
        goto play_err;
    }

    if (!param->audio_path || strlen(param->audio_path) == 0) {
        ESP_LOGE(TAG, "invalid audio path");
        goto play_err;
    }

    lingxin_log_debug("Playing local audio: %s", param->audio_path);

    /* Play via AudioService bridge */
    audio_service_play_local_sound(param->audio_path);

    lingxin_log_debug("Local audio playback finished");
    if (callback) {
        callback(0);
    }
    return;

play_err:
    if (callback) {
        callback(-1);
    }
}

void lingxin_local_player_set_volume(lingxin_local_player_t player, int volume)
{
    /* Volume managed by AudioService/Board in v2.6.6 */
    lingxin_log_debug("Volume set request: %d (managed by AudioService)", volume);
}

void lingxin_local_player_destory(lingxin_local_player_t player)
{
    ESP_LOGI(TAG, "lingxin_local_player_destory (v2.6.6 adapter, no-op)");
}
