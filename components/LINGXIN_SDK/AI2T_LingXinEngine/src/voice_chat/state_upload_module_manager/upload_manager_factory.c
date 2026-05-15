// 上行模块的管理类
#include "upload_manager_factory.h"
#include "lingxin_log.h"

UploadModuleInterface *upload_factory_manager_init(ChatStateMediaType type)
{
    UploadModuleInterface *upload_manager = NULL;

    if (type == Media_Type_Chat)
    {
        extern const UploadModuleInterface g_chatUploadManager;
        upload_manager = (UploadModuleInterface *)&g_chatUploadManager;
    }
    else if (type == Media_Type_TextOnly)
    {
        extern const UploadModuleInterface g_scheduleUploadManager;
        upload_manager = (UploadModuleInterface *)&g_scheduleUploadManager;
    }
    else if (type == Media_Type_Multimodal)
    {
        lingxin_log_debug("多模态上传模块初始化");
        extern const UploadModuleInterface g_multimodalUploadManager;
        upload_manager = (UploadModuleInterface *)&g_multimodalUploadManager;
    }
    else
    {
        lingxin_log_debug("Video recording not supported yet");
        return NULL;
    }

    return upload_manager;
}