
#ifndef AUDIO_BUFFER_PLAY_H
#define AUDIO_BUFFER_PLAY_H

#include <stdbool.h>
#include "download_audio_play_interface.h"

// 功能：在方法里面实现模块的初始化逻辑。并且Chat套件内核会多次回调这个方法，如果当前模块已经初始化成功，可直接回调初始化成功的回调事件。
// 调用时机：由chat套件内核发起调用，客户实现
void module_bufferPlay_audioInit(PlaybackEventHandler callback, void *user_data);

// buf为mp3数据 rlen为当前数据长度
void module_bufferPlay_data(void *buf, int rlen);

// 功能：指Chat套件内核调用流式播放模块告诉他已经没有流式播放数据了，并非要立刻停止播放。
// 调用时机：由chat套件内核发起调用，客户实现
void module_bufferPlay_audioEnd();

// 功能：停止当前的播放逻辑
// 调用时机：由chat套件内核发起调用，客户实现
void module_bufferPlay_terminate();

// 功能：设置当前播放的音量
// 调用时机：由chat套件内核发起调用，客户实现
void module_bufferPlay_setVolume(int volume);

// （可选实现）
// 功能：检查当前设备类型的流式播放格式是否支持
// 调用时机：由chat套件内核发起调用，客户实现
// 参数format：流式播放格式，已有格式为"mp3"、"raw_opus"
// 返回值：true-支持，false-不支持
bool module_bufferPlay_formatCheck(char *format);

#endif // AUDIO_BUFFER_PLAY_H
