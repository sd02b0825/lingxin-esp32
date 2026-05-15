#ifndef __LINGXIN_SEMAPHORE_H__
#define __LINGXIN_SEMAPHORE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * 信号量句柄
 */
typedef void *lingxin_semaphore_t;

/**
 * 创建信号量
 * @param cnt 信号量初始值
 */
lingxin_semaphore_t lingxin_semaphore_create(uint32_t cnt);

/**
 * 等待信号量
 * @param sem 信号量句柄
 * @param timeout_ms 等待超时时间，单位为毫秒
 */
void lingxin_semaphore_pend(lingxin_semaphore_t sem, uint32_t timeout_ms);

/**
 * 发送信号量
 * @param sem 信号量句柄
 */
void lingxin_semaphore_post(lingxin_semaphore_t sem);

/**
 * 给信号量设值，用于清零信号量
 * @param sem 信号量句柄
 */
void lingxin_semaphore_set_value(lingxin_semaphore_t sem, uint32_t cnt);

/**
 * 销毁信号量
 * @param sem 信号量句柄
 */
void lingxin_semaphore_destroy(lingxin_semaphore_t sem);

#ifdef __cplusplus
}
#endif

#endif /* __LINGXIN_SEMAPHORE_H__ */