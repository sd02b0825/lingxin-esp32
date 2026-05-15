#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "lingxin_test_runner.h"
#include "lingxin_log.h"
#include "lingxin_hook_websocket.h"
#include "lingxin_memory.h"
#include "lingxin_system.h"
#include "lingxin_chat_api_inner.h"

#define TEST_WS_IP "8.154.30.254"

static WebsocketClient *lingxin_test_ws = NULL;

static VoiceChatInitProps *init_props = NULL;
static StartNewChatProps *start_props = NULL;
static char* biz_parameter = NULL;
static LingxinMultimodalInputListenerProps multimodal_functions = {0};
static bool multimodal_inputing = false;
static char* multimodal_current_unique_id = NULL;
static char* multimodal_current_content_type = NULL;
static int multimodal_frame_index = 0;

static void lingxin_test_connect_ws();
static void lingxin_test_ws_listener(WebSocketEventType event, const char *data, size_t data_len, const int is_binary, void *userContext);
static void lingxin_test_receive_data(char *data, size_t data_len, int is_binary);

static char* lingxin_test_chat_biz_parameter_get_func();
static void lingxin_test_chat_life_cycle_event_listener(ChatLifeCycleEvent event, void *payload);
static void lingxin_test_multimodal_input_listener(LingxinMultimodalInputListenerProps props);

static void lingxin_test_set_chat_biz_parameter(cJSON *props_cjson);
static void lingxin_test_voice_chat_init(cJSON *props_cjson);
static void lingxin_test_set_start_new_chat_props(cJSON *props_cjson);
static void lingxin_test_start_new_chat(cJSON *props_cjson);
static void lingxin_test_stop_record(cJSON *props_cjson);
static void lingxin_test_exit_chat(cJSON *props_cjson);
static void lingxin_test_set_volume(cJSON *props_cjson);
static void lingxin_test_send_binary_stream_start(cJSON *props_cjson);
static void lingxin_test_send_binary_stream_end(cJSON *props_cjson);
static void lingxin_test_send_text(cJSON *props_cjson);
static void lingxin_test_multimodal_start_record(cJSON *props_cjson);
static void lingxin_test_multimodal_stop_record(cJSON *props_cjson);
static void lingxin_test_multimodal_input_end(cJSON *props_cjson);
static void lingxin_test_system_abort(cJSON *props_cjson);
static void lingxin_test_memory_check(cJSON *props_cjson);

void lingxin_test_runner_init(VoiceChatInitProps *props) {
  // 首先进行test server建联
  lingxin_test_connect_ws();
  // 存储初始化参数
  init_props = lingxin_malloc(sizeof(VoiceChatInitProps));
  memset(init_props, 0, sizeof(VoiceChatInitProps));
  init_props->auth_app_id_get_func = props->auth_app_id_get_func;
  init_props->auth_license_get_func = props->auth_license_get_func;
  init_props->auth_sn_get_func = props->auth_sn_get_func;
  init_props->auth_app_code_get_func = props->auth_app_code_get_func;
  init_props->device_code_get_func = props->device_code_get_func;
  init_props->flash_cache_path = (char *)lingxin_malloc(strlen(props->flash_cache_path) + 1);
  strcpy(init_props->flash_cache_path, props->flash_cache_path);
  init_props->welcome_audio_path = (char *)lingxin_malloc(strlen(props->welcome_audio_path) + 1);
  strcpy(init_props->welcome_audio_path, props->welcome_audio_path);
  init_props->props_init_tag = (char *)lingxin_malloc(strlen(props->props_init_tag) + 1);
  strcpy(init_props->props_init_tag, props->props_init_tag);
  init_props->is_log_upload_on = props->is_log_upload_on;
  init_props->is_schedule_task_on = props->is_schedule_task_on;
}

void lingxin_test_runner_wakeup() {
    inner_start_new_chat(start_props);
}


