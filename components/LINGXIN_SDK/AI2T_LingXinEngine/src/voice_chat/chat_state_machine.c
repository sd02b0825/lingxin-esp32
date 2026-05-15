#include "chat_state_machine.h"
#include "schedule_timer_manager.h"
#include "lingxin_semaphore.h"
#include "lingxin_common.h"
#include <stddef.h>
#include <stdio.h>
#include "lingxin_log.h"

#include "lingxin_mutex.h"

// 状态的策略类
#include "state_task_complete_stategies.h"
#include "state_task_exit_strategies.h"
#include "state_task_download_init_strategies.h"
#include "state_task_download_transfer_strategies.h"
#include "state_task_download_end_strategies.h"
// 添加上传策略头文件包含
#include "state_task_upload_transfer_strategies.h"

// 引入模块管理类
#include "chat_runtime_context.h"
#include "state_download_manager.h"
#include "lingxin_download_stream_control_manager.h"
#include "state_upload_manager.h"

#define chat_log_key "chat_state_machine"

#define chat_exit_log_key "chat_exit_log_key"

#define chat_terminate_log_key "chat_terminate_log_key"

static int module_timeout_need_callback = 0;

static lingxin_mutex_t lingxin_config_get_state_mutex = NULL;

static lingxin_mutex_t lingxin_config_terminate_state_mutex = NULL;

static ChatState chat_current_state = State_Idle;
static ChatState chat_last_state = State_Idle;
/**
 * State_Terminate（唤醒打断）阶段的中间状态。
 * 用于跟踪录音模块和流式播放模块是否已完成打断，
 * 两者均完成后才进入下一步（播放打断提示音或重启录音）。
 */
typedef struct
{
  bool record_terminated;      // 上行录音模块是否已打断完成
  bool buffer_play_terminated; // 下行流式播放模块是否已打断完成
  bool record_has_inited;      // 录音模块是否已完成初始化（打断后重新初始化用）
} InnerStateForTerminate;
static InnerStateForTerminate inner_state_for_terminate = {false, false, false};

/**
 * State_NoVoice_Terminate（noVoice 打断）阶段的中间状态。
 * 用于跟踪录音模块和流式播放模块是否均已打断完成，
 * 两者完成后向服务端发送打断指令，随后进入新一轮 noVoice 循环。
 */
typedef struct
{
  bool record_terminated;      // 上行录音模块是否已打断完成
  bool buffer_play_terminated; // 下行流式播放模块是否已打断完成
} InnerStateForNoVoiceTerminate;
static InnerStateForNoVoiceTerminate inner_state_for_novoice_terminate = {false, false};

/**
 * State_Terminate 打断后重启对话流程的中间状态。
 * 控制从"打断完成"到"重新开始上行录音传输"之间的多个异步步骤，
 * 需要：播放打断提示音完成、录音重新初始化完成、voice chat 打断/AI结束，
 * 全部就绪后才切换到 State_Upload_Transfer 并开始发送录音。
 */
typedef struct
{
  bool play_terminate_prompted;       // 打断提示音是否已播放完毕
  bool record_restarted;              // 录音模块是否已重新初始化完成
  bool voice_chat_terminated;         // voice chat 打断指令是否已收到服务端确认（TerminateEnd）
  bool voice_chat_task_ended;         // 服务端是否已推送 AIEnd（任务结束）
  bool voice_chat_seed_start;         // upload_manager_start 是否已发起（防止重复调用）
  bool voice_chat_terminate_has_seed; // 打断指令是否已发送（防止重复发送）
} InnerStateForTerminateRestart;
static InnerStateForTerminateRestart inner_state_for_terminate_restart = {false, false, false, false, false, false};

/**
 * State_NoVoice_Terminate 打断后重启 noVoice 循环的中间状态。
 * 等待 voice chat 打断完成后，切换到 State_NoVoice_Start 开启新一轮循环。
 */
typedef struct
{
  bool voice_chat_terminated;       // voice chat 打断是否已收到服务端确认（TerminateEnd）
  bool no_voice_terminate_end_fail; // 打断指令发送失败标记（需重试发送打断指令）
} InnerStateForNoVoiceTerminateRestart;
static InnerStateForNoVoiceTerminateRestart inner_state_for_novoice_terminate_restart = {false, false};

static InnerStateForExit inner_state_for_exit = {false, false, false, false, .is_normal_exit = true};

// 向用户暴露关闭tts/asr的时机
static void noVoiceTerminateEndCallback()
{
  lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "noVoiceTerminateEndCallback\n");
  state_machine_run_event(State_Event_NoVoice_TerminateEnd);
}

static void set_no_voice_chat_continue_params(StateEventPayload *payload)
{
  if (payload && payload->schedule_timer_payload)
  {
    lingxin_log_debug("当前传递的信息 schedule_timer_payload，scheduleTaskId：%s，input_mode:%s", payload->schedule_timer_payload->schedule_task_id, payload->schedule_timer_payload->input_mode);
    if (payload->schedule_timer_payload->schedule_task_id)
    {
      update_current_context(CTX_FIELD_CURRENT_SCHEDULE_ID, (ContextValue){.string = payload->schedule_timer_payload->schedule_task_id});
    }
  }
}

// 根据枚举值返回描述字符串
static inline const char *get_chat_state_description(ChatState state)
{
  switch (state)
  {
  case State_Idle:
    return "State_Idle  等待唤醒态";
  case State_Welcome:
    return "State_Welcome  欢迎语播放态";
  case State_Terminate:
    return "State_Terminate 打断状态";
  case State_Upload_Init:
    return "State_Upload_Init 新一轮对话开始，录音开始";
  case State_Upload_Transfer:
    return "State_Upload_Transfer 录音传输态";
  case State_Download_Init:
    return "State_Download_Init 流式播放开始态";
  case State_Download_Play:
    return "State_Download_Play 流式播放中";
  case State_Download_End:
    return "State_Download_End 下行结束";
  case State_Task_Complete:
    return "State_Task_Complete 结束一轮对话";
  case State_Exit:
    return "State_Exit 主动结束对话";
  case State_NoVoice_Start:
    return "State_NoVoice_Start 新一轮noVoice循环开始";
  case State_NoVoice_Terminate:
    return "State_NoVoice_Terminate 打断当前循环，开启新一轮noVoice循环";
  default:
    lingxin_log_warn("未知状态 %d", state);
    return "未知状态";
  }
}

