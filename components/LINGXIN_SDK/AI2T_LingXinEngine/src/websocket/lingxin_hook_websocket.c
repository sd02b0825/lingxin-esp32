#include <stdlib.h>
#include <string.h>
#include "lingxin_hook_websocket.h"
#include "lingxin_system_time.h"
#include "lingxin_memory.h"

// 临时放到这里，防止影响客户适配
static long last_websocket_active_time = 0;

// 临时放到这里，防止影响客户适配
static bool should_freeze = false;
void freeze_websocket(bool freeze)
{
    should_freeze = freeze;
}
// 临时放到这里，防止影响客户适配
bool get_websocket_freeze_status(void)
{
    return should_freeze;
}
// 临时放到这里，防止影响客户适配
void set_websocket_freeze_status(bool status)
{
    should_freeze = status;
}

static void on_hook_websocket_event(WebSocketEventType event, const char *data, const size_t len, const int is_binary, void *user_context)
{
  WebsocketConfig *raw_config = (WebsocketConfig *)user_context;
  if (!raw_config)
  {
    return;
  }
  last_websocket_active_time = lingxin_get_timestamp_s();
  raw_config->listener(event, data, len, is_binary, raw_config->userContext);
  if (event == ON_WEBSOCKET_DESTROY)
  {
    lingxin_free(raw_config);
    raw_config = NULL;
  }
}

WebsocketClient *hook_websocket_init(WebsocketConfig *raw_config)
{
  WebsocketConfig *hook_config = (WebsocketConfig *)lingxin_calloc(1, sizeof(WebsocketConfig));
  if (!hook_config)
  {
    return NULL;
  }
  memcpy(hook_config, raw_config, sizeof(WebsocketConfig));
  hook_config->listener = on_hook_websocket_event;
  hook_config->userContext = raw_config;
  return initWebsocket(hook_config);
}

bool hook_websocket_start(WebsocketClient *client)
{
  return startWebsocket(client);
}

bool hook_websocket_send_text(WebsocketClient *client, const char *message)
{
  if (websocketSendText(client, message))
  {
    last_websocket_active_time = lingxin_get_timestamp_s();
    return true;
  }
  return false;
}

int hook_websocket_send_binary(WebsocketClient *client, const char *audio, size_t size)
{
  int result = websocketSendBinary(client, audio, size);
  if (result > 0)
  {
    last_websocket_active_time = lingxin_get_timestamp_s();
  }
  return result;
}

void hook_websocket_close(WebsocketClient *client)
{
  closeWebsocket(client);
}

/**
 * 这里判断的是板子上当前所有的websocket实例，是否进入空闲状态
 * 距离上次数据收发（ping、pong除外）超过5秒，则认为websocket处于空闲状态
 */
bool is_all_websocket_idle()
{
  return (lingxin_get_timestamp_s() - last_websocket_active_time) > 20;
}