static void lingxin_test_connect_ws() {
    lingxin_log_debug("lingxin_test_connect_ws");
    WebsocketConfig *config = (WebsocketConfig *)lingxin_malloc(sizeof(WebsocketConfig));
    memset(config, 0, sizeof(WebsocketConfig));
    config->protocol = "ws";
    config->host = TEST_WS_IP;
    config->path = "";
    config->port = 8082;
    config->listener = lingxin_test_ws_listener;
    config->userContext = NULL;
    lingxin_log_debug("lingxin_test_connect_ws init");
    lingxin_test_ws = initWebsocket(config);
    if (!lingxin_test_ws) {
        lingxin_free(config);
        return;
    }
    lingxin_log_debug("lingxin_test_connect_ws start");
    startWebsocket(lingxin_test_ws);
}

static void lingxin_test_ws_listener(WebSocketEventType event, const char *data, size_t data_len, const int is_binary, void *userContext) {
    switch (event) {
        case ON_WEBSOCKET_CONNECTION_SUCCESS:
            lingxin_log_debug("lingxin_test_connect_ws success");
            break;
        case ON_WEBSOCKET_DATA_RECEIVED:
            lingxin_test_receive_data((char*)data, data_len, is_binary);
            break;
        case ON_WEBSOCKET_ERROR: 
            lingxin_log_debug("lingxin_test_connect_ws error");
            lingxin_test_connect_ws();
            break;
        case ON_WEBSOCKET_DESTROY:
            lingxin_log_debug("lingxin_test_connect_ws destroy");
            lingxin_test_connect_ws();
            break;
        default:
            break;
    }
}

/**
 * 协议
 * {
 *   func: "lingxin_test_voice_chat_init",
 *   props: {
 *     send_uni_size: 120,
 *     send_cbuf_scale: 50,
 *   }
 * }
 * {
 *   func: "lingxin_test_send_binary_stream_start",
 *   props: {
 *     content_type: "image/png",
 *     unique_id: "file_name_1.png",
 *   }
 * }
 * {
 *   func: "lingxin_test_send_text",
 *   props: {
 *     text: "hello world",
 *   }
 * }
 */
typedef void (*lingxin_test_func_t)(cJSON *props_cjson);
static const char* name_map[] = {
    "lingxin_test_set_chat_biz_parameter",
    "lingxin_test_voice_chat_init",
    "lingxin_test_set_start_new_chat_props",
    "lingxin_test_start_new_chat",
    "lingxin_test_stop_record",
    "lingxin_test_exit_chat",
    "lingxin_test_set_volume",
    "lingxin_test_send_binary_stream_start",
    "lingxin_test_send_binary_stream_end",
    "lingxin_test_send_text",
    "lingxin_test_multimodal_start_record",
    "lingxin_test_multimodal_stop_record",
    "lingxin_test_multimodal_input_end",
    "lingxin_test_system_abort",
    "lingxin_test_memory_check"
};
static const lingxin_test_func_t func_map[] = {
    lingxin_test_set_chat_biz_parameter,
    lingxin_test_voice_chat_init,
    lingxin_test_set_start_new_chat_props,
    lingxin_test_start_new_chat,
    lingxin_test_stop_record,
    lingxin_test_exit_chat,
    lingxin_test_set_volume,
    lingxin_test_send_binary_stream_start,
    lingxin_test_send_binary_stream_end,
    lingxin_test_send_text,
    lingxin_test_multimodal_start_record,
    lingxin_test_multimodal_stop_record,
    lingxin_test_multimodal_input_end,
    lingxin_test_system_abort,
    lingxin_test_memory_check
};
static void lingxin_test_receive_data(char *data, size_t data_len, int is_binary) {
    if (is_binary) {
        if (!multimodal_inputing) {
            lingxin_log_warn("lingxin_test_receive_data: not in inputing, ignore");
            return;
        }
        if (!multimodal_functions.send_stream) {
            lingxin_log_error("lingxin_test_receive_data: multimodal_functions.send_stream is null");
            return;
        }
        LingxinSendStreamProps send_stream_props = {
            .unique_id = multimodal_current_unique_id,
            .index = multimodal_frame_index++,
            .frame = data,
            .content_len = data_len,
            .content_type = multimodal_current_content_type,
            .is_last = false,
        };
        multimodal_functions.send_stream(&send_stream_props);
    } else {
        cJSON *data_cjson = cJSON_Parse(data);
        if (!data_cjson) {
            const char* error = cJSON_GetErrorPtr();
            lingxin_log_error("parse data error: data=%s, error_ptr=%s", data, error);
            return;
        }
        cJSON *func_cjson = cJSON_GetObjectItemCaseSensitive(data_cjson, "func");
        if (!func_cjson || !cJSON_IsString(func_cjson)) {
            lingxin_log_error("parse func error");
            cJSON_Delete(data_cjson);
            return;
        }
        cJSON *props_cjson = cJSON_GetObjectItemCaseSensitive(data_cjson, "props");
        char *func = func_cjson->valuestring;
        lingxin_log_debug("lingxin_test_receive_data: func=%s", func);
        for (unsigned int i = 0; i < sizeof(name_map) / sizeof(name_map[0]); i++) {
            if (strcmp(func, name_map[i]) == 0 && func_map[i]) {
                func_map[i](props_cjson);
                break;
            }
        }
        cJSON_Delete(data_cjson);
    }
}

