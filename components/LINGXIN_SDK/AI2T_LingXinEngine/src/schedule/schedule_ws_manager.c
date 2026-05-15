#include "schedule_first_ws.h"
#include "schedule_ws_manager.h"
#include "chat_state_machine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lingxin_common.h"
#include "lingxin_log.h"
#include "lingxin_memory.h"
#include "lingxin_thread.h"
// #define LINGXIN_SCHEDULE_LOOP

#ifdef LINGXIN_SCHEDULE_LOOP
#define LINGXIN_SCHEDULE_LOOP_INTERVAL 15 * 1000 // 15s
static lingxin_tid_t schedule_loop_thread_id = 0;
#endif

static bool isFirstExec = true;

static void systemEventListener(ScheduleChatHandler *globalHandler, const char *data)
{
    lingxin_log_debug("-----SCHEDULECHAT_EVENT_ON_SYSTEM_EVENT-----%s", data);
    state_machine_receive_schedule_data((void *)data);
}

static void doCreate()
{
    startFirstScheduleConnect(systemEventListener);
}

#ifdef LINGXIN_SCHEDULE_LOOP
static void* schedule_reconnect_task(void *arg)
{
    while (1)
    {
        lingxin_thread_sleep(LINGXIN_SCHEDULE_LOOP_INTERVAL);
        doCreate();
    }
    return NULL;
}
#endif

void initScheduleChat()
{
    if (isFirstExec)
    {
        isFirstExec = false;
        doCreate();
#ifdef LINGXIN_SCHEDULE_LOOP
        // 创建轮询线程
        lingxin_thread_param_t thread_param = {
            .priority = 3,
            .stack_size = 4096,
            .name = "sche_loop",
        };
        int ret = lingxin_thread_create(&schedule_loop_thread_id, &thread_param, schedule_reconnect_task, NULL);
        if (ret != 0) {
            lingxin_log_error("Failed to create schedule reconnect thread");
        }
#endif
    }
}