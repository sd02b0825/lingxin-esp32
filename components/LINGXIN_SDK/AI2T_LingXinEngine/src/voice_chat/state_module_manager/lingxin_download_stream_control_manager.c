#include "lingxin_download_stream_control_manager.h"
#include "lingxin_log.h"
#include "lingxin_semaphore.h"
#include <stdio.h>
#include <stdbool.h>

// 使用 lingxin_semaphore_t 类型替代 OS_SEM
static lingxin_semaphore_t w_sem = NULL;
static bool is_sem_initialized = false;

/**
 * 信号量控制
 * 使用方法：
 *          用于控制服务端数据推送，当执行 pend 之后，服务端无法再发送webSocket指令回来
 *          执行 post 之后服务端即可发送webSocket指令
 * 使用场景：
 *          控制数据，在首次初始化流式播放时候，由于初始化线程可能稍微比较耗时，需要在流式播放模块初始化结束之后，才可以让服务端发送mp3数据
 */

// 信号量删除
void lingxin_websocket_control_del(void)
{
    if (is_sem_initialized && w_sem != NULL) {
        lingxin_semaphore_destroy(w_sem);
        w_sem = NULL;
        is_sem_initialized = false;
    }
}

// 信号量创建
void lingxin_websocket_control_create(void)
{
    if (!is_sem_initialized) {
        w_sem = lingxin_semaphore_create(0); // 初始计数为 0
        if (w_sem != NULL) {
            is_sem_initialized = true;
        } else {
            lingxin_log_error("Failed to create websocket control semaphore");
        }
    }
}

void lingxin_unlock_write_websocket_controle(void)
{
    lingxin_log_debug("chat内核 zzz: 打开写");
    if (w_sem != NULL) {
        // 先将信号量值设为 0（确保状态干净），然后 post 使其变为 1，允许通过 pend
        lingxin_semaphore_set_value(w_sem, 0);
        lingxin_semaphore_post(w_sem);
    }
}

void lingxin_lock_write_websocket_control(void)
{
    lingxin_log_debug("chat内核 zzz: 阻止写");
    if (w_sem != NULL) {
        // 永久等待（timeout_ms = 0 表示无限等待？注意：需确认你的 os_sem_pend 实现）
        // 根据你原来的 os_sem_pend(&w_sem, 0)，这里传 0
        // 但注意：在你的封装中 timeout_ms / 10，所以传 0 就是 0 ticks（可能立即返回？）
        // 如果原意是“永久阻塞”，应传一个极大值，比如 UINT32_MAX
        // 不过先按原逻辑：传 0
        lingxin_semaphore_pend(w_sem, 0);
    }
}