static inline const char *get_wakeup_event_description(StateEvent event)
{
  switch (event)
  {
  case State_Event_Wakeup_Detected:
    return "State_Event_Wakeup_Detected 唤醒事件";
  case State_Event_NoVoice_Error:
    return "State_Event_NoVoice_Error noVoice下行阶段服务端推送error";
  case State_Event_NoVoice_Start:
    return "State_Event_NoVoice_Start 开启新一轮noVoice循环";
  case State_Event_Welcome_Play_End:
    return "State_Event_Welcome_Play_End 播放欢迎语结束事件";
  case State_Event_VoiceChat_TerminateEnd:
    return "State_Event_VoiceChat_TerminateEnd voice chat voice chat 打断成功事件";
  case State_Event_NoVoice_TerminateEnd:
    return "State_Event_NoVoice_TerminateEnd 拉起新一轮noVoice前打断成功事件";
  case State_Event_VoiceChat_AIEnd:
    return "State_Event_VoiceChat_AIEnd 服务端结束推音频流事件";
  case State_Event_Vad_Stop:
    return "State_Event_Vad_Stop vad 停止";
  case State_Event_Vad_Exit:
    return "State_Event_Vad_Exit 退出唤醒";
  case State_Event_BufferPlay_AudioInitEnd:
    return "State_Event_BufferPlay_AudioInitEnd 流式播放初始化结束";
  case State_Event_BufferPlay_PlayEnd:
    return "State_Event_BufferPlay_PlayEnd 流式播放结束";
  case State_Event_BufferPlay_TerminateEnd:
    return "State_Event_BufferPlay_TerminateEnd 流式播放打断后暂停";
  case State_Event_BufferPlay_Error:
    return "State_Event_BufferPlay_Error 流式播放模块出错";
  case State_Event_Upload_InitEnd:
    return "State_Event_Upload_InitEnd 上行模块初始化成功";
  case State_Event_Upload_CloseEnd:
    return "State_Event_Upload_CloseEnd 上行模块停止录音";
  case State_Event_Upload_TerminateEnd:
    return "State_Event_Upload_TerminateEnd 上行模块打断事件";
  case State_Event_VoiceChat_ExitEnd:
    return "State_Event_VoiceChat_ExitEnd voice chat引擎销毁成功";
  case State_Event_WillExit:
    return "State_Event_WillExit 主动退出对话事件";
  case State_Event_TerminatePrompt_PlayEnd:
    return "State_Event_TerminatePrompt_PlayEnd 打断唤醒提示音播放成功"; // 增加打断唤醒提示音
  case State_Event_ContinuePrompt_PlayEnd:
    return "State_Event_ContinuePrompt_PlayEnd 连续对话提示音播放成功"; // 增加连续对话提示音

  case Event_Inc_TaskComplete_PlayEnd:
    return "Event_Inc_TaskComplete_PlayEnd task complete 状态内部使用的事件";
  case Event_Inc_Download_End:
    return "Event_Inc_Download_End 下行状态结束"; //;   // 增加连续对话提示音
  case Event_Inc_TaskComplete_End:
    return "Event_Inc_TaskComplete_End task complete 状态内部使用的事件，task complete 逻辑已完成";
  default:
    lingxin_log_warn("未知事件 %d", event);
    return "未知事件";
  }
}

static ChatPhaseCode get_chat_phase_state(ChatState state)
{
  if (state == State_Idle)
  {
    return CHAT_PHASE_STANDBY;
  }
  else if (state == State_Welcome)
  {
    return CHAT_PHASE_STARTING;
  }
  else if (state == State_Upload_Init || state == State_Upload_Transfer)
  {
    return CHAT_PHASE_INPUTING; // 2. 输入中
  }
  else if (state == State_Download_Init || state == State_NoVoice_Start)
  {
    return CHAT_PHASE_THINKING; // 3. 思考中
  }
  else if (state == State_Download_Play || state == State_Task_Complete)
  {
    return CHAT_PHASE_OUTPUTING; // 4.输出中
  }
  else if (state == State_Terminate || state == State_NoVoice_Terminate)
  {
    return CHAT_PHASE_INTERRUPTING; // 5. 打断中
  }
  else if (state == State_Exit)
  {
    return CHAT_PHASE_EXITING; // 6. 退出中
  }
  else
  {
    return CHAT_PHASE_STANDBY;
  }
}

/**
 * 校验从当前状态到目标状态的转换是否合法
 * 使用表驱动方式，便于后续扩展新状态的校验规则
 *
 * 状态流转全景：
 *   Idle          → Welcome, Upload_Init, NoVoice_Start
 *   Welcome       → Upload_Init, Exit
 *   Upload_Init   → Upload_Transfer, Terminate, Exit
 *   Upload_Transfer → Download_Init, Task_Complete, Terminate, Exit
 *   Download_Init → Download_Play, Terminate, Exit
 *   Download_Play → Task_Complete, Download_End, Terminate, Exit
 *   Download_End  → Task_Complete, Exit
 *   Task_Complete → Idle, Upload_Init, Terminate, Exit
 *   Terminate     → Upload_Init, Upload_Transfer, Terminate, Exit, NoVoice_Start
 *   Exit          → Idle, Exit
 *   NoVoice_Start → Terminate, Exit, State_Download_Init
 *   NoVoice_Terminate → NoVoice_Start, Exit, Terminate
 */
