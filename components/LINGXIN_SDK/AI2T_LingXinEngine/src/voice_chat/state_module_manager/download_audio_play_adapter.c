// audio_player_adapter.c
#include "download_audio_adapter.h"
#include "audio_buffer_play.h"
#include "download_audio_play_interface.h"

static void audio_init(PlaybackEventHandler callback, void *user_data)
{
    // 把回调传给底层播放器
    module_bufferPlay_audioInit(callback, user_data);
}

static void audio_feedData(const void *buf, int len)
{
    module_bufferPlay_data((void *)buf, len);
}

static void audio_endOfStream(void)
{
    module_bufferPlay_audioEnd();
}

static void audio_terminate(void)
{
    module_bufferPlay_terminate();
}

static void audio_setVolume(int volume)
{
    module_bufferPlay_setVolume(volume);
}

const PlaybackInterface g_audioPlayer = {
    .init = audio_init,
    .feedData = audio_feedData,
    .endOfStream = audio_endOfStream,
    .terminate = audio_terminate,
    .setVolume = audio_setVolume};
