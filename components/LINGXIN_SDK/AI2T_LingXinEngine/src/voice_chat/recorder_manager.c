#include <stdlib.h> 
#include <string.h>
#include "lingxin_cbuffer.h"
#include "lingxin_mutex.h"
#include "lingxin_semaphore.h"
#include "lingxin_thread.h"
#include "lingxin_recorder.h"
#include "chat_state_machine.h"
#include "lingxin_log.h"
#include <stdio.h>
#include "lingxin_system_time.h"
#include "lingxin_memory.h"
#include "upload_record_interface.h"
#include "lingxin_recorder_manager.h"

#define LINGXIN_RECORDER_CALLBACK_LIST_MAX_LENGTH 5

static lingxin_mutex_t send_thread_mutex = NULL;
static lingxin_mutex_t record_close_mutex = NULL;
static lingxin_mutex_t record_close_callback_list_mutex = NULL;

static lingxin_recorder_t *lingxin_recorder = NULL;
static int recorder_ready = 0;
static int server_ready = 0;
static int send_flag = 0;
static LingxinCircularBuffer* send_cbuf = NULL;
static lingxin_semaphore_t send_r_sem = NULL;
static int send_thread_running = 0;
static lingxin_tid_t send_thread_pid = 0;
static int* send_thread_pid_ptr = NULL;
static int send_thread_stop_flag = 0; // 0: 不退出 1: 立刻退出 2: 等待发完再退出

static int send_uni_size = 0;
static int send_cbuf_scale = 200;

typedef void (*SendThreadStopCallback)();

static void inner_record_init();
static void record_open_callback(int result);
static void record_close_callback_for_start(int result);
static void record_close_callback_for_stop(int result);
static void record_close_callback_for_stop_wait_send_left(int result);

static void* send_record(void *arg);
static int stop_send_thread(int wait_send);
static int start_send_thread();

static int get_frame_size();
static void set_send_flag();

/****************** 录音模块初始化 ******************/
int module_record_manager_init(int custom_send_uni_size, int custom_send_cbuf_scale) {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_manager_init", "send_uni_size is %d, send_cbuf_scale is %d", custom_send_uni_size, custom_send_cbuf_scale);
    if (!send_thread_mutex) {
        send_thread_mutex = lingxin_mutex_create();
    }
    if (!record_close_mutex) {
        record_close_mutex = lingxin_mutex_create();
    }
    if (!record_close_callback_list_mutex) {
        record_close_callback_list_mutex = lingxin_mutex_create();
    }
    if (custom_send_uni_size) {
        send_uni_size = custom_send_uni_size;
    }
    if (custom_send_cbuf_scale) {
        send_cbuf_scale = custom_send_cbuf_scale;
    }
    send_uni_size = get_frame_size();
    if (!send_cbuf) {
        send_cbuf = lingxin_cbuffer_init(send_cbuf_scale, send_uni_size);
        if (send_cbuf == NULL) {
            lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_init_fail");
            return -1;
        }
    }
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_init_success");
    return 0;
}


/****************** 录音器开启 ******************/
LingxinRecorderStartCallback temp_start_callback = NULL;
static void run_start_callback(bool is_success) {
    if (temp_start_callback) {
        temp_start_callback(is_success);
        temp_start_callback = NULL;
    } else {
        lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_run_start_callback_fail");
    }
}
int module_record_start(LingxinRecorderStartCallback start_callback) {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_recorder_start_called");
    temp_start_callback = start_callback;
    recorder_ready = 0;
    server_ready = 0;
    set_send_flag();
    // [开启录音-1]关闭可能存在的录音
    lingxin_mutex_lock(record_close_mutex);
    if (lingxin_recorder == NULL) {
        record_close_callback_for_start(0);
    } else {
        lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_close_before_recorder_start");
        lingxin_recorder_close(lingxin_recorder, record_close_callback_for_start);
    }
    return 0;
}
static void record_close_callback_for_start(int result) {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_close_callback_before_recorder_start");
    if (result == 0) {
        if (lingxin_recorder != NULL) {
            lingxin_log_ut(LINGXIN_DEBUG, "record_adapter_recorder_destory_before_recorder_start");
            lingxin_recorder_destroy(lingxin_recorder);
            lingxin_recorder = NULL;
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_destory_recorder_success_before_recorder_start");
        }
        lingxin_mutex_unlock(record_close_mutex);
        // [开启录音-2]退出可能存在的录音发送线程
        int res = stop_send_thread(0);
        if (res == 0) {
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_stop_send_thread_success_before_recorder_start");
            // [开启录音-3]初始化新的录音
            inner_record_init();
        } else {
            lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_stop_send_thread_fail_before_recorder_start");
            run_start_callback(false);
        }
    } else {
        lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_close_recorder_fail_before_recorder_start");
        lingxin_mutex_unlock(record_close_mutex);
        run_start_callback(false);
    }
}
static void inner_record_init() {
    // [开启录音-3.1]开启新的录音发送线程
    int res = start_send_thread();
    if (res == 0) {
        lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_start_send_thread_success");
        // [开启录音-3.2]开启录音
        lingxin_mutex_lock(record_close_mutex);
        recorder_ready = 1;
        set_send_flag();
        lingxin_recorder = lingxin_recorder_create();
        lingxin_recorder_open_param_t props = {
            .frame_size = send_uni_size,
        };
        lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_open");
        lingxin_recorder_open(lingxin_recorder, &props, record_open_callback);
    } else {
        lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_start_send_thread_fail");
        run_start_callback(false);
    }
}
static void record_open_callback(int result) {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_open_callback"); 
    if (result == 0) {
        lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_open_recorder_success");
        // [开启录音-4]通知状态机录音模块启动已完成
        lingxin_mutex_unlock(record_close_mutex);
        run_start_callback(true);
    } else {
        lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_open_recorder_fail");
        lingxin_mutex_unlock(record_close_mutex);
        run_start_callback(false);
    }
}


