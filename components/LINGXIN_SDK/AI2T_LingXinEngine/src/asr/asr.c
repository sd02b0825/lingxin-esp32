#include "asr.h"
#include "cJSON.h"
#include "lingxin_common.h"
#include "lingxin_json_util.h"
#include "lingxin_hook_websocket.h"
#include <stdarg.h>
#include "lingxin_log.h"
#include "lingxin_memory.h"

struct ASRHandler
{
  WebsocketClient *websocket;
  ASREventListener listener;
  ASRExtraInfo *extraInfo;
  struct ASRHandler **selfPointer; // 存储双指针的引用
};

static char *getASRLogPre(ASRHandler *handler)
{
  char *logInstanceId = NULL;
  if (handler && handler->selfPointer && handler->extraInfo)
  {
    logInstanceId = handler->extraInfo->instanceId;
  }
  return (!logInstanceId || strlen(logInstanceId) == 0) ? "" : logInstanceId;
}

static void triggerCallback(ASRHandler *handler, ASREventType eventType,
                            const char *data, const size_t len)
{
  if (!handler)
  {
     lingxin_log_error("triggerCallback handler null");
    return;
  }
  if (!handler->listener)
  {
    lingxin_log_error( "[%s], triggerCallback listener null", getASRLogPre(handler));
    return;
  }
  handler->listener(eventType, data, len, handler->extraInfo);
}

static void dealEventFromServer(ASRHandler *handler, const char *event,
                                cJSON *message)
{
  if (!event)
  {
   lingxin_log_error("[%s]", "event is null", getASRLogPre(handler));
    return;
  }

  if (strcmp(event, "task_started") == 0)
  {
    if (handler->extraInfo)
    {
      char *reqId = parseRequestId(message);
      if (!handler->extraInfo->requestId || strlen(handler->extraInfo->requestId) == 0)
      {
        handler->extraInfo->requestId = lingxin_strdup(reqId);
      }
      else if (strlen(reqId) == strlen(handler->extraInfo->requestId))
      {
        memcpy(handler->extraInfo->requestId, reqId, strlen(reqId));
      }
      else
      {
        char *old_requestId = handler->extraInfo->requestId;
        handler->extraInfo->requestId = lingxin_strdup(reqId);
        lingxin_free(old_requestId);
      }
    }
    triggerCallback(handler, ASR_EVENT_ON_SEND_START, NULL, 0);
  }
  else if (strcmp(event, "text_result_generated") == 0)
  {
    //这里用到了cJSON_PrintUnformatted，注意要释放
    const char *textResult = parsePayloadStr(message);
    if (!textResult)
    {
     lingxin_log_error("[%s], Failed to parse text result", getASRLogPre(handler));
      return;
    }
    const int length = strlen(textResult);
    lingxin_log_debug("[%s], dealEventFromServer: %d", getASRLogPre(handler), length);

    triggerCallback(handler, ASR_EVENT_ON_SEND_RESULT, textResult, length);
    // 释放内存
    cJSON_free((char*)textResult);
    
  }
  else if (strcmp(event, "task_ended") == 0)
  {
    triggerCallback(handler, ASR_EVENT_ON_SEND_END, NULL, 0);
  }
  else if (strcmp(event, "error") == 0)
  {
    //这里用到了cJSON_PrintUnformatted，注意要释放
    char *errorInfo = parseErrorInfo(message);
    triggerCallback(handler, ASR_EVENT_ON_ERROR, errorInfo, strlen(errorInfo));
    cJSON_free(errorInfo);
  }
}

static void onMessageReceived(ASRHandler *handler, const char *message)
{
  lingxin_log_debug("[%s], asr onMessageReceived, eventStr = %s", getASRLogPre(handler), message);

  cJSON *jsonMessage = cJSON_Parse(message);
  if (!jsonMessage)
  {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr)
    {
     lingxin_log_error("[%s], Error json: %s", getASRLogPre(handler), message);
    }
    return;
  }
  // 解析 event
  const char *eventStr = parseEvent(jsonMessage);
  dealEventFromServer(handler, eventStr, jsonMessage);
  // Free the JSON object
  cJSON_Delete(jsonMessage);
}

static void freeASR(ASRHandler **handlerAddress)
{
  if (!handlerAddress)
  {
   lingxin_log_error("freeASR handlerAddress null");
    return;
  }
  ASRHandler *handler = *handlerAddress;
  if (!handler)
  {
   lingxin_log_error("freeASR handler null");
    return;
  }
  lingxin_log_debug("[%s], freeASR begin", getASRLogPre(handler));

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
    }
    if(handler->extraInfo->requestId) {
      lingxin_free(handler->extraInfo->requestId);
    }
    lingxin_free(handler->extraInfo);
    handler->extraInfo = NULL;
  }
  lingxin_free(handler);
  *handlerAddress = NULL;
  lingxin_log_debug("freeASR finish");
}

static void onWebSocketEvent(WebSocketEventType event, const char *data,
                             const size_t len, const int isBinary,
                             void *userData)
{
  ASRHandler *handler = (ASRHandler *)userData;
  if (!handler)
  {
   lingxin_log_error("onWebSocketEvent handler null");
    return;
  }
  switch (event)
  {
  case ON_WEBSOCKET_CONNECTION_SUCCESS:
    triggerCallback(handler, ASR_EVENT_ON_READY, NULL, 0);
    break;
  case ON_WEBSOCKET_DATA_RECEIVED:
    onMessageReceived(handler, data);
    break;
  case ON_WEBSOCKET_ERROR:
    lingxin_log_error("[%s], triggerCallback ON_WEBSOCKET_ERROR", getASRLogPre(handler));
    triggerCallback(handler, ASR_EVENT_ON_ERROR, data, len);
    break;
  case ON_WEBSOCKET_DESTROY:
    lingxin_log_debug("[%s], triggerCallback ON_WEBSOCKET_DESTROY", getASRLogPre(handler));
    triggerCallback(handler, ASR_EVENT_ON_DESTROY, data, len);
    freeASR(handler->selfPointer);
    break;
  default:
    break;
  }
}

