
#include "chat_state_machine.h"
#include "chat_runtime_context.h"

void turn_to_download_transfer(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload);

// 接受到事件
void state_download_transfer_receive_event(StateEvent event, StateEventPayload *payload);

void state_download_feed_data(void *data, int len);