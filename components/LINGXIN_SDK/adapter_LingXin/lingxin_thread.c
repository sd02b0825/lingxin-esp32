#include <stddef.h>
#include <stdlib.h>
#include <stdint.h> // 添加 uintptr_t 支持
#include "lingxin_thread.h"
#include "lingxin_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
/* lingxin_func_wrap/func.h not available in v2.6.6 build */
#define lingxin_xTaskCreate xTaskCreate
#include "lingxin_memory.h"

static const char *TAG = "lingxin_adapter_thread";
// 包装参数结构体
typedef struct
{
    void *(*start_routine)(void *);
    void *user_args;
} thread_wrapper_args_t;

// 自定义线程状态枚举
typedef enum
{
    THREAD_STATE_STARTED = 0,
    THREAD_STATE_RUNNING,
    THREAD_STATE_FINISHED, // 线程自身执行完成
    THREAD_STATE_DELETED   // 线程已被lingxin_thread_destroy删除的状态
} custom_thread_state_t;

// 线程状态节点结构
typedef struct thread_state_node
{
    TaskHandle_t task_handle;
    custom_thread_state_t state;
    struct thread_state_node *next;
} thread_state_node_t;
// 线程状态链表和保护锁
static thread_state_node_t *g_thread_nodes = NULL;
static SemaphoreHandle_t g_thread_state_mutex = NULL;

// 初始化线程状态管理器
static void init_thread_state_manager(void)
{
    if (g_thread_state_mutex == NULL)
    {
        g_thread_state_mutex = xSemaphoreCreateMutex();
    }
}

