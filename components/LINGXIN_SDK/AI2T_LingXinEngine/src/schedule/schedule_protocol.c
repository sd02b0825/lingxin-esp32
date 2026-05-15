#include "schedule_first_ws.h"
#include "cJSON.h"
#include "lingxin_common.h"
#include "lingxin_json_util.h"
#include "lingxin_hook_websocket.h"
#include "chat_state_machine.h"
#include "lingxin_timer.h"
#include "lingxin_log.h"
#include "lingxin_memory.h"
#include "lingxin_mutex.h"
#include "lingxin_voice_chat_config.h"

#ifdef LINGXI_USE_VOICE_QUEUE
#include "lingxin_event_queue.h"
#include "lingxin_voice_queue.h"
#endif // LINGXI_USE_VOICE_QUEUE

struct ScheduleChatHandler
{
  ScheduleChatConfig *config;
  WebsocketClient *websocket;
  SystemEventListener listener;
  ScheduleWsExtraInfo *extraInfo;
  ScheduleWsState *state;
  lingxin_mutex_t state_mutex;  // 保护所有状态变量
};

static void dealEventFromServer(ScheduleChatHandler *handler, const char *event,
                                cJSON *message)
{
  lingxin_log_debug("dealEventFromServer: %s", event);
  if (!event)
  {
    lingxin_log_debug("没有event");
    return;
  }
  // 等待打断期间，只接收task_terminated和error
  if (strcmp(event, "error") == 0)
  {
    char *errorInfo = parseErrorInfo(message);
    lingxin_log_debug("error: %s", errorInfo);
    cJSON_free(errorInfo);
  }
  else if (strcmp(event, "system_event") == 0)
  {
    lingxin_mutex_lock(handler->state_mutex);
    int timer_id = handler->state->sysEventTimerId;
    handler->state->sysEventTimerId = INVALID_TIMER_ID;
    handler->state->isRecievedSystemEvent = true;
    bool wsConnected = handler->state->isWsConnectSuccess;
    bool alreadyDestroying = handler->state->isDestroying;
    bool shouldDestroy = wsConnected && !alreadyDestroying;
    if (shouldDestroy) {
      handler->state->isDestroying = true;
    }
    lingxin_mutex_unlock(handler->state_mutex);
    if(timer_id != INVALID_TIMER_ID) {
        lingxin_log_debug("delete system_event timeout timer %d", timer_id);
        lingxin_one_shot_timer_delete(timer_id);
    }
    const char *payload = parsePayloadStr(message);
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "schedule_manager_recv_system_event", "payload: %s", payload);
    handler->listener(handler, payload);
    if (shouldDestroy)
    {
      scheduleWsDestroy(handler);
    }
    cJSON_free((char *)payload);
  }
}

static void onEventMessageReceived(ScheduleChatHandler *handler,
                                   const char *message)
{
  lingxin_log_debug(" onEventMessageReceived: %s", message);
  cJSON *jsonMessage = cJSON_Parse(message);
  if (!jsonMessage)
  {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr)
    {
      lingxin_log_error(" onEventMessageReceived: Error json: %s", message);
    }
    return;
  }
  // 解析 event
  const char *eventStr = parseEvent(jsonMessage);
  dealEventFromServer(handler, eventStr, jsonMessage);
  cJSON_Delete(jsonMessage);
}

void freeConfig(ScheduleChatConfig *config)
{
  if (config)
  {
    if(config->taskId) {
        lingxin_free(config->taskId);
        config->taskId = NULL;
    }
    lingxin_free(config);
    config = NULL;
    lingxin_log_debug("freeConfig finished");
  }
}

static void freeScheduleWs(ScheduleChatHandler **handlerAddress)
{

  if (!handlerAddress)
  {
    lingxin_log_error("freeScheduleWs handlerAddress null");
    return;
  }

  ScheduleChatHandler *handler = *handlerAddress;
  if (!handler)
  {
    lingxin_log_error("freeScheduleWs handler null");
    return;
  }
  lingxin_log_debug(" freeScheduleWs begin");
  if (handler->state_mutex && handler->state) {
    lingxin_mutex_lock(handler->state_mutex);
    int timer_id = handler->state->sysEventTimerId;
    handler->state->sysEventTimerId = INVALID_TIMER_ID;
    lingxin_mutex_unlock(handler->state_mutex);
    
    if (timer_id != INVALID_TIMER_ID) {
      lingxin_log_debug("Cleaning up timeout timer in freeScheduleWs");
      lingxin_one_shot_timer_delete(timer_id);
    }
  }
  if(handler->config) {
    freeConfig(handler->config);
    handler->config = NULL;
  }
  if (handler->websocket && handler->websocket->config)
  {
    free_websocket_config(handler->websocket->config);
    handler->websocket->config = NULL;
  }
  if (handler->extraInfo)
  {
    if (handler->extraInfo->instanceId)
    {
      lingxin_free(handler->extraInfo->instanceId);
      handler->extraInfo->instanceId = NULL;
    }
    lingxin_free(handler->extraInfo);
    handler->extraInfo = NULL;
  }
  if (handler->state)
  {
    lingxin_free(handler->state);
    handler->state = NULL;
  }
  if (handler->state_mutex)
  {
    lingxin_mutex_destroy(handler->state_mutex);
    handler->state_mutex = NULL;
  }
  lingxin_free(handler);
  *handlerAddress = NULL;

  lingxin_log_debug("freeScheduleWs after");
}

