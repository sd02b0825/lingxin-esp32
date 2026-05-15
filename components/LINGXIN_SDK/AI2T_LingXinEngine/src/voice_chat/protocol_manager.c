#include "audio_buffer_play.h"
#include "lingxin_protocol_manager.h"
#include "chat_state_machine.h"
#include "lingxin_voice_chat_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "lingxin_semaphore.h"
#include "lingxin_common.h"
#include "lingxin_log.h"
#include "cJSON.h"
#include "lingxin_json_util.h"
#include "lingxin_hook_websocket.h"
#include "lingxin_user_track.h"
#include "lingxin_system_time.h"
#include "lingxin_memory.h"
#include "lingxin_base64_utils.h"

#define MAX_LISTENERS 10 // 定义最大监听器数量

struct VoiceChatHandler
{
  VoiceChatConfig *chat_config;
  WebsocketClient *websocket;
  bool waitTerminate;
  VoiceChatContextInfo *contextInfo;
  struct VoiceChatHandler **selfPointer; // 存储双指针的引用
  bool need_free_taskId;
};
static int global_chat_listener_count = 0;
static ChatEventListener global_chat_listeners[MAX_LISTENERS];
static VoiceChatHandler *globalHandler = NULL;

static bool isAIResponseding = false; // 已给服务端发请求，带响应，或者 已在响应，YES 才能发打断
static bool isAudioSending = false;
static bool waitTerminateOrEndSuccess = false;
void setWaitTerminateOrEndSuccess(bool target)
{
  waitTerminateOrEndSuccess = target;
}

static char *getReqId(VoiceChatHandler *handler)
{
  return (handler && handler->contextInfo && handler->contextInfo->requestId) ? handler->contextInfo->requestId : "";
}

static void notify_all_listeners(ChatEventType event, const char *data, const size_t len)
{
  // 遍历所有注册的监听器并调用它们
  for (int i = 0; i < global_chat_listener_count; i++)
  {
    if (global_chat_listeners[i])
    {
      global_chat_listeners[i](event, data, len);
    }
  }
}

static void on_voice_chat_error(char *error)
{
  lingxin_log_ut_with_args(LINGXIN_ERROR, "on_voice_chat_error", error);
  notify_all_listeners(CHAT_EVENT_ON_ERROR, error, strlen(error));
  lingxin_report_error(error);
}

static bool fill_config_params(VoiceChatConfig *chat_config, ChatStartNewParams *params)
{
  if (!chat_config)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_get_config_fail", "config is null");
    core_node_record("chat_manager_get_config_fail");
    return false;
  }
  char *appKey = lingxin_auth_license_get();
  if (!appKey || strlen(appKey) == 0)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_get_config_fail", "appKey is null");
    core_node_record("chat_manager_get_config_fail");
    return false;
  }
  char *sn = lingxin_auth_sn_get();
  if (!sn || strlen(sn) == 0)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_get_config_fail", "sn is null");
    core_node_record("chat_manager_get_config_fail");
    return false;
  }
  char *appId = lingxin_auth_appId_get();
  if (!appId || strlen(appId) == 0)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_get_config_fail", "appId is null");
    core_node_record("chat_manager_get_config_fail");
    return false;
  }
  char *agentCode = lingxin_auth_appCode_get();
  if (!agentCode || strlen(agentCode) == 0)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_get_config_fail", "agentCode is null");
    core_node_record("chat_manager_get_config_fail");
    return false;
  }

  char *custom_parameters = lingxin_chat_custom_parameter_get();
  char *biz_parameters = lingxin_chat_biz_parameter_get();
  char *flow_control_parameters = lingxin_chat_flow_control_parameter_get();

  chat_config->serverPath = WEBSOCKET_CHAT_PATH;
  chat_config->appKey = appKey;
  chat_config->sn = sn;
  chat_config->appId = appId;
  if (!params->taskId || strlen(params->taskId) == 0)
  {
    chat_config->taskId = chat_config->taskId ? chat_config->taskId : generateUUID(32);
  }
  else
  {
    chat_config->taskId = lingxin_strdup(params->taskId);
  }
  char *vad_str = params->server_vad ? "true" : "false";
  char *play_prologue_str = params->play_prologue ? "true" : "false";

  char *payload_format = "{\"cloud_vad\":%s,\"task\":\"%s\",\"input_mode\":\"%s\",\"output_mode\":\"%s\",\"schedule_task_id\":\"%s\",\"agent_code\":\"%s\",\"agent_basic_inputs\":{\"user_input\":\"%s\"},\"agent_ext_inputs\":{\"play_prologue\":%s},\"custom_parameters\":%s,\"biz_parameters\":%s,\"flow_control_parameters\":%s}";
  int length = snprintf(NULL, 0, payload_format, vad_str, params->task, params->input_mode, params->output_mode, params->scheduleTaskId, agentCode, params->user_input, play_prologue_str, custom_parameters, biz_parameters, flow_control_parameters);
  if (length <= 0)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_get_config_fail", "payload format error");
    core_node_record("chat_manager_get_config_fail");
    return false;
  }
  // TODO: 改成lingxin_realloc
  char *new_payload = realloc((char *)chat_config->payload, length + 1);
  if (!new_payload)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_get_config_fail", "payload realloc fail");
    core_node_record("chat_manager_get_config_fail");
    return false;
  }
  chat_config->payload = (const char *)new_payload;
  snprintf((char *)chat_config->payload, length + 1, payload_format, vad_str, params->task, params->input_mode, params->output_mode, params->scheduleTaskId, agentCode, params->user_input, play_prologue_str, custom_parameters, biz_parameters);
  // 释放biz_parameters，申请在lingxin_chat_biz_parameter_get中
  lingxin_free(biz_parameters);
  lingxin_log_ut(LINGXIN_DEBUG, "chat_manager_get_config_finish");
  return true;
}

