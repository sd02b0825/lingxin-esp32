#ifndef D4B54680_F5B1_42CF_AC3C_DF659F2E55AB
#define D4B54680_F5B1_42CF_AC3C_DF659F2E55AB

#include "chat_state_machine.h"
#include "chat_runtime_context.h"
#include "state_module_manager/state_upload_manager.h"
#include "upload_record_interface.h"
#include "chat_state_machine_event.h"

void turn_to_upload_transfer(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload);

// 接受到事件
void state_upload_transfer_receive_event(StateEvent event, StateEventPayload *payload);

#endif /* D4B54680_F5B1_42CF_AC3C_DF659F2E55AB */