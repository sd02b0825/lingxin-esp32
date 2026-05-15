#include "chat_state_machine.h"
#include "lingxin_timer.h"
#include "schedule_ws_manager.h"
#include "schedule_timer_manager.h"
#include "lingxin_log.h"
#include "lingxin_mutex.h"
#include "lingxin_semaphore.h"
#include "lingxin_thread.h"
#include "lingxin_json_util.h"
#include <stdlib.h>
#include <string.h>
#include "lingxin_chat_api_inner.h"
#include "lingxin_memory.h"
#include "lingxin_queue.h"

typedef struct
{
    char *taskId;   // 任务唯一ID
    int timerId;    // 定时器ID（由系统分配）
    long countdown; // 倒计时时间（单位：毫秒或秒）
} TimerTask;

#define MAX_TIMER_TASKS 20
TimerTask timerTasks[MAX_TIMER_TASKS]; // 存储所有定时任务
int taskCount = 0;                     // 当前任务数量

// 定时任务模块初始化相关
static int is_schedule_task_on = 0;     // 定时任务是否开启
static bool is_task_emit_error = false; // 定时任务是否触发失败,是否接收到error消息
static lingxin_mutex_t task_emit_error_mutex = NULL;

static bool is_update_thread_initialized = false;
static bool is_trigger_thread_initialized = false;
static lingxin_tid_t timer_update_thread_id = 0;
static lingxin_tid_t timer_trigger_thread_id = 0;
static void init_timer_update_thread();
static void init_timer_trigger_thread();

// 适配层代码包装
static void inner_delete_schedule_timer(int timerId);

// 定时任务同步设置链路
static void free_system_event_str(void *ptr);
void initTimerTaskList(char *scheduleStr);        // sys_event消息队列入队
static void *timer_update_thread_entry(void *arg); // sys_event消息队列出队
#define MAX_SYS_EVENT 5
static ring_queue_t schedule_str_queue = NULL;
static lingxin_semaphore_t timer_update_sem = NULL;
// 出队具体操作：更新定时器列表
static void update_timer_list(TaskItem sync_task_list[], int task_num, int advance_connect_time);
static void clear_timer_list();
static bool create_timer(TimerTask *timerTask, int advance_connect_time);

// 定时任务触发链路
static void free_timer_trigger_task(void *args);
static void timer_callback(void *priv); // 定时任务触发队列入队
static void *timer_trigger_thread_entry(); // 定时任务触发队列出队
#define MAX_TRIGGER_QUEUE_SIZE 10
static ring_queue_t timer_trigger_queue = NULL;
static lingxin_semaphore_t timer_trigger_sem = NULL;
// 出队具体操作：定时器触发时与状态机交互
static void timer_trigger_func(void *timer_task); 

/********************定时任务模块对外暴露时机 ********************/
void novoice_user_custom_listener(StateEvent event)
{
    switch (event)
    {
    case State_Event_NoVoice_TerminateEnd:
    {
        lingxin_log_ut(LINGXIN_DEBUG, "schedule_task_before_trigger");
        // 用户在开启NoVoice循环前注入的自定义action(关闭tts/asr等)
        lingxin_emit_chat_event(CHAT_LIFE_CYCLE_EVENT_SCHEDULE_EMIT, NULL);
        lingxin_log_ut(LINGXIN_DEBUG, "schedule_task_before_trigger_finish");
        break;
    }
    default:
    {
        break;
    }
    }
}

void recieve_schedule_task_error()
{
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_task_recv_trigger_error");
    lingxin_mutex_lock(task_emit_error_mutex);
    is_task_emit_error = true;
    lingxin_mutex_unlock(task_emit_error_mutex);
}

/********************定时任务模块适配层方法包装 ********************/
static void inner_delete_schedule_timer(int timerId)
{
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_task_delete_begin");
    // 删除定时器
    if (lingxin_one_shot_timer_delete(timerId) == 0)
    {
        lingxin_log_ut(LINGXIN_DEBUG, "schedule_task_delete_finished");
    }
    else
    {
        lingxin_log_ut(LINGXIN_ERROR, "schedule_task_delete_failed");
    }
}


/******************** 定时器更新同步队列链路 ********************/
// 释放字符串队列中的字符串
static void free_system_event_str(void *ptr)
{
    lingxin_free(ptr);
}