static char* lingxin_test_chat_biz_parameter_get_func() {
    return biz_parameter ? biz_parameter : NULL;
}
static void lingxin_test_chat_life_cycle_event_listener(ChatLifeCycleEvent event, void *payload) {
    switch (event) {
        case CHAT_LIFE_CYCLE_EVENT_EXIT: {
            ExitPayload* exit_payload = (ExitPayload*)payload;
            lingxin_log_debug("CHAT_LIFE_CYCLE_EVENT_EXIT: exit_code=%d", exit_payload->exit_code);
            lingxin_checkpoint_report_with_int("lingxin_test_chat_exited", exit_payload->exit_code);
            break;
        }
        case CHAT_LIFE_CYCLE_EVENT_SCHEDULE_EMIT: {
            lingxin_log_debug("CHAT_LIFE_CYCLE_EVENT_SCHEDULE_EMIT");
            break;
        }
        case CHAT_LIFE_CYCLE_EVENT_TEXT_OUT: {
            char* text_out_payload = (char*)payload;
            lingxin_log_debug("CHAT_LIFE_CYCLE_EVENT_TEXT_OUT: %s", text_out_payload);
            break;
        }
        case CHAT_LIFE_CYCLE_EVENT_PLAY_END: {
            lingxin_log_debug("CHAT_LIFE_CYCLE_EVENT_PLAY_END");
            lingxin_memory_print_statistics();
            break;
        }
        case CHAT_LIFE_CYCLE_EVENT_ERROR: {
            char* error_payload = (char*)payload;
            lingxin_log_debug("CHAT_LIFE_CYCLE_EVENT_ERROR: %s", error_payload);
        }
        default:
            break;
    }
}

static void lingxin_test_multimodal_input_listener(LingxinMultimodalInputListenerProps props) {
    lingxin_log_debug("lingxin_test_multimodal_input_listener: event=%d", props.event);
    switch (props.event) {
        case LINGXIN_MULTIMODAL_EVENT_INPUT_START: {
            // 重置数据
            multimodal_frame_index = 1;
            multimodal_functions = props;
            multimodal_inputing = true;
            break;
        }
        case LINGXIN_MULTIMODAL_EVENT_RECORDER_STOP: {
            break;
        }
        case LINGXIN_MULTIMODAL_EVENT_STREAM_INPUT_SUCCESS: {
            lingxin_log_debug("LINGXIN_MULTIMODAL_EVENT_STREAM_INPUT_SUCCESS");
            break;
        }
        case LINGXIN_MULTIMODAL_EVENT_INPUT_INTERRUPT: {
            if (!multimodal_inputing) {
                lingxin_log_warn("lingxin_test_multimodal_input_end: not in inputing, ignore");
                break;
            }
            multimodal_inputing = false;
            if (!multimodal_functions.input_end) {
                lingxin_log_error("LINGXIN_MULTIMODAL_EVENT_INPUT_INTERRUPT: multimodal_functions.input_end is null");
                break;
            }
            multimodal_functions.input_end(NULL);
            break;
        }
        default:
            break;
    }
}

