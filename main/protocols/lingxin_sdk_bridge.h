/**
 * lingxin_sdk_bridge.h - C/C++ bridge declarations
 *
 * Provides extern "C" functions that are called from SDK adapter C code,
 * implemented in lingxin_sdk_bridge.cc to access C++ AudioService/Board.
 */

#ifndef LINGXIN_SDK_BRIDGE_H
#define LINGXIN_SDK_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AudioService record bridge ---- */

/**
 * Start feeding AudioProcessor PCM output to SDK record ringbuf.
 * Called by lingxin_recorder_open().
 */
void audio_service_start_record_to_sdk(void);

/**
 * Stop feeding AudioProcessor PCM output to SDK record ringbuf.
 * Called by lingxin_recorder_close().
 */
void audio_service_stop_record_to_sdk(void);

/**
 * Write PCM data to SDK record ringbuf.
 * Called from AudioProcessor OnOutput callback when in SDK mode.
 */
int lingxin_record_write_pcm(const uint8_t *data, size_t len);

/**
 * Check if SDK record ringbuf is available.
 */
int lingxin_record_ringbuf_available(void);

/**
 * Check if SDK record mode is active.
 */
int lingxin_sdk_is_record_mode(void);

/* ---- AudioService playback bridge ---- */

/**
 * Push encoded audio packet to AudioService decode queue.
 * Called by module_bufferPlay_data().
 * codec: "mp3", "pcm", "opus"
 * sample_rate: typically 16000
 */
void audio_service_push_decode_packet(const uint8_t *data, int len, const char *codec, int sample_rate);

/**
 * Reset AudioService decoder (stop current playback).
 * Called by module_bufferPlay_terminate().
 */
void audio_service_reset_decoder(void);

/* ---- AudioService local sound bridge ---- */

/**
 * Play a local sound file via AudioService.
 * Called by lingxin_local_player_play().
 * audio_path: filesystem path to audio file
 */
void audio_service_play_local_sound(const char *audio_path);

/* ---- Device info bridge ---- */

/**
 * Get device name from v2.6.6 Board.
 */
const char *lingxin_bridge_get_device_name(void);

/**
 * Get device version from v2.6.6 build info.
 */
const char *lingxin_bridge_get_device_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LINGXIN_SDK_BRIDGE_H */
