
#ifndef DOWNLOAD_AUDIO_PLAY_INTERFACE_H
#define DOWNLOAD_AUDIO_PLAY_INTERFACE_H

#include <stdint.h>
#include "chat_state_machine_event.h"
typedef enum {
    Lingxin_Download_Audio_InitEnd,
    Lingxin_Download_Audio_TerminateEnd,
    Lingxin_Download_Audio_PlayEnd
} LingxinDownloadAudioEvent;

typedef void (*PlaybackEventHandler)(LingxinDownloadAudioEvent event, void *user_data);

typedef struct {
    void (*init)(PlaybackEventHandler callback, void *user_data);
    void (*feedData)(const void *buf, int len);
    void (*endOfStream)(void);
    void (*terminate)(void);
    void (*setVolume)(int volume);
} PlaybackInterface;

#endif // DOWNLOAD_AUDIO_PLAY_INTERFACE_H