#include "lingxin_common.h"
#include "lingxin_hook_websocket.h"
#include "lingxin_tls_utils.h"
#include <stdarg.h>
#include "lingxin_log.h"
#include "lingxin_system_time.h"
#include "lingxin_memory.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

char *generateUUID(int length)
{
  const char charset[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  int charset_len = sizeof(charset) - 1;

  char *uuid = (char *)lingxin_malloc(length + 1);
  // 初始化随机数生成器
  srand((unsigned int)lingxin_get_timestamp_s());
  for (int i = 0; i < length; ++i)
  {
    uuid[i] = charset[rand() % charset_len];
  }
  uuid[length] = '\0'; // 确保字符串以 null 结尾
  return uuid;
}

static void long_long_timestamp_to_string(long long n, char *buf)
{
  // 时间戳总是正数，且在合理范围内
  if (n == 0)
  {
    strcpy(buf, "0");
    return;
  }

  int i = 0;
  char temp[16]; // 时间戳最多13位，16字节足够

  // 提取各位数字
  while (n)
  {
    temp[i] = '0' + (n % 10);
    i++;
    n /= 10;
  }
  temp[i] = '\0';
  // 反转字符串
  int len = i;
  for (int j = 0; j < len; j++)
  {
    buf[j] = temp[len - 1 - j];
  }
  buf[len] = '\0';
}

static char *generateTimestampMS()
{
  char *timestamp_str = (char *)lingxin_calloc(1, 14);
  // 这里1000后面的LL不能少，否则可能会类型溢出，还有一种写法：(long long)lingxin_get_timestamp_s() * 1000
  long_long_timestamp_to_string(lingxin_get_timestamp_s() * 1000LL, timestamp_str);
  return timestamp_str;
}

WebsocketConfig *createWebsocketConfig(void *handler, const char *sn, const char *appKey, const char *appId, const char *path, WebSocketEventListener listener)
{
  WebsocketConfig *config = (WebsocketConfig *)lingxin_malloc(sizeof(WebsocketConfig));
  char *timestamp = generateTimestampMS();
  config->header_signature = generateSignature(sn, appKey, appId, timestamp);
  config->header_sn = lingxin_strdup(sn);
  config->header_app_id = lingxin_strdup(appId);
  config->header_timestamp = timestamp;
  config->protocol = PROTOCOL_WEBSOCKET;
  config->host = REQUEST_URL;
  config->path = path;
  config->port = REQUEST_PORT;
  config->listener = listener;
  config->userContext = handler;
  return config;
}

void free_websocket_config(WebsocketConfig *config)
{
  if (!config)
  {
    return;
  }
  if (config->header_signature)
  {
    lingxin_free(config->header_signature);
    config->header_signature = NULL;
  }
  if (config->header_sn)
  {
    lingxin_free(config->header_sn);
    config->header_sn = NULL;
  }
  if (config->header_app_id)
  {
    lingxin_free(config->header_app_id);
    config->header_app_id = NULL;
  }
  if (config->header_timestamp)
  {
    lingxin_free(config->header_timestamp);
    config->header_timestamp = NULL;
  }
  lingxin_free(config);
  config = NULL;
}

HttpConfig *createHttpConfig(const char *appId, const char *sn, const char *appKey, const char *host, const char *path, const char *reqBody)
{
  HttpConfig *config = (HttpConfig *)lingxin_calloc(1, sizeof(HttpConfig));
  if (!config)
  {
    lingxin_log_error("Failed to allocate memory for HttpConfig");
    return NULL;
  }
  config->headers = (HttpHeader *)lingxin_malloc(sizeof(HttpHeader));
  if (!config->headers)
  {
    lingxin_log_error("Failed to allocate memory for HttpHeader");
    lingxin_free(config);
    return NULL;
  }
  char *timestamp = generateTimestampMS();
  config->headers->signature = generateSignature(sn, appKey, appId, timestamp);
  config->headers->timestamp = timestamp;
  config->headers->app_id = lingxin_strdup(appId);
  config->headers->sn = lingxin_strdup(sn);
  config->host = host;
  config->path = path;
  config->port = REQUEST_PORT;
  config->protocol = PROTOCOL_HTTP;
  config->post_data = lingxin_strdup((reqBody && strlen(reqBody)) != 0 ? reqBody : "{}");
  return config;
}

void free_http_config(HttpConfig *config)
{
  if (!config)
  {
    return;
  }
  if (config->headers)
  {
    if (config->headers->signature)
    {
      lingxin_free(config->headers->signature);
      config->headers->signature = NULL;
    }
    if (config->headers->app_id)
    {
      lingxin_free(config->headers->app_id);
      config->headers->app_id = NULL;
    }
    if (config->headers->sn)
    {
      lingxin_free(config->headers->sn);
      config->headers->sn = NULL;
    }
    if(config->headers->timestamp) {
      lingxin_free(config->headers->timestamp);
      config->headers->timestamp = NULL;
    }
    lingxin_free(config->headers);
    config->headers = NULL;
  }
  if (config->post_data)
  {
    lingxin_free(config->post_data);
    config->post_data = NULL;
  }
  lingxin_free(config);
  config = NULL;
}

void parse_host_path_from_url(const char *url, char **host, char **path)
{
  if (!url || !host || !path)
  {
    return;
  }

  *host = NULL;
  *path = NULL;

  const char *url_ptr = url;

  // 跳过schema部分（如果存在）
  const char *schema_end = strstr(url_ptr, "://");
  if (schema_end)
  {
    url_ptr = schema_end + 3;
  }

  // 查找路径开始位置
  const char *path_start = strchr(url_ptr, '/');

  if (path_start)
  {
    // 提取host部分
    int host_len = path_start - url_ptr;
    *host = lingxin_malloc(host_len + 1);
    if (*host)
    {
      strncpy(*host, url_ptr, host_len);
      (*host)[host_len] = '\0';
    }

    // 提取path部分
    *path = lingxin_strdup(path_start);
  }
  else
  {
    // 只有host，没有路径
    *host = lingxin_strdup(url_ptr);
    *path = lingxin_strdup("/");
  }
}

static void callback_http_post(void *contents, size_t size, void *userp)
{
  char **response = (char **)userp;
  // 如果response已经分配过内存，先释放
  if (*response)
  {
    lingxin_free(*response);
    *response = NULL;
  }
  *response = (char *)lingxin_malloc(size + 1);
  if (!*response)
  {
    lingxin_log_error("respose malloc failed");
    return;
  }

  // 拷贝新数据到响应缓冲区
  memcpy(*response, contents, size);
  (*response)[size] = '\0'; // 添加字符串结束符
}

bool http_post_without_callback(HttpConfig *config, char **response)
{
  if (!config)
  {
    lingxin_log_error("config null");
    return false;
  }
  return http_post(config, callback_http_post, response) != 0;
}

void parse_file_name_from_path(char file_name[64], const char *file_path)
{
  if (!file_name || !file_path)
  {
    return;
  }
  // 提取文件名部分
  const char *temp = strrchr(file_path, '/');
  if (!temp)
    temp = strrchr(file_path, '\\');
  temp = temp ? temp + 1 : file_path;

  // 去掉扩展名
  const char *dot = strrchr(temp, '.');
  size_t len = dot ? (size_t)(dot - temp) : strlen(temp);
  if (len >= 64)
  {
    len = 63;
  }

  strncpy(file_name, temp, len);
  file_name[len] = '\0';
}