static bool validate_state_transition(ChatState target_state, StateEvent event)
{
  typedef struct
  {
    ChatState source_state;
    const ChatState *allowed_targets;
    int allowed_count;
  } StateTransitionRule;

  /* ---- 每个源状态允许转换到的目标状态列表 ---- */

  static const ChatState idle_allowed[] = {
      State_Welcome, State_Upload_Init, State_NoVoice_Start};

  static const ChatState welcome_allowed[] = {
      State_Upload_Init, State_Exit};

  static const ChatState upload_init_allowed[] = {
      State_Upload_Transfer, State_Terminate, State_NoVoice_Terminate, State_Exit};

  static const ChatState upload_transfer_allowed[] = {
      State_Download_Init, State_Task_Complete, State_Terminate, State_NoVoice_Terminate, State_Exit};

  static const ChatState download_init_allowed[] = {
      State_Download_Play, State_Terminate, State_NoVoice_Terminate, State_Exit};

  static const ChatState download_play_allowed[] = {
      State_Task_Complete, State_Download_End, State_Terminate, State_NoVoice_Terminate, State_Exit};

  static const ChatState download_end_allowed[] = {
      State_Task_Complete, State_Exit};

  static const ChatState task_complete_allowed[] = {
      State_Idle, State_Upload_Init, State_Terminate, State_NoVoice_Terminate, State_Exit};

  static const ChatState terminate_allowed[] = {
      State_Upload_Init, State_Upload_Transfer, State_Terminate, State_NoVoice_Terminate, State_Exit, State_NoVoice_Start};

  static const ChatState exit_allowed[] = {
      State_Idle, State_Exit};

  static const ChatState novoice_start_allowed[] = {
      State_Download_Init, State_Terminate, State_NoVoice_Terminate, State_Exit};

  static const ChatState novoice_terminate_allowed[] = {
      State_NoVoice_Start, State_Terminate, State_Exit};

  /* ---- 状态转换规则表 ---- */

#define RULE(src, arr)                        \
  {                                           \
    src, arr, sizeof(arr) / sizeof(ChatState) \
  }

  static const StateTransitionRule transition_rules[] = {
      RULE(State_Idle, idle_allowed),
      RULE(State_Welcome, welcome_allowed),
      RULE(State_Upload_Init, upload_init_allowed),
      RULE(State_Upload_Transfer, upload_transfer_allowed),
      RULE(State_Download_Init, download_init_allowed),
      RULE(State_Download_Play, download_play_allowed),
      RULE(State_Download_End, download_end_allowed),
      RULE(State_Task_Complete, task_complete_allowed),
      RULE(State_Terminate, terminate_allowed),
      RULE(State_Exit, exit_allowed),
      RULE(State_NoVoice_Start, novoice_start_allowed),
      RULE(State_NoVoice_Terminate, novoice_terminate_allowed),
  };

#undef RULE

  static const int rules_count = sizeof(transition_rules) / sizeof(StateTransitionRule);

  for (int i = 0; i < rules_count; i++)
  {
    if (chat_current_state != transition_rules[i].source_state)
    {
      continue;
    }

    for (int j = 0; j < transition_rules[i].allowed_count; j++)
    {
      if (target_state == transition_rules[i].allowed_targets[j])
      {
        return true;
      }
    }

    lingxin_log_debug("非法状态切换，事件%s 当前是 %s 切换到 %s",
                      get_wakeup_event_description(event),
                      get_chat_state_description(chat_current_state),
                      get_chat_state_description(target_state));
    return false;
  }

  // 未在规则表中的源状态（理论上不应出现），记录警告并允许
  lingxin_log_warn("未配置转换规则的源状态 %s，目标 %s",
                   get_chat_state_description(chat_current_state),
                   get_chat_state_description(target_state));
  return true;
}

static void updateChatState(ChatState state, StateEvent event)
{
  if (chat_last_state == State_Download_Init && event != State_Event_BufferPlay_AudioInitEnd)
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "need unlock websocket controle");
    lingxin_unlock_write_websocket_controle();
  }
  chat_last_state = chat_current_state;

  chat_current_state = state;
  lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, " %s to %s, event: %s", get_chat_state_description(chat_last_state), get_chat_state_description(chat_current_state), get_wakeup_event_description(event));

  ChatPhaseChangePayload payload = {0};
  payload.phase_code = get_chat_phase_state(chat_current_state);
  lingxin_emit_chat_event(CHAT_LIFE_CYCLE_EVENT_CHAT_PHASE_CHANGE, &payload);
}

ChatState get_current_chat_state()
{
  return chat_current_state;
}

bool get_chat_state_terminate()
{
  return (chat_current_state == State_Terminate && !inner_state_for_terminate_restart.voice_chat_terminated) || chat_current_state == State_Exit || (chat_current_state == State_NoVoice_Terminate && !inner_state_for_novoice_terminate_restart.voice_chat_terminated);
}

static void on_global_chat_event(ChatEventType event, const char *data, const size_t len)
{
  switch (event)
  {
  case CHAT_EVENT_ON_SYSTEM_EVENT:
    lingxin_log_debug("-----SCHEDULECHAT_EVENT_ON_SYSTEM_EVENT-----%s", data);
    state_machine_receive_schedule_data((void *)data);
    break;
  default:
    break;
  }
}

/**
 * 新重构之后的接口
 */
// 初始化 machine
void voice_chat_machine_init(bool need_terminate_prompt, bool need_continue_prompt)
{
  add_protocol_event_listener(on_global_chat_event);
  if (lingxin_config_get_state_mutex == NULL)
  {
    lingxin_config_get_state_mutex = lingxin_mutex_create();
  }
  if (lingxin_config_terminate_state_mutex == NULL)
  {
    lingxin_config_terminate_state_mutex = lingxin_mutex_create();
  }
  ChatStateRuntimeContext default_cfg = {0};
  default_cfg.need_terminate_prompt = need_terminate_prompt;
  default_cfg.has_need_terminate_prompt = true;
  default_cfg.need_continue_prompt = need_continue_prompt;
  default_cfg.has_need_continue_prompt = true;
  init_chat_runtime_context(&default_cfg);
}
static void clear_all_inner_state()
{
  memset(&inner_state_for_terminate, 0, sizeof(InnerStateForTerminate));
  memset(&inner_state_for_terminate_restart, 0, sizeof(InnerStateForTerminateRestart));

  memset(&inner_state_for_novoice_terminate, 0, sizeof(InnerStateForNoVoiceTerminate));
  memset(&inner_state_for_novoice_terminate_restart, 0, sizeof(InnerStateForNoVoiceTerminateRestart));

  memset(&inner_state_for_exit, 0, sizeof(InnerStateForExit));
}
static void set_preset_inner_state(InnerStateCollection *preset_inner_state)
{
  if (preset_inner_state && preset_inner_state->inner_state_for_exit)
  {
    InnerStateForExit *source = preset_inner_state->inner_state_for_exit;

    inner_state_for_exit.voice_chat_exited = source->voice_chat_exited;
    inner_state_for_exit.voice_chat_terminated = source->voice_chat_terminated;
    inner_state_for_exit.record_terminated = source->record_terminated;
    inner_state_for_exit.buffer_play_terminated = source->buffer_play_terminated;
    inner_state_for_exit.is_normal_exit = source->is_normal_exit;

    // 同步更新 is_normal_exit 到运行时上下文
    if (source->is_normal_exit)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, " is_normal_exit: %d", true);
    }
    update_current_context(CTX_FIELD_IS_NORMAL_EXIT, (ContextValue){.boolean = source->is_normal_exit});

    lingxin_log_debug("set 退出的值 inner_state_for_exit for exit: voice_chat_exited=%d, voice_chat_terminated=%d, record_terminated=%d, buffer_play_terminated=%d, is_normal_exit=%d",
                      inner_state_for_exit.voice_chat_exited,
                      inner_state_for_exit.voice_chat_terminated,
                      inner_state_for_exit.record_terminated,
                      inner_state_for_exit.buffer_play_terminated,
                      inner_state_for_exit.is_normal_exit);
  }
  else
  {
    // 没有预设退出状态时，默认非主动退出
    update_current_context(CTX_FIELD_IS_NORMAL_EXIT, (ContextValue){.boolean = false});
  }
}

