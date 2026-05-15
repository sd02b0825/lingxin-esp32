#include <stdio.h>
#include "chat_state_machine.h"
#include "chat_runtime_context.h"


// 切换到任务完成态 current_context仅供只读使用
void turn_to_task_complete(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload);

// 接受到事件
void state_task_complete_receive_event(StateEvent event, StateEventPayload *payload);