// 入队
void initTimerTaskList(char *scheduleStr)
{
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_list_update_begin");
    // 0. 同步定时任务须满足的配置
    if (!is_schedule_task_on)
    {
        // 0-1. 端侧定时任务是否开启
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_list_update_failed", "schedule task not open");
        return;
    }
    if (!is_update_thread_initialized || !is_trigger_thread_initialized)
    {
        // 0-2. 定时任务相关线程是否初始化完成
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_list_update_failed", "thread init not ready");
        return;
    }
    if (!timer_trigger_queue)
    {
        // 0-3. 定时任务相关线程是否初始化完成
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_list_update_failed", "timer_trigger_queue not ready");
        return;
    }
    if (!schedule_str_queue)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_list_update_failed", "schedule_str_queue not ready");
        return;
    }

    lingxin_mutex_lock(task_emit_error_mutex);
    bool should_emit = is_task_emit_error;
    if (should_emit)
    {
        is_task_emit_error = false;
    }
    lingxin_mutex_unlock(task_emit_error_mutex);

    // 1. 控制在接收到error后的system_event之后才向状态机发送事件拉起voice循环
    if (should_emit)
    {
        state_machine_run_event(State_Event_NoVoice_Error);
    }

    // 2. 拷贝字符串并入队（解析移到出队后）
    char *str_copy = lingxin_strdup(scheduleStr);
    if (!str_copy)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_list_update_failed", "strdup failed");
        return;
    }
    if (ring_queue_enqueue(schedule_str_queue, str_copy))
    {
        lingxin_semaphore_post(timer_update_sem);
    }
    else
    {
        lingxin_log_error("schedule_str enqueue failed");
        lingxin_free(str_copy);
    }

    lingxin_log_debug("initTimerTaskList finish");
}
// 出队
static void *timer_update_thread_entry(void *arg)
{
    while (1)
    {
        lingxin_log_debug("定时器设置线程 before pend");
        lingxin_semaphore_pend(timer_update_sem, 0);
        lingxin_log_debug("定时器设置线程 after pend");
        char *scheduleStr = (char *)ring_queue_dequeue(schedule_str_queue);
        if (scheduleStr)
        {
            lingxin_log_ut(LINGXIN_DEBUG, "schedule_list_parse_begin");

            // 在单一线程中解析
            ScheduleTaskList parseRes = {0};
            if (parseScheduleTaskList(scheduleStr, &parseRes) == 0 && parseRes.taskCount > 0)
            {
                update_timer_list(parseRes.tasks, parseRes.taskCount, parseRes.advanceConnectTime);
            }

            if (parseRes.tasks)
            {
                for (int i = 0; i < parseRes.taskCount; i++)
                {
                    if (parseRes.tasks[i].taskId)
                    {
                        lingxin_free(parseRes.tasks[i].taskId);
                        parseRes.tasks[i].taskId = NULL;
                    }
                }
                lingxin_free(parseRes.tasks);
                parseRes.tasks = NULL;
            }
            parseRes.taskCount = 0;
            // 释放字符串
            lingxin_free(scheduleStr);
        }
        lingxin_log_ut(LINGXIN_DEBUG, "schedule_list_update_finished");
    }
    return NULL;
}

static void clear_timer_list() // 清空当前维护的计时器数组
{
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_list_clear_begin");
    int cleared_count = 0;
    for (int i = 0; i < taskCount && i < MAX_TIMER_TASKS; i++)
    {
        if (timerTasks[i].timerId != INVALID_TIMER_ID)
        {
            inner_delete_schedule_timer(timerTasks[i].timerId);
        }
        // ✅ 释放 taskId 内存
        if (timerTasks[i].taskId)
        {
            lingxin_free(timerTasks[i].taskId);
            timerTasks[i].taskId = NULL;
        }
        // 重置结构体内容
        timerTasks[i].timerId = INVALID_TIMER_ID;
        timerTasks[i].countdown = 0;
        cleared_count++;
    }
    taskCount = 0;
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "schedule_list_clear_finished", "cleared %d tasks, current task count is %d",
                             cleared_count, taskCount);
}

static bool create_timer(TimerTask *timerTask, int advance_connect_time)
{
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_task_set_begin");
    timerTask->timerId = lingxin_one_shot_timer_create(timerTask, timer_callback, timerTask->countdown * 1000);
    if (timerTask->timerId == INVALID_TIMER_ID)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_task_set_failed", "invalid timerId");
        // 如果 taskId 分配了内存，但注册失败，应立即释放
        if (timerTask->taskId)
        {
            lingxin_free(timerTask->taskId);
            timerTask->taskId = NULL;
        }
        return false;
    }
    taskCount++;
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "schedule_task_set_success", "taskId is %s, timerId is %d, finished task count is %d", timerTask->taskId, timerTask->timerId, taskCount);
    return true;
}

