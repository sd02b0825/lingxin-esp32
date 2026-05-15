#include "lingxin_timer.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include "esp_log.h"
#include "lingxin_log.h"

static const char *TAG = "lingxin_adapter_timer";

#define MAX_PERIODIC_TIMERS 10
#define MAX_ONE_SHOT_TIMERS 10

typedef struct
{
    void *priv;
    void (*func)(void *priv);
    esp_timer_handle_t handle;
    bool used;
    uint32_t period_ms; // 新增：仅周期性定时器使用
} timer_entry_t;

static timer_entry_t periodic_timers[MAX_PERIODIC_TIMERS] = {0};
static timer_entry_t one_shot_timers[MAX_ONE_SHOT_TIMERS] = {0};
static SemaphoreHandle_t timer_mutex = NULL;

static void ensure_mutex_init(void)
{
    if (timer_mutex == NULL)
    {
        timer_mutex = xSemaphoreCreateMutex();
    }
}

static int find_free_slot(timer_entry_t *table, size_t size)
{
    for (int i = 0; i < (int)size; i++)
    {
        if (!table[i].used)
        {
            return i;
        }
    }
    return -1;
}

static void periodic_timer_callback(void *arg)
{
    timer_entry_t *entry = (timer_entry_t *)arg;
    if (entry && entry->func)
    {
        entry->func(entry->priv);
    }
}
static void one_shot_timer_callback(void *arg)
{
    timer_entry_t *entry = (timer_entry_t *)arg;
    if (entry && entry->func)
    {
        // 先调用用户回调
        entry->func(entry->priv);
    }
    // // 自动释放资源（一次性）
    // if (timer_mutex && xSemaphoreTake(timer_mutex, portMAX_DELAY) == pdTRUE)
    // {
    //     // 查找并标记为未使用
    //     for (int i = 0; i < MAX_ONE_SHOT_TIMERS; i++)
    //     {
    //         if (&one_shot_timers[i] == entry)
    //         {
    //             if (one_shot_timers[i].handle != NULL)
    //             {
    //                 esp_timer_stop(one_shot_timers[i].handle);
    //                 esp_timer_delete(one_shot_timers[i].handle);
    //                 one_shot_timers[i].handle = NULL;
    //             }
    //             memset(&one_shot_timers[i], 0, sizeof(timer_entry_t));
    //             break;
    //         }
    //     }
    //     xSemaphoreGive(timer_mutex);
    // }
}

// 周期性定时器

/**
 * @brief sys_timer定时扫描增加接口，创建一个周期性定时器
 * @param priv 定时器回调函数func的私有参数
 * @param func 超时扫描回调函数
 * @param msec 超时时间， 单位：毫秒
 * @return 定时器分配的id号, 创建失败时返回INVALID_TIMER_ID
 */
int lingxin_sys_timer_add(void *priv, void (*func)(void *priv), long msec)
{
    if (!func || msec <= 0)
    {
        return INVALID_TIMER_ID;
    }
    ensure_mutex_init();
    if (xSemaphoreTake(timer_mutex, portMAX_DELAY) != pdTRUE)
    {
        return INVALID_TIMER_ID;
    }
    int slot = find_free_slot(periodic_timers, MAX_PERIODIC_TIMERS);
    if (slot < 0)
    {
        xSemaphoreGive(timer_mutex);
        return INVALID_TIMER_ID;
    }
    esp_timer_handle_t handle = NULL;
    esp_timer_create_args_t args = {
        .callback = periodic_timer_callback,
        .arg = &periodic_timers[slot],
        .dispatch_method = ESP_TIMER_TASK,
        .name = "periodic_timer"};

    esp_err_t err = esp_timer_create(&args, &handle);
    if (err != ESP_OK)
    {
        xSemaphoreGive(timer_mutex);
        return INVALID_TIMER_ID;
    }

    err = esp_timer_start_periodic(handle, msec * 1000ULL); // 转微秒
    if (err != ESP_OK)
    {
        esp_timer_delete(handle);
        xSemaphoreGive(timer_mutex);
        return INVALID_TIMER_ID;
    }

    periodic_timers[slot].priv = priv;
    periodic_timers[slot].func = func;
    periodic_timers[slot].handle = handle;
    periodic_timers[slot].used = true;
    periodic_timers[slot].period_ms = (uint32_t)msec; // 保存周期

    xSemaphoreGive(timer_mutex);
    return slot; // 返回逻辑 ID（非负整数）
}

/**
 * @brief sys_timer定时扫描删除接口
 * @param timer_id sys_timer_add分配的id号
 * @return 删除结果（0-成功，-1-失败）
 */
