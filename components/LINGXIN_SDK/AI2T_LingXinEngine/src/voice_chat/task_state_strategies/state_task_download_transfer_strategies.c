// 下行模块数据传输状态策略
#include "state_task_download_transfer_strategies.h"
#include "lingxin_log.h"
#include "chat_state_machine.h"
#include "state_download_manager.h"

void turn_to_download_transfer(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload)
{
    if (!current_context)
    {
        lingxin_log_debug(" current_context is null");
        return;
    }
}

// 接受到事件
void state_download_transfer_receive_event(StateEvent event, StateEventPayload *payload)
{
    switch (event)
    {
    case State_Event_VoiceChat_AIEnd:
    {
        // 结束播放
        playback_manager_end_stream();
        break;
    }

    case State_Event_BufferPlay_PlayEnd:
    {
        // 结束播放
        lingxin_log_debug("下行模块播放结束，进入下行结束态");
        turn_to_with_preset_inner_state(State_Download_End, event, NULL);
        break;
    }

    default:
        lingxin_log_debug("");
        break;
    }
}

void state_download_feed_data(void *data, int len)
{
    playback_manager_feed_data(data, len);
}