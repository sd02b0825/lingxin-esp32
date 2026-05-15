
#ifndef CHAT_STATE_MACHINE_EVENT_H
#define CHAT_STATE_MACHINE_EVENT_H

// 模块抛给状态机的事件
typedef enum
{
  State_Event_Wakeup_Detected          = 0,  // 唤醒事件
  State_Event_Welcome_Play_End         = 1,  // 播放欢迎语结束事件

  State_Event_VoiceChat_TerminateEnd   = 3,  // voice chat 打断成功事件
  State_Event_VoiceChat_AIEnd          = 5,  // voice chat 结束推送语音流（注：原文注释有误，已修正）
  State_Event_Vad_Stop                 = 6,  // vad 停止
  State_Event_Vad_Exit                 = 7,  // vad 退出唤醒

  State_Event_BufferPlay_AudioInitEnd  = 8,  // 流式播放初始化结束
  State_Event_BufferPlay_PlayEnd       = 9,  // 流式播放结束
  State_Event_BufferPlay_TerminateEnd  = 10, // 流式播放打断后暂停
  State_Event_BufferPlay_Error         = 11, // 流式播放模块出错

  State_Event_Upload_InitEnd             = 12, // 录音模块初始化成功
  State_Event_Upload_CloseEnd              = 13, // 录音模块停止录音
  State_Event_Upload_TerminateEnd      = 14, // 录音模块打断事件

  State_Event_VoiceChat_ExitEnd        = 15, // voice chat 对话退出成功
  State_Event_WillExit                 = 16, // 用户调用退出

  State_Event_NoVoice_Start            = 17, // 开启新一轮novoice模式
  State_Event_NoVoice_Error            = 18, // noVoice下行阶段服务端推送error
  State_Event_NoVoice_TerminateEnd     = 19,

  State_Event_TerminatePrompt_PlayEnd  = 20, // 增加打断唤醒提示音
  State_Event_ContinuePrompt_PlayEnd   = 21, // 增加连续对话提示音

  // ==== 仅 task complete 状态内部使用的事件 ====
  Event_Inc_TaskComplete_PlayEnd       = 22, // 用于表明已经将时机转发给客户
  Event_Inc_TaskComplete_End       = 25, // 用于表明已经将时机转发给客户

  // ==== 仅 下行 状态内部使用的事件 ====
  Event_Inc_Download_End               = 24, // 用于下行状态结束

} StateEvent;


/******************** 在chat套件内核中已实现，客户调用 ********************/
// 模块抛出事件给调用状态机
void state_machine_run_event(StateEvent event); 
// 录音模块把录音数据发送给状态机
void state_machine_post_record_data(void *buf, int rlen, int index); 

#endif // CHAT_STATE_MACHINE_EVENT_H