static void event_send_log(char *event_name, int state, char *args)
{
  switch (state)
  {
  case 1:
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "chat_manager_send_event", "event: %s, status: begin, request_id:%s", event_name, getReqId(globalHandler));
    break;
  case 2:
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_send_event", "event: %s, status: fail, request_id:%s, reason: %s", event_name, getReqId(globalHandler), args ? args : "");
    break;
  case 3:
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "chat_manager_send_event", "event: %s, status: finish, request_id:%s", event_name, getReqId(globalHandler));
    break;
  default:
    break;
  }
}

static bool sendStartTask(VoiceChatHandler *handler)
{
  event_send_log("start_task", 1, NULL);
  if (!handler)
  {
    event_send_log("start_task", 2, "handler null");
    return false;
  }

  if (!handler->chat_config || !handler->chat_config->taskId || !handler->chat_config->payload)
  {
    event_send_log("start_task", 2, "params valid");
    return false;
  }

  // 计算所需缓冲区大小
  char *format_str = "{\"header\":{\"action\":\"start_task\",\"task_id\":\"%s\"},\"payload\":%s}";
  int message_len = snprintf(NULL, 0, format_str, handler->chat_config->taskId, handler->chat_config->payload);
  if (message_len <= 0)
  {
    event_send_log("start_task", 2, "params format fail");
    return false;
  }
  // 动态分配内存
  char *message = (char *)lingxin_malloc(message_len + 1);
  if (!message)
  {
    event_send_log("start_task", 2, "memory allocate fail");
    return false;
  }
  // 格式化消息
  snprintf(message, message_len + 1, format_str, handler->chat_config->taskId, handler->chat_config->payload);
  bool result = hook_websocket_send_text(handler->websocket, message);

  if (!result)
  {
    event_send_log("start_task", 2, "send fail");
    lingxin_free(message);
    return false;
  }
  core_node_record("start_task");
  event_send_log("start_task", 3, NULL);
  lingxin_free(message);
  return result;
}

static void sendEndTask(VoiceChatHandler *handler)
{
  event_send_log("end_task", 1, NULL);
  if (!handler)
  {
    event_send_log("end_task", 2, "handler null");
    return;
  }

  if (!handler->chat_config || !handler->chat_config->taskId)
  {
    event_send_log("end_task", 2, "params null");
    return;
  }
  if (handler->waitTerminate)
  {
    event_send_log("end_task", 2, "waitTerminate, can not send end_task");
    return;
  }
  char message[256];
  snprintf(message, sizeof(message), "{\"header\":{\"action\":\"end_task\",\"task_id\":\"%s\",\"request_id\":\"%s\"},\"payload\":{}}", handler->chat_config->taskId, getReqId(handler));
  bool result = hook_websocket_send_text(handler->websocket, message);
  if (!result)
  {
    event_send_log("end_task", 2, "send fail");
    return;
  }
  waitTerminateOrEndSuccess = true;
  core_node_record("end_task");
  event_send_log("end_task", 3, NULL);
}

