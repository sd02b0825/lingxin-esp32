#include <stdio.h>
#include <string.h>
#include "lingxin_log.h"
#include "chat_api.h"
#include "lingxin_chat_api_inner.h"
#include "cJSON.h"

// 引入各个模块所需的头文件
#include "lingxin_voice_chat_config.h"
#include "lingxin_file.h"
#include "lingxin_user_track.h"
#include "chat_state_machine.h"
#include "chat_runtime_context.h"
#include "lingxin_recorder_manager.h"
#include "lingxin_local_player_manager.h"
#include "schedule_timer_manager.h"
#include "lingxin_recorder.h"
#include "audio_buffer_play.h"
#include "lingxin_http.h"
#include "lingxin_common.h"
#include "lingxin_mutex.h"
#include "lingxin_thread.h"
#include "lingxin_memory.h"
#include "lingxin_device_info.h"

#include "lingxin_chat_upload_manager.h"

#ifdef LINGXIN_TEST
#include "lingxin_test_runner.h"
#endif

#define LINGXIN_PROPS_INIT_TAG "lingxin_props_init_tag"

// 标记是否初始化
static int is_inited = 0;

// 用户注册的对话模式事件回调方法
static ChatLifeCycleEventListener chat_event_listenner = NULL;
// 多模态事件回调方法
static LingxinMultimodalInputListener multimodal_input_listener = NULL;
// 云端配置获取线程的ID
static int lingxin_config_get_thread_id = 0;
static int lingxin_config_get_state = 0; // 0: 未开始; 1: 完成; -1: 超时
static lingxin_mutex_t lingxin_config_get_state_mutex = NULL;
// 云端配置获取线程入口函数
static void* lingxin_config_get_thread_entry(void *arg);
// 解析云端配置
static bool lingxin_parse_server_config(char* response, LingxinServerConfig *server_config);

/**
 * 初始化方法
 */
VoiceChatInitProps get_voice_chat_init_default_props()
{
    VoiceChatInitProps props = {0};
    props.is_schedule_task_on = 1;
    props.is_log_upload_on = 1;
    props.props_init_tag = LINGXIN_PROPS_INIT_TAG;
    return props;
}