static void update_timer_list(TaskItem sync_task_list[], int task_num, int advance_connect_time)
{
    clear_timer_list();
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "schedule_list_sync_begin", "current parsed task total count is %d, advanced time is %d", task_num, advance_connect_time);

    // 根据传入的任务列表重新设置定时器
    for (int i = 0; i < task_num && i < MAX_TIMER_TASKS; i++)
    {
        lingxin_log_debug("定时任务列表添加任务 %d,任务id为%s，倒计时为%d", i, sync_task_list[i].taskId, sync_task_list[i].countdown);
        TaskItem *task = &sync_task_list[i];
        TimerTask *timer = &timerTasks[i];
        timer->taskId = task->taskId ? lingxin_strdup(task->taskId) : NULL;
        int countdownSec = task->countdown - advance_connect_time;
        timer->countdown = (countdownSec > 0) ? (long)countdownSec : 0;
    }
    // 基本属性设置完后再设置定时器
    for (int i = 0; i < task_num && i < MAX_TIMER_TASKS; i++)
    {
        TimerTask *timer = &timerTasks[i];
        create_timer(timer, advance_connect_time); // 设置软件定时器
    }
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_list_sync_finished");
}

/******************** 定时器触发队列链路 ********************/
static void free_timer_trigger_task(void *args)
{
    TimerTask *task = (TimerTask *)args;
    if (task)
    {
        if (task->taskId)
        {
            lingxin_free(task->taskId);
            task->taskId = NULL;
        }
        lingxin_free(task);
        task = NULL;
    }
}

// 去重匹配函数
static bool timer_task_match(const void *item, const void *ctx)
{
    const TimerTask *queue_task = (const TimerTask *)item;
    const TimerTask *target = (const TimerTask *)ctx;

    if (!queue_task || !target)
    {
        return false;
    }

    // 检查 timerId 是否相同
    if (queue_task->timerId == target->timerId)
    {
        return true;
    }

    // 检查 taskId 是否相同（两个都非空且相等）
    if (target->taskId && queue_task->taskId &&
        strcmp(target->taskId, queue_task->taskId) == 0)
    {
        return true;
    }
    return false;
}
// 入队
static void timer_callback(void *priv)
{
    TimerTask *timer_task = (TimerTask *)priv;
    if (!timer_task || timer_task->timerId == INVALID_TIMER_ID || timer_task->taskId == NULL)
    {
        return;
    }

    // 去重检查：使用 ring_queue_find
    if (ring_queue_find(timer_trigger_queue, timer_task_match, timer_task))
    {
        lingxin_log_debug("Timer task with timerId %d or taskId %s already exists in queue, skipping",
                          timer_task->timerId, timer_task->taskId);
        return;
    }

    TimerTask *new_task = (TimerTask *)lingxin_calloc(1, sizeof(TimerTask));
    if (!new_task)
    {
        lingxin_log_error("Failed to allocate memory for timer_trigger_queue task");
        goto enqueue_fail;
    }
    new_task->timerId = timer_task->timerId;
    new_task->countdown = timer_task->countdown;
    new_task->taskId = timer_task->taskId ? lingxin_strdup(timer_task->taskId) : NULL;
    if (!ring_queue_enqueue(timer_trigger_queue, new_task))
    {
        // 入队失败
        lingxin_log_debug("timer_trigger_queue is full, discarding new task");
        goto enqueue_fail;
    }
    else
    {
        // 入队成功
        lingxin_semaphore_post(timer_trigger_sem);
    }
    return;

enqueue_fail:
    free_timer_trigger_task(new_task);
    return;
}
// 出队
static void *timer_trigger_thread_entry()
{
    while (1)
    {
        lingxin_log_debug("timer_trigger_thread_entry before pend");
        lingxin_semaphore_pend(timer_trigger_sem, 0);
        lingxin_log_debug("timer_trigger_thread_entry after pend");
        TimerTask *timer_task = (TimerTask *)ring_queue_dequeue(timer_trigger_queue);
        if (timer_task != NULL)
        {
            timer_trigger_func(timer_task);
            free_timer_trigger_task(timer_task);
        }

        lingxin_log_debug("timer_trigger_thread_entry finish");
    }
    return NULL;
}
// 出队具体操作
static void timer_trigger_func(void *timer_task)
{
    TimerTask *timer_task_info = (TimerTask *)timer_task;
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "schedule_task_callback_begin", " dequeue trigger timerId: %d, trigger taskId: %s, countdown: %d", timer_task_info->timerId, timer_task_info->taskId, timer_task_info->countdown);

    if (!timer_task_info->taskId)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_task_callback_failed", "taskId is null");
        lingxin_log_debug("当前定时任务id为空%d", timer_task_info->taskId);
        return;
    }
    if (timer_task_info->timerId == INVALID_TIMER_ID)
    {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "schedule_task_callback_failed", "timerId is invalid");
        lingxin_log_debug("当前定时任务倒计时id为无效timerId\n");
        return;
    }
    // 定时器触发后的处理逻辑
    inner_delete_schedule_timer(timer_task_info->timerId);
    lingxin_log_ut(LINGXIN_DEBUG, "schedule_task_trigger_success");
    ScheduleTimerPayload schedule_trigger_payload = {0};
    if (timer_task_info->taskId != NULL)
    {
        schedule_trigger_payload.schedule_task_id = lingxin_strdup(timer_task_info->taskId);
        if (schedule_trigger_payload.schedule_task_id == NULL)
        {
            lingxin_log_error("scheduleTaskId内存分配失败");
        }
    }
    schedule_trigger_payload.input_mode = "no_voice";
    StateEventPayload payload = {
        .schedule_timer_payload = &schedule_trigger_payload};
    state_machine_run_event_with_payload(State_Event_NoVoice_Start, &payload);
    if(schedule_trigger_payload.schedule_task_id) {
        lingxin_free(schedule_trigger_payload.schedule_task_id);
        schedule_trigger_payload.schedule_task_id = NULL;
    }
    
}

