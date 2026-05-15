// recorder_adapter.c
#include "chat_state_machine.h"
#include "chat_runtime_context.h"
#include "lingxin_log.h"
#include "upload_record_interface.h"

#include "schedule_timer_manager.h"

static void onChatEvent(ChatEventType event, const char *data, const size_t len);

static void onChatEvent(ChatEventType event, const char *data, const size_t len)
{
  switch (event)
  {
  case CHAT_EVENT_ON_AI_READY:
    state_machine_run_event_with_payload(State_Event_Upload_CloseEnd, NULL);
    break;
  case CHAT_EVENT_ON_ERROR:
    // 通知定时任务模块定时任务触发失败
    recieve_schedule_task_error();
    break;
  default:
    break;
  }
}

static void voice_chat_continue()
{

  ChatStartNewParams voice_chat_continue_params = {0};
  voice_chat_continue_params.taskId = "";
  voice_chat_continue_params.task = "";
  voice_chat_continue_params.input_mode = "";
  voice_chat_continue_params.output_mode = "";
  voice_chat_continue_params.user_input = "";
  voice_chat_continue_params.scheduleTaskId = "";

  // 三个参数 char* input_mode;  char *output_mode;
  // 默认值
  voice_chat_continue_params.output_mode = "voice";
  voice_chat_continue_params.input_mode = "no_voice";

  voice_chat_continue_params.task = "chat_vad";

  if (get_current_context() != NULL)
  {
    if (get_current_context()->has_current_schedule_id)
    {
      voice_chat_continue_params.scheduleTaskId = get_current_context()->current_schedule_id;
      voice_chat_continue_params.is_schedule_timer_task = true;
    }

    if (get_current_context()->has_current_user_input)
    {
      voice_chat_continue_params.user_input = get_current_context()->current_user_input;
    }

    if (get_current_context()->has_current_task_id)
    {
      voice_chat_continue_params.taskId = get_current_context()->current_task_id;
    }

    if (get_current_context()->has_play_prologue)
    {
      voice_chat_continue_params.play_prologue = get_current_context()->play_prologue;
    }

    // 如果上下文中有设置 output_mode 则使用上下文的值
    if (get_current_context()->has_output_mode)
    {
      voice_chat_continue_params.output_mode = get_current_context()->output_mode;
    }

    // 如果上下文中有设置 input_mode 则使用上下文的值
    if (get_current_context()->has_input_mode)
    {
      voice_chat_continue_params.input_mode = get_current_context()->input_mode;
    }

    print_chat_context(get_current_context());
  }

  lingxin_log_debug("纯 text 模块任务 voice_chat_continue_params.taskId:%s, voice_chat_continue_params.task:%s voice_chat_continue_params.user_input:%s, voice_chat_continue_params.scheduleTaskId:%s", voice_chat_continue_params.taskId, voice_chat_continue_params.task, voice_chat_continue_params.user_input, voice_chat_continue_params.scheduleTaskId);

  voice_chat_start_new(&voice_chat_continue_params);
}

static int schedule_upload_manager_init()
{
  // 初始化录音器模块
  lingxin_log_debug("定时任务模块 上行初始化成功");
  add_protocol_event_listener(onChatEvent);
  state_machine_run_event_with_payload(State_Event_Upload_InitEnd, NULL);

  return 1;
}
static void send_start()
{
  voice_chat_continue();
}

static void schdule_upload_manager_destory(void)
{
  remove_protocol_event_listener(onChatEvent);
}

const UploadModuleInterface g_scheduleUploadManager = {
    .upload_init = schedule_upload_manager_init,
    .upload_start = send_start,
    .upload_terminate = NULL,
    .upload_destory = schdule_upload_manager_destory};