int voice_chat_init(VoiceChatInitProps *init_props) {
#ifdef LINGXIN_TEST
    lingxin_test_runner_init(init_props);
    return -1;
#else
    return inner_voice_chat_init(init_props);
#endif
}
int inner_voice_chat_init(VoiceChatInitProps *init_props) {
    if (is_inited) {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_voice_chat_init_already_inited");
        return 0;
    }
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_voice_chat_init_start");

    // 检测传参是否合法
    if (init_props && strcmp(init_props->props_init_tag, LINGXIN_PROPS_INIT_TAG) != 0)
    {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_voice_chat_init_fail_props_illegal");
        return -1;
    }
    // 检测传参是否为空
    if (!init_props)
    {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_voice_chat_init_fail_props_empty");
        return -1;
    }

    /******************************** voice chat & websocket模块初始化 ********************************/
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_voice_chat_config_init_start");
    // 注册动态获取appId、license、sn、appCode的函数，以及获取业务参数、自定义参数的方法、websocket检测配置
    if (init_props->auth_app_id_get_func && init_props->auth_license_get_func && init_props->auth_sn_get_func && init_props->auth_app_code_get_func)
    {
        module_voice_chat_config_init(
            init_props->auth_app_id_get_func,
            init_props->auth_license_get_func,
            init_props->auth_sn_get_func,
            init_props->auth_app_code_get_func,
            init_props->device_code_get_func,
            init_props->chat_biz_parameter_get_func,
            init_props->chat_custom_parameter_get_func,
            init_props->chat_flow_control_parameter_get_func,
            init_props->websocket_check_interval,
            init_props->websocket_check_timeout);
        lingxin_log_ut(LINGXIN_DEBUG, "chat_api_voice_chat_config_init_success");
    }
    else
    {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_voice_chat_init_fail_empty_auth_func");
        return -1;
    }

    /********************************* 云端配置获取与校验 ********************************/
    // 设置默认值
    lingxin_config_get_state_mutex = lingxin_mutex_create();
    LingxinServerConfig server_config = { .enable_log_upload = true };
    lingxin_thread_param_t thread_param = {
        .priority = 16,
        .stack_size = 4096*2,
        .name = "lx_config",
    };
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_lingxin_config_get_thread_create");
    lingxin_thread_create(&lingxin_config_get_thread_id, &thread_param, lingxin_config_get_thread_entry, &server_config);
    int count = 0;
    while (1) {
        lingxin_mutex_lock(lingxin_config_get_state_mutex);
        if (lingxin_config_get_state || count == 30) {
            if (count == 30) {
                lingxin_log_ut(LINGXIN_ERROR, "chat_api_lingxin_config_get_timeout");
                lingxin_config_get_state = -1;
            }
            lingxin_mutex_unlock(lingxin_config_get_state_mutex);
            break;
        } else {
            lingxin_mutex_unlock(lingxin_config_get_state_mutex);
            count++;
            lingxin_log_debug("lingxin_config_get_state=%d, count=%d", lingxin_config_get_state, count);
            lingxin_thread_sleep(100);
        }
    }
    lingxin_mutex_destroy(lingxin_config_get_state_mutex);
    lingxin_config_get_state_mutex = NULL;

    /******************************** 校验上下行音频格式 ********************************/
    if (server_config.input_format && !lingxin_recorder_format_check(server_config.input_format)) {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_input_format_illegal");
        return -2;
    }
    if (server_config.output_format && !module_bufferPlay_formatCheck(server_config.output_format)) {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_output_format_illegal");
        return -2;
    }

    /******************************** 埋点初始化 ********************************/
    if (server_config.enable_log_upload && init_props->is_log_upload_on) {
        // 检测日志功能必传参数
        if (!init_props->flash_cache_path) {
            lingxin_log_ut(LINGXIN_ERROR, "chat_api_voice_chat_init_fail_flash_cache_path_empty");
            return -1;
        }
        lingxin_log_ut(LINGXIN_DEBUG, "chat_api_user_track_init_start");
        if (user_track_init(init_props->flash_cache_path)) {
            lingxin_log_ut(LINGXIN_DEBUG, "chat_api_user_track_init_success");
        } else {
            lingxin_log_ut(LINGXIN_ERROR, "chat_api_user_track_init_fail");
        }
    }

    /******************************** 状态机初始化 ********************************/
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_state_machine_init_start");
    bool enable_terminate_audio = init_props->terminate_audio_path != NULL;
    bool enable_continue_audio = init_props->continue_audio_path != NULL;
    voice_chat_machine_init(enable_terminate_audio, enable_continue_audio);
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_state_machine_init_success");

    /******************************** 录音模块初始化 ********************************/
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_record_manager_init_start");
    module_record_manager_init(init_props->send_uni_size, init_props->send_cbuf_scale);
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_record_manager_init_success");

    /******************************** 本地播放模块初始化 ********************************/
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_local_player_manager_init_start");
    // 设置开场白音频的路径
    if (init_props->welcome_audio_path)
    {
        module_local_play_set_welcome_audio_path(init_props->welcome_audio_path);
    }
    // 设置打断时音频的路径
    if (init_props->terminate_audio_path)
    {
        module_local_play_set_terminate_audio_path(init_props->terminate_audio_path);
    }
    // 设置连续对话进入下一轮对话前音频的路径
    if (init_props->continue_audio_path)
    {
        module_local_play_set_continue_audio_path(init_props->continue_audio_path);
    }
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_local_player_manager_init_success");

    /******************************** 定时任务模块初始化 ********************************/
    // 设置是否开启定时任务
    if (server_config.enable_schedule_task && init_props->is_schedule_task_on) {
        lingxin_log_ut(LINGXIN_DEBUG, "chat_api_schedule_timer_manager_init_start");
        module_schedule_init();
        lingxin_log_ut(LINGXIN_DEBUG, "chat_api_schedule_timer_manager_init_success");
    } else {
        lingxin_log_ut(LINGXIN_DEBUG, "chat_api_schedule_timer_manager_no_init");
    }

    /******************************* 注册chat生命周期监听函数 ******************************/
    chat_event_listenner = init_props->chat_life_cycle_event_listener;

    // 标记初始化完成
    is_inited = 1;
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_voice_chat_init_success");
    return 0;
}

