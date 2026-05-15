#include "lingxin_log.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "lingxin_websocket.h"
#include "lingxin_device_info.h"
/* lingxin_func_wrap/func.h and tools/tools.h are not available in v2.6.6 build;
   provide inline replacements here */
#define lingxin_xTaskCreate xTaskCreate
static inline void diagnose_task_create_failure(int stack_size) {
    lingxin_log_error("Failed to create task with stack_size=%d, available heap=%u", stack_size, (unsigned)esp_get_free_heap_size());
}
#include "esp_wifi.h"
#include "esp_system.h"
#include "lingxin_memory.h"
#include "lingxin_system_time.h"

#define NO_DATA_TIMEOUT_SEC 5

static const char *TAG = "lingxin_adapter_websocket";
#define WEBSOCKET_STACK_SIZE 8 * 1024
#define WEBSOCKET_BUFFER_SIZE 8 * 1024

// 添加操作队列用于异步处理WebSocket操作
static QueueHandle_t websocket_op_queue = NULL;
static int websocket_queue_size = 10;
static TaskHandle_t websocket_manager_task_handle = NULL;

typedef enum
{
    WEBSOCKET_OP_CLOSE,
    WEBSOCKET_OP_DESTROY
} websocket_op_type_t;

typedef struct
{
    websocket_op_type_t type;
    WebsocketClient *client;
} websocket_op_t;

typedef struct
{
    esp_websocket_client_config_t websocket_cfg;
    esp_websocket_client_handle_t esp_client;
    bool is_destroyed;
    volatile bool is_enter_cleanup;
    SemaphoreHandle_t cleanup_mutex;
    // WebSocket消息重组相关字段
    char *reassembly_buffer;    // 重组缓冲区
    size_t reassembly_size;     // 已接收大小
    size_t reassembly_capacity; // 缓冲区容量
    bool is_reassembling;       // 是否正在重组
} WebsocketClientHandler;
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

static WebsocketClientHandler *get_client_handler(WebsocketClient *client)
{
    if (!client || !client->clientHandler)
    {
        lingxin_log_error("Invalid client or handler");
        return NULL;
    }
    return (WebsocketClientHandler *)client->clientHandler;
}
static void full_clean_websocket(WebsocketClient *client)
{
    lingxin_log_debug("Start full_clean_websocket...");
    if (!client)
    {
        return;
    }

    WebsocketClientHandler *handler = get_client_handler(client);
    if (handler)
    {
        // 重置is_destroyed状态
        handler->is_destroyed = true;

        // 清理重组缓冲区
        if (handler->is_reassembling && handler->reassembly_buffer != NULL)
        {
            lingxin_log_debug("Cleaning up incomplete reassembly buffer");
            lingxin_free(handler->reassembly_buffer);
            handler->reassembly_buffer = NULL;
            handler->is_reassembling = false;
        }
        // 清理互斥锁
        if (handler->cleanup_mutex)
        {
            vSemaphoreDelete(handler->cleanup_mutex);
            handler->cleanup_mutex = NULL;
        }

        // 释放存储的 headers（如果有的话）
        if (handler->websocket_cfg.headers)
        {
            lingxin_free((char *)handler->websocket_cfg.headers);
            handler->websocket_cfg.headers = NULL;
        }

        if (handler->websocket_cfg.task_name)
        {
            lingxin_free((char *)handler->websocket_cfg.task_name);
            handler->websocket_cfg.task_name = NULL;
        }

        if (handler->esp_client)
        {
            // 注销事件处理程序
            esp_websocket_unregister_events(handler->esp_client, WEBSOCKET_EVENT_ANY, websocket_event_handler);
            // 销毁客户端
            esp_websocket_client_destroy(handler->esp_client);
            // handler->esp_client = NULL;
        }
        lingxin_free(handler);
        handler = NULL;
    }

    lingxin_free(client);
    client = NULL;
    lingxin_log_debug("Finish full_clean_websocket...");
}