static void dealResponseDataEvent(cJSON *jsonMessage)
{
  cJSON *payload_json = cJSON_GetObjectItemCaseSensitive(jsonMessage, "payload");
  if (!cJSON_IsObject(payload_json))
  {
    lingxin_log_error("payload not an object");
    return;
  }
  cJSON *data_json = cJSON_GetObjectItemCaseSensitive(payload_json, "data");
  if (!cJSON_IsObject(data_json))
  {
    lingxin_log_error("data not an object");
    return;
  }

  char *frame_content = NULL;
  char *frame_type = NULL;
  // int frame_index = 0;
  bool last_frame = false;
  size_t frame_length;

  cJSON *frame_base64_json = cJSON_GetObjectItemCaseSensitive(data_json, "frame_base64");
  if (cJSON_IsString(frame_base64_json) && frame_base64_json->valuestring)
  {
    frame_content = lingxin_base64_decode(frame_base64_json->valuestring, strlen(frame_base64_json->valuestring), &frame_length);
  }
  // cJSON *frame_index_json = cJSON_GetObjectItemCaseSensitive(data_json, "frame_index");
  // if (cJSON_IsNumber(frame_index_json) && frame_index_json->valueint)
  // {
  //   frame_index = frame_index_json->valueint;
  // }
  cJSON *frame_type_json = cJSON_GetObjectItemCaseSensitive(data_json, "frame_type");
  if (cJSON_IsString(frame_type_json) && frame_type_json->valuestring)
  {
    frame_type = frame_type_json->valuestring;
  }
  cJSON *last_frame_json = cJSON_GetObjectItemCaseSensitive(data_json, "last_frame");
  if (cJSON_IsBool(last_frame_json))
  {
    last_frame = cJSON_IsTrue(last_frame_json);
  }
  if (strcmp(frame_type, "audio") == 0)
  {
    if (!globalHandler->waitTerminate)
    {
      // lingxin_log_ut_with_args(LINGXIN_DEBUG, "chat_manager_parse_frame", "after decode, data length is %d", frame_length);
      if (frame_content)
      {
        state_machine_receive_mp3_data((void *)frame_content, (int)frame_length);
      }
      if (last_frame)
      {
        // TODO: 通知状态机音频下行结束，1.12.0版本后，状态机再实现
      }
    }
  }
  else
  {
    notify_all_listeners(CHAT_EVENT_ON_STREAM_DATA, frame_content, frame_length);
  }
  // 释放解码后的数据
  if (frame_content)
  {
    lingxin_free(frame_content);
  }
}

static void module_terminateCallback()
{
  state_machine_run_event(State_Event_VoiceChat_TerminateEnd);
}

static void dealEventFromServer(VoiceChatHandler *handler, const char *event, cJSON *message_json, const char *message_string)
{
  // 等待打断期间，只接收task_terminated和error
  // strcmp(event, "xxx")代表，event不等于xxx

  char *reqId = parseRequestId(message_json);
  if (handler->waitTerminate && strcmp(event, "task_terminated") && strcmp(event, "task_ended") && strcmp(event, "error"))
  {
    lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_discard_event", "wait terminate, event: %s, request_id:%s", event, reqId);
    return;
  }
  lingxin_log_ut_with_args(LINGXIN_DEBUG, "chat_manager_recv_event", "event: %s, request_id: %s", event, reqId);
  if (strcmp(event, "task_started") == 0)
  {
    if (reqId && handler->contextInfo)
    {
      if (!handler->contextInfo->requestId || strlen(handler->contextInfo->requestId) == 0)
      {
        handler->contextInfo->requestId = lingxin_strdup(reqId);
      }
      else if (strlen(reqId) == strlen(handler->contextInfo->requestId))
      {
        memcpy(handler->contextInfo->requestId, reqId, strlen(reqId));
      }
      else
      {
        char *old_requestId = handler->contextInfo->requestId;
        handler->contextInfo->requestId = lingxin_strdup(reqId);
        lingxin_free(old_requestId);
      }
    }
    // 复位，防止异常
    handler->waitTerminate = false;
    waitTerminateOrEndSuccess = false;
    notify_all_listeners(CHAT_EVENT_ON_AI_READY, "", 0);
  }
  else if (strcmp(event, "task_stage_end") == 0)
  {
    sendEndTask(handler);
  }
  else if (strcmp(event, "task_terminated") == 0)
  {
    handler->waitTerminate = false;
    isAIResponseding = false;
    waitTerminateOrEndSuccess = false;
    module_terminateCallback();
  }
  else if (strcmp(event, "task_ended") == 0)
  {
    handler->waitTerminate = false;
    isAIResponseding = false;
    waitTerminateOrEndSuccess = false;
    state_machine_run_event(State_Event_VoiceChat_AIEnd);
  }
  else if (strcmp(event, "text_output") == 0)
  {
    // 这里用到了cJSON_PrintUnformatted，注意要释放
    const char *payload = parsePayloadStr(message_json);
    lingxin_emit_chat_event(CHAT_LIFE_CYCLE_EVENT_TEXT_OUT, (char *)payload);
    cJSON_free((char *)payload);
  }
  else if (strcmp(event, "error") == 0)
  {
    on_voice_chat_error((char *)message_string);
  }
  else if (strcmp(event, "vad_end") == 0)
  {
    notify_all_listeners(CHAT_EVENT_ON_VAD_END, "", 0);
  }
  else if (strcmp(event, "vad_exit") == 0)
  {
    notify_all_listeners(CHAT_EVENT_ON_VAD_EXIT, "", 0);
  }
  else if (strcmp(event, "system_event") == 0)
  {
    // 这里用到了cJSON_PrintUnformatted，注意要释放
    const char *payload = parsePayloadStr(message_json);
    notify_all_listeners(CHAT_EVENT_ON_SYSTEM_EVENT, payload, strlen(payload));
    cJSON_free((char *)payload);
  }
  else if (strcmp(event, "response_data") == 0)
  {
    dealResponseDataEvent(message_json);
  }
  else if (strcmp(event, "request_ended") == 0)
  {
    const char *payload = parsePayloadStr(message_json);
    notify_all_listeners(CHAT_EVENT_ON_REQUEST_DATA_END, payload, strlen(payload));
    cJSON_free((char *)payload);
  }
  else
  {
    lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_unknown_event", "event: %s unknown, ignore", event);
  }
}