static void* lingxin_config_get_thread_entry(void *arg) {
    LingxinServerConfig* server_config_ptr = (LingxinServerConfig*)arg;

    // 构造请求参数
    char *app_id = lingxin_auth_appId_get();
    char *sn = lingxin_auth_sn_get();
    char *license = lingxin_auth_license_get();
    char *device_code = lingxin_device_code_get();
    char body[128] = "";
    if (device_code) {
        snprintf(body, sizeof(body), "{\"device_code\":\"%s\"}", device_code);
    }
    HttpConfig *config = createHttpConfig(app_id, sn, license, REQUEST_URL, LINGXIN_SERVER_CONFIG_GET_PATH, body);
    // 发送请求
    char *response = NULL;
    bool is_post_success = http_post_without_callback(config, &response);
    if (lingxin_config_get_state_mutex && server_config_ptr && !lingxin_config_get_state) {
        lingxin_mutex_lock(lingxin_config_get_state_mutex);
        if (is_post_success) {
            // 解析返回值
            lingxin_parse_server_config(response, server_config_ptr);
        }
        lingxin_config_get_state = 1;
        lingxin_mutex_unlock(lingxin_config_get_state_mutex);
    }
    // 释放内存
    lingxin_free(response);
    free_http_config(config);

    lingxin_thread_destroy(lingxin_config_get_thread_id, LINGXIN_THREAD_DESTROY_WAIT);
    return NULL;
}

/**
 * 进入对话模式
 */
StartNewChatProps get_start_new_chat_default_props()
{
    StartNewChatProps props = {0};
    props.props_init_tag = LINGXIN_PROPS_INIT_TAG;
    return props;
}
int start_new_chat(StartNewChatProps *start_props) {
#ifdef LINGXIN_TEST
    lingxin_test_runner_wakeup();
    return -1;
#else
    return inner_start_new_chat(start_props);
#endif
}
int inner_start_new_chat(StartNewChatProps *start_props)
{
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_start_new_chat_called");

    // 检测是否初始化
    if (!is_inited)
    {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_start_new_chat_fail_not_inited");
        return -2;
    }

    // 检测传参是否合法
    if (start_props && strcmp(start_props->props_init_tag, LINGXIN_PROPS_INIT_TAG) != 0)
    {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_start_new_chat_fail_props_illegal");
        return -1;
    }

    // 设置taskid
    if (start_props && start_props->task_id) {
        update_sesseion_context(CTX_FIELD_CURRENT_TASK_ID, (ContextValue){.string = start_props->task_id});
    } else {
        update_sesseion_context(CTX_FIELD_CURRENT_TASK_ID, (ContextValue){.string = NULL});
    }
    // 设置single_round
    if (start_props && start_props->single_round) {
        update_sesseion_context(CTX_FIELD_SINGLE_ROUND, (ContextValue){.boolean = true});
    } else {
        update_sesseion_context(CTX_FIELD_SINGLE_ROUND, (ContextValue){.boolean = false});
    }
    // 设置disable_welcome_audio
    if (start_props && start_props->disable_welcome_audio) {
        update_sesseion_context(CTX_FIELD_DISABLE_WELCOME_AUDIO, (ContextValue){.boolean = true});
    } else {
        lingxin_log_debug("inner_start_new_chat CTX_FIELD_DISABLE_WELCOME_AUDIO");
        update_sesseion_context(CTX_FIELD_DISABLE_WELCOME_AUDIO, (ContextValue){.boolean = false});        
    }
    // 设置task
    if (start_props && start_props->task) {
        if (strcmp(start_props->task, "chat_multimodal") == 0) {
            multimodal_input_listener = start_props->multimodal_input_listener;
            update_sesseion_context(CTX_FIELD_GLOBAL_TASK, (ContextValue){.string = "chat_multimodal"});
            update_sesseion_context(CTX_FIELD_DISABLE_SERVER_VAD, (ContextValue){.boolean = true});
        } else if (strcmp(start_props->task, "chat_multimodal_vad") == 0) {
            multimodal_input_listener = start_props->multimodal_input_listener;
            update_sesseion_context(CTX_FIELD_GLOBAL_TASK, (ContextValue){.string = "chat_multimodal"});
            update_sesseion_context(CTX_FIELD_DISABLE_SERVER_VAD, (ContextValue){.boolean = false});
        } else {
            multimodal_input_listener = NULL;
            update_sesseion_context(CTX_FIELD_GLOBAL_TASK, (ContextValue){.string = start_props->task});
            update_sesseion_context(CTX_FIELD_DISABLE_SERVER_VAD, (ContextValue){.boolean = false});
        }
    } else if (start_props && start_props->disable_vad) {
        multimodal_input_listener = NULL;
        update_temp_context(CTX_FIELD_GLOBAL_TASK, (ContextValue){.string = "chat"});
        update_sesseion_context(CTX_FIELD_GLOBAL_TASK, (ContextValue){.string = "chat_vad"});
        update_sesseion_context(CTX_FIELD_DISABLE_SERVER_VAD, (ContextValue){.boolean = false});
    } else {
        multimodal_input_listener = NULL;
        update_sesseion_context(CTX_FIELD_GLOBAL_TASK, (ContextValue){.string = "chat_vad"});
        update_sesseion_context(CTX_FIELD_DISABLE_SERVER_VAD, (ContextValue){.boolean = false});
    }
    // 临时设置user_input
    if (start_props && start_props->user_input) {
        update_temp_context(CTX_FIELD_CURRENT_USER_INPUT, (ContextValue){.string = start_props->user_input});
        update_temp_context(CTX_FIELD_UPLOAD_TYPE, (ContextValue){.media_type = Media_Type_TextOnly});
        update_temp_context(CTX_FIELD_INPUT_MODE, (ContextValue){.string = "no_voice"});
    }
    // 临时设置play_prologue
    if (start_props && start_props->play_prologue) {
        update_temp_context(CTX_FIELD_PLAY_PROLOGUE, (ContextValue){.boolean = true});
        update_temp_context(CTX_FIELD_UPLOAD_TYPE, (ContextValue){.media_type = Media_Type_TextOnly});
        update_temp_context(CTX_FIELD_INPUT_MODE, (ContextValue){.string = "voice"});
    }

    // 发送事件
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_start_new_chat_send_event");
    state_machine_run_event(State_Event_Wakeup_Detected);
    return 0;
}