static void websocket_cleanup(WebsocketClient *client)
{
    lingxin_log_debug("Start websocket_cleanup...");
    if (!client || !client->config)
    {
        lingxin_log_error("Event with invalid client context");
        return;
    }
    WebsocketClientHandler *handler = get_client_handler(client);
    if (!handler)
    {
        lingxin_log_error("Invalid handler");
        return;
    }
    WebSocketEventListener listener = client->config->listener;
    if (!listener)
    {
        lingxin_log_error("Invalid listener");
        return;
    }

    // 使用互斥锁保护清理状态的读写
    if (handler->cleanup_mutex != NULL)
    {
        if (xSemaphoreTake(handler->cleanup_mutex, portMAX_DELAY) == pdTRUE)
        {
            // 双重检查，防止在获取锁的过程中其他线程已经完成了清理
            if (handler->is_enter_cleanup)
            {
                lingxin_log_debug("Websocket already cleaned up, skipping duplicate cleanup");
                xSemaphoreGive(handler->cleanup_mutex);
                return;
            }

            // 标记为已清理
            handler->is_enter_cleanup = true;
            xSemaphoreGive(handler->cleanup_mutex);
        }
        else
        {
            lingxin_log_error("Failed to acquire cleanup mutex");
            return;
        }
    }
    else
    {
        lingxin_log_error("cleanup_mutex not initialized");
        // 如果互斥锁未初始化，使用 volatile 标志进行基本检查
        if (handler->is_enter_cleanup)
        {
            lingxin_log_debug("Websocket already entering cleanup (no mutex), skipping duplicate cleanup");
            return;
        }
        handler->is_enter_cleanup = true;
        return;
    }

    void *user_ctx = client->config->userContext;
    // 标记为已销毁状态
    handler->is_destroyed = true;
    listener(ON_WEBSOCKET_DESTROY, NULL, 0, 0, user_ctx);
    if (handler->esp_client)
    {
        // 销毁客户端
        // 通过队列异步处理销毁操作，避免在WebSocket任务中直接关闭
        websocket_op_t op = {
            .type = WEBSOCKET_OP_DESTROY,
            .client = client};

        // 发送到操作队列
        if (websocket_op_queue != NULL)
        {
            lingxin_log_debug("Sending websocket destroy op to queue");
            xQueueSend(websocket_op_queue, &op, 0);
        }
    }
}

