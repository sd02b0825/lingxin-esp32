// chat_runtime_context.h
#ifndef CHAT_RUNTIME_CONTEXT_H
#define CHAT_RUNTIME_CONTEXT_H

#include <stdbool.h>
#include "chat_state_machine.h"  // 包含 ChatStateMediaType
#include "chat_api.h"            // 包含 ExitCode 等定义

// 只声明结构体，不重新定义 enum
typedef struct {
    ExitCode exit_code;
    bool has_exit_code;

    bool is_normal_exit;
    bool has_is_normal_exit;

    bool need_terminate_prompt;
    bool has_need_terminate_prompt;

    bool need_continue_prompt;
    bool has_need_continue_prompt;

    bool is_vad_exit;
    bool has_is_vad_exit;

    bool input_timeout_audio;
    bool has_input_timeout_audio;

    // 下行类型
    ChatStateMediaType download_type;
    bool has_download_type;

    char *global_task; // 对于 task，使用字符串存储
    bool has_global_task;

    char *current_task_id;
    bool has_current_task_id;

    bool single_round;
    bool has_single_round;

    /**
     * 支持二开定制
     *  */ 
    ChatStateMediaType upload_type;
    bool has_upload_type;

    bool disable_server_vad;           // 本轮对话是否启用云端VAD
    bool has_disable_server_vad;

    bool disable_welcome_audio;           // 本轮对话是否禁用欢迎语
    bool has_disable_welcome_audio;

    char *current_user_input;
    bool has_current_user_input;

    char *current_schedule_id;
    bool has_current_schedule_id;

    bool play_prologue;           // 本轮对话是否播放开场白
    bool has_play_prologue;

    char *output_mode;             // 输出模式 (如 "voice")
    bool has_output_mode;

    char *input_mode;              // 输入模式 (如 "no_voice")
    bool has_input_mode;
} ChatStateRuntimeContext;

// 字段枚举（这个是你自己用的，可以保留）
typedef enum {
    CTX_FIELD_EXIT_CODE, // 0
    CTX_FIELD_IS_NORMAL_EXIT, // 1
    CTX_FIELD_NEED_TERMINATE_PROMPT, // 2
    CTX_FIELD_NEED_CONTINUE_PROMPT, // 3
    CTX_FIELD_IS_VAD_EXIT, // 4
    CTX_FIELD_INPUT_TIMEOUT_AUDIO, // 5
    CTX_FIELD_DOWNLOAD_TYPE, // 6
    CTX_FIELD_GLOBAL_TASK, // 7
    CTX_FIELD_SINGLE_ROUND, // 8
    CTX_FIELD_COUNT,          // 9
    CTX_FIELD_UPLOAD_TYPE, // 10
    CTX_FIELD_CURRENT_TASK_ID,  // 11
    CTX_FIELD_DISABLE_SERVER_VAD,  // 12
    CTX_FIELD_CURRENT_USER_INPUT, // 13
    CTX_FIELD_CURRENT_SCHEDULE_ID, // 14
    CTX_FIELD_DISABLE_WELCOME_AUDIO, // 15
    CTX_FIELD_PLAY_PROLOGUE, // 16
    CTX_FIELD_OUTPUT_MODE, // 17
    CTX_FIELD_INPUT_MODE // 18
} ChatContextField;

// 联合体
typedef union {
    ExitCode exit_code;
    bool boolean;
    ChatStateMediaType media_type;
    const char *string;
} ContextValue;

// 函数声明
bool init_chat_runtime_context(const ChatStateRuntimeContext *default_config);
void destroy_chat_runtime_context(void);
bool start_new_chat_runtime_context(void);
void end_current_chat_runtime_context(void);
void reset_session_context(void);

const ChatStateRuntimeContext* get_current_context(void);

// 更新临时上下文
bool update_temp_context(ChatContextField field, ContextValue value);

// 更新会话上下文
bool update_sesseion_context(ChatContextField field, ContextValue value);

bool update_current_context(ChatContextField field, ContextValue value);

/**
 * 工具函数
 */

// 打印函数
void print_chat_context(const ChatStateRuntimeContext *ctx);

// start_task指令：根据端侧的type置换为服务端需要的字符串
char *get_input_type_string();

// start_task指令：根据端侧的type置换为服务端需要的字符串
char *get_output_type_string();

#endif // CHAT_RUNTIME_CONTEXT_H