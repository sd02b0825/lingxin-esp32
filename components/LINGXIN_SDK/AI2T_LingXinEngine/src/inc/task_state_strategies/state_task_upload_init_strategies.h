#ifndef A0D29965_3A0B_4525_B6E0_63209E080EE9
#define A0D29965_3A0B_4525_B6E0_63209E080EE9

#include "chat_state_machine.h"
#include "chat_runtime_context.h"
#include "upload_record_interface.h"
#include "chat_state_machine_event.h"

void turn_to_upload_init(const ChatStateRuntimeContext *current_context, InnerStateCollection *innerPayload);

// 接受到事件
void state_upload_init_receive_event(StateEvent event, StateEventPayload *payload);


#endif /* A0D29965_3A0B_4525_B6E0_63209E080EE9 */