static void emit_exit_code(ExitCode tempCode)
{
  ExitPayload payload = {0};
  if (tempCode >= 0)
  {
    payload.exit_code = tempCode; // 使用退出时的错误信息
  }

  lingxin_emit_chat_event(CHAT_LIFE_CYCLE_EVENT_EXIT, &payload); // 添加结束回调
}

/**
 * 切换到 Idle 时清理所有运行时资源。
 *
 * 【重要】本函数会调用 destroy_chat_runtime_context() 销毁 current_config，
 * 调用后 get_current_context() 将返回 NULL。
 * 因此，任何需要从 current_config 中读取的数据（如 exit_code）
 * 必须在调用本函数之前完成提取。
 */
static void cleanup_on_enter_idle()
{
  module_timeout_need_callback = 0;
  delete_lingxin_chat_timer();
  lingxin_websocket_control_del();
  destroy_chat_runtime_context();
}

/**
 * 进入新状态后执行对应的入口动作
 */
static void execute_state_entry_action(ChatState state, InnerStateCollection *preset_inner_state, StateEvent event)
{
  switch (state)
  {
  case State_Idle:
    // exit_code 已在 turn_to_with_preset_inner_state 中 cleanup 之前发出，此处无需重复
    break;
  case State_Welcome:
    module_local_play_welcome_audio();
    break;

  case State_Upload_Init:
    if (get_current_context() != NULL)
    {
      upload_manager_init(get_current_context()->upload_type);
    }
    else
    {
      lingxin_log_debug("不需要录音，直接等待回复");
    }
    if (module_timeout_need_callback == 0)
    {
      upload_manager_start();
    }
    break;

  case State_NoVoice_Start:
    noVoiceTerminateEndCallback();
    update_current_context(CTX_FIELD_UPLOAD_TYPE, (ContextValue){.media_type = Media_Type_TextOnly});
    upload_manager_init(Media_Type_TextOnly);
    upload_manager_start();
    break;

  case State_Upload_Transfer:
    break;

  case State_NoVoice_Terminate:
  case State_Terminate:
    upload_manager_terminate();
    playback_manager_terminate();
    break;

  case State_Exit:
    if (get_current_context() != NULL && get_current_context()->is_vad_exit)
    {
      lingxin_log_debug("已经vad exit 退出逻辑");
      turn_to_exit(get_current_context(), preset_inner_state, event);
    }
    else
    {
      upload_manager_terminate();
      playback_manager_terminate();
    }
    break;

  case State_Task_Complete:
    turn_to_task_complete(get_current_context(), NULL);
    break;

  case State_Download_Init:
    turn_to_download_init(get_current_context(), NULL);
    break;

  case State_Download_End:
    turn_to_download_end(get_current_context(), NULL);
    break;

  default:
    break;
  }
}

void turn_to_with_preset_inner_state(ChatState state, StateEvent event, InnerStateCollection *preset_inner_state)
{
  // 加锁：保护状态切换的核心逻辑
  lingxin_mutex_lock(lingxin_config_get_state_mutex);
  lingxin_log_debug("lingxin_config_get_state_mutex 开始锁住，事件%s 当前是 %s 的时候，即将切换到 %s",
                    __func__, get_wakeup_event_description(event), get_chat_state_description(state));

  if (!validate_state_transition(state, event))
  {
    lingxin_mutex_unlock(lingxin_config_get_state_mutex);
    lingxin_log_debug("lingxin_config_get_state_mutex 结束锁住");
    return;
  }

  if (state == State_Idle)
  {
    /**
     * 【关键顺序】必须先提取 exit_code 再执行 cleanup。
     * cleanup_on_enter_idle() 内部会调用 destroy_chat_runtime_context()，
     * 销毁 current_config 后 get_current_context() 返回 NULL，
     * 届时 exit_code 将无法读取，会导致对外始终透出 0 (EXIT_REASON_USER_INITIATED)。
     */
    ExitCode exit_code = get_current_context() != NULL ? get_current_context()->exit_code : EXIT_REASON_USER_INITIATED;
    emit_exit_code(exit_code);

    cleanup_on_enter_idle();
  }

  clear_all_inner_state();
  set_preset_inner_state(preset_inner_state);
  updateChatState(state, event);

  if (chat_last_state == State_Terminate && state != State_Terminate)
  {
    // 去除 打断到打断的逻辑
    module_timeout_need_callback = 0;
  }

  lingxin_mutex_unlock(lingxin_config_get_state_mutex);
  lingxin_log_debug("lingxin_config_get_state_mutex 结束锁住");

  // 解锁后执行入口动作（避免在锁内执行耗时操作）
  execute_state_entry_action(state, preset_inner_state, event);
}
void turn_to(ChatState state, StateEvent event)
{
  turn_to_with_preset_inner_state(state, event, NULL);
}

// 方法申明
static void from_terminate_restart_to_binary_transfer(InnerStateForTerminateRestart *inner_state, StateEvent event);