/****************** 录音开始发送 ******************/

LingxinRecorderDataCallback temp_data_callback = NULL;
void module_record_start_send(LingxinRecorderDataCallback data_callback) {
    lingxin_log_ut(LINGXIN_DEBUG, "record_manager_start_send");
    temp_data_callback = data_callback;
    server_ready = 1;
    set_send_flag();
} 

/****************** 录音器结束 ******************/
static LingxinRecorderStopCallback temp_stop_callback_list[LINGXIN_RECORDER_CALLBACK_LIST_MAX_LENGTH];
static int temp_stop_callback_list_length = 0;
static void run_stop_callback(bool is_success) {
    while (1) {
        lingxin_mutex_lock(record_close_callback_list_mutex);
        if (!temp_stop_callback_list_length) {
            lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_run_stop_callback_fail");
            lingxin_mutex_unlock(record_close_callback_list_mutex);
            return;
        }
        LingxinRecorderStopCallback currrent_stop_callback = temp_stop_callback_list[0];
        for (int i = 1; i < temp_stop_callback_list_length; i++) {
            temp_stop_callback_list[i - 1] = temp_stop_callback_list[i];
        }
        temp_stop_callback_list[temp_stop_callback_list_length - 1] = NULL;
        temp_stop_callback_list_length--;
        lingxin_mutex_unlock(record_close_callback_list_mutex);
        currrent_stop_callback(is_success);
    }
}
void module_record_stop(int wait_send_left, LingxinRecorderStopCallback stop_callback) {
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_manager_stop_called", "wait_send_left = %d", wait_send_left);
    if (stop_callback) {
        if (temp_stop_callback_list_length >= LINGXIN_RECORDER_CALLBACK_LIST_MAX_LENGTH) {
            lingxin_log_ut(LINGXIN_WARN, "recorder_manager_stop_callback_list_full");
        } else {
            lingxin_mutex_lock(record_close_callback_list_mutex);
            temp_stop_callback_list[temp_stop_callback_list_length++] = stop_callback;
            lingxin_mutex_unlock(record_close_callback_list_mutex);
        }
    }
    // [正常结束录音-1]关闭可能存在的录音
    lingxin_recorder_callback_t callback = wait_send_left 
        ? record_close_callback_for_stop_wait_send_left 
        : record_close_callback_for_stop;
    lingxin_mutex_lock(record_close_mutex);
    if (lingxin_recorder == NULL) {
        callback(0);
    } else {
        lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_close_when_stop");
        lingxin_recorder_close(lingxin_recorder, callback);
    }
}
static void record_close_callback_for_stop(int result) {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_close_callback_when_stop");
    if (result == 0) {
        if (lingxin_recorder != NULL) {
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_destroy_when_stop");
            lingxin_recorder_destroy(lingxin_recorder);
            lingxin_recorder = NULL;
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_destory_recorder_success_when_stop");
        }
        lingxin_mutex_unlock(record_close_mutex);
        // [正常结束录音-2]退出可能存在的录音发送线程
        int res = stop_send_thread(0);
        if (res == 0) {
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_stop_send_thread_success_when_stop");
            // [正常结束录音-3]通知状态机录音模块正常结束已完成
            run_stop_callback(true);
        } else {
            lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_stop_send_thread_fail_when_stop");
            run_stop_callback(false);
        }
    } else {
        lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_close_recorder_fail_when_stop");
        lingxin_mutex_unlock(record_close_mutex);
        run_stop_callback(false);
    }
}
static void record_close_callback_for_stop_wait_send_left(int result) {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_close_callback_when_stop_wait");
    if (result == 0) {
        if (lingxin_recorder != NULL) {
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_recorder_destroy_when_stop_wait");
            lingxin_recorder_destroy(lingxin_recorder);
            lingxin_recorder = NULL;
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_destory_recorder_success_when_stop_wait");
        }
        lingxin_mutex_unlock(record_close_mutex);
        // [正常结束录音-2]退出可能存在的录音发送线程，但等待发完当前内容
        int res = stop_send_thread(1);
        if (res == 0) {
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_manager_stop_send_thread_success_when_stop_wait");
            // [正常结束录音-3]通知状态机录音模块正常结束已完成
            // state_machine_run_event(State_Event_Record_Stop);
        } else {
            lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_stop_send_thread_fail_when_stop_wait");
            run_stop_callback(false);
        }
    } else {
        lingxin_log_ut(LINGXIN_ERROR, "recorder_manager_close_recorder_fail_when_stop_wait");
        lingxin_mutex_unlock(record_close_mutex);
        run_stop_callback(false);
    }
}


