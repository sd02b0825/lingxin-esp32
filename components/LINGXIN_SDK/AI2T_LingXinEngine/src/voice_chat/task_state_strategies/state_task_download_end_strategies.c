// 下行模块结束状态策略
#include "state_task_download_end_strategies.h"
#include "lingxin_log.h"

void turn_to_download_end(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload)
{
    state_download_end_receive_event(Event_Inc_Download_End, NULL);
}

// 接受到事件
void state_download_end_receive_event(StateEvent event, StateEventPayload *payload)
{
    switch (event)
    {
    case Event_Inc_Download_End:
    {
        lingxin_log_debug("进入下行结束态");
        turn_to_with_preset_inner_state(State_Task_Complete, event, NULL);
        break;
    }

    default:
        break;
    }
}