// 从 State_Terminate 到 State_Upload_Transfer 的2个中间状态处理
static void from_terminate_to_restart(InnerStateForTerminate *inner_state, StateEvent event)
{
  switch (event)
  {
  case State_Event_Upload_TerminateEnd:
    inner_state->record_terminated = true;
    break;
  case State_Event_BufferPlay_TerminateEnd:
    inner_state->buffer_play_terminated = true;
    break;
  default:
    break;
  }

  if (inner_state->record_terminated && inner_state->buffer_play_terminated)
  {
    if (get_current_context() != NULL && get_current_context()->need_terminate_prompt)
    {
      module_local_play_terminate_audio();
    }
    else
    {
      inner_state_for_terminate_restart.play_terminate_prompted = true;
      if (get_current_context() != NULL)
      {
        inner_state->record_has_inited = true;
        upload_manager_init(get_current_context()->upload_type); // 初始化录音
        // turn_to(State_Upload_Init, event);
      }

      from_terminate_restart_to_binary_transfer(&inner_state_for_terminate_restart, event);

      // if (!inner_state_for_terminate_restart.voice_chat_task_ended)
      // {
      //   module_voiceChat_terminate();
      // }
    }
  }
}

static void from_terminate_restart_to_binary_transfer(InnerStateForTerminateRestart *inner_state, StateEvent event)
{
  // 加锁：保护状态切换的核心逻辑. 
  lingxin_mutex_lock(lingxin_config_terminate_state_mutex);
  bool need_record = get_current_context() != NULL && (get_current_context()->upload_type != Media_Type_TextOnly);
  switch (event)
  {
  case State_Event_TerminatePrompt_PlayEnd:
    inner_state->play_terminate_prompted = true;

    // 纯文本下，不需要初始化录音
    inner_state->record_restarted = !need_record;

    break;
  case State_Event_Upload_InitEnd:
    inner_state->record_restarted = true;
    break;
  case State_Event_VoiceChat_TerminateEnd:
    inner_state->voice_chat_terminated = true;
    break;
  case State_Event_VoiceChat_AIEnd:
    inner_state->voice_chat_task_ended = true;
    break;
  default:
    break;
  }

  lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_terminate_log_key, "打断接受的事件=%s, 录音初始化成功=%d, 打断指令发送完成=%d, 或者收到AIEnd事件=%d",
                           get_wakeup_event_description(event),
                           inner_state->record_restarted,
                           inner_state->voice_chat_terminated,
                           inner_state->voice_chat_task_ended);

  lingxin_mutex_unlock(lingxin_config_terminate_state_mutex);

  if (event == State_Event_TerminatePrompt_PlayEnd && need_record)
  {
    // 如果有播放du，需要在这里初始化播放器
    inner_state_for_terminate.record_has_inited = true;
    upload_manager_init(get_current_context()->upload_type); // 初始化录音
  }

  // 调用打断的逻辑
  if (inner_state_for_terminate.record_terminated &&
      inner_state_for_terminate.buffer_play_terminated &&
      inner_state->play_terminate_prompted &&
      inner_state->voice_chat_task_ended == false &&
      inner_state->voice_chat_terminate_has_seed == false)
  {
    lingxin_log_debug("调用打断指令");
    inner_state->voice_chat_terminate_has_seed = true;
    module_voiceChat_terminate();
  }

  // 防止 upload_manager_start 重复调用
  if (!inner_state->voice_chat_seed_start && need_record && inner_state_for_terminate.record_has_inited && (inner_state->voice_chat_terminated || inner_state->voice_chat_task_ended))
  {
    inner_state->voice_chat_seed_start = true;
    lingxin_log_debug("调用 upload_manager_start 录音开始发送");
    upload_manager_start();
  }

  if (need_record && inner_state->record_restarted && (inner_state->voice_chat_terminated || inner_state->voice_chat_task_ended))
  {
    turn_to(State_Upload_Transfer, event);
  }
  else if (need_record == false && (inner_state->voice_chat_terminated || inner_state->voice_chat_task_ended))
  {
    lingxin_log_debug("纯文本回到text");
    turn_to(State_Upload_Init, event);
  }
}
// 从 State_NoVoice_Terminate 到 等待服务端发送音频数据 的2个中间状态处理
static void from_novoice_terminate_to_restart(InnerStateForNoVoiceTerminate *inner_state, StateEvent event)
{
  switch (event)
  {
  case State_Event_Upload_TerminateEnd:
    inner_state->record_terminated = true;
    break;
  case State_Event_BufferPlay_TerminateEnd:
    inner_state->buffer_play_terminated = true;
    break;
  default:
    break;
  }
  if (inner_state->record_terminated && inner_state->buffer_play_terminated)
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "state_machine_novoice_from_terminate_to_restart");
    int r = module_voiceChat_terminate();
    if (r == 2)
    {
      inner_state_for_novoice_terminate_restart.no_voice_terminate_end_fail = true;
    }
  }
}

static void from_novoice_terminate_restart_to_wait_buffer(InnerStateForNoVoiceTerminateRestart *inner_state, StateEvent event)
{
  switch (event)
  {
  case State_Event_VoiceChat_TerminateEnd:
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "state_machine_recv_voice_chat_terminate_end");
    inner_state->no_voice_terminate_end_fail = false;
    if (inner_state->voice_chat_terminated != true)
    {
      inner_state->voice_chat_terminated = true;
    }
    break;
  default:
    break;
  }

  if (inner_state->voice_chat_terminated)
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "seed noVoice tart_task");

    turn_to(State_NoVoice_Start, event);
  }
}
// 从 State_Exit 到 State_Idle 的中间状态处理
static void from_exit_to_idle(InnerStateForExit *inner_state, StateEvent event)
{
  switch (event)
  {
  case State_Event_Upload_TerminateEnd:
    inner_state->record_terminated = true;
    break;
  case State_Event_BufferPlay_TerminateEnd:
    inner_state->buffer_play_terminated = true;
    if (!inner_state->voice_chat_terminated)
    {
      module_voiceChat_terminate();
    }

    break;
  case State_Event_VoiceChat_TerminateEnd:
  case State_Event_VoiceChat_AIEnd:
    inner_state->voice_chat_terminated = true;
    break;
  case State_Event_VoiceChat_ExitEnd:
    inner_state->voice_chat_exited = true;

    if (!inner_state->voice_chat_terminated)
    {
      lingxin_log_debug("退出状态: , 设置 voice_chat_terminated 为 true");
      inner_state->voice_chat_terminated = true;
    }
    break;
  default:
    break;
  }

  lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_exit_log_key, "退出状态: , 上行模块退出=%s, 下行模块退出=%s, 打断指令调用=%s,websocket断联=%s",
                           inner_state->record_terminated ? "true" : "false",
                           inner_state->buffer_play_terminated ? "true" : "false",
                           inner_state->voice_chat_terminated ? "true" : "false",
                           inner_state->voice_chat_exited ? "true" : "false");
  if (inner_state->record_terminated && inner_state->buffer_play_terminated && inner_state->voice_chat_terminated)
  {
    if (inner_state->voice_chat_exited)
    {

      if (get_current_context() != NULL && get_current_context()->exit_code >= 0)
      {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, " exit_code %d", get_current_context()->exit_code);
      }

      turn_to(State_Idle, event);
    }
    else
    {
      bool b = module_voiceChat_exit();
      if (!b)
      {
        turn_to(State_Idle, event);
      }
    }
  }
}

