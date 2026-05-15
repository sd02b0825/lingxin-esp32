#include "lingxin_local_player.h"
#include "lingxin_log.h"
#include "chat_state_machine.h"

static int initial_volume = 80;
static lingxin_local_player_t welcome_audio_player = NULL;
static lingxin_local_player_t terminate_audio_player = NULL;
static lingxin_local_player_t continue_audio_player = NULL;
static char *welcome_audio_path = NULL;
static char *terminate_audio_path = NULL;
static char *continue_audio_path = NULL;

void module_local_play_set_welcome_audio_path(char *audio_path) {
    welcome_audio_path = audio_path;
}
void module_local_play_set_terminate_audio_path(char *audio_path) {
    terminate_audio_path = audio_path;
}
void module_local_play_set_continue_audio_path(char *audio_path) {
    continue_audio_path = audio_path;
}
static void create_player_and_start(lingxin_local_player_t *player, char *audio_path, lingxin_local_player_callback_t callback) {
    if (audio_path) {
        lingxin_local_player_play_param_t param = {
            .audio_path = audio_path,
            .initial_volume = initial_volume,
        };
        lingxin_log_ut(LINGXIN_DEBUG, "local_player_adapter_create");
        *player = lingxin_local_player_create();
        lingxin_log_ut(LINGXIN_DEBUG, "local_player_adapter_play");
        lingxin_local_player_play(*player, &param, callback);
    } else {
        callback(0); // 如果audio_path为空，则直接执行回调函数
    }
}
static void destory_player(lingxin_local_player_t *player) {
    if (*player) {
        lingxin_log_ut(LINGXIN_DEBUG, "local_player_adapter_destory");
        lingxin_local_player_destory(*player);
        *player = NULL;
    }
}

static void play_welcome_audio_callback(int result) {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "local_player_manager_welcome_audio_play_end", "%d", result);
    destory_player(&welcome_audio_player);
    state_machine_run_event(State_Event_Welcome_Play_End);
}
static void play_terminate_audio_callback(int result) {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "local_player_manager_terminate_audio_play_end", "%d", result);
    destory_player(&terminate_audio_player);
    state_machine_run_event(State_Event_TerminatePrompt_PlayEnd);
}
static void play_continue_audio_callback(int result) {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "local_player_manager_continue_audio_play_end", "%d", result);
    destory_player(&continue_audio_player);
    state_machine_run_event(State_Event_ContinuePrompt_PlayEnd);
}

// 播放欢迎语
void module_local_play_welcome_audio() {
    if (welcome_audio_player) {
        lingxin_log_ut(LINGXIN_WARN, "local_player_manager_welcome_audio_already_start");
        return;
    }
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "local_player_manager_welcome_audio_play_start", "%s", welcome_audio_path);
    create_player_and_start(&welcome_audio_player, welcome_audio_path, play_welcome_audio_callback);
}

// 播放打断音频
void module_local_play_terminate_audio() {
    if (terminate_audio_player) {
        lingxin_log_ut(LINGXIN_WARN, "local_player_manager_terminate_audio_already_start");
        return;
    }
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "local_player_manager_terminate_audio_play_start", "%s", terminate_audio_path);
    create_player_and_start(&terminate_audio_player, terminate_audio_path, play_terminate_audio_callback);
}

// 播放连续对话的音频
void module_local_play_continue_audio() {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "local_player_manager_continue_audio_play_start", "%s", continue_audio_path);
    create_player_and_start(&continue_audio_player, continue_audio_path, play_continue_audio_callback);
}

// 设置本地播放音频的音量
void module_local_play_set_volume(int volume) 
{
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "local_player_manager_set_volume", "%d", volume);
    initial_volume = volume;
    
    if (welcome_audio_player) {
        lingxin_log_ut(LINGXIN_DEBUG, "local_player_adapter_set_volume welcome_audio_player");
        lingxin_local_player_set_volume(welcome_audio_player, volume);
    }
    if (terminate_audio_player) {
        lingxin_log_ut(LINGXIN_DEBUG, "local_player_adapter_set_volume terminate_audio_player");
        lingxin_local_player_set_volume(terminate_audio_player, volume);
    }
    if (continue_audio_player) {
        lingxin_log_ut(LINGXIN_DEBUG, "local_player_adapter_set_volume continue_audio_player");
        lingxin_local_player_set_volume(continue_audio_player, volume);
    }
}

