#include "lingxin_http.h"
#include "lingxin_device_info.h"
#include "lingxin_log.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lingxin_memory.h"
typedef struct {
    char *response_buffer;
    int buffer_size;
    int data_length;
    RequestCallback user_callback;
    void *user_data;
    bool response_complete;
} HttpClientContext;

static const char *TAG = "lingxin_adapter_http";

static void print_http_config(HttpConfig *config, const char *url)
{
    if (!config)
    {
        lingxin_log_error("Config is NULL");
        return;
    }

    ESP_LOGI(TAG, "=== HTTP Configuration Details ===");
    ESP_LOGI(TAG, "Protocol: %s", config->protocol ? config->protocol : "NULL");
    ESP_LOGI(TAG, "Host: %s", config->host ? config->host : "NULL");
    ESP_LOGI(TAG, "Port: %d", config->port);
    ESP_LOGI(TAG, "Path: %s", config->path ? config->path : "NULL");
    ESP_LOGI(TAG, "Full URL: %s", url ? url : "NULL");

    if (config->post_data)
    {
        ESP_LOGI(TAG, "POST Data Length: %d", (int)strlen(config->post_data));
    }

    if (config->headers)
    {
        HttpHeader *hdr = config->headers;
        ESP_LOGI(TAG, "=== HTTP Headers ===");
        ESP_LOGI(TAG, "app_id: %s", hdr->app_id ? hdr->app_id : "Not set");
        ESP_LOGI(TAG, "sn: %s", hdr->sn ? hdr->sn : "Not set");
        ESP_LOGI(TAG, "signature: %s", hdr->signature ? hdr->signature : "Not set");
        ESP_LOGI(TAG, "timestamp: %s", hdr->timestamp ? hdr->timestamp : "Not set");
    }
}

/**
 * @brief 内部回调函数，用于接收 HTTP 响应数据
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    HttpClientContext *ctx = (HttpClientContext *)evt->user_data;
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        // 数据到达，透传给用户回调
        if (evt->user_data != NULL && evt->data_len > 0)
        {
            // 确保缓冲区足够大
            if (ctx->data_length + evt->data_len + 1 > ctx->buffer_size)
            {
                ctx->buffer_size = ctx->data_length + evt->data_len + 1024; // 多分配一些空间
                ctx->response_buffer = lingxin_realloc(ctx->response_buffer, ctx->buffer_size);
                if (!ctx->response_buffer)
                {
                    lingxin_log_error("Failed to realloc buffer");
                    return ESP_FAIL;
                }
            }

            // 将数据复制到缓冲区
            memcpy(ctx->response_buffer + ctx->data_length, evt->data, evt->data_len);
            ctx->data_length += evt->data_len;
            ctx->response_buffer[ctx->data_length] = '\0'; // 确保字符串结尾
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        lingxin_log_debug("HTTP request finished");
        ctx->response_complete = true;
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * @brief http post 请求
 * @param config 请求参数
 * @param userCallback 结果回调方法
 * @param userData 需要在回调中透传的 context
 * @return 请求发送结果，0：失败；其他数字：成功
 */
int http_post(HttpConfig *config, RequestCallback userCallback, void *userData)
{
    if (!config || !config->host || !config->path || !config->protocol)
    {
        lingxin_log_error("Invalid HttpConfig");
        return 0;
    }
    // 创建上下文
    HttpClientContext ctx = {
        .response_buffer = NULL,
        .buffer_size = 2048,
        .data_length = 0,
        .user_callback = userCallback,
        .user_data = userData,
        .response_complete = false
    };

    ctx.response_buffer = lingxin_malloc(ctx.buffer_size);
    if (!ctx.response_buffer) {
        lingxin_log_error("Failed to allocate buffer");
        return 0;
    }

    esp_http_client_config_t client_config = {
        .url = NULL,
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
        .user_data = &ctx, // 将 config 作为 user_data 传递，在 event_handler 中可访问 headers 等字段
        .timeout_ms = 6000,  // 超时时间设为6秒
    };

    // 构造完整 URL
    char url[256];
    int port = config->port;
    if (port <= 0)
    {
        port = (strcmp(config->protocol, "https") == 0) ? 443 : 80;
    }
    snprintf(url, sizeof(url), "%s://%s:%d/%s", config->protocol, config->host, port, config->path);
    lingxin_log_debug("URL: %s", url);
    client_config.url = url;

    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (!client)
    {
        lingxin_log_error("Failed to initialize HTTP client");
        lingxin_free(ctx.response_buffer);
        ctx.response_buffer = NULL;
        return 0;
    }
    // 设置标准HTTP头部
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_header(client, "Cache-Control", "no-store");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "*/*");

    // 设置自定义头部
    if (config->headers)
    {
        HttpHeader *hdr = config->headers;
        lingxin_log_debug("Custom headers: signature=%s, sn=%s, app_id=%s, timestamp=%s，sdk_name=%s, sdk_version=%s",
            hdr->signature ? hdr->signature : "NULL",
            hdr->sn ? hdr->sn : "NULL",
            hdr->app_id ? hdr->app_id : "NULL",
            hdr->timestamp ? hdr->timestamp : "NULL",
            get_lingxin_device_name() ? get_lingxin_device_name() : "NULL",
            get_lingxin_device_version() ? get_lingxin_device_version() : "NULL");
        if (hdr->signature)
            esp_http_client_set_header(client, "signature", hdr->signature);
        if (hdr->sn)
            esp_http_client_set_header(client, "sn", hdr->sn);
        if (hdr->app_id)
            esp_http_client_set_header(client, "app_id", hdr->app_id);
        if (hdr->timestamp)
            esp_http_client_set_header(client, "timestamp", hdr->timestamp);
        if (get_lingxin_device_name())
            esp_http_client_set_header(client, "sdk_name", get_lingxin_device_name());
        if (get_lingxin_device_version())
            esp_http_client_set_header(client, "sdk_version", get_lingxin_device_version());
    }

    // 设置 POST 数据
    if (config->post_data)
    {
        lingxin_log_debug("POST data: %s", config->post_data);
        int content_len = strlen(config->post_data);
        char content_length_str[16];
        snprintf(content_length_str, sizeof(content_length_str), "%d", content_len);
        // 设置 Content-Length 头部
        esp_http_client_set_header(client, "Content-Length", content_length_str);

        esp_http_client_set_post_field(client, config->post_data, strlen(config->post_data));
    }

    // 执行请求
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code >= 200 && status_code < 300)
        {
            // 调用用户回调
            if (ctx.data_length > 0 && userCallback) {
                userCallback(ctx.response_buffer, ctx.data_length, userData);
            }   
            esp_http_client_cleanup(client);
            lingxin_free(ctx.response_buffer);
            ctx.response_buffer = NULL;
            return 1; // 成功
        }
        else
        {
            lingxin_log_error("HTTP Post failed with status code: %d, response: %s", status_code,
                ctx.data_length > 0 ? ctx.response_buffer : "(empty)");
        }
    }
    else
    {
        lingxin_log_error("HTTP Post request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    lingxin_free(ctx.response_buffer);
    ctx.response_buffer = NULL;
    return 0; // 失败
}
