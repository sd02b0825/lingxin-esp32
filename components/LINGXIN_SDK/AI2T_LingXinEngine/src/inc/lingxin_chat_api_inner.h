#ifndef __LINGXIN_CHAT_API_INNER_H__
#define __LINGXIN_CHAT_API_INNER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "chat_api.h"

// #define LINGXIN_TEST

/**
 * 内部对外发送事件的方法（直接透传）
 */
void lingxin_emit_chat_event(ChatLifeCycleEvent event, void *payload);

/**
 * 报告错误
 */
void lingxin_report_error(char *error);

/**
 * 触发多模态输入
 */
int lingxin_emit_multimodal_input_event(LingxinMultimodalInputListenerProps props);

/*
 * 内部初始化方法
 */
int inner_voice_chat_init(VoiceChatInitProps *init_props);

/**
 * 内部启动新对话方法
 */
int inner_start_new_chat(StartNewChatProps *start_props);

#ifdef __cplusplus
}
#endif

#endif /* __LINGXIN_CHAT_API_INNER_H__ */