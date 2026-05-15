#include "state_download_manager.h"
#include "lingxin_log.h"

static const PlaybackInterface *s_player = NULL;

// 回调函数：播放器 → PlaybackManager → 状态机
static void on_playback_event_from_player(LingxinDownloadAudioEvent event, void *user_data)
{
    lingxin_log_debug("在download manager 接受到事件 %d", event);
    StateEvent eve = 0;
    switch (event)
    {
    case Lingxin_Download_Audio_InitEnd:
    {
        eve = State_Event_BufferPlay_AudioInitEnd;
        break;
    }
    case Lingxin_Download_Audio_TerminateEnd:
    {
        eve = State_Event_BufferPlay_TerminateEnd;
        break;
    }
    case Lingxin_Download_Audio_PlayEnd:
    {
        eve = State_Event_BufferPlay_PlayEnd;
        break;
    }
    default:
        break;
    }
    if (eve == 0)
    {
        lingxin_log_debug("无效事件，忽略");
        return;
    }
    state_machine_run_event_with_payload(eve, NULL);
}

void playback_manager_init(ChatStateMediaType type)
{

    lingxin_log_debug("进入下行状态，初始化播放器");

    if (type == Media_Type_Chat)
    {
        extern const PlaybackInterface g_audioPlayer;
        s_player = &g_audioPlayer;
    }
    else
    {
        lingxin_log_debug("Video playback not supported yet");
        return;
    }

    s_player->init(on_playback_event_from_player, NULL);
}

void playback_manager_feed_data(void *buf, int len)
{
    if (s_player && s_player->feedData)
    {
        s_player->feedData(buf, len);
    }
}

void playback_manager_end_stream(void)
{
    if (s_player && s_player->endOfStream)
    {
        s_player->endOfStream();
    }
    else
    {
        state_machine_run_event_with_payload(State_Event_BufferPlay_PlayEnd, NULL);
    }
}

void playback_manager_terminate(void)
{
    if (s_player && s_player->terminate)
    {
        s_player->terminate();
    }
    else
    {
        state_machine_run_event_with_payload(State_Event_BufferPlay_TerminateEnd, NULL);
    }
}

void playback_manager_set_volume(int volume)
{
    if (s_player && s_player->setVolume)
    {
        s_player->setVolume(volume);
    }
}