static void print_websocket_event_info(void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    lingxin_log_debug("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
    if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
    {
        lingxin_log_debug("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
        lingxin_log_debug("reported from tls stack", data->error_handle.esp_tls_stack_err);
        lingxin_log_debug("captured as transport's socket errno", data->error_handle.esp_transport_sock_errno);
    }
}
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    WebsocketClient *client = (WebsocketClient *)handler_args;
    if (!client || !client->config)
    {
        lingxin_log_error("Event with invalid client context");
        return;
    }

    WebsocketClientHandler *handler = get_client_handler(client);
    WebSocketEventListener listener = client->config->listener;
    void *user_ctx = client->config->userContext;
    if (!handler || !listener)
    {
        lingxin_log_error("Event with invalid listener Or handler");
        return;
    }
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (!data)
    {
        lingxin_log_error("Event with invalid data");
        return;
    }

    switch (event_id)
    {
    case WEBSOCKET_EVENT_BEGIN:
        lingxin_log_debug("WEBSOCKET_EVENT_BEGIN");
        break;
#if WS_TRANSPORT_HEADER_CALLBACK_SUPPORT
    case WEBSOCKET_EVENT_HEADER_RECEIVED:
        lingxin_log_debug("WEBSOCKET_EVENT_HEADER_RECEIVED: %.*s", data->data_len, data->data_ptr);
        break;
#endif
    case WEBSOCKET_EVENT_CONNECTED:
        lingxin_log_debug("WEBSOCKET_EVENT_CONNECTED");
        listener(ON_WEBSOCKET_CONNECTION_SUCCESS, NULL, 0, 0, user_ctx);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        lingxin_log_debug("%s", WEBSOCKET_EVENT_DISCONNECTED == event_id ? "WEBSOCKET_EVENT_DISCONNECTED" : "WEBSOCKET_EVENT_CLOSED");
        print_websocket_event_info(event_data);
        // 连接异常关闭时，清理资源
        // websocket_cleanup(client);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == WS_TRANSPORT_OPCODES_BINARY || data->op_code == WS_TRANSPORT_OPCODES_TEXT)
        {
            int is_binary = (data->op_code == WS_TRANSPORT_OPCODES_BINARY) ? 1 : 0;
            // 检查是否需要重组（payload_len > 0 表示有总长度信息）
            if (data->payload_len > 0 && (data->payload_offset > 0 || data->payload_len > data->data_len))
            {
                // === 分片消息处理 ===

                if (data->payload_offset == 0)
                {
                    // 第一个分片：分配缓冲区
                    if (handler->is_reassembling && handler->reassembly_buffer != NULL)
                    {
                        // 清理之前未完成的重组
                        lingxin_log_warn("Previous reassembly not complete, cleaning up");
                        lingxin_free(handler->reassembly_buffer);
                    }

                    handler->reassembly_capacity = data->payload_len;
                    handler->reassembly_buffer = (char *)lingxin_malloc(handler->reassembly_capacity);
                    if (handler->reassembly_buffer == NULL)
                    {
                        lingxin_log_error("Failed to allocate reassembly buffer: %d bytes", handler->reassembly_capacity);
                        handler->is_reassembling = false;
                        break;
                    }
                    handler->reassembly_size = 0;
                    handler->is_reassembling = true;
                    lingxin_log_debug("Start reassembly: capacity=%d", handler->reassembly_capacity);
                }

                // 追加当前分片
                if (handler->is_reassembling && handler->reassembly_buffer != NULL)
                {
                    if (data->payload_offset + data->data_len <= handler->reassembly_capacity)
                    {
                        memcpy(handler->reassembly_buffer + data->payload_offset,
                               data->data_ptr,
                               data->data_len);
                        handler->reassembly_size = data->payload_offset + data->data_len;
                        lingxin_log_debug("Reassembled fragment: %d/%d bytes",
                                          handler->reassembly_size, handler->reassembly_capacity);

                        // 检查是否完成
                        if (handler->reassembly_size >= handler->reassembly_capacity)
                        {
                            lingxin_log_debug("Reassembly complete: %d bytes", handler->reassembly_size);

                            // 传递完整消息给上层
                            listener(ON_WEBSOCKET_DATA_RECEIVED,
                                     handler->reassembly_buffer,
                                     handler->reassembly_size,
                                     is_binary,
                                     user_ctx);

                            // 清理重组缓冲区
                            lingxin_free(handler->reassembly_buffer);
                            handler->reassembly_buffer = NULL;
                            handler->reassembly_size = 0;
                            handler->is_reassembling = false;
                        }
                    }
                    else
                    {
                        lingxin_log_error("Buffer overflow: offset=%d, len=%d, capacity=%d",
                                          data->payload_offset, data->data_len, handler->reassembly_capacity);
                        // 清理并放弃重组
                        lingxin_free(handler->reassembly_buffer);
                        handler->reassembly_buffer = NULL;
                        handler->is_reassembling = false;
                    }
                }
                else
                {
                    lingxin_log_error("Received fragment but reassembly not initialized");
                }
            }
            else
            {
                // === 完整消息（未分片），直接传递 ===
                lingxin_log_debug("Received complete message: %d bytes", data->data_len);
                listener(ON_WEBSOCKET_DATA_RECEIVED,
                         data->data_ptr,
                         (size_t)data->data_len,
                         is_binary,
                         user_ctx);
            }
        }
        else if (data->op_code == WS_TRANSPORT_OPCODES_CLOSE && data->data_len == 2)
        {
            lingxin_log_debug("Received closed message with code=%d", 256 * data->data_ptr[0] + data->data_ptr[1]);
        }
        else
        {
            if (data->op_code == WS_TRANSPORT_OPCODES_PONG)
            {
                lingxin_log_debug("Received pong");
            }
        }

        break;
    case WEBSOCKET_EVENT_ERROR:
        lingxin_log_debug("WEBSOCKET_EVENT_ERROR");
        print_websocket_event_info(event_data);
        // listener(ON_WEBSOCKET_ERROR, NULL, 0, 0, user_ctx);
        break;
    case WEBSOCKET_EVENT_FINISH:
        lingxin_log_debug("WEBSOCKET_EVENT_FINISH");
        // 自动重连禁用时，连接关闭必经之路
        websocket_cleanup(client);
        break;
    }
}

// WebSocket管理任务，用于处理关闭等操作
static void websocket_manager_task(void *pvParameters)
{
    websocket_op_t op;

    while (1)
    {
        // 等待操作请求
        if (xQueueReceive(websocket_op_queue, &op, portMAX_DELAY))
        {
            switch (op.type)
            {
            case WEBSOCKET_OP_CLOSE:
            {
                lingxin_log_debug("processing close op");
                WebsocketClient *client = op.client;
                WebsocketClientHandler *handler = get_client_handler(client);
                if (handler && handler->esp_client)
                {
                    lingxin_log_debug("Websocket Closing");
                    lingxin_log_debug("websocket is connected:  %s", esp_websocket_client_is_connected(handler->esp_client) ? "true" : "false");
                    esp_err_t ret = esp_websocket_client_close(handler->esp_client, pdMS_TO_TICKS(2000));
                    lingxin_log_debug("esp_websocket_client_close ret: %d", ret);
                    // 不管成功还是失败，都有几率不回调WEBSOCKET_EVENT消息，均清理资源
                    // websocket_cleanup(client);
                }
                else
                {
                    lingxin_log_error("Websocket handler or esp_client is null");
                }
                break;
            }

            case WEBSOCKET_OP_DESTROY:
            {
                lingxin_log_debug("processing destroy op");
                WebsocketClient *client = op.client;
                full_clean_websocket(client);
                break;
            }
            }
        }
    }
}

// 初始化WebSocket管理器
static esp_err_t init_websocket_manager(void)
{
    if (websocket_op_queue == NULL)
    {
        // 创建操作队列
        websocket_op_queue = xQueueCreate(websocket_queue_size, sizeof(websocket_op_t));
        if (websocket_op_queue == NULL)
        {
            lingxin_log_error("Failed to create websocket operation queue");
            return ESP_FAIL;
        }

        // 创建管理任务
        BaseType_t task_ret = lingxin_xTaskCreate(websocket_manager_task,
                                                  "lx_adapter_ws",
                                                  4096,
                                                  NULL,
                                                  5,
                                                  &websocket_manager_task_handle);
        if (task_ret != pdPASS)
        {
            lingxin_log_error("Failed to create webscoket queue processing task");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static char *get_user_agent(WebsocketConfig *config)
{
    char *result = NULL;
    if (config->header_app_id && config->header_sn && config->header_signature && config->header_timestamp)
    {
        char *format_ua = "app_id:%s\r\nsn:%s\r\nsignature:%s\r\ntimestamp:%s\r\nsdk_name:%s\r\nsdk_version:%s\r\n";
        int length = snprintf(NULL, 0, format_ua, config->header_app_id, config->header_sn, config->header_signature, config->header_timestamp, get_lingxin_device_name(), get_lingxin_device_version());
        if (length > 0)
        {
            result = lingxin_malloc(length + 1);
            if (result)
            {
                snprintf(result, length + 1, format_ua, config->header_app_id, config->header_sn, config->header_signature, config->header_timestamp, get_lingxin_device_name(), get_lingxin_device_version());
                lingxin_log_debug("UA: %s", result);
            }
        }
    }

    return result ? result : NULL;
}

/**
 * @brief 初始化websocket实例
 * @param config: websocket 配置
 * @return WebsocketClient: websocket实例
 */
WebsocketClient *initWebsocket(WebsocketConfig *config)
{
    if (!config)
    {
        lingxin_log_error("config is null");
        return NULL;
    }

    if (init_websocket_manager() != ESP_OK)
    {
        lingxin_log_error("Failed to init websocket manager");
        return NULL;
    }

    WebsocketClient *client = (WebsocketClient *)lingxin_calloc(1, sizeof(WebsocketClient));
    if (client == NULL)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "adapter_websocket_init_fail", "WebsocketClient calloc fail");
        return NULL;
    }

    WebsocketClientHandler *handler =
        (WebsocketClientHandler *)lingxin_calloc(1, sizeof(WebsocketClientHandler));
    if (handler == NULL)
    { // 添加空指针检查
        lingxin_log_ut_with_args(LINGXIN_ERROR, "adapter_websocket_init_fail", "WebsocketClientHandler calloc fail");
        if (client)
        {
            lingxin_free(client);
            client = NULL;
        }
        return NULL;
    }
    handler->is_destroyed = false;
    handler->is_enter_cleanup = false;
    // 初始化重组相关字段
    handler->reassembly_buffer = NULL;
    handler->reassembly_size = 0;
    handler->reassembly_capacity = 0;
    handler->is_reassembling = false;
    
    handler->cleanup_mutex = xSemaphoreCreateMutex();
    if (handler->cleanup_mutex == NULL)
    {
        lingxin_log_error("Failed to create mutex");
        if (handler)
        {
            lingxin_free(handler);
            handler = NULL;
        }
        if (client)
        {
            lingxin_free(client);
            client = NULL;
        }
        return NULL;
    }
    client->clientHandler = handler;
    client->config = config;

    char url[256];
    snprintf(url, sizeof(url), "%s://%s:%d/%s", config->protocol,
             config->host, config->port, config->path);

    esp_websocket_client_config_t websocket_cfg = {0};
    websocket_cfg.uri = url;
    websocket_cfg.headers = get_user_agent(config);
    websocket_cfg.user_context = client;
    websocket_cfg.disable_auto_reconnect = true;
    websocket_cfg.transport = WEBSOCKET_TRANSPORT_OVER_TCP;
    websocket_cfg.task_stack = WEBSOCKET_STACK_SIZE;
    websocket_cfg.buffer_size = WEBSOCKET_BUFFER_SIZE;
    // 设置3s发一次ping
    websocket_cfg.ping_interval_sec = 3;
    // websocket_cfg.pingpong_timeout_sec = 20;
    // 设置唯一线程名
    char task_name[32];
    long now_time = lingxin_get_timestamp_s();
    snprintf(task_name, sizeof(task_name), "lx_ws_%ld", now_time);
    websocket_cfg.task_name = lingxin_strdup(task_name);
    lingxin_log_debug("websocket_cfg.task_name: %s", task_name);

    handler->websocket_cfg = websocket_cfg;
    esp_websocket_client_handle_t esp_client = esp_websocket_client_init(&websocket_cfg);
    if (esp_client == NULL)
    {
        lingxin_log_error("Failed to init websocket client");
        goto init_err;
    }
    handler->esp_client = esp_client;
    esp_err_t ret = esp_websocket_register_events(esp_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    if (ret != ESP_OK)
    {
        lingxin_log_error("esp_websocket_register_events failed: %d", ret);
        // 清理资源
        goto init_err;
    }
    lingxin_log_debug("Connecting to %s...", websocket_cfg.uri);

    return client;

init_err:
    full_clean_websocket(client);
    return NULL;
}

/**
 * @brief 启动 websocket，比如启动线程等操作
 * @param client: initWebsocket 函数返回的 WebsocketClient
 * @return void
 */
bool startWebsocket(WebsocketClient *client)
{
    if (client == NULL || client->clientHandler == NULL)
    {
        lingxin_log_error("client is null");
        goto start_err;
    }
    WebsocketClientHandler *handler = (WebsocketClientHandler *)client->clientHandler;
    esp_err_t ret = esp_websocket_client_start(handler->esp_client);
    if (ret != ESP_OK)
    {
        lingxin_log_error("Failed to start websocket client");
        diagnose_task_create_failure(WEBSOCKET_STACK_SIZE);
        goto start_err;
    }
    lingxin_log_debug("Websocket client started successfully");
    return true;
start_err:
    full_clean_websocket(client);
    return false;
}

/**
 * @brief 发送文本数据
 * @param client: initWebsocket 函数返回的 WebsocketClient
 * @param message:  文本数据
 * @return bool: 成功，true；失败，false
 */
bool websocketSendText(WebsocketClient *client, const char *message)
{
    if (client == NULL)
    {
        lingxin_log_error("client is null");
        return false;
    }
    WebsocketClientHandler *handler = (WebsocketClientHandler *)client->clientHandler;
    if (handler == NULL)
    {
        lingxin_log_error("handler is null");
        return false;
    }
    if (handler->esp_client && esp_websocket_client_is_connected(handler->esp_client))
    {
        lingxin_log_debug("Sending text, strlen(message) = %d", strlen(message));
        int ret = esp_websocket_client_send_text(handler->esp_client, message, strlen(message), pdMS_TO_TICKS(1000)); // portMAX_DELAY
        if (ret == -1)
        {
            lingxin_log_warn("Failed to send text");
            return false;
        }
        // lingxin_log_debug("Success to send text");
        return true;
    }
    return false;
}

/**
 * @brief 发送二进制数据
 * @param client: initWebsocket 函数返回的 WebsocketClient
 * @param audioData:  二进制数据
 * @param dataSize:  二进制数据长度
 * @return int: 发送成功的二进制数据长度
 */
int websocketSendBinary(WebsocketClient *client, const char *audioData, size_t dataSize)
{
    if (client == NULL || client->clientHandler == NULL)
    {
        lingxin_log_error("client is null");
        return 0;
    }
    WebsocketClientHandler *handler = (WebsocketClientHandler *)client->clientHandler;
    if (handler->esp_client && esp_websocket_client_is_connected(handler->esp_client))
    {
        lingxin_log_debug("Sending fragmented binary message, datasize is %d", dataSize);
        int ret = esp_websocket_client_send_bin(handler->esp_client, audioData, dataSize, pdMS_TO_TICKS(1000));
        // lingxin_log_debug("Sending fragmented binary message end, send ret is %d", ret);
        return ret;
    }
    else if (!esp_websocket_client_is_connected(handler->esp_client))
    {
        lingxin_log_error("websocket is not connected");
    }
    else
    {
        lingxin_log_error("handler -> esp_client is null");
    }
    return 0;
}

/**
 * @brief 关闭 websocket
 * @param client: initWebsocket 函数返回的 WebsocketClient
 * @return void
 */
void closeWebsocket(WebsocketClient *client)
{
    lingxin_log_debug("closeWebsocket begin");
    if (client == NULL || client->clientHandler == NULL)
    {
        lingxin_log_error("client is null");
        return;
    }
    // 通过队列异步处理关闭操作，避免在WebSocket任务中直接关闭
    websocket_op_t op = {
        .type = WEBSOCKET_OP_CLOSE,
        .client = client};

    // 发送到操作队列
    if (websocket_op_queue != NULL)
    {
        lingxin_log_debug("Sending websocket close op to queue");
        xQueueSend(websocket_op_queue, &op, 0);
    }
}

/**
 * @brief 检测 websocket 是否存活，防止 websocket 异常断联没有回调上层
 * @param client: initWebsocket 函数返回的 WebsocketClient
 * @return bool: websocket 是否存活
 */
bool isWebsocketAlive(WebsocketClient *client)
{
    if (!client)
    {
        lingxin_log_error("client is null");
        return false;
    }

    WebsocketClientHandler *handler = get_client_handler(client);
    if (!handler)
    {
        lingxin_log_error("handler is null");
        return false;
    }
    lingxin_log_debug("isWebsocketAlive: %d", !handler->is_destroyed);
    return !handler->is_destroyed;
}