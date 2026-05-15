/**
 * 适配层可选方法中的内部默认实现 
 */
#include "audio_buffer_play.h"
#include "lingxin_recorder.h"
#include "lingxin_log.h"
#include <stddef.h>
#include <string.h>

#ifndef _MSC_VER
__attribute__((weak)) bool lingxin_recorder_format_check(char *format) {
    return strcmp(format, "pcm") == 0;
}

__attribute__((weak)) bool module_bufferPlay_formatCheck(char *format) {
    return strcmp(format, "mp3") == 0;
}
#endif