static void onEventMessageReceived(VoiceChatHandler *handler, const char *message)
{
  cJSON *jsonMessage = cJSON_Parse(message);
  if (!jsonMessage)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_recv_parse_error", "message not json: %s, request_id:%s", message, getReqId(handler));
    return;
  }
  // 解析 event
  const char *eventStr = parseEvent(jsonMessage);
  if (!eventStr)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_recv_parse_error", "event null, request_id:%s", getReqId(handler));
    return;
  }
  dealEventFromServer(handler, eventStr, jsonMessage, message);
  cJSON_Delete(jsonMessage);
}

static void freeVoiceChat(VoiceChatHandler **handlerAddress)
{
  lingxin_log_ut(LINGXIN_WARN, "chat_manager_exit_free_begin");

  if (!handlerAddress)
  {
    lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_exit_free_fail", "handlerAddress null");
    return;
  }

  VoiceChatHandler *handler = *handlerAddress;
  if (!handler)
  {
    lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_exit_free_fail", "handler null");
    return;
  }
  if (handler->websocket && handler->websocket->config)
  {
    free_websocket_config(handler->websocket->config);
    handler->websocket->config = NULL;
  }
  if (handler->chat_config)
  {
    if (handler->chat_config->taskId)
    {
      lingxin_free(handler->chat_config->taskId);
      handler->chat_config->taskId = NULL;
    }
    lingxin_free(handler->chat_config);
    handler->chat_config = NULL;
  }
  if (handler->contextInfo)
  {
    if (handler->contextInfo->instanceId)
    {
      lingxin_free(handler->contextInfo->instanceId);
      handler->contextInfo->instanceId = NULL;
    }
    if (handler->contextInfo->requestId)
    {
      lingxin_free(handler->contextInfo->requestId);
      handler->contextInfo->requestId = NULL;
    }
    lingxin_free(handler->contextInfo);
    handler->contextInfo = NULL;
  }
  lingxin_free(handler);
  handler = NULL;
  *handlerAddress = NULL;
  lingxin_log_ut(LINGXIN_WARN, "chat_manager_exit_free_finish");
}

static void on_voice_chat_init_fail()
{
  lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_start_new_fail", "chat init fail");
  core_node_record("chat_manager_start_new_fail");
  state_machine_receive_error(EXIT_REASON_WEBSOCKET_CONNECTION_FAILED);
}