static void lingxin_test_set_chat_biz_parameter(cJSON *props_cjson) {
    if (biz_parameter) {
        lingxin_free(biz_parameter);
    }
    if (props_cjson && cJSON_IsObject(props_cjson)) {
        cJSON *biz_parameter_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "biz_parameter");
        if (biz_parameter_cjson && cJSON_IsString(biz_parameter_cjson)) {
            biz_parameter = lingxin_strdup(biz_parameter_cjson->valuestring);
        } else {
            biz_parameter = NULL;
        }
    } else {
        biz_parameter = NULL;
    }
    
}
static void lingxin_test_voice_chat_init(cJSON *props_cjson) {
    set_volume(40);
    if (props_cjson && cJSON_IsObject(props_cjson)) {
        cJSON *send_uni_size_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "send_uni_size");
        if (send_uni_size_cjson && cJSON_IsNumber(send_uni_size_cjson)) {
            init_props->send_uni_size = send_uni_size_cjson->valueint;                
        } else {
            init_props->send_uni_size = 0;
        }
        cJSON *send_cbuf_scale_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "send_cbuf_scale");
        if (send_cbuf_scale_cjson && cJSON_IsNumber(send_cbuf_scale_cjson)) {
            init_props->send_cbuf_scale = send_cbuf_scale_cjson->valueint;                
        } else {
            init_props->send_cbuf_scale = 0;
        }
        cJSON *enable_continue_audio_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "enable_continue_audio");
        if (enable_continue_audio_cjson && cJSON_IsBool(enable_continue_audio_cjson) && cJSON_IsTrue(enable_continue_audio_cjson)) {
            init_props->continue_audio_path = init_props->welcome_audio_path;               
        } else {
            init_props->continue_audio_path = NULL;
        }
        cJSON *enable_terminate_audio_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "enable_terminate_audio");
        if (enable_terminate_audio_cjson && cJSON_IsBool(enable_terminate_audio_cjson) && cJSON_IsTrue(enable_terminate_audio_cjson)) {
            init_props->terminate_audio_path = init_props->welcome_audio_path;             
        } else {
            init_props->terminate_audio_path = NULL;
        }
    }
    init_props->chat_life_cycle_event_listener = lingxin_test_chat_life_cycle_event_listener;
    init_props->chat_biz_parameter_get_func = lingxin_test_chat_biz_parameter_get_func;
    inner_voice_chat_init(init_props);
}

static void lingxin_test_set_start_new_chat_props(cJSON *props_cjson) {
    if (!start_props) {
        start_props = lingxin_malloc(sizeof(StartNewChatProps));
    } else {
        // 释放旧数据
        if (start_props->task) {
            lingxin_free(start_props->task);
        }
        if (start_props->task_id) {
            lingxin_free(start_props->task_id);
        }
        if (start_props->user_input) {
            lingxin_free(start_props->user_input);
        }
    }
    memset(start_props, 0, sizeof(StartNewChatProps));
    StartNewChatProps default_props = get_start_new_chat_default_props();
    *start_props = default_props;
    if (!props_cjson || !cJSON_IsObject(props_cjson)) {
        return;
    }
    cJSON *task_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "task");
    if (task_cjson && cJSON_IsString(task_cjson)) {
        start_props->task = lingxin_strdup(task_cjson->valuestring);
    }
    cJSON *task_id_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "task_id");
    if (task_id_cjson && cJSON_IsString(task_id_cjson)) {
        start_props->task_id = lingxin_strdup(task_id_cjson->valuestring);
    }
    cJSON *user_input_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "user_input");
    if (user_input_cjson && cJSON_IsString(user_input_cjson)) {
        start_props->user_input = lingxin_strdup(user_input_cjson->valuestring);           
    }
    cJSON *single_round_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "single_round");
    if (single_round_cjson && cJSON_IsBool(single_round_cjson)) {
        start_props->single_round = cJSON_IsTrue(single_round_cjson);            
    }
    cJSON *disable_vad_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "disable_vad");
    if (disable_vad_cjson && cJSON_IsBool(disable_vad_cjson)) {
        start_props->disable_vad = cJSON_IsTrue(disable_vad_cjson);            
    }
    cJSON *disable_welcome_audio_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "disable_welcome_audio");
    if (disable_welcome_audio_cjson && cJSON_IsBool(disable_welcome_audio_cjson)) {
        start_props->disable_welcome_audio = cJSON_IsTrue(disable_welcome_audio_cjson);            
    }
    if (start_props->task && strcmp(start_props->task, "chat_multimodal") == 0) {
        start_props->multimodal_input_listener = lingxin_test_multimodal_input_listener;
    }
    if (start_props->task && strcmp(start_props->task, "chat_multimodal_vad") == 0) {
        start_props->multimodal_input_listener = lingxin_test_multimodal_input_listener;
    }
}
static void lingxin_test_start_new_chat(cJSON *props_cjson) {
    lingxin_test_set_start_new_chat_props(props_cjson);
    inner_start_new_chat(start_props);
}

