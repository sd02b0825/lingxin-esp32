// chat_runtime_context.c
#include "chat_runtime_context.h"
#include <string.h>
#include "lingxin_log.h"
#include "lingxin_mutex.h"
#include "lingxin_memory.h"

// -------------------------------
// 全局上下文指针
// -------------------------------

static ChatStateRuntimeContext *global_config = NULL;
static ChatStateRuntimeContext *session_config = NULL;
static ChatStateRuntimeContext *temp_config = NULL;
static ChatStateRuntimeContext *current_config = NULL;

// 静态函数前置声明（避免 implicit declaration）
static const char *exit_code_to_str(ExitCode code);
static const char *media_type_to_str(ChatStateMediaType type);
static const char *bool_to_str(bool b);
#define SAFE_STR(s) ((s) ? (s) : "NULL")

// log keys
#define chat_runtime_context_init_log_key "chat_runtime_context_init"
#define chat_runtime_context_update_log_key "chat_runtime_context_update"

bool set_session_context(const ChatStateRuntimeContext *src);

bool set_temp_session_context(const ChatStateRuntimeContext *src);

static void set_current_upload_type(char *task, char *user_input, char *schedule_task_id)
{
    ChatStateRuntimeContext *ctx = current_config;
    if (!ctx)
    {
        lingxin_log_ut_with_args(LINGXIN_WARN, chat_runtime_context_update_log_key, "Current context is NULL");
        return;
    }

    if (schedule_task_id && strlen(schedule_task_id) > 0)
    {
        ctx->upload_type = Media_Type_TextOnly;
        ctx->has_upload_type = true;
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_runtime_context_update_log_key, "Set upload_type to TextOnly due to schedule_task_id");
        return;
    }

    if (user_input && strlen(user_input) > 0)
    {
        ctx->upload_type = Media_Type_TextOnly;
        ctx->has_upload_type = true;
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_runtime_context_update_log_key, "Set upload_type to TextOnly due to user_input");
        return;
    }

    if (task && (strcmp(task, "chat_multimodal") == 0))
    {
        ctx->upload_type = Media_Type_Multimodal;
        ctx->has_upload_type = true;
        lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_runtime_context_update_log_key, "Set upload_type to Multimodal due to task");
        return;
    }

    // 默认上传类型为 Audio
    ctx->upload_type = Media_Type_Chat;
    ctx->has_upload_type = true;
    lingxin_log_ut_with_args(LINGXIN_DEBUG, chat_runtime_context_update_log_key, "Set upload_type to Chat (Audio) by default");
}

// -------------------------------
// 静态内存池（替代 malloc）
// -------------------------------

// 预留 4 个上下文实例空间（global, session, temp, current）
static ChatStateRuntimeContext s_context_pool[4];
static bool s_context_in_use[4] = {false}; // 标记是否被占用

// 分配一个上下文实例
static ChatStateRuntimeContext *alloc_context(void)
{
    for (int i = 0; i < 4; i++)
    {
        if (!s_context_in_use[i])
        {
            s_context_in_use[i] = true;
            memset(&s_context_pool[i], 0, sizeof(ChatStateRuntimeContext));
            return &s_context_pool[i];
        }
    }
    return NULL;
}

static void free_string(ChatStateRuntimeContext *ctx)
{
    if (!ctx)
        return;
    if (ctx->global_task)
    {
        lingxin_free(ctx->global_task);
        ctx->global_task = NULL;
        ctx->has_global_task = false;
    }
    if (ctx->current_task_id)
    {
        lingxin_free(ctx->current_task_id);
        ctx->current_task_id = NULL;
        ctx->has_current_task_id = false;
    }
    if (ctx->current_user_input)
    {
        lingxin_free(ctx->current_user_input);
        ctx->current_user_input = NULL;
        ctx->has_current_user_input = false;
    }
    if (ctx->current_schedule_id)
    {
        lingxin_free(ctx->current_schedule_id);
        ctx->current_schedule_id = NULL;
        ctx->has_current_schedule_id = false;
    }
    if (ctx->output_mode)
    {
        lingxin_free(ctx->output_mode);
        ctx->output_mode = NULL;
        ctx->has_output_mode = false;
    }
    if (ctx->input_mode)
    {
        lingxin_free(ctx->input_mode);
        ctx->input_mode = NULL;
        ctx->has_input_mode = false;
    }
}