/**
 * 用户主动调用停止录音
 */
int stop_chat_record(StopChatRecordProps *stop_record_props)
{
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_stop_chat_record_called");
    
    lingxin_chat_upload_manager_stop_record();

    return 0;
}

/**
 * 退出对话模式
 */
ExitChatProps get_exit_chat_default_props()
{
    ExitChatProps props = {0};
    props.props_init_tag = LINGXIN_PROPS_INIT_TAG;
    return props;
}
int exit_chat(ExitChatProps *exit_props)
{
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_exit_chat_called");

    // 检测传参是否合法
    if (exit_props && strcmp(exit_props->props_init_tag, LINGXIN_PROPS_INIT_TAG) != 0)
    {
        lingxin_log_ut(LINGXIN_ERROR, "chat_api_exit_chat_fail_props_illegal");
        return -1;
    }

    // 构造payload
    WillExitPayload will_exit_payload = {0};
    if (exit_props)
    {
        will_exit_payload.disable_close_ws_immediately = exit_props->disable_close_ws_immediately;
    }
    StateEventPayload payload = {
        .will_exit_payload = &will_exit_payload};

    // 发送事件
    lingxin_log_ut(LINGXIN_DEBUG, "chat_api_exit_chat_send_event");
    state_machine_run_event_with_payload(State_Event_WillExit, &payload);
    return 0;
}

/**
 * 设置音量（0-100）
 */
int set_volume(int volume)
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "chat_api_set_volume", "%d", volume);
    int real_volume = volume;
    if (volume < 0)
    {
        real_volume = 0;
    }
    else if (volume > 100)
    {
        real_volume = 100;
    }
    module_bufferPlay_setVolume(real_volume);
    module_local_play_set_volume(real_volume);
    return 0;
}

/**
 * 设置对话生命周期监听函数
 */
