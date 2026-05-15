#ifndef __LINGXIN_RECORDER_MANAGER_H__
#define __LINGXIN_RECORDER_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef void (*LingxinRecorderStartCallback)(bool is_success);
typedef void (*LingxinRecorderStopCallback)(bool is_success);
typedef void (*LingxinRecorderDataCallback)(void *buf, int rlen, int index);

/**
 * 初始化录音模块
 * @param custom_send_uni_size 录音单次发送的大小
 * @param custom_send_cbuf_scale 录音缓冲区相对于录音单次发送的大小比例
 */
int module_record_manager_init(int custom_send_uni_size, int custom_send_cbuf_scale);

/**
 * 开启录音
 * @param start_callback 录音开始回调
 */
int module_record_start(LingxinRecorderStartCallback start_callback);

/**
 * 录音可以开始发送
 * @param data_callback 录音数据回调
 */
void module_record_start_send(LingxinRecorderDataCallback data_callback);

/**
 * 录音结束
 * @param wait_send_left 是否等待发送剩余数据
 * @param stop_callback 结束回调
 */
void module_record_stop(int wait_send_left, LingxinRecorderStopCallback stop_callback);

#ifdef __cplusplus
}
#endif

#endif /* __LINGXIN_RECORDER_MANAGER_H__ */