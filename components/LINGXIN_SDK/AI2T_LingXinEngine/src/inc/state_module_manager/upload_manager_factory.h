// upload_manager_factory.h
#ifndef UPLOAD_MANAGER_FACTORY_H
#define UPLOAD_MANAGER_FACTORY_H

#include "upload_record_interface.h"
#include "chat_state_machine.h"


// 初始化 & 控制
UploadModuleInterface * upload_factory_manager_init(ChatStateMediaType type);


#endif // UPLOAD_MANAGER_FACTORY_H