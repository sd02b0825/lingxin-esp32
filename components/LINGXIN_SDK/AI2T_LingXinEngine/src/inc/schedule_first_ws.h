#ifndef AI_IOT_SDK_SCHEDULE_CHAT_H
#define AI_IOT_SDK_SCHEDULE_CHAT_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

    typedef struct ScheduleChatHandler ScheduleChatHandler;

    typedef struct
    {
        char *taskId;
        char *requestId; // 请求ID
        char *instanceId;
    } ScheduleWsExtraInfo;

    typedef struct
    {
        bool isRecievedSystemEvent;  // 是否收到 system_event
        bool isWsConnectSuccess;      // WebSocket 是否连接成功
        bool isDestroying;            // 是否正在销毁（防止重复销毁）
        int sysEventTimerId;          // 定时器 ID
    } ScheduleWsState;

    typedef void (*SystemEventListener)(ScheduleChatHandler *globalHandler, const char *data);

    typedef struct
    {
        bool showLog;
        const char *serverPath;
        const char *payload;
        const char *taskId;
        const char *appKey;
        const char *appId;
        const char *sn;
    } ScheduleChatConfig;

    char *startFirstScheduleConnect(SystemEventListener listener);

    void scheduleWsDestroy(ScheduleChatHandler *handler);

#ifdef __cplusplus
}
#endif
#endif // AI_IOT_SDK_SCHEDULE_CHAT_H