#include <string.h>
#include "chat_api.h"
#include "lingxin_chat_api_inner.h"
#include "lingxin_protocol_manager.h"
#include "lingxin_log.h"
#include "lingxin_memory.h"
#include "lingxin_recorder_manager.h"
#include "upload_record_interface.h"
#include "chat_state_machine.h"
#include "chat_runtime_context.h"

static bool multimodal_finish_input(LingxinInputEndProps *input_end_props);
static bool multimodal_send_stream(LingxinSendStreamProps *send_stream_props);
static bool multimodal_send_text(LingxinSendTextProps *send_text_props);
static bool multimodal_start_record_by_user(LingxinStartRecordProps *start_record_props);
static bool multimodal_stop_record_by_user(LingxinStopRecordProps *start_record_props);
static void on_multimodal_chat_event(ChatEventType event, const char *data, const size_t len);

static bool is_recorder_open = false;
static bool is_ai_ready = false;
static bool wait_input_terminate = false;
static bool is_server_vad_enabled()
{
    const ChatStateRuntimeContext *ctx = get_current_context();
    return ctx && !ctx->disable_server_vad;
}

/**
 * 从type中解析出前缀部分 (如从 "audio/wav" 中提取 "audio")
 */
static bool get_prefix_from_type(const char *type, char *prefix_type, size_t prefix_size)
{
    if (!type || !prefix_type || prefix_size == 0)
    {
        return false;
    }

    char *slash_pos = strchr(type, '/');
    if (slash_pos)
    {
        size_t prefix_len = slash_pos - type;
        // 确保有足够的空间存储结果和null终止符
        if (prefix_len < prefix_size)
        {
            strncpy(prefix_type, type, prefix_len);
            prefix_type[prefix_len] = '\0';
            return true;
        }
    }
    // 如果没有找到斜杠，复制整个字符串
    size_t type_len = strlen(type);
    if (type_len < prefix_size)
    {
        strcpy(prefix_type, type);
        return true;
    }

    return false;
}

static bool multimodal_send_stream(LingxinSendStreamProps *send_stream_props)
{
    lingxin_log_debug("multimodal_send_stream");
    if (!send_stream_props)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_send_stream", "send_text_props is null");
        return false;
    }
    if (wait_input_terminate)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_send_stream", "cannot receive data, %d", wait_input_terminate);
        return false;
    }
    char frame_type[32];
    get_prefix_from_type(send_stream_props->content_type, frame_type, sizeof(frame_type));
    return voiceChat_send_request_data_stream(send_stream_props->unique_id, send_stream_props->index, send_stream_props->frame,
                                              send_stream_props->content_len, send_stream_props->content_type, frame_type, send_stream_props->is_last);
}

static bool multimodal_send_text(LingxinSendTextProps *send_text_props)
{
    lingxin_log_debug("multimodal_send_text");
    if (!send_text_props)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_send_text", "send_text_props  is null");
        return false;
    }
    if (wait_input_terminate)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_send_stream", "cannot receive data, %d", wait_input_terminate);
        return false;
    }
    return voiceChat_send_request_data_text(send_text_props->content);
}

static void multimodal_recorder_data_callback(void *buf, int rlen, int index)
{
    voiceChat_send_request_data_stream("audio_recorder_data", index, buf, rlen, "audio/pcm", "audio", false);
}

/**
 * 录音开始回调和收到ai_ready的顺序不能保证，所以，两个地方分别都调一次
 */
static void try_to_start_send_record()
{
    if (!is_recorder_open || !is_ai_ready)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "try_to_start_send_record", "wait to send %d %d", is_recorder_open, is_ai_ready);
        return;
    }
    module_record_start_send(multimodal_recorder_data_callback);
}

static void multimodal_input_event_callback(LingxinMultimodalInputEvent event, char *event_payload, bool is_server_vad)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "multimodal_input_event_callback", "%d, %d", event, is_server_vad);

    LingxinMultimodalInputListenerProps props = {
        .input_end = multimodal_finish_input,
        .send_stream = multimodal_send_stream,
        .send_text = multimodal_send_text,
        .start_record = is_server_vad ? NULL : multimodal_start_record_by_user,
        .stop_record = is_server_vad ? NULL : multimodal_stop_record_by_user,
        .event = event,
        .event_payload = event_payload};
    lingxin_emit_multimodal_input_event(props);
}

