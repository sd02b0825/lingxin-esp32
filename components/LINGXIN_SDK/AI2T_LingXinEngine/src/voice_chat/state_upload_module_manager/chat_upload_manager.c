// recorder_adapter.c
#include "lingxin_recorder_manager.h"
#include "upload_record_interface.h"
#include "chat_state_machine.h"
#include "chat_runtime_context.h"
#include "lingxin_log.h"

#include "lingxin_protocol_manager.h"
#include "lingxin_chat_upload_manager.h"
#include "chat_state_machine_event.h"

static void on_record_init(bool success);
static void on_record_stop(bool success);
static void on_record_terminate(bool success);

static void sent_start_callback(void *buf, int rlen, int index)
{
    if (buf != NULL && rlen > 0)
    {
        voiceChatSendAudio(buf, rlen);
    }
    else
    {
        lingxin_log_error("chat模块 上行录音发送失败");
    }
}

static void on_record_stop_no_event(bool success)
{
    voiceChatStopSendAudio();
    if (!success)
    {
        lingxin_log_error("chat模块 上行录音打断失败");
    }
}

// 监听器
static void onChatEvent(ChatEventType event, const char *data, const size_t len)
{
    lingxin_log_debug("chat模块 上行事件回调 event:%d", event);
    switch (event)
    {
    case CHAT_EVENT_ON_VAD_END:
        module_record_stop(0, on_record_stop);
        break;
    case CHAT_EVENT_ON_VAD_EXIT:
        // 通知状态机结束 chat
        state_machine_run_event_with_payload(State_Event_Vad_Exit, NULL);
        module_record_stop(0, on_record_stop_no_event);
        break;
    case CHAT_EVENT_ON_AI_READY:
        // 调用录音发送的方法
        module_record_start_send(sent_start_callback);
        break;
    default:
        break;
    }
}

static void on_record_init(bool success)
{
    state_machine_run_event_with_payload(State_Event_Upload_InitEnd, NULL);
    if (!success)
    {
        lingxin_log_error("chat模块 上行初始化失败");
    }
}

static void on_record_stop(bool success)
{
    voiceChatStopSendAudio();
    state_machine_run_event_with_payload(State_Event_Upload_CloseEnd, NULL);
    if (!success)
    {
        lingxin_log_error("chat模块 上行停止录音失败");
    }
}

static void on_record_terminate(bool success)
{
    state_machine_run_event_with_payload(State_Event_Upload_TerminateEnd, NULL);
    if (!success)
    {
        lingxin_log_error("chat模块 上行录音打断失败");
    }
}

static void voice_chat_continue()
{
    ChatStartNewParams voice_chat_continue_params = {0};
    // 初始化参数
    voice_chat_continue_params.taskId = "";
    voice_chat_continue_params.task = "";
    voice_chat_continue_params.input_mode = "";
    voice_chat_continue_params.output_mode = "";
    voice_chat_continue_params.user_input = "";
    voice_chat_continue_params.scheduleTaskId = "";

    if (get_current_context() != NULL)
    {
        if (get_current_context()->has_global_task)
        {
            voice_chat_continue_params.task = get_current_context()->global_task;
        }

        if (get_current_context()->has_current_task_id)
        {
            voice_chat_continue_params.taskId = get_current_context()->current_task_id;
        }

        if (get_current_context()->has_disable_server_vad)
        {
            voice_chat_continue_params.server_vad = !(get_current_context()->disable_server_vad);
        }

        voice_chat_continue_params.output_mode = get_output_type_string();
        voice_chat_continue_params.input_mode = get_input_type_string();

        print_chat_context(get_current_context());
    }

    lingxin_log_debug("voice_chat_continue_params.taskId:%s, voice_chat_continue_params.task:%s voice_chat_continue_params.user_input:%s, voice_chat_continue_params.scheduleTaskId:%s", voice_chat_continue_params.taskId, voice_chat_continue_params.task, voice_chat_continue_params.user_input, voice_chat_continue_params.scheduleTaskId);
    voice_chat_start_new(&voice_chat_continue_params); // 普通连续对话
}

static int record_init()
{
    // 添加事件的监听器
    add_protocol_event_listener(onChatEvent);
    // 初始化录音器模块
    lingxin_log_debug("chat模块 上行初始化成功");
    module_record_start(on_record_init);

    return 1;
}
static void send_start()
{
    voice_chat_continue();
}

static void recorder_terminate(void)
{
    // TODO: 打断时需要阻塞websocket, 移动到外层
    extern void freeze_websocket();
    freeze_websocket();
    module_record_stop(0, on_record_terminate);
}

static void recorder_destory(void)
{
    remove_protocol_event_listener(onChatEvent);
}

void lingxin_chat_upload_manager_stop_record()
{
    module_record_stop(1, on_record_stop);
}

const UploadModuleInterface g_chatUploadManager = {
    .upload_init = record_init,
    .upload_start = send_start,
    .upload_terminate = recorder_terminate,
    .upload_destory = recorder_destory,
};
