#include "chat_state_machine.h"
#include "chat_runtime_context.h"

void turn_to_download_end(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload);

// 接受到状态机的事件
void state_download_end_receive_event(StateEvent event, StateEventPayload *payload);