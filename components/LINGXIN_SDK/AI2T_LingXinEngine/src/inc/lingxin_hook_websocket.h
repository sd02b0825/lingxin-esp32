#ifndef LINGXIN_HOOK_WEBSOCKET_H
#define LINGXIN_HOOK_WEBSOCKET_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include "lingxin_websocket.h"

  WebsocketClient *hook_websocket_init(WebsocketConfig *config);

  bool hook_websocket_start(WebsocketClient *client);

  bool hook_websocket_send_text(WebsocketClient *client, const char *message);

  int hook_websocket_send_binary(WebsocketClient *client, const char *audio, size_t size);

  void hook_websocket_close(WebsocketClient *client);

  bool is_all_websocket_idle();
#ifdef __cplusplus
}
#endif

#endif // LINGXIN_HOOK_WEBSOCKET_H