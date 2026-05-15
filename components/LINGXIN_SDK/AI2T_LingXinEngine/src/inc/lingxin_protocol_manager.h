#ifndef LINGXIN_PROTOCOL_MANAGER_H
#define LINGXIN_PROTOCOL_MANAGER_H

#include <stdbool.h>
#include "chat_api.h"

typedef enum
{
  CHAT_EVENT_ON_AI_READY,
  CHAT_EVENT_ON_ERROR,
  CHAT_EVENT_ON_VAD_END,
  CHAT_EVENT_ON_VAD_EXIT,
  CHAT_EVENT_ON_STREAM_DATA,
  CHAT_EVENT_ON_SYSTEM_EVENT,
  CHAT_EVENT_ON_REQUEST_DATA_END,
} ChatEventType;

typedef void (*ChatEventListener)(ChatEventType event, const char *data, const size_t len);

bool add_protocol_event_listener(ChatEventListener listener);
bool remove_protocol_event_listener(ChatEventListener listener);

typedef struct VoiceChatHandler VoiceChatHandler;

typedef struct
{
  char *requestId; // 请求ID
  char *instanceId;
} VoiceChatContextInfo;

typedef struct
{
  const char *serverPath;
  const char *payload;
  const char *taskId;
  const char *appKey;
  const char *appId;
  const char *sn;
} VoiceChatConfig;

typedef struct
{
  bool server_vad;
  bool is_schedule_timer_task;
  char *taskId;
  char *task;
  char *input_mode;
  char *output_mode;
  char *user_input;
  char *scheduleTaskId;
  bool play_prologue;
} ChatStartNewParams;

typedef void (*ChatStartNewCallback)();
typedef void (*TerminateCheckCallback)();
typedef void (*ErrorCallback)(char *data);

void setWaitTerminateOrEndSuccess(bool target);

bool voice_chat_start_new(ChatStartNewParams *params);

void voiceChatSendAudio(void *buf, int rlen);
void voiceChatStopSendAudio();
bool isVoiceChatResponding();
bool isVoiceChatInited();

/**
 * 打断对话
 * @return 0 打断指令没有发出去 1 打断指令已经正常发送出去 2 打断指令或者end_task指令之前已经发送出去过，需要等待响应回来
 */
int module_voiceChat_terminate();

// 被退出对话流程
bool module_voiceChat_exit();

typedef struct
{
  char *unique_id;
  char *frame_type;
} Multimodal_Chat_Confirm_Data;
// 多模态发送流
bool voiceChat_send_request_data_stream(char *unique_id, int index, const char *content, int content_len, char *content_type, char *frame_type, bool is_last);
// 多模态发送文字
bool voiceChat_send_request_data_text(char *content);
// 多模态结束任务
bool voiceChat_send_end_up_task(size_t confirm_data_count, Multimodal_Chat_Confirm_Data confirm_data[]);

#endif // LINGXIN_PROTOCOL_MANAGER_H