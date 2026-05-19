/**
 * lingxin_audio_record.c - v2.6.6 adapter
 * 
 * 重写适配层：对接 v2.6.6 AudioService 上行 PCM 输出到 SDK record ringbuf。
 * 不再依赖 ESP-GMF (lingxin_gmf)，而是由 AudioService 的 AudioProcessor
 * 输出直接写入 ringbuf，本模块的 audio_processing_task 从 ringbuf 读取
 * 并通过 lingxin_process_record_data 送入 SDK。
 */

#include "lingxin_recorder.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lingxin_log.h"
#include "lingxin_memory.h"
#include "lingxin_semaphore.h"
#include "lingxin_system_time.h"

static const char *TAG = "lx_adapter_recorder";

#define RINGBUF_SIZE (1024 * 256)  /* 256KB, ~8s 16kHz 16bit mono PCM */
#define RINGBUF_LOG_INTERVAL_US (500000)

typedef struct {
    int frame_size;
    volatile bool should_stop;
    lingxin_semaphore_t task_done_sem;
    RingbufHandle_t ringbuf;       /* SDK record ringbuf */
    uint8_t *pending_buf;          /* partial frame between ringbuf chunks */
    size_t pending_len;
    lingxin_recorder_callback_t open_callback;
} recorder_hdl;

/**
 * Feed PCM into SDK send buffer in fixed frame_size chunks.
 * lingxin_cbuffer_put always copies frame_size bytes; variable-length input must be split here.
 */
static void feed_pcm_in_frames(recorder_hdl *hdl, const uint8_t *data, size_t len)
{
    const int fs = hdl->frame_size;
    const uint8_t *src = data;
    size_t src_len = len;

    if (!hdl || fs <= 0 || !hdl->pending_buf || !data || len == 0) {
        return;
    }

    if (hdl->pending_len > 0) {
        const size_t need = (size_t)fs - hdl->pending_len;
        const size_t take = src_len < need ? src_len : need;
        memcpy(hdl->pending_buf + hdl->pending_len, src, take);
        hdl->pending_len += take;
        src += take;
        src_len -= take;
        if (hdl->pending_len == (size_t)fs) {
            lingxin_process_record_data(hdl->pending_buf, fs);
            hdl->pending_len = 0;
        }
    }

    while (src_len >= (size_t)fs) {
        lingxin_process_record_data(src, fs);
        src += fs;
        src_len -= fs;
    }

    if (src_len > 0) {
        memcpy(hdl->pending_buf, src, src_len);
        hdl->pending_len = src_len;
    }
}

/* Global ringbuf handle for AudioService bridge to write PCM data into */
static RingbufHandle_t g_record_ringbuf = NULL;

static int64_t s_ringbuf_fail_last_log_us = 0;
static uint32_t s_ringbuf_fail_count = 0;

/**
 * AudioService bridge: call this from C++ to write PCM data into SDK record ringbuf.
 * This function is declared extern in the bridge header.
 */
int lingxin_record_write_pcm(const uint8_t *data, size_t len)
{
    if (g_record_ringbuf == NULL || data == NULL || len == 0) {
        return -1;
    }
    BaseType_t ret = xRingbufferSend(g_record_ringbuf, data, len, pdMS_TO_TICKS(100));
    if (ret != pdTRUE) {
        s_ringbuf_fail_count++;
        int64_t now = esp_timer_get_time();
        if (now - s_ringbuf_fail_last_log_us >= RINGBUF_LOG_INTERVAL_US) {
            ESP_LOGW(TAG, "record ringbuf full, dropped %lu chunks (last len=%u)",
                     (unsigned long)s_ringbuf_fail_count, (unsigned)len);
            s_ringbuf_fail_last_log_us = now;
            s_ringbuf_fail_count = 0;
        }
        return -2;
    }
    return 0;
}

/**
 * Check if the record ringbuf is available (i.e., recorder has been created and is open)
 */
int lingxin_record_ringbuf_available(void)
{
    return (g_record_ringbuf != NULL) ? 1 : 0;
}

static void audio_processing_task(void *arg)
{
    recorder_hdl *hdl = (recorder_hdl *)arg;
    if (!hdl) {
        lingxin_log_error("Recorder handle is NULL in audio_processing_task");
        vTaskDelete(NULL);
        return;
    }

    lingxin_log_debug("audio_processing_task started, frame_size=%d", hdl->frame_size);

    while (hdl && !hdl->should_stop) {
        size_t actual_size = 0;
        void *item = xRingbufferReceive(hdl->ringbuf, &actual_size, pdMS_TO_TICKS(10));

        if (item != NULL && actual_size > 0) {
            feed_pcm_in_frames(hdl, (const uint8_t *)item, actual_size);
            vRingbufferReturnItem(hdl->ringbuf, item);
        }
    }

    lingxin_log_debug("audio_processing_task exiting");

    if (hdl->task_done_sem) {
        lingxin_semaphore_post(hdl->task_done_sem);
    }
    vTaskDelete(NULL);
}

lingxin_recorder_t lingxin_recorder_create()
{
    recorder_hdl *hdl = lingxin_calloc(1, sizeof(recorder_hdl));
    if (!hdl) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "adapter_recorder_create_fail", "Failed to calloc recorder hdl");
        goto create_fail;
    }

    hdl->task_done_sem = lingxin_semaphore_create(0);
    if (!hdl->task_done_sem) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "adapter_recorder_create_fail", "Failed to create task_done_sem");
        goto create_fail;
    }

    hdl->ringbuf = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (!hdl->ringbuf) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "adapter_recorder_create_fail", "Failed to create record ringbuf");
        goto create_fail;
    }
    g_record_ringbuf = hdl->ringbuf;

    lingxin_log_ut(LINGXIN_DEBUG, "adapter_recorder_create_success");
    return (lingxin_recorder_t)hdl;