static void onWebSocketEvent(WebSocketEventType event, const char *data, const size_t len, const int isBinary, void *userData)
{
  VoiceChatHandler *handler = (VoiceChatHandler *)userData;
  if (!handler)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_recv_parse_error", "handler nul");
    return;
  }
  switch (event)
  {
  case ON_WEBSOCKET_CONNECTION_SUCCESS:
    lingxin_log_ut(LINGXIN_DEBUG, "chat_manager_websocket_connect_success");
    sendStartTask(handler);
    break;
  case ON_WEBSOCKET_CONNECTION_FAIL:
    on_voice_chat_init_fail();
    break;
  case ON_WEBSOCKET_DATA_RECEIVED:
    if (isBinary)
    {
      if (!handler->waitTerminate)
      {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, "chat_manager_recv_binary", "binary data length: %d", len);
        state_machine_receive_mp3_data((void *)data, (int)len);
      }
      else
      {
        lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_discard_data", "binary data length: %d", len);
      }
    }
    else
    {
      onEventMessageReceived(handler, data);
    }
    break;
  case ON_WEBSOCKET_ERROR:
  {
    char *errorData = (char *)lingxin_malloc(len + 1);
    if (!errorData)
    {
      lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_recv_parse_error", "allocate memory fail for error data");
      return;
    }
    // 复制数据
    memcpy(errorData, data, len);
    // 添加字符串结束符
    errorData[len] = '\0';
    on_voice_chat_error(errorData);
    lingxin_free(errorData);
  }
  break;
  case ON_WEBSOCKET_DESTROY:

    isAudioSending = false;
    isAIResponseding = false;
    lingxin_log_ut(LINGXIN_ERROR, "chat_manager_exit_callback");
    freeVoiceChat(handler->selfPointer);
    state_machine_run_event(State_Event_VoiceChat_ExitEnd);
    lingxin_log_ut(LINGXIN_ERROR, "chat_manager_exit_finish");
    break;
  default:
    break;
  }
}

bool isVoiceChatInited() { return globalHandler != NULL; }

bool isVoiceChatResponding() { return isAIResponseding; }
static int voiceChatTerminateCheck()
{
  event_send_log("terminate_task", 1, NULL);
  if (!isAIResponseding)
  {
    event_send_log("terminate_task", 2, "not need terminate");
    return 0;
  }
  if (waitTerminateOrEndSuccess)
  {
    event_send_log("terminate_task", 2, "before end_task or terminate success,cannot terminate");
    return 2;
  }
  if (!globalHandler)
  {
    event_send_log("terminate_task", 2, "handler null");
    return 0;
  }
  if (!globalHandler->chat_config || !globalHandler->chat_config->taskId)
  {
    event_send_log("terminate_task", 2, "taskId null");
    return 0;
  }
  char message[256];
  snprintf(message, sizeof(message), "{\"header\":{\"action\":\"terminate_task\",\"task_id\":\"%s\",\"request_id\":\"%s\"},\"payload\":{}}", globalHandler->chat_config->taskId, getReqId(globalHandler));
  if (!hook_websocket_send_text(globalHandler->websocket, message))
  {
    event_send_log("terminate_task", 2, "send fail");
    return 0;
  }
  waitTerminateOrEndSuccess = true;
  isAudioSending = false;
  globalHandler->waitTerminate = true;
  core_node_record("terminate_task");
  event_send_log("terminate_task", 3, NULL);
  return 1;
}

/**
 * 打断对话
 * @return 0 打断指令没有发出去 1 打断指令已经正常发送出去 2 打断指令或者end_task指令之前已经发送出去过，需要等待响应回来
 */
int module_voiceChat_terminate()
{
  int r = voiceChatTerminateCheck();
  if (r == 0)
  {
    lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_terminate_callback", "terminate fail, directly callback");
    // 打断失败，这里不阻塞下次运行
    isAIResponseding = false;
    module_terminateCallback();
  }
  else
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "chat_manager_terminate_callback", r == 2 ? "wait terminated" : "normal terminate");
  }
  return r;
}

bool module_voiceChat_exit()
{
  lingxin_log_ut(LINGXIN_DEBUG, "chat_manager_exit_begin");

  if (!globalHandler || !globalHandler->selfPointer || !*globalHandler->selfPointer)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_exit_fail", "handler or handler->selfPointer null");
    return false;
  }
  closeWebsocket(globalHandler->websocket);
  return true;
}