/****************** 发送线程（读缓存） ******************/
static int start_send_thread() {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_thread_start_called");
    lingxin_mutex_lock(send_thread_mutex);

    // 0. 检查发送线程是否已经运行
    if (send_thread_running) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_thread_start_fail", "send thread is already running.");
        goto start_send_thread_err;
    }
    // 1. 清空录音发送缓冲区
    if (send_cbuf == NULL) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_thread_start_fail", "fail to get send buffer.");
        goto start_send_thread_err;
    }
    lingxin_cbuffer_reset(send_cbuf);
    // 2. 初始化信号量
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_semaphore_create");
    send_r_sem = lingxin_semaphore_create(0);
    if (send_r_sem == NULL) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_thread_start_fail", "fail to initialize send semaphore.");
        goto start_send_thread_err;
    }
    // 3. 重置flag
    send_thread_stop_flag = 0;
    // 4. 创建录音发送线程
    long now_time = lingxin_get_timestamp_s();
    char name[16];
    snprintf(name, sizeof(name), "s_%ld", now_time);
    lingxin_thread_param_t thread_param = {
        .priority = 16,
        .stack_size = 4096*2,
        .name = name,
    };
    if (!send_thread_pid_ptr) {
        send_thread_pid_ptr = (int*)lingxin_malloc(sizeof(int));
    }
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_thread_create");
    int ret = lingxin_thread_create(send_thread_pid_ptr, &thread_param, send_record, NULL);
    if (ret == 0) {  // 检查线程创建是否成功
        send_thread_pid = *send_thread_pid_ptr;
        lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_thread_start_success", "pid is %d", send_thread_pid);
        send_thread_running = 1;
    } else {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_thread_start_fail", "fail to create send thread, ret is %d.", ret);
        send_thread_pid = 0;
        send_thread_running = 0;
        goto start_send_thread_err;
    }
    lingxin_mutex_unlock(send_thread_mutex);
    return 0;

