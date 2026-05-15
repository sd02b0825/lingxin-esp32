
#include "chat_state_machine.h"
#include "chat_runtime_context.h"

void turn_to_download_init(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload);

// 接受到事件
void state_download_init_receive_event(StateEvent event, StateEventPayload *payload);