void state_machine_receive_error(ExitCode exit_code)
{
  if (get_current_context() != NULL && get_current_context()->is_normal_exit)
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "当前是用户主动退出对话模式，不处理错误事件 %d", exit_code);
    return; // 如果当前是用户主动退出对话模式，不处理错误事件
  }

  if (exit_code == EXIT_REASON_WEBSOCKET_DISCONNECT || exit_code == EXIT_REASON_WEBSOCKET_CONNECTION_FAILED)
  {
    // 保存退出时的错误信息
    update_current_context(CTX_FIELD_IS_VAD_EXIT, (ContextValue){.boolean = false});

    update_current_context(CTX_FIELD_EXIT_CODE, (ContextValue){.exit_code = exit_code});
    // 如果是websocket连接失败或者销毁，直接进入退出状态
    lingxin_log_ut_with_args(LINGXIN_ERROR, chat_log_key, "receive websocket connect error, enter exit %d", exit_code);
    InnerStateForExit inner_state = {false, false, true, true, false};
    InnerStateCollection inner_state_collection = {
        .inner_state_for_exit = &inner_state,
    };
    turn_to_with_preset_inner_state(State_Exit, State_Event_WillExit, &inner_state_collection);
  }
}

static void module_timeout_handler()
{
  // 改成分步执行
  update_current_context(CTX_FIELD_IS_VAD_EXIT, (ContextValue){.boolean = false});
  update_current_context(CTX_FIELD_EXIT_CODE, (ContextValue){.exit_code = EXIT_REASON_EXCEPTION_TIMEOUT});

  InnerStateForExit inner_state = {false, false, true, false, false};
  InnerStateCollection inner_state_collection = {
      .inner_state_for_exit = &inner_state,
  };
  turn_to_with_preset_inner_state(State_Exit, State_Event_WillExit, &inner_state_collection);
}

static void timer_callback()
{
  lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "in timer callback");

  if (module_timeout_need_callback && chat_current_state != State_Idle && chat_current_state != State_Exit)
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "timer callback run success");
    module_timeout_handler();
  }

  delete_lingxin_chat_timer();
}

