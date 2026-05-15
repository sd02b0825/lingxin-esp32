#ifndef LINGXIN_UTIL_JSON_UTIL_H
#define LINGXIN_UTIL_JSON_UTIL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cJSON.h"
#include "schedule_timer_manager.h"

  const char *parsePayloadStr(cJSON *json);
  char *parseRequestId(cJSON *json);
  const char *parseEvent(cJSON *json);
  int isUseLLMStreaming(const char *message);
  int isJSON(const char *message);
  int parsePoolSize(const char *message);
  char *parseErrorInfo(cJSON *json);
  int parseScheduleTaskList(const char *jsonStr, ScheduleTaskList *outList);
#ifdef __cplusplus
}
#endif
#endif // LINGXIN_UTIL_JSON_UTIL_H