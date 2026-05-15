#include <stdio.h>
#include "lingxin_log.h"
#include "chat_state_machine.h"
#include "state_task_exit_strategies.h"
#include "state_upload_manager.h"
#include "state_download_manager.h"
// 切换到任务完成态
void turn_to_exit(const ChatStateRuntimeContext *current_context, InnerStateCollection *payload, StateEvent event)
{

    InnerStateForExit *s = payload ? payload->inner_state_for_exit : NULL;
    if (!s)
        return;

    if (s->record_terminated &&
        s->buffer_play_terminated &&
        s->voice_chat_terminated &&
        s->voice_chat_exited)
    {
        // 执行退出动作
        lingxin_log_debug("退出状态: 已全部终止，进入Idle状态");
        turn_to_with_preset_inner_state(State_Idle, event, NULL);
        return;
    }

    if (!s->record_terminated)
    {
        lingxin_log_debug("退出状态: 终止上行");
        upload_manager_terminate();
    }
    if (!s->buffer_play_terminated)
    {
        lingxin_log_debug("退出状态: 终止播放");
        playback_manager_terminate();
    }
    if (!s->voice_chat_terminated)
    {
        lingxin_log_debug("退出状态: 终止对话");
        module_voiceChat_terminate();
    }
    if (!s->voice_chat_exited)
    {
        lingxin_log_debug("退出状态: 销毁websocket连接");
        module_voiceChat_exit();
    }

    if (!current_context)
    {
        lingxin_log_debug(" current_context is null");
        return;
    }
}

// 接受到事件
void state_exit_receive_event(StateEvent event, StateEventPayload *payload)
{
    lingxin_log_debug("state_machine_receive_event: %d", event);
    // 根据当前状态和事件，决定状态转移
    switch (event)
    {

    default:
        lingxin_log_debug("exit状态接受异常事件 %d", event);
        break;
    }
}