int lingxin_sys_timer_del(int timer_id)
{
    if (timer_id < 0 || timer_id >= MAX_PERIODIC_TIMERS)
    {
        return -1;
    }

    ensure_mutex_init();
    if (xSemaphoreTake(timer_mutex, portMAX_DELAY) != pdTRUE)
    {
        return -1;
    }

    if (!periodic_timers[timer_id].used)
    {
        xSemaphoreGive(timer_mutex);
        return -1;
    }

    esp_timer_handle_t handle = periodic_timers[timer_id].handle;
    if (handle != NULL)
    {
        esp_timer_stop(handle);
        esp_timer_delete(handle);
    }

    memset(&periodic_timers[timer_id], 0, sizeof(timer_entry_t));
    xSemaphoreGive(timer_mutex);
    return 0;
}

/**
 * @brief sys_timer定时扫描重置接口
 * @param timer_id sys_timer_add分配的id号
 * @return 重置结果（0-成功，-1-失败）
 */
int lingxin_sys_timer_re_run(int timer_id)
{
    if (timer_id < 0 || timer_id >= MAX_PERIODIC_TIMERS)
    {
        return -1;
    }

    ensure_mutex_init();
    if (xSemaphoreTake(timer_mutex, portMAX_DELAY) != pdTRUE)
    {
        return -1;
    }

    if (!periodic_timers[timer_id].used)
    {
        xSemaphoreGive(timer_mutex);
        return -1;
    }

    esp_timer_handle_t handle = periodic_timers[timer_id].handle;
    uint32_t period_ms = periodic_timers[timer_id].period_ms;

    // 停止当前定时器
    esp_timer_stop(handle);
    // 重新启动（从现在起再等 period_ms）
    esp_err_t err = esp_timer_start_periodic(handle, (uint64_t)period_ms * 1000ULL);
    if (err != ESP_OK)
    {
        xSemaphoreGive(timer_mutex);
        return -1;
    }

    xSemaphoreGive(timer_mutex);
    return 0;
}

// 一次性定时器

/**
 * @brief 创建一个一次性定时器
 * @param priv 定时器回调函数func的私有参数
 * @param func 定时器回调函数
 * @param countdown 超时时间， 单位：毫秒
 * @return 定时器分配的id号, 创建失败时返回INVALID_TIMER_ID
 */
int lingxin_one_shot_timer_create(void *priv, void (*func)(void *priv), long countdown)
{
    if (!func)
    {
        return INVALID_TIMER_ID;
    }
    ensure_mutex_init();
    if (xSemaphoreTake(timer_mutex, portMAX_DELAY) != pdTRUE)
    {
        lingxin_log_warn("xSemaphoreTake failed");
        return INVALID_TIMER_ID;
    }

    int slot = find_free_slot(one_shot_timers, MAX_ONE_SHOT_TIMERS);
    if (slot < 0)
    {
        xSemaphoreGive(timer_mutex);
        return INVALID_TIMER_ID;
    }

    esp_timer_handle_t handle = NULL;
    esp_timer_create_args_t args = {
        .callback = one_shot_timer_callback,
        .arg = &one_shot_timers[slot],
        .dispatch_method = ESP_TIMER_TASK,
        .name = "one_shot_timer"};

    esp_err_t err = esp_timer_create(&args, &handle);
    if (err != ESP_OK)
    {
        xSemaphoreGive(timer_mutex);
        return INVALID_TIMER_ID;
    }

    lingxin_log_debug("one_shot_timer_create: %p, %d ms", handle, countdown);
    err = esp_timer_start_once(handle, (countdown == 0 ? 1 : (uint64_t)countdown ) * 1000ULL);
    if (err != ESP_OK)
    {
        esp_timer_delete(handle);
        xSemaphoreGive(timer_mutex);
        return INVALID_TIMER_ID;
    }

    one_shot_timers[slot].priv = priv;
    one_shot_timers[slot].func = func;
    one_shot_timers[slot].handle = handle;
    one_shot_timers[slot].used = true;

    xSemaphoreGive(timer_mutex);
    return slot;
}

/**
 * @brief 删除指定的一次性定时器
 * @param timerId 定时器ID
 * @return 删除结果（0-成功，-1-失败）
 */
int lingxin_one_shot_timer_delete(int timerId)
{
    if (timerId < 0 || timerId >= MAX_ONE_SHOT_TIMERS)
    {
        return -1;
    }

    ensure_mutex_init();
    if (xSemaphoreTake(timer_mutex, portMAX_DELAY) != pdTRUE)
    {
        return -1;
    }

    if (!one_shot_timers[timerId].used)
    {
        xSemaphoreGive(timer_mutex);
        return -1;
    }

    esp_timer_handle_t handle = one_shot_timers[timerId].handle;
    if (handle != NULL)
    {
        esp_timer_stop(handle);
        esp_timer_delete(handle);
        one_shot_timers[timerId].handle = NULL;
        one_shot_timers[timerId].used = false;
    }

    memset(&one_shot_timers[timerId], 0, sizeof(timer_entry_t));
    xSemaphoreGive(timer_mutex);
    return 0;
}

/**
 * @brief 获取系统的滴答秒（系统启动以来经过的秒）
 * @return 系统的滴答秒
 */
long lingxin_get_sys_time_sec(void)
{
    return esp_timer_get_time() / 1000000ULL; // 微秒转秒
}