// 释放上下文（不清除内存，仅标记可用）
static void free_context(ChatStateRuntimeContext *ctx)
{
    if (!ctx)
        return;
    free_string(ctx);
    for (int i = 0; i < 4; i++)
    {
        if (ctx == &s_context_pool[i])
        {
            s_context_in_use[i] = false;
            return;
        }
    }
}

// 深拷贝上下文（使用 strncpy）
static bool copy_context(ChatStateRuntimeContext *dst, const ChatStateRuntimeContext *src)
{
    if (!dst || !src)
        return false;

    // 先清空目标
    memset(dst, 0, sizeof(ChatStateRuntimeContext));

    // 结构体赋值（包含所有 has_xxx）
    *dst = *src;

    return true;
}

// 字符串赋值互斥锁
static lingxin_mutex_t lingxin_taskid_mutex = NULL;

// -------------------------------
// 模块生命周期
// -------------------------------

bool init_chat_runtime_context(const ChatStateRuntimeContext *default_config)
{
    // 初始化互斥锁
    if (lingxin_taskid_mutex == NULL)
    {
        lingxin_taskid_mutex = lingxin_mutex_create();
    }

    // 清理旧状态
    if (session_config)
        free_context(session_config);
    if (temp_config)
        free_context(temp_config);
    if (current_config)
        free_context(current_config);

    // 分配 global_config
    global_config = alloc_context();
    if (!global_config)
    {
        lingxin_log_ut_with_args(LINGXIN_WARN, chat_runtime_context_init_log_key, "Failed to allocate global_config");
        return false;
    }

    // 首次初始化session context
    ChatStateRuntimeContext context = {0};
    set_session_context(&context);

    // 拷贝默认配置
    if (default_config)
    {
        if (!copy_context(global_config, default_config))
        {
            free_context(global_config);
            global_config = NULL;
            return false;
        }
    }

    return true;
}

void destroy_chat_runtime_context(void)
{
    // 仅标记释放，不操作堆
    if (session_config)
        free_context(session_config);
    session_config = NULL;
    if (temp_config)
        free_context(temp_config);
    temp_config = NULL;
    if (current_config)
        free_context(current_config);
    current_config = NULL;
    // if (global_config)    free_context(global_config);    global_config = NULL;

    // 初始化session context
    ChatStateRuntimeContext context = {0};
    set_session_context(&context);
}

// -------------------------------
// session_config: 多轮会话配置
// -------------------------------

bool set_session_context(const ChatStateRuntimeContext *src)
{
    if (!src)
        return false;

    // if (session_config->current_task_id) {
    //     lingxin_mutex_lock(lingxin_taskid_mutex);
    //     lingxin_free(session_config->current_task_id);
    //     session_config->current_task_id = NULL;
    //     session_config->has_current_task_id = false;
    //     lingxin_mutex_unlock(lingxin_taskid_mutex);
    // }

    ChatStateRuntimeContext *new_ctx = alloc_context();
    if (!new_ctx || !copy_context(new_ctx, src))
    {
        if (new_ctx)
            free_context(new_ctx);
        return false;
    }

    if (session_config)
    {
        free_context(session_config);
    }
    session_config = new_ctx;
    return true;
}

void reset_session_context(void)
{
    if (session_config)
    {
        free_context(session_config);
        session_config = NULL;
    }
}

// -------------------------------
// temp_config: 临时单轮配置
// -------------------------------

bool set_temp_session_context(const ChatStateRuntimeContext *src)
{
    if (!src)
        return false;

    // if (temp_config->current_task_id) {
    //     lingxin_mutex_lock(lingxin_taskid_mutex);
    //     lingxin_free(temp_config->current_task_id);
    //     temp_config->current_task_id = NULL;
    //     temp_config->has_current_task_id = false;
    //     lingxin_mutex_unlock(lingxin_taskid_mutex);
    // }

    ChatStateRuntimeContext *new_ctx = alloc_context();
    if (!new_ctx || !copy_context(new_ctx, src))
    {
        if (new_ctx)
            free_context(new_ctx);
        return false;
    }

    if (temp_config)
    {
        free_context(temp_config);
    }
    temp_config = new_ctx;
    return true;
}