static void sysEventTimeoutCallback(void *userData) {
    ScheduleChatHandler *handler = (ScheduleChatHandler *)userData;
    if (!handler)
    {
        lingxin_log_error("onWebSocketEvent handler null");
        return;
    }
    lingxin_mutex_lock(handler->state_mutex);
    bool wsConnected = handler->state->isWsConnectSuccess;
    bool eventReceived = handler->state->isRecievedSystemEvent;
    bool alreadyDestroying = handler->state->isDestroying;
    bool shouldDestroy = wsConnected && !eventReceived && !alreadyDestroying;
    if (shouldDestroy) {
      handler->state->isDestroying = true;
    }
    handler->state->sysEventTimerId = INVALID_TIMER_ID;
    lingxin_mutex_unlock(handler->state_mutex);

    if(!wsConnected) {
        lingxin_log_warn("ws is not connected when timeout");
        return;
    }
    if(eventReceived) {
        lingxin_log_debug("already received system event when timeout");
        return;
    }
    if(alreadyDestroying) {
        lingxin_log_debug("already destroying when timeout");
        return;
    }
    lingxin_log_warn("Timeout waiting for system_event (10s), destroying websocket");
    scheduleWsDestroy(handler);
}
static void onWebSocketEvent(WebSocketEventType event, const char *data,
                             const size_t len, const int isBinary,
                             void *userData)
{
  ScheduleChatHandler *handler = (ScheduleChatHandler *)userData;
  if (!handler)
  {
    lingxin_log_error("onWebSocketEvent handler null");
    return;
  }
  switch (event)
  {
  case ON_WEBSOCKET_CONNECTION_SUCCESS:
    lingxin_log_debug("ON_WEBSOCKET_CONNECTION_SUCCESS");

    lingxin_mutex_lock(handler->state_mutex);
    handler->state->isWsConnectSuccess = true;
    bool eventReceived = handler->state->isRecievedSystemEvent;
    bool alreadyDestroying = handler->state->isDestroying;
    bool shouldDestroy = eventReceived && !alreadyDestroying;
    if (shouldDestroy) {
      handler->state->isDestroying = true;
    }
    lingxin_mutex_unlock(handler->state_mutex);

    if (shouldDestroy)
    {
      lingxin_log_debug("system_event already received before connection success");
      scheduleWsDestroy(handler);
    }
    else
    {
      // 创建定时器
      int timer_id = lingxin_one_shot_timer_create(handler, sysEventTimeoutCallback, 10 * 1000);
      lingxin_mutex_lock(handler->state_mutex);
      handler->state->sysEventTimerId = timer_id;
      lingxin_mutex_unlock(handler->state_mutex);
      
      if (timer_id == INVALID_TIMER_ID) {
        lingxin_log_error("Failed to create timeout timer");
      } else {
        lingxin_log_debug("Created timeout timer (10s) for system_event, timer_id=%d", timer_id);
      }
    }
    break;
  case ON_WEBSOCKET_DATA_RECEIVED:
    if (isBinary)
    {
      lingxin_log_debug("服务端推送音频数据");
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
      lingxin_log_error(" Failed to allocate memory for error data");
      return;
    }
    // 复制数据
    memcpy(errorData, data, len);
    // 添加字符串结束符
    errorData[len] = '\0';
    lingxin_log_debug("Websocket Error data: %s", errorData);
    lingxin_free(errorData);
  }
  break;
  case ON_WEBSOCKET_DESTROY:
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_ws_destroy_finished");
    lingxin_log_debug("销毁初始化时用于定时任务同步的ws连接");
    freeScheduleWs(&handler);
    break;
  default:
    break;
  }
}