static void recorder_start_callback_from_user(bool is_success)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_start_callback_from_user", "%d", is_success);

    if (!is_success)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_recorder_start_callback", "start fail");
        return;
    }
    is_recorder_open = true;
    try_to_start_send_record();
    multimodal_input_event_callback(LINGXIN_MULTIMODAL_EVENT_RECORDER_START, "", false);
}

static bool multimodal_start_record_by_user(LingxinStartRecordProps *start_record_props)
{
    lingxin_log_ut(LINGXIN_DEBUG, "multimodal_start_record_by_user");
    module_record_start(recorder_start_callback_from_user);
    return true;
}

static void recorder_stop_callback_from_user(bool is_success)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_stop_callback_from_user", "%d", is_success);

    if (!is_success)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_stop_record", "recorder stop failed");
        return;
    }
    multimodal_input_event_callback(LINGXIN_MULTIMODAL_EVENT_RECORDER_STOP, "", false);
}

static bool multimodal_stop_record_by_user(LingxinStopRecordProps *start_record_props)
{
    lingxin_log_ut(LINGXIN_DEBUG, "multimodal_stop_record_by_user");
    // 用户手动暂停录音，需要等待剩余录音数据发完
    module_record_stop(1, recorder_stop_callback_from_user);
    return true;
}

static bool multimodal_finish_input(LingxinInputEndProps *input_end_props)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "multimodal_finish_input", "%d", wait_input_terminate);

    // 重置start_recorder_send需要的变量
    is_recorder_open = false;
    is_ai_ready = false;

    if (wait_input_terminate)
    {
        wait_input_terminate = false;
        lingxin_log_ut_with_args(LINGXIN_DEBUG, "multimodal_upload_end_input", "State_Event_Upload_TerminateEnd");
        state_machine_run_event_with_payload(State_Event_Upload_TerminateEnd, NULL);
        return true;
    }
    // 无confirm data 场景
    if (!input_end_props || input_end_props->confirm_data_count <= 0 || !input_end_props->confirm_data_array)
    {
        // 发送结束任务请求
        bool result = voiceChat_send_end_up_task(0, NULL);
        // 通知状态机上行流程结束
        if (result)
        {
            lingxin_log_ut_with_args(LINGXIN_DEBUG, "multimodal_upload_end_input", "State_Event_Upload_CloseEnd");
            state_machine_run_event_with_payload(State_Event_Upload_CloseEnd, NULL);
        }
        return result;
    }
    // 有confirm data 场景
    Multimodal_Chat_Confirm_Data *multimodal_confirm_data = lingxin_calloc(1, input_end_props->confirm_data_count * sizeof(Multimodal_Chat_Confirm_Data));
    if (!multimodal_confirm_data)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_end_input", "confirm data calloc failed");
        return false;
    }
    for (size_t i = 0; i < input_end_props->confirm_data_count; i++)
    {
        // 复制ID
        multimodal_confirm_data[i].unique_id = input_end_props->confirm_data_array[i].unique_id;

        char prefix_type[32]; // 假设前缀最大长度为31字符+NULL
        if (!get_prefix_from_type(input_end_props->confirm_data_array[i].content_type, prefix_type, sizeof(prefix_type)))
        {
            lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_end_input", "%d, get_prefix_from_type fail", i);
            return false;
        }
        multimodal_confirm_data[i].frame_type = lingxin_calloc(1, strlen(prefix_type) + 1);
        if (!multimodal_confirm_data[i].frame_type)
        {
            lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_end_input", "%d, frame_type calloc fail", i);
            return false;
        }
        strcpy(multimodal_confirm_data[i].frame_type, prefix_type);
    }

    // 发送结束任务请求
    bool result = voiceChat_send_end_up_task(input_end_props->confirm_data_count, multimodal_confirm_data);

    // 通知状态机上行流程结束
    if (result)
    {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, "multimodal_upload_end_input", "State_Event_Upload_CloseEnd");
        state_machine_run_event_with_payload(State_Event_Upload_CloseEnd, NULL);
    }
    // 清理临时分配的内存
    for (size_t i = 0; i < input_end_props->confirm_data_count; i++)
    {
        if (multimodal_confirm_data[i].frame_type)
        {
            lingxin_free((void *)multimodal_confirm_data[i].frame_type);
        }
    }
    lingxin_free(multimodal_confirm_data);

    return result;
}