static bool voiceChatCreate(ChatStartNewParams *params)
{
  WebsocketConfig *websocketConfig = NULL;
  globalHandler = (VoiceChatHandler *)lingxin_calloc(1, sizeof(struct VoiceChatHandler));
  if (!globalHandler)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_init_fail", "voicechat handler allocate fail");
    goto create_fail;
  }
  globalHandler->chat_config = (VoiceChatConfig *)lingxin_calloc(1, sizeof(VoiceChatConfig));
  if (!fill_config_params(globalHandler->chat_config, params))
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_init_fail", "config get fail");
    goto create_fail;
  }
  globalHandler->waitTerminate = false;
  websocketConfig = createWebsocketConfig(globalHandler, globalHandler->chat_config->sn, globalHandler->chat_config->appKey, globalHandler->chat_config->appId, globalHandler->chat_config->serverPath, onWebSocketEvent);
  if (!websocketConfig)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_init_fail", "websocket config create fail");
    goto create_fail;
  }
  globalHandler->websocket = hook_websocket_init(websocketConfig);
  if (!globalHandler->websocket)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_init_fail", "websocket init fail");
    goto create_fail;
  }
  globalHandler->contextInfo = (VoiceChatContextInfo *)lingxin_calloc(1, sizeof(VoiceChatContextInfo));
  if (globalHandler->contextInfo)
  {
    globalHandler->contextInfo->requestId = NULL;
    globalHandler->contextInfo->instanceId = generateUUID(16);
  }
  if (hook_websocket_start(globalHandler->websocket) == false)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_init_fail", "websocket start fail");
    goto create_fail;
  }
  globalHandler->selfPointer = &globalHandler; // 设置 selfPointer
  lingxin_log_ut(LINGXIN_DEBUG, "chat_manager_init_finish");
  return true;

create_fail:
  core_node_record("chat_manager_init_fail");
  if (globalHandler)
  {
    if (globalHandler->chat_config)
    {
      if (globalHandler->chat_config->payload)
      {
        lingxin_free(globalHandler->chat_config->payload);
        globalHandler->chat_config->payload = NULL;
      }
      lingxin_free(globalHandler->chat_config);
      globalHandler->chat_config = NULL;
    }
    if (websocketConfig)
    {
      free_websocket_config(websocketConfig);
    }

    lingxin_free(globalHandler);
    globalHandler = NULL;
  }
  return false;
}

bool add_protocol_event_listener(ChatEventListener listener)
{
  if (!listener)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_add_event_listener", "listener is null");
    return false;
  }
  // 检查监听器是否已存在
  for (int i = 0; i < global_chat_listener_count; i++)
  {
    if (global_chat_listeners[i] == listener)
    {
      lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_add_event_listener", "listener already exists");
      return false;
    }
  }
  // 检查是否已达到最大监听器数量
  if (global_chat_listener_count >= MAX_LISTENERS)
  {
    lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_add_event_listener", "max listeners reached");
    return false;
  }
  // 添加新的监听器
  global_chat_listeners[global_chat_listener_count] = listener;
  global_chat_listener_count++;
  return true;
}

bool remove_protocol_event_listener(ChatEventListener listener)
{
  if (!listener)
  {
    return false;
  }
  bool result = false;
  // 查找并移除指定的监听器
  for (int i = 0; i < global_chat_listener_count; i++)
  {
    if (global_chat_listeners[i] == listener)
    {
      // 将后续监听器向前移动一位
      for (int j = i; j < global_chat_listener_count - 1; j++)
      {
        global_chat_listeners[j] = global_chat_listeners[j + 1];
      }
      global_chat_listener_count--;
      global_chat_listeners[global_chat_listener_count] = NULL;
      result = true;
      break;
    }
  }
  return result;
}

bool voice_chat_start_new(ChatStartNewParams *params)
{
  lingxin_log_ut(LINGXIN_DEBUG, "chat_manager_start_new_begin");
  if (!params)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_start_new_fail", "params null");
    core_node_record("chat_manager_start_new_fail");
    return false;
  }
  if (params->is_schedule_timer_task)
  {
    // 如果是拉起新一轮novoice循环,允许continue
    isAIResponseding = false;
  }
  // 不允许连续多次continue
  if (globalHandler && isAIResponseding)
  {
    lingxin_log_ut_with_args(LINGXIN_WARN, "chat_manager_start_new_fail", "isAIResponseding true");
    return false;
  }
  // 之前没有建联或者建联后断联了
  if (globalHandler == NULL)
  {
    if (!voiceChatCreate(params))
    {
      on_voice_chat_init_fail();
      return false;
    }
    isAudioSending = false;
    isAIResponseding = true;
  }
  else
  {
    if (!fill_config_params(globalHandler->chat_config, params))
    {
      lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_start_new_fail", "config get fail");
      core_node_record("chat_manager_start_new_fail");
      return false;
    }
    // 非一次调用continue，需要重新发送start
    if (!sendStartTask(globalHandler))
    {
      lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_start_new_fail", "start send fail");
      core_node_record("chat_manager_start_new_fail");
      return false;
    }
    isAIResponseding = true;
  }
  return true;
}

