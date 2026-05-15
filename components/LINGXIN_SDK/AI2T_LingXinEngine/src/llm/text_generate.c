#include "lingxin_common.h"
#include "lingxin_json_util.h"
#include "lingxin_http.h"
#include "llm_generate.h"
#include "lingxin_log.h"
#include "lingxin_memory.h"

struct LLMCallerStruct
{
  int useStreaming;
  GenerateTextRequestCallback userCallback;
};

static char *getValidMessage(const char *data, size_t total_size)
{
  // 分配足够的内存来存储提取的消息和终止符
  char *message = (char *)lingxin_malloc((total_size + 1) * sizeof(char));
  if (message == NULL)
  {
    lingxin_log_error("Memory allocation failed\n");
    return NULL;
  }
  // 复制最多total_size个字符到message中
  strncpy(message, data, total_size);
  // 确保字符串以空字符终止
  message[total_size] = '\0';

  return message;
}

static void callbackFromLLMRequest(void *contents, size_t size, void *userp)
{
  size_t total_size = size;
  // lingxin_log_debug("-------------------------custom_write_callback
  // --------------------------\n"); lingxin_log_debug("%.*s\n", (int)total_size,
  // contents);
  struct LLMCallerStruct *callerData = (struct LLMCallerStruct *)userp;
  if (callerData->userCallback == NULL)
  {
    lingxin_log_debug("use callback null\n");
    return;
  }
  char *message = getValidMessage(contents, total_size);
  // lingxin_free(contents);
  // 非流式输出
  if (callerData->useStreaming == 0)
  {
    // lingxin_log_debug("----------------------not streaming
    // data--------------------------\n"); printWithEscapedNewlines(message);
    // lingxin_log_debug("\n");
    callerData->userCallback(message, 1);
    lingxin_free(message);
    return;
  }
  // lingxin_log_debug("----------------------streaming
  // data--------------------------\n"); 流式输出
  // printWithEscapedNewlines(message);
  // lingxin_log_debug("\n");
  // if (strcmp(message, "data:") == 0 || strcmp(message, "\n\n") == 0)
  // {
  //     // lingxin_log_debug("----------------------invali streaming
  //     data--------------------------\n"); lingxin_free(message); return;
  // }
  if (strcmp(message, "[DONE]") == 0)
  {
    // lingxin_log_debug("----------------------streaming data
    // finish--------------------------\n");
    callerData->userCallback("", 1);
    lingxin_free(message);
    return;
  }
  if (isJSON(message))
  {
    // lingxin_log_debug("----------------------streaming data
    // result--------------------------\n");
    callerData->userCallback(message, 0);
  }
  lingxin_free(message);
}

void generateText(const char *appId, const char *sn, const char *appKey, const char *input, GenerateTextRequestCallback callback)
{
  if (!appId || !sn || !appKey || !input)
  {
    lingxin_log_error("generateText: Invalid input parameters");
    return;
  }

  const int useStreaming = isUseLLMStreaming(input);
  struct LLMCallerStruct callerData = {.useStreaming = useStreaming,
                                       .userCallback = callback};
  HttpConfig *config = createHttpConfig(appId, sn, appKey, REQUEST_URL, LLM_TEXT_PATH, input);
  http_post(config, callbackFromLLMRequest, &callerData);
  free_http_config(config);
}