char *asrCreate(ASRHandler **handlerAddress, ASRConfig *config, ASREventListener listener)
{
  lingxin_log_debug("asrCreate  begin");

  ASRHandler *handler = (ASRHandler *)lingxin_calloc(1, sizeof(ASRHandler));

  if (!handler)
  {
    lingxin_log_error("Failed to allocate memory for ASR handler");
    return NULL;
  }
  WebsocketConfig *websocketConfig =
      createWebsocketConfig(handler, config->sn, config->appKey, config->appId,
                            WEBSOCKET_ASR_PATH, onWebSocketEvent);
  if (!websocketConfig)
  {
    lingxin_log_error("Failed to create WebsocketConfig");
    lingxin_free(handler);
    return NULL;
  }
  WebsocketClient *client = hook_websocket_init(websocketConfig);

  if (!client)
  {
    lingxin_log_error("Failed to initialize WebSocket client");
    free_websocket_config(websocketConfig);
    lingxin_free(handler);
    return NULL;
  }
  handler->websocket = client;
  handler->listener = listener;
  handler->extraInfo = NULL;

  ASRExtraInfo *extraInfo = (ASRExtraInfo *)lingxin_calloc(1, sizeof(ASRExtraInfo));

  if (extraInfo)
  {
    extraInfo->taskId = NULL;
    extraInfo->requestId = NULL;
    extraInfo->instanceId = generateUUID(16);
    handler->extraInfo = extraInfo;
  }
  hook_websocket_start(handler->websocket);

  handler->selfPointer = handlerAddress; // 设置 selfPointer
  *handlerAddress = handler;             // 返回 handler

  lingxin_log_debug("[%s], asrCreate finish", getASRLogPre(handler));
  return extraInfo ? extraInfo->instanceId : "";
}

bool asrSendStart(ASRHandler *handler, const char *taskId, const char *payload)
{
  lingxin_log_debug("asrSendStart begin");

  if (!handler)
  {
   lingxin_log_error("handler null");
    return false;
  }
  if (!taskId || !payload)
  {
    lingxin_log_error("[%s], taskId or payload null", getASRLogPre(handler));
    return false;
  }

  if (handler->extraInfo)
  {
    handler->extraInfo->taskId = (char *)taskId;
  }

  int length = snprintf(NULL, 0, "{\"header\":{\"action\":\"start_task\",\"task_id\":\"%s\"},\"payload\":%s}", taskId, payload);
  if(length <= 0) {
    return false;
  }
  char *message = lingxin_malloc(length + 1);
  if (!message)
  {
    lingxin_log_error("[%s],Failed to allocate memory for message", getASRLogPre(handler));
    return false;
  }
  snprintf(message, length+1,"{\"header\":{\"action\":\"start_task\",\"task_id\":\"%s\"},\"payload\":%s}", taskId, payload);
  bool result = hook_websocket_send_text(handler->websocket, message);
  lingxin_free(message);
  lingxin_log_debug("[%s],asrSendStart result: %s", getASRLogPre(handler), result ? "true" : "false");
  return result;
}

int asrSend(ASRHandler *handler, const char *audioData, size_t dataSize)
{
  lingxin_log_debug("[%s], asrSend begin", getASRLogPre(handler));

  if (!handler)
  {
    lingxin_log_error("handler null");
    return 0;
  }
  if (!audioData || !dataSize)
  {
    lingxin_log_error("[%s], audioData or dataSize null", getASRLogPre(handler));
    return 0;
  }
  int result = hook_websocket_send_binary(handler->websocket, audioData, dataSize);
  lingxin_log_debug("[%s], asrSend result: %d", getASRLogPre(handler), result);
  return result;
}

void asrDestroy(ASRHandler *handler)
{
  lingxin_log_debug("[%s], asrDestroy begin", getASRLogPre(handler));

  if (!handler || !handler->selfPointer || !*handler->selfPointer)
  {
    lingxin_log_error("handler or handler->selfPointer null");
    return;
  }
  hook_websocket_close(handler->websocket);
  lingxin_log_debug("asrDestroy after");
}

bool asrSendStop(ASRHandler *handler, const char *taskId)
{
  lingxin_log_debug("[%s], asrSendStop begin", getASRLogPre(handler));

  if (!handler)
  {
    lingxin_log_error("handler null");
    return false;
  }
  if (!taskId)
  {
    lingxin_log_error("[%s], taskId null", getASRLogPre(handler));
    return false;
  }
  if (handler->extraInfo)
  {
    handler->extraInfo->taskId = (char *)taskId;
  }
  char message[256];
  snprintf(message, sizeof(message), "{\"header\":{\"action\":\"end_task\",\"task_id\":\"%s\",\"request_id\":\"%s\"},\"payload\":{}}", taskId, (handler->extraInfo && handler->extraInfo->requestId) ? handler->extraInfo->requestId : "");
  bool result = hook_websocket_send_text(handler->websocket, message);
  lingxin_log_debug("[%s], asrSendStop result: %s", getASRLogPre(handler), result ? "true" : "false");
  return result;
}