void lingxin_emit_chat_event(ChatLifeCycleEvent event, void *payload)
{
    if (chat_event_listenner)
    {
        chat_event_listenner(event, payload); // 给对话生命周期监听函数加一层非空校验
    }
}

/**
 * 触发多模态输入
 */
int lingxin_emit_multimodal_input_event(LingxinMultimodalInputListenerProps props)
{
    if (multimodal_input_listener)
    {
        multimodal_input_listener(props); // 给多模态输入监听函数加一层非空校验
        return 0;
    } else {
        return -1; // 多模态输入监听函数为空，触发失败
    }
}

/**
 * 报告错误
 */
void lingxin_report_error(char *error_str)
{
    cJSON *error_obj = cJSON_Parse(error_str);
    if (!error_obj)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        lingxin_log_error("Error json: %s", error_str);
        lingxin_log_error("Error error_ptr: %s", error_ptr);
    }
    else
    {
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(error_obj, "payload");
        if (payload && cJSON_IsObject(payload))
        {
            cJSON *notice_type = cJSON_GetObjectItemCaseSensitive(payload, "notice_type");
            if (notice_type && cJSON_IsString(notice_type))
            {
                char *notice_type_str = notice_type->valuestring;
                if (strcmp(notice_type_str, "TO_USER") == 0)
                {
                    lingxin_emit_chat_event(CHAT_LIFE_CYCLE_EVENT_ERROR, error_str);
                }
                else if (strcmp(notice_type_str, "TO_USER_SDK") == 0)
                {
                    lingxin_emit_chat_event(CHAT_LIFE_CYCLE_EVENT_ERROR, error_str);
                }
            }
        }
        cJSON_Delete(error_obj);
    }
}

static bool lingxin_parse_server_config(char* response, LingxinServerConfig *server_config_ptr) {
    char *json_str = response;
    if (json_str == NULL) {
        lingxin_log_error("parse response empty");
        goto parse_error;
    }
    lingxin_log_debug("parse responese: %s", json_str);
    cJSON *json_obj = cJSON_Parse(json_str);
    if (!json_obj) {
        const char* err = cJSON_GetErrorPtr();
        lingxin_log_error("parse response error: response=%s, error_ptr=%s", json_str, err);
        goto parse_error;
    }
    cJSON *data = cJSON_GetObjectItemCaseSensitive(json_obj, "data");
    if (!data || !cJSON_IsObject(data)) {
        lingxin_log_error("parse data error");
        cJSON_Delete(json_obj);
        goto parse_error;
    }
    cJSON *input_format = cJSON_GetObjectItemCaseSensitive(data, "input_format");
    if (input_format && cJSON_IsString(input_format)) {
        server_config_ptr->input_format = lingxin_strdup(input_format->valuestring);
    }
    cJSON *input_sample_rate = cJSON_GetObjectItemCaseSensitive(data, "input_sample_rate");
    if (input_sample_rate && cJSON_IsNumber(input_sample_rate)) {
        server_config_ptr->input_sample_rate = input_sample_rate->valueint;
    }
    cJSON *output_format = cJSON_GetObjectItemCaseSensitive(data, "output_format");
    if (output_format && cJSON_IsString(output_format)) {
        server_config_ptr->output_format = lingxin_strdup(output_format->valuestring);
    }
    cJSON *output_sample_rate = cJSON_GetObjectItemCaseSensitive(data, "output_sample_rate");
    if (output_sample_rate && cJSON_IsNumber(output_sample_rate)) {
        server_config_ptr->output_sample_rate = output_sample_rate->valueint;
    }
    cJSON *enable_schedule_task = cJSON_GetObjectItemCaseSensitive(data, "enable_schedule_task");
    if (enable_schedule_task && cJSON_IsBool(enable_schedule_task)) {
        server_config_ptr->enable_schedule_task = cJSON_IsTrue(enable_schedule_task);
    }
    cJSON *enable_log_upload = cJSON_GetObjectItemCaseSensitive(data, "enable_log_upload");
    if (enable_log_upload && cJSON_IsBool(enable_log_upload)) {
        server_config_ptr->enable_log_upload = cJSON_IsTrue(enable_log_upload);
    }
    lingxin_log_debug("parse success");
    cJSON_Delete(json_obj);
    return true;

parse_error:
    return false;
}