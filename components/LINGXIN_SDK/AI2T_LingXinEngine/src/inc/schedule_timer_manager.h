#ifndef SCHEDULE_TIMER_MANAGER_H
#define SCHEDULE_TIMER_MANAGER_H
#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>
#include "chat_state_machine_event.h"

    typedef struct
    {
        int countdown;
        char *taskId;
    } TaskItem;
    typedef struct
    {
        TaskItem *tasks;
        int taskCount;
        int advanceConnectTime;
    } ScheduleTaskList;

    void recieve_schedule_task_error();
    void initTimerTaskList(char *scheduleStr);
    void module_schedule_init();
    void novoice_user_custom_listener(StateEvent event);
#ifdef __cplusplus
}
#endif
#endif