// 新增：内部配置获取函数
static bool fill_schedule_config(ScheduleChatConfig *config)
{
  if (!config)
  {
    lingxin_log_error("config is null");
    return false;
  }
  
  char *appKey = lingxin_auth_license_get();
  if (!appKey || strlen(appKey) == 0)
  {
    lingxin_log_error("license is null, please check lingxin_auth_license_get() implementation");
    return false;
  }
  char *sn = lingxin_auth_sn_get();
  if (!sn || strlen(sn) == 0)
  {
    lingxin_log_error("sn is null, please check lingxin_auth_sn_get() implementation");
    return false;
  }
  char *appId = lingxin_auth_appId_get();
  if (!appId || strlen(appId) == 0)
  {
    lingxin_log_error("appId is null, please check lingxin_auth_appId_get() implementation");
    return false;
  }
  char *agentCode = lingxin_auth_appCode_get();
  if (!agentCode || strlen(agentCode) == 0)
  {
    lingxin_log_error("appCode is null, please check lingxin_auth_appCode_get() implementation");
    return false;
  }

  config->serverPath = WEBSOCKET_CHAT_PATH;
  config->appKey = appKey;
  config->sn = sn;
  config->appId = appId;
  config->showLog = true;
  config->taskId = generateUUID(32);
  
  return true;
}
char *startFirstScheduleConnect(SystemEventListener listener)
{
  ScheduleChatHandler *handler = NULL;
  WebsocketConfig *websocketConfig = NULL;
  ScheduleWsExtraInfo *extraInfo = NULL;
  ScheduleChatConfig *config = NULL;

  lingxin_log_ut(LINGXIN_DEBUG, "schedule_manager_first_ws_begin");

  config = (ScheduleChatConfig *)lingxin_calloc(1, sizeof(ScheduleChatConfig));
  if (!config)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "config allocate fail");
    goto create_fail;
  }
  
  if (!fill_schedule_config(config))
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "config get fail");
    goto create_fail;
  }
  handler =
      (ScheduleChatHandler *)lingxin_calloc(1, sizeof(struct ScheduleChatHandler));
  if (!handler)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "Failed to allocate memory for startFirstScheduleConnect handler!");
    goto create_fail;
  }

  handler->state = (ScheduleWsState *)lingxin_calloc(1, sizeof(ScheduleWsState));
  if (!handler->state)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "Failed to allocate state!");
    goto create_fail;
  }
  handler->listener = listener;
  handler->state->isRecievedSystemEvent = false;
  handler->state->isWsConnectSuccess = false;
  handler->state->isDestroying = false;
  handler->state->sysEventTimerId = INVALID_TIMER_ID;
  handler->config = config;

  handler->state_mutex = lingxin_mutex_create();
  if (!handler->state_mutex)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "Failed to create mutex!");
    goto create_fail;
  }

  websocketConfig =
      createWebsocketConfig(handler, config->sn, config->appKey, config->appId,
                            config->serverPath, onWebSocketEvent);
  if (!websocketConfig)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "Failed to create WebsocketConfig!");
    goto create_fail;
  }
  handler->websocket = hook_websocket_init(websocketConfig);
  if (!handler->websocket)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "Failed to init websocket!");
    goto create_fail;
  }

  handler->extraInfo = NULL;
  extraInfo = (ScheduleWsExtraInfo *)lingxin_calloc(1, sizeof(ScheduleWsExtraInfo));
  if (!extraInfo)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "Failed to allocate extraInfo");
    goto create_fail;
  }
  extraInfo->taskId = (char *)config->taskId;
  extraInfo->requestId = "";
  extraInfo->instanceId = generateUUID(16);
  handler->extraInfo = extraInfo;

  if(hook_websocket_start(handler->websocket) == false) {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_manager_first_ws_failed", "websocket start fail");
    goto create_fail;
  }

  lingxin_log_ut(LINGXIN_DEBUG, "schedule_manager_first_ws_finished");
  return extraInfo->instanceId;

create_fail:
  if(config) 
  {
    freeConfig(config);
  }

  if (websocketConfig) 
  {
    free_websocket_config(websocketConfig);
  }
  
  if (handler)
  {
    if(handler->state) {
        lingxin_free(handler->state);
        handler->state = NULL;
    }
    if(handler->state_mutex) {
      lingxin_mutex_destroy(handler->state_mutex);
      handler->state_mutex = NULL;
    }

    if(handler->extraInfo) {
        if(handler->extraInfo->instanceId) {
          lingxin_free(handler->extraInfo->instanceId);
          handler->extraInfo->instanceId = NULL;
        }
      lingxin_free(handler->extraInfo);
      handler->extraInfo = NULL;
    }

    lingxin_free(handler);
    handler = NULL;
  }
  return NULL;
}

void scheduleWsDestroy(ScheduleChatHandler *handler)
{
  lingxin_log_ut(LINGXIN_DEBUG, "schedule_ws_destroy_begin");

  if (!handler)
  {
    lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_ws_destroy_failed", "handler is null");
    return;
  }
  hook_websocket_close(handler->websocket);
  lingxin_log_debug("scheduleWsDestroy after");
}