static void recorder_stop_callback_from_vad_end(bool is_success)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_stop_callback_from_vad_end", "%d", is_success);

    if (!is_success)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_stop_record", "recorder stop failed");
        return;
    }
    multimodal_input_event_callback(LINGXIN_MULTIMODAL_EVENT_RECORDER_STOP, "", true);
}

static void on_multimodal_chat_event(ChatEventType event, const char *data, const size_t len)
{
    switch (event)
    {
    case CHAT_EVENT_ON_VAD_END:
    case CHAT_EVENT_ON_VAD_EXIT:
        module_record_stop(0, recorder_stop_callback_from_vad_end);
        break;
    case CHAT_EVENT_ON_STREAM_DATA:
        /* code */
        break;
    case CHAT_EVENT_ON_AI_READY:
    {
        is_ai_ready = true;
        bool is_server_vad = is_server_vad_enabled();
        if (is_server_vad)
        {
            try_to_start_send_record();
        }
        else
        {
            state_machine_run_event_with_payload(State_Event_Upload_InitEnd, NULL);
        }

        multimodal_input_event_callback(LINGXIN_MULTIMODAL_EVENT_INPUT_START, "", is_server_vad);
        break;
    }
    case CHAT_EVENT_ON_REQUEST_DATA_END:
    {
        multimodal_input_event_callback(LINGXIN_MULTIMODAL_EVENT_STREAM_INPUT_SUCCESS, (char *)data, is_server_vad_enabled());
        break;
    }
    case CHAT_EVENT_ON_ERROR:
        /* code */
        break;
    default:
        break;
    }
}

static void recorder_start_callback_from_init_case_vad(bool is_success)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_start_callback_from_init_case_vad", "%d", is_success);

    if (!is_success)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_start_callback_from_init_case_vad", "start fail");
        return;
    }
    is_recorder_open = true;
    state_machine_run_event_with_payload(State_Event_Upload_InitEnd, NULL);
    multimodal_input_event_callback(LINGXIN_MULTIMODAL_EVENT_RECORDER_START, "", true);
    try_to_start_send_record();
}

static int multimodal_upload_init()
{
    lingxin_log_ut(LINGXIN_DEBUG, "multimodal_upload_init");

    add_protocol_event_listener(on_multimodal_chat_event);
    // 云端vad场景才自动打开录音，否则不处理
    if (is_server_vad_enabled())
    {
        module_record_start(recorder_start_callback_from_init_case_vad);
    }
    return 1;
}
static void multimodal_upload_start()
{
    bool is_server_vad = is_server_vad_enabled();

    lingxin_log_ut_with_args(LINGXIN_DEBUG, "multimodal_upload_start", "%d", is_server_vad);

    const ChatStateRuntimeContext *ctx = get_current_context();
    char *final_task = "chat_multimodal";
    char *final_taskId = NULL;
    if (ctx != NULL)
    {
        final_task = (char *)ctx->global_task;
        final_taskId = (char *)ctx->current_task_id;
    }
    ChatStartNewParams multimodal_start_params = {
        .is_schedule_timer_task = false,
        .server_vad = is_server_vad,
        .input_mode = "",
        .output_mode = "",
        .scheduleTaskId = "",
        .user_input = "",
        .task = final_task,
        .taskId = final_taskId};
    voice_chat_start_new(&multimodal_start_params);
}

static void recorder_stop_callback_from_terminate(bool is_success)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_stop_callback_from_terminate", "%d", is_success);

    if (!is_success)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "multimodal_upload_terminate_record", "recorder stop failed");
        return;
    }

    if (is_ai_ready)
    {
        multimodal_input_event_callback(LINGXIN_MULTIMODAL_EVENT_INPUT_INTERRUPT, "", is_server_vad_enabled());
    }
    else
    {
        // 没有通知过客户开始输入，则直接结束输入
        multimodal_finish_input(NULL);
    }
}
static void multimodal_upload_terminate()
{
    lingxin_log_ut(LINGXIN_DEBUG, "multimodal_upload_terminate");

    wait_input_terminate = true;
    module_record_stop(0, recorder_stop_callback_from_terminate);
}

static void multimodal_upload_destroy()
{
    lingxin_log_ut(LINGXIN_DEBUG, "multimodal_upload_destroy");

    remove_protocol_event_listener(on_multimodal_chat_event);
}

const UploadModuleInterface g_multimodalUploadManager = {
    .upload_init = multimodal_upload_init,
    .upload_start = multimodal_upload_start,
    .upload_terminate = multimodal_upload_terminate,
    .upload_destory = multimodal_upload_destroy};