// 外部调用：兼容老的不带payload的调用
void state_machine_run_event(StateEvent event)
{
  state_machine_run_event_with_payload(event, NULL);
}
// 外部调用：事件流转
void state_machine_run_event_with_payload(StateEvent event, StateEventPayload *payload)
{
  if (chat_current_state == State_Idle && (event != State_Event_Wakeup_Detected && event != State_Event_NoVoice_Start))
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "State_Idle 不再接受别的事件, 接受到事件 %s，当前状态 %s, ", get_wakeup_event_description(event), get_chat_state_description(chat_current_state));
    return;
  }
  lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "接受到事件 %s，当前状态 %s", get_wakeup_event_description(event), get_chat_state_description(chat_current_state));
  switch (event)
  {

  // 唤醒事件
  case State_Event_Wakeup_Detected:
  {
    if (chat_current_state == State_Exit || chat_current_state == State_Welcome || chat_current_state == State_NoVoice_Terminate || chat_current_state == State_NoVoice_Start)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "不允许唤醒打断, 当前状态为%s", get_chat_state_description(chat_current_state));
      return;
    }
    else if (get_current_context() && get_current_context()->single_round)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "单轮对话模式下不允许唤醒打断, 当前状态为%s", get_chat_state_description(chat_current_state));
      return;
    }
    else if (chat_current_state == State_Idle)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "首次唤醒 %s", get_wakeup_event_description(event)); // 首次唤醒

      lingxin_websocket_control_create();

      start_new_chat_runtime_context(); // 重置主动退出标志

      bool disable_welcome_audio = get_current_context() && get_current_context()->disable_welcome_audio;
      if (disable_welcome_audio)
      {
        turn_to(State_Upload_Init, event);
      }
      else
      {
        turn_to(State_Welcome, event);
      }
    }
    else
    {
      if (isVoiceChatInited() == false)
      {
        lingxin_log_warn("voice chat未初始化完成，不允许唤醒打断");
        return; // 如果voice chat没有初始化完成，则不允许唤醒打断
      }

      end_current_chat_runtime_context();

      start_new_chat_runtime_context();

      if (get_chat_state_terminate())
      {
        lingxin_log_warn("重复打断唤醒 %s", get_wakeup_event_description(event));
        reset_lingxin_chat_timer_run(); // 重置定时任务
      }
      else
      {
        bool resetSuccess = reset_lingxin_chat_timer_run();
        if (!resetSuccess)
        {
          lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "首次打断唤醒，初始化定时器 %s", get_wakeup_event_description(event));
          init_lingxin_chat_timer(NULL, timer_callback);
        }
      }

      module_timeout_need_callback = 1;

      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "打断唤醒 %s", get_wakeup_event_description(event)); // 打断唤醒

      turn_to(State_Terminate, State_Event_Wakeup_Detected);
    }
    break;
  }
  case State_Event_NoVoice_Error:
  {
    // noVoice下行error时，重启一轮chat循环
    bool is_text_only = (get_current_context() != NULL && get_current_context()->upload_type == Media_Type_TextOnly) ||
                        chat_current_state == State_Upload_Transfer ||
                        chat_current_state == State_Upload_Init;

    if (chat_current_state == State_NoVoice_Terminate ||
        chat_current_state == State_NoVoice_Start ||
        is_text_only)
    {
      lingxin_log_ut(LINGXIN_DEBUG, "state_machine_recv_novoice_error");
      // lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "noVoice下行error时，重启一轮chat循环"); // 打断唤醒
      setWaitTerminateOrEndSuccess(false); // 弥补定时任务触发失败时未回复task_started, 未将waitTerminateOrEndSuccess置为false
      end_current_chat_runtime_context();

      start_new_chat_runtime_context();

      turn_to(State_Terminate, event);
    }
    break;
  }
  // 开启一轮novoice循环
  case State_Event_NoVoice_Start:
  {

    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "state_machine_recv_novoice_start");
    if (chat_current_state == State_Exit || chat_current_state == State_NoVoice_Terminate || chat_current_state == State_NoVoice_Start)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "当前状态为%s,不允许触发当前定时任务", get_chat_state_description(chat_current_state));
      return; // 退出状态，不可触发定时任务
    }

    if (chat_current_state == State_Idle)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "state_machine_novoice_start_from_idle");
      lingxin_websocket_control_create();

      end_current_chat_runtime_context();

      start_new_chat_runtime_context();

      set_no_voice_chat_continue_params(payload);

      turn_to(State_NoVoice_Start, event);
      // 创建websocket控制, idle态触发定时任务场景下需要先创建
      lingxin_websocket_control_create();
    }
    else
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "state_machine_novoice_start_from_chating");
      end_current_chat_runtime_context();

      start_new_chat_runtime_context();

      set_no_voice_chat_continue_params(payload);

      turn_to(State_NoVoice_Terminate, event);
    }

    break;
  }

  // 欢迎语播放结束事件
  case State_Event_Welcome_Play_End:
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "chat_state_run_event_welcome_play_end");
    if (chat_current_state == State_NoVoice_Start || chat_current_state == State_NoVoice_Terminate)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "进入定时任务触发阶段时，不允许播放欢迎语结束的事件");
      return; // 进入定时任务触发阶段时，不允许播放欢迎语结束的事件
    }
    turn_to(State_Upload_Init, event); // 进入新一轮对话的初始化
    break;
  }

  // 录音开启成功事件
  case State_Event_Upload_InitEnd:
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "chat_state_run_event_record_ready");
    if (chat_current_state == State_Upload_Init)
    {
      turn_to(State_Upload_Transfer, event);
    }
    else if (chat_current_state == State_Terminate)
    {
      from_terminate_restart_to_binary_transfer(&inner_state_for_terminate_restart, event);
    }
    else if (chat_current_state == State_NoVoice_Terminate)
    {
      // 打断State_Terminate状态下录音初始化操作
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "打断State_Terminate状态下录音初始化操作");
      return;
    }
    break;
  }

  case State_Event_Upload_CloseEnd:
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "chat_state_run_event_record_stop");
    if (chat_current_state == State_Idle)
    {
      return; // TODO: State_Idle这类特殊状态是不是可以单独做处理，其他的状态也可能需要相同的处理
    }
    else
    {
      if (chat_current_state == State_Upload_Transfer || chat_current_state == State_NoVoice_Start)
      {
        // 4.1 事件更新状态
        state_upload_transfer_receive_event(event, payload);
      }
    }

    break;
  }
  case State_Event_Vad_Exit:
  {
    // 退出逻辑， 只有在录音传输的过程才可以执行退出
    if (chat_current_state == State_Upload_Transfer)
    {
      state_upload_transfer_receive_event(event, payload);
    }

    break;
  }
  case State_Event_BufferPlay_AudioInitEnd:
  {
    if (chat_current_state == State_Download_Init)
    {
      // 改成通过策略模式，进行状态切换
      state_download_init_receive_event(event, payload);
    }

    break;
  }
  case State_Event_VoiceChat_AIEnd:
  {
    if (chat_current_state == State_NoVoice_Terminate)
    {
      lingxin_log_debug("no_voice_terminate_end_fail is %s", inner_state_for_novoice_terminate_restart.no_voice_terminate_end_fail ? "true" : "false");
      if (inner_state_for_novoice_terminate_restart.no_voice_terminate_end_fail == true)
      {
        module_voiceChat_terminate();
      }
      return;
    }

    if (chat_current_state == State_Upload_Transfer)
    {
      state_upload_transfer_receive_event(event, payload);
      return;
    }

    if (chat_current_state == State_Exit)
    {
      from_exit_to_idle(&inner_state_for_exit, event); // 确保录音、流式播放、对话退出并断联后再进入Idle
      return;
    }

    if (chat_current_state == State_Task_Complete)
    {
      state_task_complete_receive_event(event, payload);
      return;
    }

    if (chat_current_state == State_Terminate)
    {
      // download init状态下，收到AI end事件，直接进入download play状态
      from_terminate_restart_to_binary_transfer(&inner_state_for_terminate_restart, event);
      return;
    }

    if (chat_current_state == State_Download_Play)
    {
      state_download_transfer_receive_event(event, payload);
    }
    else
    {
      // TODO: State_Exit重构的时候删除掉 播放结束事件
      playback_manager_end_stream();
    }
    break;
  }
  case State_Event_BufferPlay_PlayEnd:
  {
    lingxin_emit_chat_event(CHAT_LIFE_CYCLE_EVENT_PLAY_END, NULL);
    if (chat_current_state == State_Task_Complete || chat_current_state == State_Idle)
    {

      // 完整的停止逻辑
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "播放退出唤醒语音，对话结束");
      turn_to(State_Idle, event);

      return;
    }
    else if (chat_current_state == State_Download_Play)
    {
      state_download_transfer_receive_event(event, payload);
    }

    break;
  }
  case State_Event_ContinuePrompt_PlayEnd:
  {
    state_task_complete_receive_event(event, payload);
    break;
  }
  case State_Event_VoiceChat_TerminateEnd:
  {
    if (chat_current_state == State_Task_Complete)
    {
      return;
    }
    else if (chat_current_state == State_Terminate)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "VoiceChat打断结束");
      from_terminate_restart_to_binary_transfer(&inner_state_for_terminate_restart, event);
    }
    else if (chat_current_state == State_NoVoice_Terminate)
    {
      from_novoice_terminate_restart_to_wait_buffer(&inner_state_for_novoice_terminate_restart, event);
    }
    else if (chat_current_state == State_Exit)
    {
      from_exit_to_idle(&inner_state_for_exit, event); // 确保录音、流式播放、对话退出并断联后再进入Idle
    }
    else if (chat_current_state == State_Upload_Init)
    {
      upload_manager_start();
    }
    break;
  }
  case State_Event_NoVoice_TerminateEnd:
  {
    novoice_user_custom_listener(event);
    break;
  }
  case State_Event_BufferPlay_TerminateEnd:
  {
    if (chat_current_state == State_Task_Complete)
    {
      return;
    }
    else if (chat_current_state == State_Terminate)
    {
      from_terminate_to_restart(&inner_state_for_terminate, event); // 确保录音和流式播放都打断成功后再进入新对话初始化
    }
    else if (chat_current_state == State_NoVoice_Terminate)
    {
      from_novoice_terminate_to_restart(&inner_state_for_novoice_terminate, event);
    }
    else if (chat_current_state == State_Exit)
    {
      from_exit_to_idle(&inner_state_for_exit, event); // 确保录音、流式播放、对话退出并断联后再进入Idle
    }
    break;
  }
  case State_Event_BufferPlay_Error:
  {
    break;
  }

  case State_Event_Upload_TerminateEnd:
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "chat_state_run_event_record_terminate_end");
    if (chat_current_state == State_Terminate)
    {
      from_terminate_to_restart(&inner_state_for_terminate, event); // 确保录音和流式播放都打断成功后再进入新对话初始化
    }
    else if (chat_current_state == State_NoVoice_Terminate)
    {
      from_novoice_terminate_to_restart(&inner_state_for_novoice_terminate, event);
    }
    else if (chat_current_state == State_Exit)
    {
      from_exit_to_idle(&inner_state_for_exit, event); // 确保录音、流式播放、对话退出并断联后再进入Idle
    }
    break;
  }

  case State_Event_WillExit:
  {
    // 避免重复触发exit
    if (chat_current_state == State_Idle)
    {
      return;
    }
    if (chat_current_state == State_Task_Complete)
    {
      state_task_complete_receive_event(event, payload);
      return;
    }
    if (chat_current_state == State_Exit)
    {
      lingxin_log_debug("重复触发退出事件");
      return;
    }
    if (payload && payload->will_exit_payload && payload->will_exit_payload->disable_close_ws_immediately)
    {
      InnerStateForExit inner_state = {false, false, false, true, true};
      InnerStateCollection inner_state_collection = {
          .inner_state_for_exit = &inner_state,
      };
      turn_to_with_preset_inner_state(State_Exit, State_Event_WillExit, &inner_state_collection);
    }
    else
    {
      InnerStateForExit inner_state = {false, false, true, false, true};
      InnerStateCollection inner_state_collection = {
          .inner_state_for_exit = &inner_state,
      };
      turn_to_with_preset_inner_state(State_Exit, State_Event_WillExit, &inner_state_collection);
    }

    break;
  }

  case State_Event_VoiceChat_ExitEnd:
  {
    // sdk发送打断指令
    if (chat_current_state == State_Exit)
    {

      if (get_current_context() != NULL && get_current_context()->is_normal_exit)
      {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "主动调用对话结束");
        from_exit_to_idle(&inner_state_for_exit, event); // 确保录音、流式播放、对话退出并断联后再进入Idle
      }
      else
      {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "异常webSocket导致对话结束");

        state_machine_receive_error(EXIT_REASON_WEBSOCKET_DISCONNECT);
      }
    }
    else if (chat_current_state == State_Idle)
    {
      lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "等待唤醒态websocket断开");
    }
    else
    {
      // 找产品对一下产品逻辑

      if (get_current_context() != NULL && get_current_context()->is_normal_exit)
      {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "主动调用对话结束");
        from_exit_to_idle(&inner_state_for_exit, event); // 确保录音、流式播放、对话退出并断联后再进入Idle
      }
      else
      {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "异常webSocket导致对话结束");

        state_machine_receive_error(EXIT_REASON_WEBSOCKET_DISCONNECT);
      }
    }
    break;
  }
  case State_Event_TerminatePrompt_PlayEnd:
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "chat_state_run_event_terminate_prompt_play_end");
    from_terminate_restart_to_binary_transfer(&inner_state_for_terminate_restart, event);
    break;
  }

  default:
  {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "事件 %s", get_wakeup_event_description(event));
    break;
  }
  }
}

