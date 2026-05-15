// 下行模块初始化策略
#include "state_task_download_init_strategies.h"
#include "lingxin_log.h"
#include "chat_state_machine.h"
#include "state_download_manager.h"
#include "lingxin_download_stream_control_manager.h"

void turn_to_download_init(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload)
{
    if (!current_context)
    {
        lingxin_log_debug(" current_context is null");
        return;
    }
    // 执行下行模块初始化策略
    lingxin_log_debug("进入download init状态，初始化播放器");
    ChatStateMediaType download_type = current_context->download_type;
    playback_manager_init(download_type);
}

// 接受到事件
void state_download_init_receive_event(StateEvent event, StateEventPayload *payload)
{
    switch (event)
    {
    case State_Event_BufferPlay_AudioInitEnd:
    {
        turn_to_with_preset_inner_state(State_Download_Play, event, NULL);
    }
    break;
    case State_Event_VoiceChat_AIEnd:
    {
        // 纯文本
        turn_to_with_preset_inner_state(State_Download_End, event, NULL);
        break;
    }

    default:
        break;
    }
}