create_fail:
    if (hdl) {
        if (hdl->task_done_sem) {
            lingxin_semaphore_destroy(hdl->task_done_sem);
        }
        if (hdl->ringbuf) {
            vRingbufferDelete(hdl->ringbuf);
        }
        lingxin_free(hdl);
    }
    g_record_ringbuf = NULL;
    return NULL;
}

/**
 * Called on the main thread after playback idle and SDK record mode start.
 */
void lingxin_recorder_finish_open(void *recorder)
{
    if (!recorder) {
        return;
    }

    recorder_hdl *hdl = (recorder_hdl *)recorder;
    lingxin_recorder_callback_t callback = hdl->open_callback;
    hdl->open_callback = NULL;

    char task_name[32];
    long now_time = lingxin_get_timestamp_s();
    snprintf(task_name, sizeof(task_name), "lx_r_%ld", now_time);
    lingxin_log_debug("create audio processing task: %s", task_name);

    BaseType_t task_ret = xTaskCreate(audio_processing_task, task_name, 4096, hdl, 5, NULL);
    if (task_ret != pdPASS) {
        lingxin_log_error("Failed to create audio processing task");
        g_record_ringbuf = NULL;
        extern void audio_service_stop_record_to_sdk(void);
        audio_service_stop_record_to_sdk();
        extern void audio_service_schedule_recorder_uplink_end(void);
        audio_service_schedule_recorder_uplink_end();
        if (callback) {
            callback(-1);
        }
        return;
    }

    lingxin_log_ut(LINGXIN_DEBUG, "adapter_recorder_open_success");
    if (callback) {
        callback(0);
    }
}

void lingxin_recorder_open(lingxin_recorder_t recorder, lingxin_recorder_open_param_t *param, lingxin_recorder_callback_t callback)
{
    if (!recorder) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "adapter_recorder_open_fail", "recorder is null");
        goto open_err;
    }

    lingxin_log_ut(LINGXIN_DEBUG, "adapter_recorder_open_begin");

    recorder_hdl *hdl = (recorder_hdl *)recorder;
    hdl->should_stop = false;
    hdl->open_callback = callback;
    hdl->frame_size = param && param->frame_size ? param->frame_size : lingxin_recorder_get_size_per_ms() * 20;
    hdl->pending_len = 0;
    if (hdl->pending_buf) {
        lingxin_free(hdl->pending_buf);
        hdl->pending_buf = NULL;
    }
    hdl->pending_buf = (uint8_t *)lingxin_malloc((size_t)hdl->frame_size);
    if (hdl->pending_buf == NULL) {
        lingxin_log_error("Failed to allocate pending_buf, frame_size=%d", hdl->frame_size);
        goto open_err;
    }

    size_t dummy_size = 0;
    void *dummy_item;
    while ((dummy_item = xRingbufferReceive(hdl->ringbuf, &dummy_size, 0)) != NULL) {
        vRingbufferReturnItem(hdl->ringbuf, dummy_item);
    }

    g_record_ringbuf = hdl->ringbuf;

    extern void audio_service_schedule_recorder_uplink_begin(void *recorder_hdl);
    audio_service_schedule_recorder_uplink_begin(recorder);
    return;

open_err:
    if (callback) {
        callback(-1);
    }
}

void lingxin_recorder_close(lingxin_recorder_t recorder, lingxin_recorder_callback_t callback)
{
    if (recorder == NULL) {
        lingxin_log_error("recorder is null");
        goto close_err;
    }

    recorder_hdl *hdl = (recorder_hdl *)recorder;

    extern void audio_service_stop_record_to_sdk(void);
    audio_service_stop_record_to_sdk();

    g_record_ringbuf = NULL;

    if (hdl) {
        lingxin_log_debug("lingxin_recorder_close set should_stop");
        hdl->should_stop = true;
        if (hdl->task_done_sem) {
            lingxin_semaphore_pend(hdl->task_done_sem, 200);
            lingxin_log_debug("audio_processing_task exited");
        }
    }

    extern void audio_service_schedule_recorder_uplink_end(void);
    audio_service_schedule_recorder_uplink_end();

    if (callback) {
        callback(0);
    }
    return;

close_err:
    if (callback) {
        callback(-1);
    }
}

void lingxin_recorder_destroy(lingxin_recorder_t recorder)
{
    recorder_hdl *hdl = (recorder_hdl *)recorder;
    if (hdl) {
        g_record_ringbuf = NULL;
        if (hdl->ringbuf) {
            vRingbufferDelete(hdl->ringbuf);
            hdl->ringbuf = NULL;
        }
        if (hdl->pending_buf) {
            lingxin_free(hdl->pending_buf);
            hdl->pending_buf = NULL;
        }
        if (hdl->task_done_sem) {
            lingxin_semaphore_destroy(hdl->task_done_sem);
        }
        lingxin_free(hdl);
    }
}

int lingxin_recorder_get_size_per_ms()
{
    return 16000 * 1 * 2 / 1000;
}

bool lingxin_recorder_format_check(char* format)
{
    if (format && (strcmp(format, "pcm") == 0 || strcmp(format, "raw_opus") == 0)) {
        return true;
    }
    return false;
}
