#ifndef LINGXIN_WEBSOCKET_H
#define LINGXIN_WEBSOCKET_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

  typedef enum
  {
    ON_WEBSOCKET_CONNECTION_SUCCESS, // 建联成功
    ON_WEBSOCKET_CONNECTION_FAIL, // 建联失败
    ON_WEBSOCKET_DATA_RECEIVED, // 收到数据
    ON_WEBSOCKET_DESTROY,  // 销毁完成
    ON_WEBSOCKET_ERROR  // 收到错误
  } WebSocketEventType;

  typedef void (*WebSocketEventListener)(WebSocketEventType event, const char *data, size_t data_len, const int isBinary, void *userContext);

  typedef struct
  {
    const char *protocol; // ws or wss
    const char *host;
    const char *path;
    const char *header_signature;
    const char *header_sn;
    const char *header_app_id;
    const char *header_timestamp;
    int port;
    void *userContext;
    WebSocketEventListener listener;
  } WebsocketConfig;

  typedef struct
  {
    void *clientHandler;
    WebsocketConfig *config;
  } WebsocketClient;

  WebsocketClient *initWebsocket(WebsocketConfig *config);

  bool startWebsocket(WebsocketClient *client);

  bool websocketSendText(WebsocketClient *client, const char *message);

  int websocketSendBinary(WebsocketClient *client, const char *audioData, size_t dataSize);

  void closeWebsocket(WebsocketClient *client);

#ifdef __cplusplus
}
#endif

#endif // LINGXIN_WEBSOCKET_H