void voiceChatSendAudio(void *audioData, int dataSize)
{
  if (!globalHandler)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_send_audio_fail", "handler null");
    return;
  }
  if (!audioData || !dataSize)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_send_audio_fail", "params null");
    return;
  }
  int result = hook_websocket_send_binary(globalHandler->websocket, audioData, dataSize);
  if (result <= 0)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "chat_manager_send_audio_fail", "send fail, request_id:%s", getReqId(globalHandler));
    return;
  }
  isAudioSending = true;
}

void voiceChatStopSendAudio()
{
  event_send_log("end_audio", 1, NULL);
  lingxin_log_ut(LINGXIN_DEBUG, "chat_manager_send_end_audio_begin");
  if (!isAudioSending)
  {
    event_send_log("end_audio", 2, "isAudioSending false");
    return;
  }
  if (!globalHandler)
  {
    event_send_log("end_audio", 2, "handler null");
    return;
  }
  if (!globalHandler->chat_config || !globalHandler->chat_config->taskId)
  {
    event_send_log("end_audio", 2, "params null");
    return;
  }
  char message[256];
  snprintf(message, sizeof(message), "{\"header\":{\"action\":\"end_audio\",\"task_id\":\"%s\",\"request_id\":\"%s\"},\"payload\":{}}", globalHandler->chat_config->taskId, getReqId(globalHandler));
  if (!hook_websocket_send_text(globalHandler->websocket, message))
  {
    event_send_log("end_audio", 2, "send fail");
    return;
  }
  isAudioSending = false;
  event_send_log("end_audio", 3, NULL);
}

// 多模态结束任务
bool voiceChat_send_end_up_task(size_t confirm_data_count, Multimodal_Chat_Confirm_Data confirm_data[])
{
  event_send_log("end_up_task", 1, NULL);
  char *reqId = getReqId(globalHandler);

  // 无confirm data 场景
  if (confirm_data_count <= 0 || !confirm_data)
  {
    char message[256];
    snprintf(message, sizeof(message), "{\"header\":{\"action\":\"end_up_task\",\"request_id\":\"%s\",\"task_id\":\"%s\"},\"payload\":{}}", reqId, globalHandler->chat_config->taskId);
    bool result = hook_websocket_send_text(globalHandler->websocket, message);
    if (!result)
    {
      event_send_log("end_up_task", 2, "send fail");
      return false;
    }
    event_send_log("end_up_task", 3, NULL);
    return result;
  }
  // 有confirm data 场景
  int base_len = snprintf(NULL, 0,
                          "{\"header\":{\"action\":\"end_up_task\",\"request_id\":\"%s\",\"task_id\":\"%s\"},\"payload\":{\"confirm_data\":[",
                          reqId, globalHandler->chat_config->taskId);
  if (base_len <= 0)
  {
    event_send_log("end_up_task", 2, "base len calc fail");
    return false;
  }

  // 计算 confirm_data 部分长度
  int data_len = 0;
  for (size_t i = 0; i < confirm_data_count; i++)
  {
    if (i > 0)
    {
      data_len += 1; // 逗号
    }
    int item_len = snprintf(NULL, 0,
                            "{\"unique_id\":\"%s\",\"frame_type\":\"%s\"}",
                            confirm_data[i].unique_id, confirm_data[i].frame_type);
    if (item_len < 0)
    {
      event_send_log("end_up_task", 2, "data item len calc fail");
      return false;
    }
    data_len += item_len;
  }

  int total_len = base_len + data_len + 4; // 4 for "]}]}"

  char *message = (char *)lingxin_malloc(total_len + 1);
  if (!message)
  {
    event_send_log("end_up_task", 2, "memory calc fail");
    return false;
  }

  int offset = snprintf(message, total_len + 1,
                        "{\"header\":{\"action\":\"end_up_task\",\"request_id\":\"%s\",\"task_id\":\"%s\"},"
                        "\"payload\":{\"confirm_data\":[",
                        reqId, globalHandler->chat_config->taskId);

  if (offset < 0 || offset >= total_len + 1)
  {
    event_send_log("end_up_task", 2, "base message format fail");
    lingxin_free(message);
    return false;
  }

  for (size_t i = 0; i < confirm_data_count; i++)
  {
    if (i > 0)
    {
      int comma_len = snprintf(message + offset, total_len + 1 - offset, ",");
      if (comma_len < 0 || comma_len >= total_len + 1 - offset)
      {
        event_send_log("end_up_task", 2, "comma len format fail");
        lingxin_free(message);
        return false;
      }
      offset += comma_len;
    }

    int item_len = snprintf(message + offset, total_len + 1 - offset,
                            "{\"unique_id\":\"%s\",\"frame_type\":\"%s\"}",
                            confirm_data[i].unique_id, confirm_data[i].frame_type);
    if (item_len < 0 || item_len >= total_len + 1 - offset)
    {
      event_send_log("end_up_task", 2, "data item format fail");
      lingxin_free(message);
      return false;
    }
    offset += item_len;
  }

  int end_len = snprintf(message + offset, total_len + 1 - offset, "]}}");
  if (end_len < 0 || end_len >= total_len + 1 - offset)
  {
    event_send_log("end_up_task", 2, "end message format fail");
    lingxin_free(message);
    return false;
  }
  bool result = hook_websocket_send_text(globalHandler->websocket, message);
  lingxin_free(message);
  if (!result)
  {
    event_send_log("end_up_task", 2, "send fail");
    return false;
  }
  event_send_log("end_up_task", 3, NULL);
  return result;
}

