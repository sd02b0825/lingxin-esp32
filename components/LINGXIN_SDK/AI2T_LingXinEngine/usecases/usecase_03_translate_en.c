/*
 * chat_with_key.c
 * 使用案例：按键翻译（第一次按下开始录音，第二次按下结束录音），支持按键打断，不支持连续翻译/云端VAD
 * 使用方法：本文件不参与编译，需要将本文件内容复制到playground.c中，然后编译运行
 */
#include <stdbool.h>
#include "playground.h"
#include "lingxin_app_config.h"
#include "chat_api.h"
#include "lingxin_log.h"

static char *get_app_id();
static char *get_license();
static char *get_sn();
static char *get_app_code();
static char *get_device_code();
static char* biz_func();
static void event_listener(ChatLifeCycleEvent event, void *payload);

bool is_recording = false;

void playground_click_event() {
    if (is_recording) {
        is_recording = false;
        stop_chat_record(NULL);
    } else {
        StartNewChatProps props = get_start_new_chat_default_props();
        props.task = "translate";
        start_new_chat(&props);
        is_recording = true;
    }
}

void playground_net_connected() {
    lingxin_log_debug("voiceChat init begin");
    VoiceChatInitProps props = get_voice_chat_init_default_props();
    props.flash_cache_path = FLASH_CACHE_PATH;
    props.auth_app_id_get_func = get_app_id;
    props.auth_license_get_func = get_license;
    props.auth_sn_get_func = get_sn;
    props.auth_app_code_get_func = get_app_code;
    props.device_code_get_func = get_device_code;
    props.chat_biz_parameter_get_func = biz_func;
    props.chat_life_cycle_event_listener = event_listener;
    props.welcome_audio_path = WELCOME_AUDIO_PATH;
    props.is_schedule_task_on = 1;
    int res = voice_chat_init(&props);
    if (res >= 0) {
        lingxin_log_debug("voiceChat init finish");
    } else {
        lingxin_log_error("voiceChat init fail");
    }
}

void playground_wake_up() {
}


static char *get_app_id() {
    return LINGXIN_APP_ID;
}
static char *get_license() {
    return LINGXIN_LICENSE;
}
static char *get_sn() {
    return LINGXIN_SN;
}
static char *get_app_code() {
    return LINGXIN_APP_CODE;
}
static char *get_device_code() {
    return LINGXIN_DEVICE_CODE;
}
static char* biz_func() {
    return "{\"target_language\":\"en\",\"enable_asr_return_text\":true,\"enable_asr_stream_return_text\":false,\"need_voice\":true}";
}
static void event_listener(ChatLifeCycleEvent event, void *payload)
{
    switch (event)
    {
    case CHAT_LIFE_CYCLE_EVENT_EXIT:
    {
        ExitPayload *ep = (ExitPayload *)payload; // 安全转换
        printf("%s CHAT_LIFE_CYCLE_EVENT_EXIT: exit_code=%d\n", __func__, ep->exit_code);
        if (ep->exit_code == EXIT_REASON_WEBSOCKET_CONNECTION_FAILED)
        {
            playground_play_tone_file(CONFIG_VOICE_PROMPT_FILE_PATH "WSConnFail.mp3");
        }
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_SCHEDULE_EMIT:
        printf("CHAT_LIFE_CYCLE_EVENT_SCHEDULE_EMIT");
        break;
    case CHAT_LIFE_CYCLE_EVENT_TEXT_OUT:
        printf("CHAT_LIFE_CYCLE_EVENT_TEXT_OUT: %s\n", (char *)payload);
        break;
    case CHAT_LIFE_CYCLE_EVENT_PLAY_END: {
        printf("CHAT_LIFE_CYCLE_EVENT_PLAY_END");
        exit_chat(NULL);
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_CHAT_PHASE_CHANGE: {
        ChatPhaseChangePayload *cpp = (ChatPhaseChangePayload *)payload;  // 安全转换
        printf("CHAT_LIFE_CYCLE_EVENT_CHAT_PHASE_CHANGE: phase_code=%d\n", cpp->phase_code);
        break;
    }
    case CHAT_LIFE_CYCLE_EVENT_ERROR:
        printf("CHAT_LIFE_CYCLE_EVENT_ERROR: %s\n", (char *)payload);
        break;
    default:
        break;
    }
}