// 设置线程状态
static void set_thread_state(TaskHandle_t task_handle, custom_thread_state_t state)
{
    if (g_thread_state_mutex == NULL)
    {
        init_thread_state_manager();
    }

    if (xSemaphoreTake(g_thread_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        // 查找现有节点
        thread_state_node_t *node = g_thread_nodes;
        thread_state_node_t *prev = NULL;

        while (node != NULL)
        {
            if (node->task_handle == task_handle)
            {
                // 如果节点已经是DELETED状态，不允许再修改状态
                if (node->state == THREAD_STATE_DELETED)
                {
                    lingxin_log_debug("Thread state is DELETED, not allowed to modify");
                    xSemaphoreGive(g_thread_state_mutex);
                    return;
                }

                node->state = state;
                // 如果设置为DELETED状态，后续不需要再创建新节点
                xSemaphoreGive(g_thread_state_mutex);
                return;
            }
            prev = node;
            node = node->next;
        }

        // 如果没有找到且不是DELETED状态，创建新节点
        if (state != THREAD_STATE_DELETED)
        {
            thread_state_node_t *new_node = (thread_state_node_t *)lingxin_malloc(sizeof(thread_state_node_t));
            if (new_node != NULL)
            {
                new_node->task_handle = task_handle;
                new_node->state = state;
                new_node->next = NULL;

                if (prev == NULL)
                {
                    g_thread_nodes = new_node;
                }
                else
                {
                    prev->next = new_node;
                }
            }
        }

        xSemaphoreGive(g_thread_state_mutex);
    }
}

// 获取线程状态
static custom_thread_state_t get_thread_state(TaskHandle_t task_handle)
{
    if (g_thread_state_mutex == NULL)
    {
        lingxin_log_debug("Thread state manager not initialized");
        return THREAD_STATE_STARTED; // 默认状态
    }

    custom_thread_state_t state = THREAD_STATE_STARTED;

    if (xSemaphoreTake(g_thread_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        thread_state_node_t *node = g_thread_nodes;
        thread_state_node_t *prev = NULL;

        while (node != NULL)
        {
            if (node->task_handle == task_handle)
            {
                // 检查节点状态是否为DELETED
                if (node->state == THREAD_STATE_DELETED)
                {
                    lingxin_log_debug("Thread state is DELETED");
                    // 如果是DELETED状态，说明线程已被销毁，清理节点
                    thread_state_node_t *node_to_delete = node;
                    if (prev == NULL)
                    {
                        g_thread_nodes = node->next;
                    }
                    else
                    {
                        prev->next = node->next;
                    }
                    if (node_to_delete)
                    {
                        lingxin_free(node_to_delete);
                        node_to_delete = NULL;
                    }

                    state = THREAD_STATE_DELETED;
                }
                else
                {
                    // 检查实际任务是否存在
                    eTaskState task_state = eTaskGetState(task_handle);
                    if (task_state == eDeleted || task_state == eInvalid)
                    {
                        lingxin_log_debug("freeRTOS Task state is deleted or invalid");
                        // 任务已不存在但状态不是DELETED，说明异常终止
                        // 标记为FINISHED并清理节点
                        thread_state_node_t *node_to_delete = node;
                        if (prev == NULL)
                        {
                            g_thread_nodes = node->next;
                        }
                        else
                        {
                            prev->next = node->next;
                        }
                        if (node_to_delete)
                        {
                            lingxin_free(node_to_delete);
                            node_to_delete = NULL;
                        }
                        state = THREAD_STATE_FINISHED;
                    }
                    else
                    {
                        state = node->state;
                    }
                }
                break;
            }
            prev = node;
            node = node->next;
        }

        xSemaphoreGive(g_thread_state_mutex);
    }

    return state;
}
// 移除线程状态（在线程完全结束后）
static void remove_thread_state(TaskHandle_t task_handle)
{
    if (g_thread_state_mutex == NULL)
    {
        return;
    }

    if (xSemaphoreTake(g_thread_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        thread_state_node_t *node = g_thread_nodes;
        thread_state_node_t *prev = NULL;

        while (node != NULL)
        {
            if (node->task_handle == task_handle)
            {
                if (prev == NULL)
                {
                    g_thread_nodes = node->next;
                }
                else
                {
                    prev->next = node->next;
                }
                if (node)
                {
                    lingxin_free(node);
                    node = NULL;
                }
                break;
            }
            prev = node;
            node = node->next;
        }

        xSemaphoreGive(g_thread_state_mutex);
    }
}

// 获取task信息
static void get_task_info(TaskHandle_t task_handle, const char *context)
{
    if (task_handle == NULL)
    {
        lingxin_log_warn("[%s] Attempt to get info for NULL task handle",
                         context ? context : "Unknown Context");
        return;
    }

    char task_name[configMAX_TASK_NAME_LEN] = "Unknown";
    UBaseType_t task_priority = 0;
    eTaskState task_state = eInvalid;

    // 获取任务名称
#if (configUSE_TRACE_FACILITY == 1)
    const char *name = pcTaskGetName(task_handle);
    if (name != NULL)
    {
        strncpy(task_name, name, configMAX_TASK_NAME_LEN - 1);
        task_name[configMAX_TASK_NAME_LEN - 1] = '\0';
    }
#endif

    // 获取任务优先级
    task_priority = uxTaskPriorityGet(task_handle);

    // 获取任务状态
    task_state = eTaskGetState(task_handle);

    // 获取任务ID（句柄值）
    uint32_t task_id = (uint32_t)(uintptr_t)task_handle;

    lingxin_log_debug("[%s] Task Info - ID: %u, Name: %s, Priority: %u, State: %d",
                      context ? context : "Unknown Context",
                      task_id,
                      task_name,
                      (unsigned int)task_priority,
                      (int)task_state);
}

// 线程适配器函数
static void thread_adapter(void *arg)
{
    thread_wrapper_args_t *wrapper_args = (thread_wrapper_args_t *)arg;

    TaskHandle_t self_handle = xTaskGetCurrentTaskHandle();
    set_thread_state(self_handle, THREAD_STATE_RUNNING);

    if (wrapper_args && wrapper_args->start_routine)
    {
        // 调用用户函数
        wrapper_args->start_routine(wrapper_args->user_args);
    }

    // 释放包装参数内存
    if (wrapper_args)
    {
        lingxin_free(wrapper_args);
        wrapper_args = NULL;
    }

    get_task_info(self_handle, "[lx_thread_adapter] before self end");
    // 标记线程为已完成状态
    set_thread_state(self_handle, THREAD_STATE_FINISHED);
    // 删除当前任务
    vTaskDelete(NULL);
}

int lingxin_thread_create(lingxin_tid_t *thread, const lingxin_thread_param_t *param, void *(*start_routine)(void *), void *args)
{
    // 默认参数
    int stack_size = 8192;               // 默认栈大小
    int priority = tskIDLE_PRIORITY + 5; // tskIDLE_PRIORITY为空闲任务的优先级 = 0
    const char *name = "lx_thread";
    if (NULL == thread || NULL == start_routine)
    {
        lingxin_log_error("Invalid parameters for thread creation");
        return -1;
    }

    if (NULL != param)
    {
        // 设置线程名称
        name = param->name[0] ? param->name : "lx_thread";

        // 设置线程优先级
        if (param->priority <= -1)
        {
            priority = tskIDLE_PRIORITY + 5;
        }
        else if (param->priority > configMAX_PRIORITIES - 1)
        {
            lingxin_log_warn("Requested priority %d exceeds maximum %d, clamping to %d",
                             param->priority, configMAX_PRIORITIES - 1, configMAX_PRIORITIES - 1);
            priority = configMAX_PRIORITIES - 1;
        }
        else
        {
            priority = param->priority;
        }

        // 设置stack_size
        if (param->stack_size == -1)
        {
            stack_size = 4096;
        }
        else
        {
            stack_size = param->stack_size;
        }
    }
    else
    {
        stack_size = 4096;
        priority = tskIDLE_PRIORITY + 5;
    }

    // 创建包装参数
    thread_wrapper_args_t *wrapper_args = (thread_wrapper_args_t *)lingxin_malloc(sizeof(thread_wrapper_args_t));
    if (wrapper_args == NULL)
    {
        lingxin_log_error("Failed to allocate wrapper args");
        return -1;
    }

    wrapper_args->start_routine = start_routine;
    wrapper_args->user_args = args;

    TaskHandle_t task_handle = NULL;
    lingxin_log_debug("Creating thread: name=%s, prio=%d, stack=%d, args=%p", name, priority, stack_size, args);
    // 创建FreeRTOS任务
    BaseType_t result = lingxin_xTaskCreate(
        thread_adapter, // 任务函数
        name,           // 任务名称
        stack_size,     // 栈深度(以字为单位)
        wrapper_args,   // 任务参数
        priority,       // 任务优先级
        &task_handle    // 任务句柄
    );
    if (pdPASS != result)
    {
        lingxin_log_error("Failed to create task, error code: %d", result);
        if (wrapper_args)
        {
            lingxin_free(wrapper_args);
            wrapper_args = NULL;
        }
        return -1;
    }
    // 存储任务句柄作为线程ID
    *thread = (lingxin_tid_t)(uintptr_t)task_handle;

    lingxin_log_debug("Thread created successfully: name=%s, prio=%d, stack=%d, tid=%d",
                      name, priority, stack_size, *thread);
    return 0;
}


void lingxin_thread_destroy(lingxin_tid_t thread, lingxin_thread_destroy_mode_t mode)
{
    if (0 == thread)
    {
        lingxin_log_warn("Attempt to destroy null thread");
        return;
    }
    // 获取调用lingxin_thread_destroy的线程信息
    TaskHandle_t self_handle = xTaskGetCurrentTaskHandle();
    get_task_info(self_handle, "[lingxin_thread_destroy] called in thread:");

    TaskHandle_t task_handle = (TaskHandle_t)(uintptr_t)thread;
    if(task_handle == self_handle) {
        lingxin_log_debug("thread %d enter self destroy", thread);
        set_thread_state(task_handle, THREAD_STATE_DELETED);
        remove_thread_state(task_handle);
        vTaskDelete(NULL);
        return;
    }
    eTaskState state = eTaskGetState(task_handle);
    lingxin_log_debug("Task state: %d, current thread is %d", state, thread);
    if (state == eInvalid || state == eDeleted)
    {
        lingxin_log_warn("Attempt to destroy invalid task handle: %d", thread);
        set_thread_state(task_handle, THREAD_STATE_DELETED);
        remove_thread_state(task_handle);
        return;
    }
    switch (mode)
    {
    case LINGXIN_THREAD_DESTROY_WAIT:
        lingxin_log_debug("thread %d enter waiting destroy", thread);
        const TickType_t check_interval = pdMS_TO_TICKS(50);
        while (1)
        {
            // 先检查eTaskSate
            eTaskState cur_state = eTaskGetState(task_handle);
            if (cur_state == eDeleted || cur_state == eInvalid || cur_state == eSuspended)
            {
                // 任务已经结束或无效，跳出循环
                lingxin_log_debug("Task %d is already deleted or invalid or suspend", thread);
                set_thread_state(task_handle, THREAD_STATE_DELETED);
                remove_thread_state(task_handle);
                break;
            }

            // 再检查自定义状态
            custom_thread_state_t custom_state = get_thread_state(task_handle);
            if (custom_state == THREAD_STATE_FINISHED || custom_state == THREAD_STATE_DELETED)
            {
                lingxin_log_debug("Task %d marked as %s in custom state tracking", thread, custom_state == THREAD_STATE_FINISHED ? "finished" : "deleted");
                set_thread_state(task_handle, THREAD_STATE_DELETED);
                remove_thread_state(task_handle);
                break;
            }
            taskYIELD();
            vTaskDelay(check_interval);
        }
        lingxin_log_debug("Thread %d finished or deleted", thread);
        break;

    case LINGXIN_THREAD_DESTROY_DETACH:
        // FreeRTOS任务默认就是分离的，无需特殊处理
        lingxin_log_debug("Thread %d detached", thread);
        break;

    case LINGXIN_THREAD_DESTROY_CANCEL:
        // 直接删除任务
        lingxin_log_debug("Thread %d cancelled, task_handle=%p", thread, task_handle);
        if (state != eInvalid && state != eDeleted)
        {
            vTaskDelete(task_handle);
        }
        else
        {
            lingxin_log_warn("Task already deleted or invalid when attempting to cancel");
        }
        set_thread_state(task_handle, THREAD_STATE_DELETED);
        remove_thread_state(task_handle);
        break;

    default:
        lingxin_log_warn("Unknown destroy mode: %d, deleting task, task_handle=%p", mode, task_handle);
        if (state != eInvalid && state != eDeleted)
        {
            vTaskDelete(task_handle);
        }
        else
        {
            lingxin_log_warn("Task already deleted or invalid when attempting to delete");
        }
        set_thread_state(task_handle, THREAD_STATE_DELETED);
        remove_thread_state(task_handle);
        break;
    }
}

void lingxin_thread_sleep(int time)
{
    if (time <= 0)
    {
        lingxin_log_warn("Invalid sleep time: %d", time);
        return;
    }
    // 将毫秒转换为FreeRTOS ticks并延时
    TickType_t ticks = pdMS_TO_TICKS(time);
    vTaskDelay(ticks);
}

char *lingxin_get_current_thread_name()
{
    TaskHandle_t self_handle = xTaskGetCurrentTaskHandle();
    return pcTaskGetName(self_handle);
}