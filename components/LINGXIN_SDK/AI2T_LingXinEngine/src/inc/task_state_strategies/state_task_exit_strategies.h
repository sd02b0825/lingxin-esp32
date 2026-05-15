#include "chat_state_machine.h"
#include "chat_runtime_context.h"

// 切换到任务完成态
void turn_to_exit(const ChatStateRuntimeContext *current_context, InnerStateCollection *payload, StateEvent event);

// 接受到事件
void state_exit_receive_event(StateEvent event, StateEventPayload *payload);