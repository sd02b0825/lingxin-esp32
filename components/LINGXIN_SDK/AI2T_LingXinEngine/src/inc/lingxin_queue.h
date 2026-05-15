#ifndef RING_QUEUE_H
#define RING_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 队列句柄
typedef void *ring_queue_t;

// 数据释放回调函数类型
typedef void (*ring_queue_free_fn_t)(void *item);

// 元素匹配回调函数类型（用于查找）
typedef bool (*ring_queue_match_fn_t)(const void *item, const void *ctx);

// 配置结构体
typedef struct {
    const char *name;              // 队列名称
    uint32_t capacity;             // 容量（必填）
    bool overwrite;                // 覆盖模式
    ring_queue_free_fn_t free_fn;  // 元素释放函数
    // 后续扩展...
    // uint32_t priority;          // 队列优先级
    // bool use_static_buffer;     // 使用静态缓冲区
    // void *static_buffer;        // 外部缓冲区
} ring_queue_config_t;

// 默认配置宏
#define RING_QUEUE_DEFAULT_CONFIG { \
    .name = NULL, \
    .capacity = 0, \
    .overwrite = false, \
    .free_fn = NULL \
}

/**
 * @brief 创建环形队列
 * @param config 配置结构体指针，不能为NULL
 * @return 队列句柄，失败返回NULL
 * @note config->capacity 必须大于0，否则创建失败
 */
ring_queue_t ring_queue_create(const ring_queue_config_t *config);

/**
 * @brief 获取队列名称
 * @param handle 队列句柄
 * @return 队列名称，失败返回NULL
 */
const char *ring_queue_get_name(ring_queue_t handle);

/**
 * @brief 入队
 * @param handle 队列句柄
 * @param item 数据指针
 * @return 成功返回true
 * @note 覆盖模式下，最旧元素会被自动释放（如果创建时指定了free_fn）
 */
bool ring_queue_enqueue(ring_queue_t handle, void *item);

/**
 * @brief 出队
 * @param handle 队列句柄
 * @return 数据指针，空队列返回NULL
 * @note 返回的指针由调用方负责释放（如果创建时free_fn为NULL，或需要提前释放）
 */
void *ring_queue_dequeue(ring_queue_t handle);

/**
 * @brief 判断是否为空
 */
bool ring_queue_is_empty(ring_queue_t handle);

/**
 * @brief 判断是否已满（非覆盖模式下有用）
 */
bool ring_queue_is_full(ring_queue_t handle);

/**
 * @brief 获取当前元素数
 */
uint32_t ring_queue_count(ring_queue_t handle);

/**
 * @brief 查找队列中是否存在匹配的元素
 * @param handle 队列句柄
 * @param match_fn 匹配函数，返回true表示匹配
 * @param ctx 传递给匹配函数的上下文
 * @return 找到返回true，未找到或参数错误返回false
 */
bool ring_queue_find(ring_queue_t handle, ring_queue_match_fn_t match_fn, const void *ctx);

/**
 * @brief 清空队列
 * @param handle 队列句柄
 * @note 所有元素会被自动释放（如果创建时指定了free_fn）
 */
void ring_queue_clear(ring_queue_t handle);

/**
 * @brief 销毁队列
 * @param handle 队列句柄
 * @note 剩余元素会被自动释放（如果创建时指定了free_fn）
 */
void ring_queue_destroy(ring_queue_t handle);

#ifdef __cplusplus
}
#endif

#endif // RING_QUEUE_H