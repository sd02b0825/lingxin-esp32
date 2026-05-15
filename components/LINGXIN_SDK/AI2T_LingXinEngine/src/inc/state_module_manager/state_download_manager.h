
#include "download_audio_play_interface.h"
#include "chat_state_machine.h"
#include "chat_state_machine_event.h"



// 初始化播放管理器（必须先调用）
void playback_manager_init(ChatStateMediaType type);

// 流式喂数据（核心接口）
void playback_manager_feed_data(void *buf, int len);

// 通知流结束（不再有数据，播放器可播完缓冲区）
void playback_manager_end_stream(void);

// 立即终止播放（打断）
void playback_manager_terminate(void);

// 设置音量（0~100）
void playback_manager_set_volume(int volume);