static void clear_temp_session_context(void)
{
    if (temp_config)
    {
        free_context(temp_config);
        temp_config = NULL;
    }

    // 清除临时变量后，重置 temp_context
    ChatStateRuntimeContext context = {0};
    set_temp_session_context(&context);
}

// -------------------------------
// current_config: 当前生效上下文
// -------------------------------

bool start_new_chat_runtime_context(void)
{
    // 释放旧的 current session
    if (current_config)
    {
        free_context(current_config);
    }
    current_config = alloc_context();
    if (!current_config)
    {
        lingxin_log_ut_with_args(LINGXIN_WARN, chat_runtime_context_init_log_key, "Failed to allocate current_config");
        return false;
    }

    // 优先级：temp > session > global
    const ChatStateRuntimeContext *sources[] = {temp_config, session_config, global_config};

#define MERGE_FIELD(field)                                    \
    do                                                        \
    {                                                         \
        if (!current_config->has_##field && src->has_##field) \
        {                                                     \
            current_config->field = src->field;               \
            current_config->has_##field = true;               \
        }                                                     \
    } while (0)

#define MERGE_STRING(field)                                     \
    do                                                          \
    {                                                           \
        if (!current_config->has_##field && src->has_##field)   \
        {                                                       \
            if (current_config->field)                          \
            {                                                   \
                lingxin_free(current_config->field);            \
            }                                                   \
            current_config->field = lingxin_strdup(src->field); \
            current_config->has_##field = true;                 \
        }                                                       \
    } while (0)

    for (int i = 0; i < 3; i++)
    {
        const ChatStateRuntimeContext *src = sources[i];
        if (!src)
            continue;

        MERGE_FIELD(exit_code);
        MERGE_FIELD(is_normal_exit);
        MERGE_FIELD(need_terminate_prompt);
        MERGE_FIELD(need_continue_prompt);
        MERGE_FIELD(is_vad_exit);
        MERGE_FIELD(input_timeout_audio);
        MERGE_FIELD(download_type);
        MERGE_FIELD(upload_type);
        MERGE_FIELD(single_round);
        MERGE_FIELD(disable_server_vad);
        MERGE_FIELD(disable_welcome_audio);
        MERGE_FIELD(play_prologue);

        MERGE_STRING(global_task);
        MERGE_STRING(current_task_id);
        MERGE_STRING(current_user_input);
        MERGE_STRING(current_schedule_id);
        MERGE_STRING(output_mode);
        MERGE_STRING(input_mode);
    }

#undef MERGE_FIELD
#undef MERGE_STRING

    // 补充默认值
    if (!current_config->has_exit_code)
    {
        current_config->exit_code = EXIT_REASON_USER_INITIATED;
        current_config->has_exit_code = true;
    }
    if (!current_config->has_is_normal_exit)
    {
        current_config->is_normal_exit = true;
        current_config->has_is_normal_exit = true;
    }
    if (!current_config->has_download_type)
    {
        current_config->download_type = Media_Type_Chat;
        current_config->has_download_type = true;
    }

    if (!current_config->has_global_task)
    {
        const char *default_task = "chat_vad";
        if (current_config->global_task)
        {
            lingxin_free(current_config->global_task);
        }
        current_config->global_task = lingxin_strdup(default_task);
        current_config->has_global_task = true;
    }

    // 初始化upload_type
    if (!current_config->has_upload_type)
    {
        set_current_upload_type(current_config->global_task, current_config->current_user_input, current_config->current_schedule_id);
    }
    else
    {
        lingxin_log_debug("当前upload_type已设置为 %s", media_type_to_str(current_config->upload_type));
    }
    lingxin_log_debug("初始化upload_type为 %s， global_task为 %s", media_type_to_str(current_config->upload_type), SAFE_STR(current_config->global_task));

    // 使用完，清除临时变量
    clear_temp_session_context();

    print_chat_context(current_config);
    return true;
}

void end_current_chat_runtime_context(void)
{
    if (current_config)
    {
        free_context(current_config);
        current_config = NULL;
    }
}

// -------------------------------
// 获取当前上下文（只读）
// -------------------------------

const ChatStateRuntimeContext *get_current_context(void)
{
    if (!current_config)
    {
        lingxin_log_ut_with_args(LINGXIN_WARN, chat_runtime_context_update_log_key, "current_config is null");
        return NULL;
    }
    return current_config;
}

// -------------------------------
// 动态更新当前上下文
// -------------------------------
bool update_runtime_context_data(ChatStateRuntimeContext *ctx, ChatContextField field, ContextValue value)
{
    if (!ctx)
    {
        lingxin_log_ut_with_args(LINGXIN_WARN, chat_runtime_context_update_log_key, "current_config is null, cannot update field %d", field);
        return false;
    }

    switch (field)
    {
    case CTX_FIELD_EXIT_CODE:
        ctx->exit_code = value.exit_code;
        ctx->has_exit_code = true;
        break;

    case CTX_FIELD_IS_NORMAL_EXIT:
        ctx->is_normal_exit = value.boolean;
        ctx->has_is_normal_exit = true;
        break;

    case CTX_FIELD_NEED_TERMINATE_PROMPT:
        ctx->need_terminate_prompt = value.boolean;
        ctx->has_need_terminate_prompt = true;
        break;

    case CTX_FIELD_NEED_CONTINUE_PROMPT:
        ctx->need_continue_prompt = value.boolean;
        ctx->has_need_continue_prompt = true;
        break;

    case CTX_FIELD_IS_VAD_EXIT:
        ctx->is_vad_exit = value.boolean;
        ctx->has_is_vad_exit = true;
        break;

    case CTX_FIELD_INPUT_TIMEOUT_AUDIO:
        ctx->input_timeout_audio = value.boolean;
        ctx->has_input_timeout_audio = true;
        break;

    case CTX_FIELD_DOWNLOAD_TYPE:
        ctx->download_type = value.media_type;
        ctx->has_download_type = true;
        break;

    case CTX_FIELD_UPLOAD_TYPE:
        ctx->upload_type = value.media_type;
        ctx->has_upload_type = true;
        break;

    case CTX_FIELD_GLOBAL_TASK:
        if (value.string)
        {
            if (ctx->global_task)
            {
                lingxin_free(ctx->global_task);
            }
            ctx->global_task = lingxin_strdup(value.string);
            ctx->has_global_task = true;
        }
        else
        {
            if (ctx->global_task)
            {
                lingxin_free(ctx->global_task);
                ctx->global_task = NULL;
            }
            ctx->has_global_task = false;
        }
        break;
    case CTX_FIELD_CURRENT_TASK_ID:
        if (value.string)
        {
            if (ctx->current_task_id)
            {
                lingxin_free(ctx->current_task_id);
            }
            ctx->current_task_id = lingxin_strdup(value.string);
            ctx->has_current_task_id = true;
        }
        else
        {
            if (ctx->global_task)
            {
                lingxin_free(ctx->global_task);
                ctx->global_task = NULL;
            }
            ctx->has_global_task = false;
        }

        break;

    case CTX_FIELD_CURRENT_USER_INPUT:
        if (value.string)
        {
            if (ctx->current_user_input)
            {
                lingxin_free(ctx->current_user_input);
            }
            ctx->current_user_input = lingxin_strdup(value.string);
            ctx->has_current_user_input = true;
        }
        else
        {
            if (ctx->current_user_input)
            {
                lingxin_free(ctx->current_user_input);
                ctx->current_user_input = NULL;
            }
            ctx->has_current_user_input = false;
        }
        break;
    case CTX_FIELD_CURRENT_SCHEDULE_ID:
        if (value.string)
        {
            if (ctx->current_schedule_id)
            {
                lingxin_free(ctx->current_schedule_id);
            }
            ctx->current_schedule_id = lingxin_strdup(value.string);
            ctx->has_current_schedule_id = true;
        }
        else
        {
            if (ctx->current_schedule_id)
            {
                lingxin_free(ctx->current_schedule_id);
                ctx->current_schedule_id = NULL;
            }
            ctx->has_current_schedule_id = false;
        }
        break;

    case CTX_FIELD_SINGLE_ROUND:
        ctx->single_round = value.boolean;
        ctx->has_single_round = true;
        break;

    case CTX_FIELD_DISABLE_SERVER_VAD:
        ctx->disable_server_vad = value.boolean;
        ctx->has_disable_server_vad = true;
        break;

    case CTX_FIELD_DISABLE_WELCOME_AUDIO:
        ctx->disable_welcome_audio = value.boolean;
        ctx->has_disable_welcome_audio = true;
        break;

    case CTX_FIELD_PLAY_PROLOGUE:
        ctx->play_prologue = value.boolean;
        ctx->has_play_prologue = true;
        break;

    case CTX_FIELD_OUTPUT_MODE:
        if (value.string)
        {
            if (ctx->output_mode)
            {
                lingxin_free(ctx->output_mode);
            }
            ctx->output_mode = lingxin_strdup(value.string);
            ctx->has_output_mode = true;
        }
        else
        {
            if (ctx->output_mode)
            {
                lingxin_free(ctx->output_mode);
                ctx->output_mode = NULL;
            }
            ctx->has_output_mode = false;
        }
        break;

    case CTX_FIELD_INPUT_MODE:
        if (value.string)
        {
            if (ctx->input_mode)
            {
                lingxin_free(ctx->input_mode);
            }
            ctx->input_mode = lingxin_strdup(value.string);
            ctx->has_input_mode = true;
        }
        else
        {
            if (ctx->input_mode)
            {
                lingxin_free(ctx->input_mode);
                ctx->input_mode = NULL;
            }
            ctx->has_input_mode = false;
        }
        break;

    default:
        lingxin_log_ut_with_args(LINGXIN_WARN, chat_runtime_context_update_log_key, "Invalid field ID: %d", field);
        return false;
    }

    return true;
}

// 更新临时上下文
bool update_temp_context(ChatContextField field, ContextValue value)
{
    if (!temp_config)
    {
        lingxin_log_debug("临时上下文未初始化，自动创建一个新的临时上下文");
        ChatStateRuntimeContext ctx = {0};
        set_temp_session_context(&ctx);
    }
    return update_runtime_context_data(temp_config, field, value);
}

// 更新会话上下文
bool update_sesseion_context(ChatContextField field, ContextValue value)
{
    if (!session_config)
    {
        lingxin_log_debug("会话上下文未初始化，自动创建一个新的会话上下文");
        ChatStateRuntimeContext ctx = {0};
        set_session_context(&ctx);
    }
    return update_runtime_context_data(session_config, field, value);
}

// 更新当前上下文
bool update_current_context(ChatContextField field, ContextValue value)
{
    return update_runtime_context_data(current_config, field, value);
}

/**
 * 工具函数
 */

static char *get_string_with_media_type(ChatStateMediaType type)
{
    if (type == Media_Type_Chat)
    {
        return "voice";
    }
    else if (type == Media_Type_Multimodal)
    {
        return "no_voice";
    }
    return "";
}

// start_task指令：根据端侧的type置换为服务端需要的字符串
char *get_input_type_string()
{
    if (!current_config || !current_config->has_upload_type)
    {
        return "";
    }
    else
    {
        return get_string_with_media_type(current_config->upload_type);
    }
}

// start_task指令：根据端侧的type置换为服务端需要的字符串
char *get_output_type_string()
{
    if (!current_config || !current_config->has_download_type)
    {
        return "";
    }
    else
    {
        return get_string_with_media_type(current_config->download_type);
    }
}

// -------------------------------
// 调试打印函数
// -------------------------------

void print_chat_context(const ChatStateRuntimeContext *ctx)
{
    if (!ctx)
    {
        lingxin_log_debug("[ChatContext] NULL context pointer.\n");
        return;
    }

    lingxin_log_debug("[ChatStateRuntimeContext] Current Context Dump:\n");
    lingxin_log_debug("------------------------------------------------\n");

#define PRINT_FIELD(fmt, name, value_expr, has_field, comment)                         \
    do                                                                                 \
    {                                                                                  \
        if (has_field)                                                                 \
        {                                                                              \
            lingxin_log_debug("%-30s = " fmt "  // %s\n", #name, value_expr, comment); \
        }                                                                              \
        else                                                                           \
        {                                                                              \
            lingxin_log_debug("%-30s = (not set)  // %s\n", #name, comment);           \
        }                                                                              \
    } while (0)

    PRINT_FIELD("%s", exit_code, exit_code_to_str(ctx->exit_code), ctx->has_exit_code, "退出时的错误信息");
    PRINT_FIELD("%s", is_normal_exit, bool_to_str(ctx->is_normal_exit), ctx->has_is_normal_exit, "是否是主动退出对话模式");
    PRINT_FIELD("%s", need_terminate_prompt, bool_to_str(ctx->need_terminate_prompt), ctx->has_need_terminate_prompt, "打断唤醒是否需要播放提示音");
    PRINT_FIELD("%s", need_continue_prompt, bool_to_str(ctx->need_continue_prompt), ctx->has_need_continue_prompt, "连续对话前是否需要播放提示音");
    PRINT_FIELD("%s", is_vad_exit, bool_to_str(ctx->is_vad_exit), ctx->has_is_vad_exit, "是否是vad退出");
    PRINT_FIELD("%s", input_timeout_audio, bool_to_str(ctx->input_timeout_audio), ctx->has_input_timeout_audio, "是否正在播放本地音频");
    PRINT_FIELD("%s", download_type, media_type_to_str(ctx->download_type), ctx->has_download_type, "当前播放的类型");
    PRINT_FIELD("%s", upload_type, media_type_to_str(ctx->upload_type), ctx->has_upload_type, "当前上传的类型");
    PRINT_FIELD("%s", global_task, SAFE_STR(ctx->global_task), ctx->has_global_task, "全局任务描述");
    PRINT_FIELD("%s", current_task_id, SAFE_STR(ctx->current_task_id), ctx->has_current_task_id, "全局任务ID描述");
    PRINT_FIELD("%s", single_round, bool_to_str(ctx->single_round), ctx->has_single_round, "本轮对话是否为单轮对话");
    PRINT_FIELD("%s", disable_server_vad, bool_to_str(ctx->disable_server_vad), ctx->has_disable_server_vad, "本轮对话是否禁用云端VAD");
    PRINT_FIELD("%s", current_user_input, SAFE_STR(ctx->current_user_input), ctx->has_current_user_input, "当前用户输入文本");
    PRINT_FIELD("%s", current_schedule_id, SAFE_STR(ctx->current_schedule_id), ctx->has_current_schedule_id, "当前定时任务调度任务ID");
    PRINT_FIELD("%s", disable_welcome_audio, bool_to_str(ctx->disable_welcome_audio), ctx->has_disable_welcome_audio, "本轮对话是否禁用欢迎语");
    PRINT_FIELD("%s", play_prologue, bool_to_str(ctx->play_prologue), ctx->has_play_prologue, "本轮对话是否播放开场白");
    PRINT_FIELD("%s", output_mode, SAFE_STR(ctx->output_mode), ctx->has_output_mode, "输出模式");
    PRINT_FIELD("%s", input_mode, SAFE_STR(ctx->input_mode), ctx->has_input_mode, "输入模式");

#undef PRINT_FIELD
    lingxin_log_debug("------------------------------------------------\n");
}

// -------------------------------
// 辅助函数（放在最后）
// -------------------------------

static const char *exit_code_to_str(ExitCode code)
{
    switch (code)
    {
    case EXIT_REASON_USER_INITIATED:
        return "EXIT_REASON_USER_INITIATED";
    case EXIT_REASON_WEBSOCKET_DISCONNECT:
        return "EXIT_REASON_WEBSOCKET_DISCONNECT";
    case EXIT_REASON_WEBSOCKET_CONNECTION_FAILED:
        return "EXIT_REASON_WEBSOCKET_CONNECTION_FAILED";
    case EXIT_REASON_NO_INPUT_TIMEOUT:
        return "EXIT_REASON_NO_INPUT_TIMEOUT";
    case EXIT_REASON_EXCEPTION_TIMEOUT:
        return "EXIT_REASON_EXCEPTION_TIMEOUT";
    default:
        return "UNKNOWN_EXIT_CODE";
    }
}

static const char *media_type_to_str(ChatStateMediaType type)
{
    switch (type)
    {
    case Media_Type_Chat:
        return "Media_Type_Chat";
    case Media_Type_TextOnly:
        return "Media_Type_TextOnly";
    case Media_Type_Multimodal:
        return "Media_Type_Multimodal";
    default:
        return "UNKNOWN_MEDIA";
    }
}

static const char *bool_to_str(bool b)
{
    return b ? "true" : "false";
}