static void lingxin_test_stop_record(cJSON *props_cjson) {
    stop_chat_record(NULL);
}

static void lingxin_test_exit_chat(cJSON *props_cjson) {
    ExitChatProps props = get_exit_chat_default_props();
    if (props_cjson && cJSON_IsObject(props_cjson)) {
        cJSON *disable_close_ws_immediately_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "disable_close_ws_immediately");
        if (disable_close_ws_immediately_cjson && cJSON_IsBool(disable_close_ws_immediately_cjson)) {
            props.disable_close_ws_immediately = cJSON_IsTrue(disable_close_ws_immediately_cjson);            
        }
    }
    exit_chat(&props);
}
static void lingxin_test_set_volume(cJSON *props_cjson) {
    if (props_cjson && cJSON_IsObject(props_cjson)) {
        cJSON *volume_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "volume");
        if (volume_cjson && cJSON_IsNumber(volume_cjson)) {
            set_volume(volume_cjson->valueint);           
        }              
    }
}

static void lingxin_test_send_binary_stream_start(cJSON *props_cjson) {
    if (!multimodal_inputing) {
        lingxin_log_warn("lingxin_test_send_binary_stream_start: not in inputing, ignore");
        return;
    }
    if (props_cjson && cJSON_IsObject(props_cjson)) {
        cJSON *content_type_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "content_type");
        if (content_type_cjson && cJSON_IsString(content_type_cjson)) {
            if (multimodal_current_content_type) {
                lingxin_free(multimodal_current_content_type);
            }
            multimodal_current_content_type = lingxin_strdup(content_type_cjson->valuestring);
        }
        cJSON *unique_id_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "unique_id");
        if (unique_id_cjson && cJSON_IsString(unique_id_cjson)) {
            if (multimodal_current_unique_id) {
                lingxin_free(multimodal_current_unique_id);
            }
            multimodal_current_unique_id = lingxin_strdup(unique_id_cjson->valuestring);
            multimodal_frame_index = 1;
        }
    }
}
static void lingxin_test_send_binary_stream_end(cJSON *props_cjson) {
    if (!multimodal_inputing) {
        lingxin_log_warn("lingxin_test_send_binary_stream_end: not in inputing, ignore");
        return;
    }
    if (!multimodal_functions.send_stream) {
        lingxin_log_error("lingxin_test_receive_data: multimodal_functions.send_stream is null");
        return;
    }
    LingxinSendStreamProps send_stream_props = {
        .unique_id = multimodal_current_unique_id,
        .index = multimodal_frame_index++,
        .frame = "",
        .content_len = 0,
        .content_type = multimodal_current_content_type,
        .is_last = true,
    };
    multimodal_functions.send_stream(&send_stream_props);

}
static void lingxin_test_send_text(cJSON *props_cjson) {
    if (props_cjson && cJSON_IsObject(props_cjson)) {
        cJSON *text_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "text");
        if (text_cjson && cJSON_IsString(text_cjson)) {
            if (multimodal_functions.send_text) {
                LingxinSendTextProps p = {
                    .content = text_cjson->valuestring,
                };
                multimodal_functions.send_text(&p);
            } else {
                lingxin_log_error("fail to send text: multimodal_functions.send_text is NULL");
            }
        }
    }
}