// 接收SDK的mp3数据
void state_machine_receive_mp3_data(void *buf, int rlen)
{
  if (chat_current_state == State_Download_Init || chat_current_state == State_Download_Play)
  {
    state_download_feed_data(buf, (int)rlen);
    return;
  }
  if (chat_current_state == State_Task_Complete)
  {
    if (get_current_context() != NULL && get_current_context()->input_timeout_audio == false)
    {
      // 修复在未开启下行直接进入vad，播放器未初始化的问题
      lingxin_log_debug("vad exit 播放器初始化");
      playback_manager_init(Media_Type_Chat);
    }    
    update_current_context(CTX_FIELD_INPUT_TIMEOUT_AUDIO, (ContextValue){.boolean = true});
    playback_manager_feed_data(buf, (int)rlen);
    return;
  }

  lingxin_log_error("状态值不对,不可接受mp3数据，当前状态是 %s", get_chat_state_description(chat_current_state));
}

// 接收上行模块的录音数据
void state_machine_post_record_data(void *buf, int rlen, int index)
{
  // lingxin_log_debug(chat_log_key, "chat_state_post_record_data");
  if (chat_current_state != State_Upload_Transfer)
  {
    lingxin_log_error("状态值不对，不可发送录音数据，当前状态是 %s", get_chat_state_description(chat_current_state));
    return;
  }
  voiceChatSendAudio(buf, (int)rlen);
}

// 接收SDK的定时任务数据
void state_machine_receive_schedule_data(void *scheduleStr)
{
  lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_log_key, "state_machine_post_schedule_data");
  initTimerTaskList(scheduleStr);
}

bool is_state_audio_playing()
{
  return chat_current_state == State_Download_Play || chat_current_state == State_Task_Complete;
}