/******************** 定时任务模块初始化 ********************/
// 定时器触发线程初始化
static void init_timer_trigger_thread()
{
    // 初始化定时器触发队列（使用 ring_queue）
    if (timer_trigger_queue == NULL)
    {
        ring_queue_config_t cfg = {
            .name = "timer_trigger",
            .capacity = MAX_TRIGGER_QUEUE_SIZE,
            .overwrite = false,                // 不覆盖，满了丢弃新任务
            .free_fn = free_timer_trigger_task // 释放 TimerTask
        };
        timer_trigger_queue = ring_queue_create(&cfg);
        if (timer_trigger_queue == NULL)
        {
            lingxin_log_error("Failed to create timer_trigger_queue");
        }
    }
    if (timer_trigger_sem == NULL)
    {
        timer_trigger_sem = lingxin_semaphore_create(0);
    }
    // 创建定时任务触发线程
    lingxin_thread_param_t thread_param = {
        .priority = 16,
        .stack_size = 4096,
        .name = "timer_trigger",
    };
    int ret = lingxin_thread_create(&timer_trigger_thread_id, &thread_param, timer_trigger_thread_entry, NULL);
    if (ret == 0)
    { // 检查线程创建是否成功
        lingxin_log_debug("线程timer_trigger创建成功，PID: %d", timer_trigger_thread_id);
        is_trigger_thread_initialized = true;
    }
    else
    {
        lingxin_log_error("Error: 线程timer_trigger创建失败，错误码: %d", ret);
        timer_trigger_thread_id = 0;
        is_trigger_thread_initialized = false;
    }
}

// 定时任务更新线程初始化
static void init_timer_update_thread() // 定时器更新线程初始化
{
    // 初始化字符串队列（使用 ring_queue）
    if (schedule_str_queue == NULL)
    {
        ring_queue_config_t cfg = {
            .name = "schedule_str",
            .capacity = MAX_SYS_EVENT,
            .overwrite = true,               // 覆盖模式
            .free_fn = free_system_event_str // 覆盖时自动释放字符串
        };
        schedule_str_queue = ring_queue_create(&cfg);
        if (schedule_str_queue == NULL)
        {
            lingxin_log_error("schedule_str_queue create failed");
        }
    }
    if (timer_update_sem == NULL)
    {
        timer_update_sem = lingxin_semaphore_create(0);
    }
    // 创建处理线程
    lingxin_thread_param_t thread_param = {
        .priority = 16,
        .stack_size = 2048 * 2,
        .name = "task_update",
    };
    int ret = lingxin_thread_create(&timer_update_thread_id, &thread_param, timer_update_thread_entry, NULL);
    if (ret == 0)
    {
        lingxin_log_debug("任务处理线程创建成功，PID: %d", timer_update_thread_id);
        is_update_thread_initialized = true; // 标记线程初始化完成
    }
    else
    {
        lingxin_log_error("任务处理线程创建失败，错误码: %d", ret);
        timer_update_thread_id = 0;
        is_update_thread_initialized = false;
    }
}

void module_schedule_init()
{
    is_schedule_task_on = 1;
    if (task_emit_error_mutex == NULL)
    {
        task_emit_error_mutex = lingxin_mutex_create();
    }
    init_timer_update_thread();
    init_timer_trigger_thread();
    initScheduleChat();
}