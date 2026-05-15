#ifndef CHAT_STATE_MACHINE_H
#define CHAT_STATE_MACHINE_H

// 添加模块函数依赖
#include "audio_buffer_play.h"
#include "lingxin_local_player_manager.h"
#include "lingxin_protocol_manager.h"
#include "lingxin_time_task_manager.h"

// 添加事件定义
#include "chat_state_machine_event.h"
// 添加对外暴露函数定义
#include "chat_api.h"
#include "lingxin_chat_api_inner.h"


// 状态机状态
typedef enum
{
  State_Idle                   = 0,  // 等待唤醒态
  State_Welcome                = 1,  // 欢迎语播放态

  State_Upload_Init                  = 2,  // 新一轮对话初始化(开始录音和请求连续对话)
  State_Upload_Transfer        = 3,  // 录音传输态
  State_Download_Init          = 5,  // 流式播放开始态
  State_Download_Play          = 6,  // 流式播放中
  State_Download_End           = 7,  // 下行结束

  State_NoVoice_Start          = 8,  // noVoice循环初始化
  State_NoVoice_Terminate      = 9,  // 打断当前对话，开启新一轮noVoice循环
  
  State_Terminate              = 10, // 打断状态
  State_Exit                   = 11, // 主动退出
  State_Task_Complete          = 12, // 任务完成态（单轮任务结束）
} ChatState;

typedef enum
{
    Media_Type_Chat = 0, // 默认对话
    Media_Type_Multimodal = 2, // 多模态对话
    Media_Type_TextOnly = 3, // 纯文本对话
} ChatStateMediaType;


// 携带payload向状态机发送事件
typedef struct {
  bool disable_welcome_audio;           // 首次唤醒是否需要开场白
  bool disable_vad;                     // 本轮对话是否启用云端VAD
  char *task_id;                        // 本轮ß对话是否指定task_id
  char *task;                   // 本轮对话的场景
  char *task_for_once;          // 本轮第一次对话的场景
  bool single_round;            // 本轮对话是否为仅单轮对话
  char *user_input;             // 用户输入的文本
} WakeupDetectedPayload;
typedef struct {
  bool disable_close_ws_immediately;    // 是否立即关闭websocket, true 就是立即关闭websocket
} WillExitPayload;
typedef struct {
  bool disable_vad;             // 本次事件是否为禁用云端VAD的场景
} VadStopPayload;

typedef struct {
  void *buf;
  int rlen;
} AudioDataPayload;

typedef struct {
  char *schedule_task_id; // 当前触发的定时任务的唯一id, 塞入scheduleTaskId中
  char *input_mode; // 当前触发的定时任务的输入模式，塞入inputMode中
} ScheduleTimerPayload;

typedef struct {
  WakeupDetectedPayload *wakeup_detected_payload;
  WillExitPayload *will_exit_payload;
  VadStopPayload *vad_stop_payload;
  AudioDataPayload *audio_data_payload;
  ScheduleTimerPayload *schedule_timer_payload;
} StateEventPayload;


/**
 * State_Exit（退出对话）阶段的中间状态。
 * 用于跟踪退出流程中各模块的完成情况，
 * 录音、流式播放、voice chat 三者均完成后才切换到 State_Idle。
 */
typedef struct
{
    bool record_terminated;      // 上行录音模块是否已打断完成
    bool buffer_play_terminated; // 下行流式播放模块是否已打断完成
    bool voice_chat_terminated;  // voice chat 打断指令是否已完成（TerminateEnd / AIEnd）
    bool voice_chat_exited;      // voice chat 引擎是否已完全销毁（ExitEnd）
    bool is_normal_exit;         // 是否为用户主动退出对话模式（true=主动退出，false=异常断开）
} InnerStateForExit;

// 内部状态集合：用于在状态切换时向目标状态传递预设的内部状态
typedef struct
{
    InnerStateForExit *inner_state_for_exit; // 退出中状态的内部状态
} InnerStateCollection;

void state_machine_run_event_with_payload(StateEvent event, StateEventPayload *payload);

// 状态机初始化
void voice_chat_machine_init(bool need_terminate_prompt, bool need_continue_prompt);

// 获取状态机是否为终止状态
bool get_chat_state_terminate();

// 接收SDK的mp3数据
void state_machine_receive_mp3_data(void *buf, int rlen);

// 接收SDK的定时任务数据
void state_machine_receive_schedule_data(void *scheduleStr);

void state_machine_receive_error(ExitCode exit_code);

// 是否正在播放音频
bool is_state_audio_playing();

// 内部策略类调用 
void turn_to_with_preset_inner_state(ChatState state, StateEvent event, InnerStateCollection *preset_inner_state);

#endif // CHAT_STATE_MACHINE_H