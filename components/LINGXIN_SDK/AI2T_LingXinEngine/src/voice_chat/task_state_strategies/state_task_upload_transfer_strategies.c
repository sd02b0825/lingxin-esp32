#include "state_task_upload_transfer_strategies.h"
#include "lingxin_log.h"
#include "chat_state_machine.h"
#include "chat_runtime_context.h"
#include "state_upload_manager.h"

void turn_to_upload_transfer(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload)
{
    // upload_manager_start(); // 录音开始
}

// 接受到事件
void state_upload_transfer_receive_event(StateEvent event, StateEventPayload *payload)
{
    switch (event)
    {
    case State_Event_VoiceChat_AIEnd:
    {
        turn_to_with_preset_inner_state(State_Idle, event, NULL);
        break;
    }
    case State_Event_Upload_CloseEnd:
        turn_to_with_preset_inner_state(State_Download_Init, event, NULL);
        break;
    case State_Event_Vad_Exit:
    {
        // 10.1 vad 退出唤醒
        update_current_context(CTX_FIELD_IS_VAD_EXIT, (ContextValue){.boolean = true});
        update_current_context(CTX_FIELD_EXIT_CODE, (ContextValue){.exit_code = EXIT_REASON_NO_INPUT_TIMEOUT});

        turn_to_with_preset_inner_state(State_Task_Complete, event, NULL);
    }
    break;

    default:
        lingxin_log_debug("state_upload_transfer_receive_event 未处理的事件 %d", event);
        break;
    }
}
