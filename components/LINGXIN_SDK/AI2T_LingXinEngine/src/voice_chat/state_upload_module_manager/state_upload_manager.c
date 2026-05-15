#include "lingxin_log.h"
#include "state_upload_manager.h"
#include "upload_manager_factory.h"

static const UploadModuleInterface *s_upload = NULL;

static ChatStateMediaType current_upload_type = Media_Type_Chat;

int upload_manager_init(ChatStateMediaType type)
{

    lingxin_log_debug("进入上传状态，初始化录音器");

    if (s_upload && s_upload->upload_destory)
    {
        s_upload->upload_destory();
    }

    // 通过工厂类方法创建相关类
    s_upload = upload_factory_manager_init(type);

    current_upload_type = type;

    s_upload->upload_init();

    return 1;
}

void upload_manager_start()
{
    if (s_upload && s_upload->upload_start)
    {
        lingxin_log_debug("上传模块 发送 task_start");
        s_upload->upload_start();
    }
}

void upload_manager_terminate(void)
{
    if (s_upload && s_upload->upload_terminate)
    {
        s_upload->upload_terminate();
    }
    else
    {
        state_machine_run_event_with_payload(State_Event_Upload_TerminateEnd, NULL);
    }
}
