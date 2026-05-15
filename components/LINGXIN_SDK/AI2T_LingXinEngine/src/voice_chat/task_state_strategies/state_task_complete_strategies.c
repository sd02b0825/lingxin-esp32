#include <stdio.h>
#include "lingxin_log.h"
#include "chat_state_machine.h"
#include "state_task_complete_stategies.h"
#include "chat_runtime_context.h"
#include "state_download_manager.h"

static void continue_strategy(StateEvent event)
{
    end_current_chat_runtime_context();

    // 执行继续对话策略
    start_new_chat_runtime_context();

    turn_to_with_preset_inner_state(State_Upload_Init, event, NULL);
}

static void continue_with_prompt_strategy()
{
    // 执行带提示音的继续对话策略
    lingxin_log_debug("播放跟进提示音");
    module_local_play_continue_audio();
}

static void vad_exit_strategy(bool input_timeout_audio)
{
    // 执行vad退出策略
    if (input_timeout_audio)
    {
        playback_manager_end_stream();
    }
    else
    {
        // 不播放退出提示音，直接退出
        state_task_complete_receive_event(Event_Inc_TaskComplete_PlayEnd, NULL);
    }
}

static void single_round_exit_strategy()
{
    // 执行单轮对话退出策略
    state_task_complete_receive_event(Event_Inc_TaskComplete_End, NULL);
}

// 切换到任务完成态
void turn_to_task_complete(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload)
{
    if (!current_context)
    {
        lingxin_log_debug(" current_context is null");
        return;
    }
    if (current_context->has_is_vad_exit && current_context->is_vad_exit)
    {
        // 等待 服务端 end 指令
        return;
    }
    if (current_context->single_round)
    {
        // 执行带提示音的继续对话策略
        single_round_exit_strategy();
    }
    else
    {
        if (current_context->need_continue_prompt)
        {
            // 执行带提示音的继续对话策略
            continue_with_prompt_strategy();
        }
        else
        {
            // 执行继续对话策略
            continue_strategy(Event_Inc_TaskComplete_End);
        }
    }
}

// 接受到事件
void state_task_complete_receive_event(StateEvent event, StateEventPayload *payload)
{
    lingxin_log_debug("state_machine_receive_event: %d", event);
    // 根据当前状态和事件，决定状态转移
    switch (event)
    {
    case State_Event_VoiceChat_AIEnd:
    {
        if (get_current_context() != NULL && get_current_context()->is_vad_exit)
        {
            // 执行退出逻辑策略
            vad_exit_strategy(get_current_context()->input_timeout_audio);
        }
        break;
    }
    case State_Event_WillExit:
    {
        if (get_current_context() != NULL && get_current_context()->is_vad_exit && get_current_context()->input_timeout_audio)
        {
            // 执行退出逻辑策略
            playback_manager_terminate();
        }
        if (payload && payload->will_exit_payload && payload->will_exit_payload->disable_close_ws_immediately)
        {
            InnerStateForExit inner_state = {true, true, true, true, true};
            InnerStateCollection inner_state_collection = {
                .inner_state_for_exit = &inner_state,
            };
            turn_to_with_preset_inner_state(State_Exit, State_Event_WillExit, &inner_state_collection);
        }
        else
        {
            InnerStateForExit inner_state = {true, true, true, false, true};
            InnerStateCollection inner_state_collection = {
                .inner_state_for_exit = &inner_state,
            };
            turn_to_with_preset_inner_state(State_Exit, State_Event_WillExit, &inner_state_collection);
        }
        break;
    }
    case State_Event_ContinuePrompt_PlayEnd:
    {
        continue_strategy(State_Event_ContinuePrompt_PlayEnd);
        break;
    }

    case State_Event_VoiceChat_ExitEnd:
    {
        break;
    }
    case Event_Inc_TaskComplete_End:
    {
        InnerStateForExit inner_state = {false, false, true, false, true};
        InnerStateCollection inner_state_collection = {
            .inner_state_for_exit = &inner_state,
        };
        turn_to_with_preset_inner_state(State_Exit, event, &inner_state_collection);
        break;
    }
    case Event_Inc_TaskComplete_PlayEnd:
    {
        // vad exit退出, 不需要断开 websocket连接
        turn_to_with_preset_inner_state(State_Idle, event, NULL);
        break;
    }
    default:
        break;
    }
}