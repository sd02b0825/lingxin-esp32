#ifndef UPLOAD_AUDIO_RECORD_INTERFACE_H
#define UPLOAD_AUDIO_RECORD_INTERFACE_H

#include <stdint.h>
#include "lingxin_protocol_manager.h"


typedef struct
{
    // 真正的上行模块的初始化
    int (*upload_init)(); // 录音 init
    void (*upload_start)();          // 发送start、接收task_started
    void (*upload_terminate)(void);     // 立即打断
    void (*upload_destory)(void);        // 销毁
} UploadModuleInterface;

#endif // UPLOAD_AUDIO_RECORD_INTERFACE_H