start_send_thread_err:
    lingxin_mutex_unlock(send_thread_mutex);
    return -1;
}
static int stop_send_thread(int wait_send) {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_thread_stop_called");
    lingxin_mutex_lock(send_thread_mutex);
    if (send_thread_running) {
        lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_thread_stop_start", "wait_send is %d", wait_send);
        if (wait_send) {
            send_thread_stop_flag = 2;
            if (send_flag) {
                lingxin_semaphore_set_value(send_r_sem, 0);
                lingxin_semaphore_post(send_r_sem);
            }
            goto finish_stop_send_thread;
        } else {
            send_thread_stop_flag = 1;
            lingxin_semaphore_set_value(send_r_sem, 0);
            lingxin_semaphore_post(send_r_sem);
            int retry = 0;
            while (send_thread_running && retry < 500) {
                lingxin_thread_sleep(10);
                retry++;
            }
            if (retry >= 500) {
                lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_thread_stop_fail", "stop send thread timeout.");
                if (send_thread_pid) {
                    send_thread_running = 0;
                    send_thread_pid_ptr = (int*)lingxin_malloc(sizeof(int));
                    send_thread_pid = 0;
                }
            } else {
                lingxin_log_ut(LINGXIN_DEBUG, "recorder_thread_stop_success");
                if (send_thread_pid) {
                    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_thread_destroy");
                    lingxin_thread_destroy(send_thread_pid, LINGXIN_THREAD_DESTROY_WAIT);
                    lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_thread_destroy_end");
                    send_thread_pid = 0;
                }
            }
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_thread_destroy_success");
        }
    }
    if (send_r_sem) {
        lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_semaphore_destroy");
        lingxin_semaphore_destroy(send_r_sem);
        send_r_sem = NULL;
    }

finish_stop_send_thread:
    lingxin_mutex_unlock(send_thread_mutex);
    return 0;
}
static void* send_record(void *arg) {
    lingxin_log_ut(LINGXIN_DEBUG, "recorder_thread_entry_function_called");
    lingxin_tid_t pid;
    int ret;
    char *buf = lingxin_malloc(send_uni_size);
    if (buf == NULL) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_thread_entry_function_fail", "buf malloc failed");
        return NULL;
    }

    int count = 0;
	while(1) {
		lingxin_semaphore_pend(send_r_sem, 0);
        pid = send_thread_pid;
        if (send_thread_stop_flag == 1) {
            lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_thread_receive_stop_signal", "flag is %d", send_thread_stop_flag);
            break;
        }
        // 读取buffer的大小和写入一致，不存在写入数据不到current_frame_size的情况
        while(lingxin_cbuffer_size(send_cbuf) >= 1) {
            if (send_thread_stop_flag == 1) {
                lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_thread_receive_stop_signal", "flag is %d", send_thread_stop_flag);
                goto send_record_thread_exit;
            }
            ret = lingxin_cbuffer_get(send_cbuf, buf);
            if(ret < 0){
                lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_thread_read_data_error", "ret=%d", ret);
                continue;
            }
            // lingxin_log_debug("recorder_thread_send_data: length is %d", send_uni_size);
            if (temp_data_callback) {
                temp_data_callback(buf, send_uni_size, ++count);
            }
            if (pid != send_thread_pid) {
                lingxin_log_debug("[send_record]清除游离线程");
                if (buf) {
                    lingxin_free(buf);
                    buf = NULL;
                }
                lingxin_log_debug("[send_record]清除游离线程，释放内存");
                while (1) {
                    lingxin_thread_sleep(60000);
                }
                return NULL;
            }
        }
        if (send_thread_stop_flag == 1 || send_thread_stop_flag == 2) {
            lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_thread_receive_stop_signal", "flag is %d", send_thread_stop_flag);
            break;
        }
	}
send_record_thread_exit:
    if (buf) {
        lingxin_free(buf);
        buf = NULL;
    }
    if (send_r_sem) {
        lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_semaphore_destroy");
        lingxin_semaphore_destroy(send_r_sem);
        send_r_sem = NULL;
    }
    pid = send_thread_pid; // 在修改线程运行状态前先保存pid，不要放后面的if语句里，因为一旦send_thread_running设置为0，send_thread_pid就可能会被置为NULL
    send_thread_running = 0;

    // 如果此时是等待发完当前内容，则需要通知状态机录音模块正常结束已完成
    if (send_thread_stop_flag == 2) {
        send_thread_pid = 0; // 将send_thread_pid置为NULL，后续用pid销毁自己，防止外部回调重复新建线程而冲突
        lingxin_log_ut_with_args(LINGXIN_DEBUG, "recorder_thread_wait_send_complete", "send_thread_pid=%d, pid=%d", send_thread_pid, pid);
        run_stop_callback(true);
        if (pid) {
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_thread_destroy_self");
            lingxin_thread_destroy(pid, LINGXIN_THREAD_DESTROY_WAIT);
            lingxin_log_ut(LINGXIN_DEBUG, "recorder_adapter_thread_destroy_self_end");
        }
    }
    return NULL;
}

/****************** 接收录音数据写入缓存（写缓存） ******************/
void lingxin_process_record_data(void *data, int len) {
    if (!send_thread_running) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_data_process_fail", "send_thread is not running.");
        return;
    }
    if (send_r_sem == NULL) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_data_process_fail", "send_r_sem is NULL.");
        return;
    }
    if (data == NULL) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_data_process_fail", "data is NULL.");
        return;
    }
    if (send_cbuf == NULL) {
        lingxin_log_ut_with_args(LINGXIN_ERROR, "recorder_data_process_fail", "send_cbuf is NULL.");
        return;
    }
    lingxin_cbuffer_put(send_cbuf, data);
    // lingxin_log_debug("recorder_data_process_success: len=%d, send_flag=%d", len, send_flag);
    if (send_flag) {
        lingxin_semaphore_set_value(send_r_sem, 0);
        lingxin_semaphore_post(send_r_sem);
    }
}

/****************** 相关参数获取 ******************/
// 获取帧大小
static int get_frame_size() {
    if (send_uni_size <= 0) {
        return lingxin_recorder_get_size_per_ms() * 20; // 默认20ms写一次
    } else {
        return send_uni_size;
    }
}

static void set_send_flag() {
    if (recorder_ready && server_ready) {
        send_flag = 1;
        if (send_thread_stop_flag == 2) {
            // 录音已经停止，需要通知发送线程发送剩余内容
            lingxin_semaphore_set_value(send_r_sem, 0);
            lingxin_semaphore_post(send_r_sem);
        }
    } else {
        send_flag = 0;
    }
}