static void lingxin_test_multimodal_start_record(cJSON *props_cjson) {
    if (!multimodal_inputing) {
        lingxin_log_warn("lingxin_test_multimodal_start_record: not in inputing, ignore");
        return;
    }
    if (!multimodal_functions.start_record) {
        lingxin_log_error("lingxin_test_multimodal_start_record: multimodal_functions.start_record is null");
        return;
    }
    multimodal_functions.start_record(NULL);
}
static void lingxin_test_multimodal_stop_record(cJSON *props_cjson) {
    if (!multimodal_inputing) {
        lingxin_log_warn("lingxin_test_multimodal_stop_record: not in inputing, ignore");
        return;
    }
    if (!multimodal_functions.stop_record) {
        lingxin_log_error("lingxin_test_multimodal_stop_record: multimodal_functions.stop_record is null");
        return;
    }
    multimodal_functions.stop_record(NULL);
}
static void lingxin_test_multimodal_input_end(cJSON *props_cjson) {
    if (!multimodal_inputing) {
        lingxin_log_warn("lingxin_test_multimodal_input_end: not in inputing, ignore");
        return;
    }
    if (!multimodal_functions.input_end) {
        lingxin_log_error("lingxin_test_multimodal_input_end: multimodal_functions.input_end is null");
        return;
    }
    multimodal_functions.input_end(NULL);
    multimodal_inputing = false;
}

static void lingxin_test_system_abort(cJSON *props_cjson) {
#ifdef LINGXIN_TEST
    lingxin_system_abort();
#endif
}

static void lingxin_test_memory_check(cJSON *props_cjson) {
    if (props_cjson && cJSON_IsObject(props_cjson)) {
        cJSON *open_cjson = cJSON_GetObjectItemCaseSensitive(props_cjson, "open");
        if (open_cjson && cJSON_IsBool(open_cjson)) {
            if (cJSON_IsTrue(open_cjson)) {
                lingxin_memory_enable_statistics();
            } else {
                lingxin_memory_destroy_statistics();
            }
        } else {
            lingxin_memory_destroy_statistics();
        }
    } else {
        lingxin_memory_destroy_statistics();
    }
}

void lingxin_checkpoint_report(char* checkpoint_name) {
    if (lingxin_test_ws) {
        cJSON *jsonObj = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonObj, "checkpoint_name", checkpoint_name);
        char* result = cJSON_PrintUnformatted(jsonObj);
        lingxin_log_debug("lingxin_checkpoint_report: %s", result);
        websocketSendText(lingxin_test_ws, result);
        cJSON_free(result);
        cJSON_Delete(jsonObj);
    }
}

void lingxin_checkpoint_report_with_int(char* checkpoint_name, int value) {
    if (lingxin_test_ws) {
        cJSON *jsonObj = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonObj, "checkpoint_name", checkpoint_name);
        cJSON_AddNumberToObject(jsonObj, "value", value);
        char* result = cJSON_PrintUnformatted(jsonObj);
        lingxin_log_debug("lingxin_checkpoint_report: %s", result);
        websocketSendText(lingxin_test_ws, result);
        cJSON_free(result);
        cJSON_Delete(jsonObj);
    }
}

void lingxin_checkpoint_report_with_string(char* checkpoint_name, const char* value) {
    if (lingxin_test_ws) {
        cJSON *jsonObj = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonObj, "checkpoint_name", checkpoint_name);
        cJSON_AddStringToObject(jsonObj, "value", value);
        char* result = cJSON_PrintUnformatted(jsonObj);
        lingxin_log_debug("lingxin_checkpoint_report: %s", result);
        websocketSendText(lingxin_test_ws, result);
        cJSON_free(result);
        cJSON_Delete(jsonObj);
    }
}