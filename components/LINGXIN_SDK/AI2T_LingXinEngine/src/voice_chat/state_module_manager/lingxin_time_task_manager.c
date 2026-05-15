#include "lingxin_timer.h"
#include "lingxin_time_task_manager.h"
#include "lingxin_log.h"
#include "chat_api.h"


static int timer_id = INVALID_TIMER_ID;

bool init_lingxin_chat_timer(void *priv, void (*func)(void *priv)) {
    lingxin_log_debug("%s 开始初始化, time_id: %d", __func__, timer_id);
    if (timer_id != INVALID_TIMER_ID) {
        return false;
    }
    
    timer_id = lingxin_sys_timer_add(priv, func, 10000); // 设置10秒后执行定时任务

    lingxin_log_debug("%s 定时器初始化, time_id: %d", __func__, timer_id);

    return true;
}

bool delete_lingxin_chat_timer() {
   if (timer_id != INVALID_TIMER_ID) {
        lingxin_log_debug("%s 定时器开始删除，timer_id: %d", __func__, timer_id);
        lingxin_sys_timer_del(timer_id);
        timer_id = INVALID_TIMER_ID;
        lingxin_log_debug("%s 定时器删除成功，timer_id: %d", __func__, timer_id);

        return true;
    }
    else {
        lingxin_log_debug("%s 定时器未初始化，无法删除，timer_id: %d", __func__, timer_id);

        return false;
    }
}

bool reset_lingxin_chat_timer_run() {
    if (timer_id == INVALID_TIMER_ID) {
        lingxin_log_debug("定时器未初始化，无法重置， time_id: %d", timer_id);
        return false;
    }
    lingxin_log_debug("%s重置定时器时间， time_id: %d", __func__, timer_id);
    lingxin_sys_timer_re_run(timer_id); // 重置定时器

    return true;
}
