
// state_upload_manager.h
#ifndef STATE_UPLOAD_MANAGER_H
#define STATE_UPLOAD_MANAGER_H

#include "upload_record_interface.h"
#include "chat_state_machine.h"


// #include "ulpload_record_adapter.h"


// 初始化 & 控制
int upload_manager_init(ChatStateMediaType type);

void upload_manager_start();     // 允许开始发送（设置内部标志）
void upload_manager_terminate(void);


#endif // STATE_UPLOAD_MANAGER_H
