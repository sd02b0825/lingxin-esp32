#include "lingxin_queue.h"
#include "lingxin_memory.h"
#include "lingxin_mutex.h"
#include "lingxin_log.h"
#include <string.h>

typedef struct {
    char *name;                      // 队列名称
    void **buffer;                   // 数据缓冲区
    uint32_t capacity;               // 最大容量
    uint32_t head;                   // 写入位置
    uint32_t tail;                   // 读取位置
    uint32_t count;                  // 当前元素数
    lingxin_mutex_t mutex;           // 互斥锁
    bool overwrite;                  // 是否覆盖模式
    ring_queue_free_fn_t free_fn;    // 元素释放函数
} ring_queue_impl_t;

ring_queue_t ring_queue_create(const ring_queue_config_t *config)
{
    if (!config) {
        lingxin_log_error("ring_queue_create: config is NULL");
        return NULL;
    }
    
    if (config->capacity == 0) {
        lingxin_log_error("ring_queue_create: capacity must > 0");
        return NULL;
    }
    
    ring_queue_impl_t *queue = (ring_queue_impl_t *)lingxin_calloc(1, sizeof(ring_queue_impl_t));
    if (!queue) {
        lingxin_log_error("ring_queue_create: malloc queue failed");
        return NULL;
    }
    
    // 保存队列名称
    if (config->name) {
        queue->name = lingxin_strdup(config->name);
    } else {
        queue->name = lingxin_strdup("unnamed");
    }
    
    queue->buffer = (void **)lingxin_calloc(config->capacity, sizeof(void *));
    if (!queue->buffer) {
        lingxin_log_error("[%s] ring_queue_create: malloc buffer failed", queue->name);
        goto create_fail;
    }
    
    queue->capacity = config->capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->overwrite = config->overwrite;
    queue->free_fn = config->free_fn;
    queue->mutex = lingxin_mutex_create();
    
    if (!queue->mutex) {
        lingxin_log_error("[%s] ring_queue_create: create mutex failed", queue->name);
        goto create_fail;
    }
    
    lingxin_log_debug("[%s] ring_queue created, capacity=%u, overwrite=%s, free_fn=%s", 
                      queue->name, config->capacity, 
                      config->overwrite ? "true" : "false",
                      config->free_fn ? "yes" : "no");
    
    return (ring_queue_t)queue;

create_fail:
    if (queue) {
        if (queue->name) {
            lingxin_free(queue->name);
            queue->name = NULL;
        }
        if (queue->buffer) {
            lingxin_free(queue->buffer);
            queue->buffer = NULL;
        }
        if (queue->mutex) {
            lingxin_mutex_destroy(queue->mutex);
            queue->mutex = NULL;
        }
        lingxin_free(queue);
    }
    return NULL;
}

const char *ring_queue_get_name(ring_queue_t handle)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue) return NULL;
    return queue->name;
}

bool ring_queue_enqueue(ring_queue_t handle, void *item)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue || !item) {
        return false;
    }
    
    lingxin_mutex_lock(queue->mutex);
    
    // 覆盖模式：如果满了，释放最旧的数据
    if (queue->count >= queue->capacity) {
        if (queue->overwrite) {
            // 释放最旧的数据
            if (queue->buffer[queue->tail] && queue->free_fn) {
                queue->free_fn(queue->buffer[queue->tail]);
            }
            queue->buffer[queue->tail] = NULL;
            queue->tail = (queue->tail + 1) % queue->capacity;
            queue->count--;
            lingxin_log_debug("[%s] ring_queue overwrite, drop oldest item", queue->name);
        } else {
            // 非覆盖模式：返回失败
            lingxin_log_warn("[%s] ring_queue full, enqueue failed", queue->name);
            lingxin_mutex_unlock(queue->mutex);
            return false;
        }
    }
    
    // 写入新数据
    queue->buffer[queue->head] = item;
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;
    
    lingxin_log_debug("[%s] ring_queue enqueue, count=%u/%u", 
                      queue->name, queue->count, queue->capacity);
    
    lingxin_mutex_unlock(queue->mutex);
    return true;
}

void *ring_queue_dequeue(ring_queue_t handle)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue) {
        return NULL;
    }
    
    lingxin_mutex_lock(queue->mutex);
    
    if (queue->count == 0) {
        lingxin_mutex_unlock(queue->mutex);
        return NULL;
    }
    
    void *item = queue->buffer[queue->tail];
    queue->buffer[queue->tail] = NULL;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;
    
    lingxin_log_debug("[%s] ring_queue dequeue, count=%u/%u", 
                      queue->name, queue->count, queue->capacity);
    
    lingxin_mutex_unlock(queue->mutex);
    return item;
}

bool ring_queue_is_empty(ring_queue_t handle)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue) return true;
    
    lingxin_mutex_lock(queue->mutex);
    bool empty = (queue->count == 0);
    lingxin_mutex_unlock(queue->mutex);
    return empty;
}

bool ring_queue_is_full(ring_queue_t handle)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue) return false;
    
    lingxin_mutex_lock(queue->mutex);
    bool full = (queue->count >= queue->capacity);
    lingxin_mutex_unlock(queue->mutex);
    return full;
}

uint32_t ring_queue_count(ring_queue_t handle)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue) return 0;
    
    lingxin_mutex_lock(queue->mutex);
    uint32_t count = queue->count;
    lingxin_mutex_unlock(queue->mutex);
    return count;
}

bool ring_queue_find(ring_queue_t handle, ring_queue_match_fn_t match_fn, const void *ctx)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue || !match_fn) {
        return false;
    }
    
    lingxin_mutex_lock(queue->mutex);
    
    bool found = false;
    for (uint32_t i = 0; i < queue->count; i++) {
        uint32_t idx = (queue->tail + i) % queue->capacity;
        if (queue->buffer[idx] && match_fn(queue->buffer[idx], ctx)) {
            found = true;
            break;
        }
    }
    
    lingxin_mutex_unlock(queue->mutex);
    return found;
}

void ring_queue_clear(ring_queue_t handle)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue) return;
    
    lingxin_mutex_lock(queue->mutex);
    
    // 释放所有元素
    if (queue->free_fn) {
        for (uint32_t i = 0; i < queue->count; i++) {
            uint32_t idx = (queue->tail + i) % queue->capacity;
            if (queue->buffer[idx]) {
                queue->free_fn(queue->buffer[idx]);
                queue->buffer[idx] = NULL;
            }
        }
    }
    
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    
    lingxin_log_debug("[%s] ring_queue cleared", queue->name);
    
    lingxin_mutex_unlock(queue->mutex);
}

void ring_queue_destroy(ring_queue_t handle)
{
    ring_queue_impl_t *queue = (ring_queue_impl_t *)handle;
    if (!queue) return;
    
    const char *name = queue->name;  // 先保存名称用于日志
    
    // 先清空队列（释放所有元素）
    ring_queue_clear(handle);
    
    // 释放队列结构
    if (queue->mutex) {
        lingxin_mutex_destroy(queue->mutex);
        queue->mutex = NULL;
    }
    if (queue->buffer) {
        lingxin_free(queue->buffer);
        queue->buffer = NULL;
    }
    if (queue->name) {
        lingxin_free(queue->name);
        queue->name = NULL;
    }
    lingxin_free(queue);
    
    lingxin_log_debug("[%s] ring_queue destroyed", name);
}