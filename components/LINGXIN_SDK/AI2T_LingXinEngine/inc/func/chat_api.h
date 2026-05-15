#ifndef __CHAT_API_H__
#define __CHAT_API_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

  typedef char *(*AuthAppIdGetFunc)(void);
  typedef char *(*AuthLicenseGetFunc)(void);
  typedef char *(*AuthSnGetFunc)(void);
  typedef char *(*AuthAppCodeGetFunc)(void);
  typedef char *(*DeviceCodeGetFunc)(void);
  typedef char *(*ChatBizParameterGetFunc)(void);
  typedef char *(*ChatCustomParameterGetFunc)(void);
  typedef char *(*ChatFlowControlParameterGetFunc)(void);

  // 退出完成Code
  typedef enum
  {
    EXIT_REASON_USER_INITIATED,              // 0. 主动退出
    EXIT_REASON_WEBSOCKET_DISCONNECT,        // 1. websocket异常断开，可能原因：断网
    EXIT_REASON_WEBSOCKET_CONNECTION_FAILED, // 2. websocket建联失败
    EXIT_REASON_NO_INPUT_TIMEOUT,            // 3. 持续无输入超时自动退出
    EXIT_REASON_EXCEPTION_TIMEOUT,           // 4. 内部异常超时退出
  } ExitCode;
  // 退出完成事件载荷
  typedef struct
  {
    ExitCode exit_code; // 退出类型
    char *reason;       // 具体的退出原因
  } ExitPayload;

  typedef enum
  {
    CHAT_PHASE_STANDBY,      // 0. 待命中
    CHAT_PHASE_STARTING,     // 1. 启动中
    CHAT_PHASE_INPUTING,     // 2. 输入中
    CHAT_PHASE_THINKING,     // 3. 思考中
    CHAT_PHASE_OUTPUTING,    // 4. 输出中
    CHAT_PHASE_INTERRUPTING, // 5. 打断中
    CHAT_PHASE_EXITING,      // 6. 退出中
  } ChatPhaseCode;

  // 对话模式阶段变化事件载荷
  typedef struct
  {
    ChatPhaseCode phase_code;
  } ChatPhaseChangePayload;

  // 对话模式生命周期事件
  typedef enum
  {
    CHAT_LIFE_CYCLE_EVENT_EXIT,              // 0. 退出完成
    CHAT_LIFE_CYCLE_EVENT_SCHEDULE_EMIT,     // 1. 定时任务触发
    CHAT_LIFE_CYCLE_EVENT_TEXT_OUT,          // 2. 指令+文本
    CHAT_LIFE_CYCLE_EVENT_PLAY_END,          // 3. 播放完成事件
    CHAT_LIFE_CYCLE_EVENT_CHAT_PHASE_CHANGE, // 4. 对话模式阶段变化
    CHAT_LIFE_CYCLE_EVENT_ERROR,             // 5. 错误事件
  } ChatLifeCycleEvent;
  typedef void (*ChatLifeCycleEventListener)(ChatLifeCycleEvent event, void *payload);

  // 多模态相关方法
  // (1) 多模态发送流
  typedef struct
  {
    char *unique_id;
    int index;
    char *frame;
    int content_len;
    char *content_type;
    bool is_last;
  } LingxinSendStreamProps;
  typedef bool (*LingxinSendStream)(LingxinSendStreamProps *send_stream_props);
  // (2) 多模态发送文本
  typedef struct
  {
    char *content;
  } LingxinSendTextProps;
  typedef bool (*LingxinSendText)(LingxinSendTextProps *send_text_props);
  // (3) 多模态打开内置录音
  typedef struct
  {
  } LingxinStartRecordProps;
  typedef bool (*LingxinStartRecord)(LingxinStartRecordProps *start_record_props);
  // (4) 多模态关闭内置录音
  typedef struct
  {
  } LingxinStopRecordProps;
  typedef bool (*LingxinStopRecord)(LingxinStopRecordProps *stop_record_props);
  // (5) 多模态本轮输入结束
  typedef struct
  {
    char *unique_id;
    char *content_type;
  } LingxinConfirmData;
  typedef struct
  {
    size_t confirm_data_count;
    LingxinConfirmData *confirm_data_array;
  } LingxinInputEndProps;
  typedef bool (*LingxinInputEnd)(LingxinInputEndProps *input_end_props);

  // 多模态事件回调
  typedef enum
  {
    LINGXIN_MULTIMODAL_EVENT_INPUT_START = 0,
    LINGXIN_MULTIMODAL_EVENT_RECORDER_START = 1,
    LINGXIN_MULTIMODAL_EVENT_RECORDER_STOP = 2,
    LINGXIN_MULTIMODAL_EVENT_INPUT_INTERRUPT = 3,
    LINGXIN_MULTIMODAL_EVENT_STREAM_INPUT_SUCCESS = 4,
  } LingxinMultimodalInputEvent;
  // 多模态输入开始监听方法参数
  typedef struct
  {
    LingxinSendStream send_stream;
    LingxinSendText send_text;
    LingxinStartRecord start_record;
    LingxinStopRecord stop_record;
    LingxinInputEnd input_end;
    LingxinMultimodalInputEvent event;
    char *event_payload;
  } LingxinMultimodalInputListenerProps;
  typedef void (*LingxinMultimodalInputListener)(LingxinMultimodalInputListenerProps props);

  // 对话模式初始化方法与参数
  typedef struct
  {
    AuthAppIdGetFunc auth_app_id_get_func;     // (必填) 获取appId的方法
    AuthLicenseGetFunc auth_license_get_func;  // (必填) 获取license的方法
    AuthSnGetFunc auth_sn_get_func;            // (必填) 获取sn的方法
    AuthAppCodeGetFunc auth_app_code_get_func; // (必填) 获取appCode的方法，也可以是agentCode

    DeviceCodeGetFunc device_code_get_func;                    // 获取设备型号的方法
    ChatBizParameterGetFunc chat_biz_parameter_get_func;       // 获取业务参数的方法
    ChatCustomParameterGetFunc chat_custom_parameter_get_func; // 获取自定义参数的方法
    ChatFlowControlParameterGetFunc chat_flow_control_parameter_get_func; // 获取流控参数的方法
    int websocket_check_interval;                              // 检测websocket连接状态的间隔时间
    int websocket_check_timeout;                               // 检测websocket连接状态的超时时间

    ChatLifeCycleEventListener chat_life_cycle_event_listener;

    int send_uni_size;   // 设置录音单次发送的大小（字节）
    int send_cbuf_scale; // 设置录音缓冲区大小对于单次发送大小的倍数

    char *welcome_audio_path;   // 设置首次唤醒后播放的音频
    char *terminate_audio_path; // 设置打断时播放的音频
    char *continue_audio_path;  // 设置连续对话进入下一轮对话前播放的音频

    int is_schedule_task_on; // 是否开启定时任务
    int is_log_upload_on;    // 是否开启日志

    char *props_init_tag; // 标记是否经过灵芯自带的初始化，用户无需关心

    char *flash_cache_path; // 设置flash中可用于缓存文件的分区路径，分区可用存储空间需要大于600K
  } VoiceChatInitProps;
  VoiceChatInitProps get_voice_chat_init_default_props();
  /**
   * 对话模式初始化
   * @param init_props 对话模式初始化参数
   * @return 0: 初始化成功，-1: 参数有误，-2: 音频格式不支持
   */
  int voice_chat_init(VoiceChatInitProps *init_props);

  // 对话模式新一轮对话方法与参数
  typedef struct
  {
    bool disable_welcome_audio;                               // 首次唤醒是否需要本地开场白
    bool disable_vad;                                         // 本轮对话是否启用云端VAD（仅首轮禁用，废弃）
    char *task_id;                                            // 本轮对话是否指定task_id
    char *task;                                               // 本轮对话的场景 (chat_vad/chat/translate/chat_multimodal)
    bool single_round;                                        // 本轮对话是否为仅单轮对话
    char *user_input;                                         // 用户输入的文本
    bool play_prologue;                                       // 是否播放云端动态开场白
    LingxinMultimodalInputListener multimodal_input_listener; // 多模输入开始事件监听

    char *props_init_tag; // 标记是否经过灵芯自带的初始化，用户无需关心
  } StartNewChatProps;
  StartNewChatProps get_start_new_chat_default_props();
  int start_new_chat(StartNewChatProps *start_props);

  // 对话模式主动停止录音方法与参数
  typedef struct
  {
  } StopChatRecordProps; // 用于后续拓展
  int stop_chat_record(StopChatRecordProps *stop_record_props);

  // 对话模式退出方法与参数
  typedef struct
  {
    bool disable_close_ws_immediately; // 是否立即关闭websocket，true为立即断联，false为不立即断联
    char *props_init_tag;              // 标记是否经过灵芯自带的初始化，用户无需关心
  } ExitChatProps;
  ExitChatProps get_exit_chat_default_props();
  int exit_chat(ExitChatProps *exit_props);

  // 加音量
  int set_volume(int volume);

#ifdef __cplusplus
}
#endif

#endif // __CHAT_API_H__