#ifndef LINGXIN_COMMON_COMMON_H
#define LINGXIN_COMMON_COMMON_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lingxin_http.h"
#include "lingxin_hook_websocket.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_HTTP "http"
#define PROTOCOL_WEBSOCKET "ws"

#ifdef ENV_DAILY
#define REQUEST_URL "math-daily.edu-aliyun.com"
#define REQUEST_PORT 80
#else
#define REQUEST_URL "eagent.edu-aliyun.com"
#define REQUEST_PORT 80
#endif

#define WEBSOCKET_CHAT_PATH "gw/ws/open/api/v1/unifiedAccess"
#define WEBSOCKET_ASR_PATH "gw/ws/open/api/v1/asr"
#define WEBSOCKET_TTS_PATH "gw/ws/open/api/v1/tts"
#define LLM_TEXT_PATH "smart/api/v1/llm/compatible-mode/chat"
#define LLM_IMAGE_PATH "gw/d/api/v1/text2image/createImage"
#define LLM_IMAGE_RESULT_PATH "gw/d/api/v1/text2image/getImageCreateResult"
#define LOG_UPLOAD_SWITCH_GET_PATH "gw/d/api/v1/terminal/meta"
#define LINGXIN_SERVER_CONFIG_GET_PATH "gw/d/api/v1/terminal/config/get"

    WebsocketConfig *createWebsocketConfig(void *handler, const char *sn, const char *appKey, const char *appId, const char *path, WebSocketEventListener listener);
    void free_websocket_config(WebsocketConfig *config);
    HttpConfig *createHttpConfig(const char *appId, const char *sn, const char *appKey, const char *host, const char *path, const char *reqBody);
    void free_http_config(HttpConfig *config);
    char *generateUUID(int length);
    void parse_host_path_from_url(const char *url, char **host, char **path);
    bool http_post_without_callback(HttpConfig *config, char **response);
    void parse_file_name_from_path(char file_name[64], const char *file_path);

#ifdef __cplusplus
}
#endif
#endif // LINGXIN_COMMON_COMMON_H