bool voiceChat_send_request_data_stream(char *unique_id, int index, const char *content, int content_len, char *content_type, char *frame_type, bool is_last)
{
  // event_send_log("request_data", 1, NULL);
  if (!unique_id || !content || !content_type)
  {
    event_send_log("request_data", 2, "unique_id or content or type null");
    return false;
  }
  if (!globalHandler)
  {
    event_send_log("request_data", 2, "handler null");
    return false;
  }
  if (!globalHandler->chat_config || !globalHandler->chat_config->taskId)
  {
    event_send_log("request_data", 2, "taskId null");
    return false;
  }
  char *encode_result = lingxin_base64_encode(content, content_len);
  if (!encode_result)
  {
    event_send_log("request_data", 2, "encode fail");
    return false;
  }
  char *reqId = getReqId(globalHandler);
  char *is_last_str = is_last ? "true" : "false";
  // 计算所需缓冲区大小
  char *format_str =
      "{\"header\":{\"action\":\"request_data\",\"request_id\":\"%s\",\"task_id\":\"%s\"},"
      "\"payload\":{\"data\":{\"unique_id\":\"%s\",\"frame_index\":%d,\"frame_base64\":\"%s\","
      "\"frame_type\":\"%s\",\"content_type\":\"%s\",\"last_frame\":%s}}}";
  int message_len = snprintf(NULL, 0, format_str, reqId, globalHandler->chat_config->taskId, unique_id, index, encode_result, frame_type, content_type, is_last_str);
  if (message_len <= 0)
  {
    event_send_log("request_data", 2, "message len calc fai");
    lingxin_free(encode_result);
    return false;
  }
  // 动态分配内存
  char *message = (char *)lingxin_malloc(message_len + 1);
  if (!message)
  {
    event_send_log("request_data", 2, "message allocate fai");
    lingxin_free(encode_result);
    return false;
  }
  // 格式化消息
  snprintf(message, message_len + 1, format_str, reqId, globalHandler->chat_config->taskId, unique_id, index, encode_result, frame_type, content_type, is_last_str);
  bool result = hook_websocket_send_text(globalHandler->websocket, message);
  if (!result)
  {
    event_send_log("request_data", 2, "send fail");
  }
  // event_send_log("request_data", 3, NULL);
  lingxin_free(message);
  lingxin_free(encode_result);
  return result;
}

bool voiceChat_send_request_data_text(char *content)
{
  if (!content)
  {
    event_send_log("request_data", 2, "content null");
    return false;
  }
  if (!globalHandler)
  {
    event_send_log("request_data", 2, "handler null");
    return false;
  }
  if (!globalHandler->chat_config || !globalHandler->chat_config->taskId)
  {
    event_send_log("request_data", 2, "taskId null");
    return false;
  }
  char *reqId = getReqId(globalHandler);

  // 计算所需缓冲区大小
  char *format_str = "{\"header\":{\"action\":\"request_data\",\"request_id\":\"%s\",\"task_id\":\"%s\"},"
                     "\"payload\":{\"data\":{\"content\":\"%s\",\"frame_type\":\"text\"}}}";

  int message_len = snprintf(NULL, 0, format_str, reqId, globalHandler->chat_config->taskId, content);
  if (message_len <= 0)
  {
    event_send_log("request_data", 2, "message len calc fail");
    return false;
  }
  // 动态分配内存
  char *message = (char *)lingxin_malloc(message_len + 1);
  if (!message)
  {
    event_send_log("request_data", 2, "message allocate fail");
    return false;
  }
  // 格式化消息
  snprintf(message, message_len + 1, format_str, reqId, globalHandler->chat_config->taskId, content);
  bool result = hook_websocket_send_text(globalHandler->websocket, message);
  if (!result)
  {
    event_send_log("request_data", 2, "send fail");
  }
  lingxin_free(message);
  return result;
}