#include "lingxin_user_track.h"
#include "cJSON.h"
#include "lingxin_voice_chat_config.h"
#include "lingxin_common.h"
#include "lingxin_file.h"
#include "lingxin_http.h"
#include "lingxin_log.h"
#include "lingxin_thread.h"
#include "lingxin_mutex.h"
#include "chat_state_machine.h"
#include "lingxin_system_time.h"
#include "lingxin_version.h"
#include "lingxin_device_info.h"
#include "lingxin_memory.h"

/**
  日志先写入内存，超过上限后缓存到文件，缓存到文件前，从主缓冲区切到备用缓冲区，切换后，清空主缓冲区
  内存中保存格式：[4位数字][log内容][4位数字][log内容]...
  文件中保存格式：
  {
    "sn": "xxx",
    "log": [
        {"content": "log内容" },
        { "content": "log内容"}
    ]
  }
 **/

// 核心节点统计文件
#define CORE_NODE_FILE_NAME "cn.lx"
// 日志缓存文件
#define USER_TRACK_FILE_NAME "ut.lx"
#define USER_TRACK_FILE_MAX_LENGTH (500 * 1024)   // 本地文件最大500K
#define USER_TRACK_MEMORY_BUFFER_SIZE (40 * 1024) // 缓冲区上限
#define MEMORY_LENGTH_FIELD_WIDTH 4               // 固定4位数字表示每一个内容的长度

#define SWITCH_UPDATE_TIME_INTERVAL 60 // 60秒更新一次日志开关
#define UPLOAD_TIME_INTERVAL 90        // 90秒上传一次日志

// 缓冲区结构
typedef struct
{
    lingxin_tid_t thread_id;
    lingxin_mutex_t mutex;
    lingxin_mutex_t core_node_mutex;
    bool running;
    // 写日志是频率很高的操作，不能频繁申请释放内存，否则会造成内存碎片化问题，所以用双缓冲方案
    char *buffer_a;          // 缓冲区A
    char *buffer_b;          // 缓冲区B
    char *recording_buffer;  // 记录log的缓冲区，可以指向缓冲区A或者缓冲区B
    char *will_cache_buffer; // 等待写入文件的缓冲区，可以指向缓冲区A或者缓冲区B
    int offset_recording_buffer;
    int offset_will_cache_buffer;
    bool enable_file_cache;
    volatile bool can_record;
    bool core_node_file_null;      // 文件清空是异步操作，这里用一个标志位来表示是否有内容
    bool ut_file_null;             // 文件清空是异步操作，这里用一个标志位来表示是否有内容
    char *file_path_core_node;     // 核心节点存储文件路径
    char *memory_buffer_core_node; // 核心节点存储内存，文件缓存不可用时激活
    int offset_core_node_buffer;
    char *file_path_ut; // 日志缓存存储文件路径
    long last_upload_time;
    long last_switch_update_time;
    char *log_upload_path;
    char *log_upload_host;
} lingxin_user_track;

static lingxin_user_track *lingxin_ut = NULL;

static int get_ut_file_length(char *file_name)
{
    if (!file_name)
    {
        lingxin_log_error("file name null");
        return -1;
    }
    return lingxin_file_length(file_name);
}

static void clear_file_content(char *file_path)
{
    lingxin_file_clear(file_path);
}

static bool parse_upload_result(char *response)
{
    if (!response)
    {
        lingxin_log_error("ut upload result null");
        return false;
    }
    cJSON *result_json = cJSON_Parse(response);
    if (!result_json)
    {
        lingxin_log_error("ut upload result parse error %s", response);
        return false;
    }
    cJSON *success_obj = cJSON_GetObjectItem(result_json, "success");
    bool upload_success = false;
    if (success_obj && cJSON_IsBool(success_obj))
    {
        upload_success = cJSON_IsTrue(success_obj);
    }
    cJSON_Delete(result_json);
    return upload_success;
}

static bool do_ut_upload(char *content_to_upload)
{
    char *sn = lingxin_auth_sn_get();
    char *app_id = lingxin_auth_appId_get();
    char *app_key = lingxin_auth_license_get();
    HttpConfig *config_upload = createHttpConfig(app_id, sn, app_key, lingxin_ut->log_upload_host, lingxin_ut->log_upload_path, content_to_upload);
    if (!config_upload)
    {
        return false;
    }
    char *post_result = NULL;
    http_post_without_callback(config_upload, &post_result);
    free_http_config(config_upload);

    if (!post_result)
    {
        lingxin_log_error("upload result null");
        return false;
    }
    bool upload_success = parse_upload_result(post_result);
    lingxin_free(post_result);
    if (!upload_success)
    {
        lingxin_log_error("upload failed");
        return false;
    }
    return true;
}

static int lxut_content_to_json(char *ut_content, int all_ut_content_len, char **jsonString)
{
    int cur_parse_pos = 0;
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        lingxin_log_error("json create fail");
        return -1;
    }
    char *sn = lingxin_auth_sn_get();
    cJSON_AddStringToObject(root, "sn", sn ? sn : "");
    cJSON *log_array = cJSON_CreateArray();
    if (!log_array)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddItemToObject(root, "log", log_array);

    while (cur_parse_pos < all_ut_content_len)
    {
        if (cur_parse_pos + MEMORY_LENGTH_FIELD_WIDTH > all_ut_content_len)
        {
            lingxin_log_error("Incomplete log data at offset %d", cur_parse_pos);
            break;
        }

        // 读取4位长度字段
        char length_str_temp[MEMORY_LENGTH_FIELD_WIDTH + 1];
        memcpy(length_str_temp, ut_content + cur_parse_pos, MEMORY_LENGTH_FIELD_WIDTH);
        length_str_temp[MEMORY_LENGTH_FIELD_WIDTH] = '\0';

        // 解析长度
        int length_of_cur_log = atoi(length_str_temp);
        if (length_of_cur_log < 0)
        {
            lingxin_log_error("Invalid length value at offset %d: %s", cur_parse_pos, length_str_temp);
            break;
        }
        // 检查是否有足够的数据
        if (cur_parse_pos + MEMORY_LENGTH_FIELD_WIDTH + length_of_cur_log > all_ut_content_len)
        {
            break;
        }
        cJSON *content_fragment = cJSON_CreateObject();
        if (content_fragment)
        {
            char *log_content = lingxin_malloc(length_of_cur_log + 1);
            if (log_content)
            {
                memcpy(log_content, ut_content + cur_parse_pos + MEMORY_LENGTH_FIELD_WIDTH, length_of_cur_log);
                log_content[length_of_cur_log] = '\0';

                cJSON_AddStringToObject(content_fragment, "content", log_content);
                cJSON_AddItemToArray(log_array, content_fragment);
                lingxin_free(log_content);
            }
            else
            {
                cJSON_Delete(content_fragment);
                lingxin_log_error("Failed to allocate memory for log content");
            }
        }
        cur_parse_pos += MEMORY_LENGTH_FIELD_WIDTH + length_of_cur_log;
    }
    *jsonString = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return cur_parse_pos;
}

static bool upload_file_cache(char *file_name)
{
    int total_file_length = get_ut_file_length(file_name);
    if (total_file_length <= 0)
    {
        lingxin_log_error("file length zero");
        return false;
    }
    int single_max_load_size = USER_TRACK_MEMORY_BUFFER_SIZE;
    int cur_file_read_pos = 0;
    char *content_file_segment = lingxin_calloc(1, single_max_load_size + 1);
    if (!content_file_segment)
    {
        lingxin_log_error("Failed to allocate memory for file content");
        return false;
    }
    bool is_file_clear = false;
    while (cur_file_read_pos < total_file_length)
    {
        int remaining_bytes = total_file_length - cur_file_read_pos;
        int actual_read_size = remaining_bytes > single_max_load_size ? single_max_load_size : remaining_bytes;

        bool read_result = lingxin_file_read(file_name, content_file_segment, cur_file_read_pos, actual_read_size);
        if (!read_result)
        {
            lingxin_log_error("Failed to read file content");
            break;
        }
        (content_file_segment)[actual_read_size] = '\0';
        char *content_file_json = NULL;
        // printf("content_file_segment: %s\n", content_file_segment);
        int parsed_index = lxut_content_to_json(content_file_segment, strlen(content_file_segment), &content_file_json);
        if (parsed_index == -1)
        {
            break;
        }
        if (parsed_index == 0 || !content_file_json)
        {
            is_file_clear = true;
            lingxin_log_error("no content has parsed, break");
            break;
        }
        // printf("lxut_content_to_json: %s\n", content_file_json);
        bool upload_result = do_ut_upload(content_file_json);
        if (upload_result)
        {
            is_file_clear = true;
        }
        cJSON_free(content_file_json);
        cur_file_read_pos += parsed_index;
    }
    // 文件内容解析失败，或者任意内容上传成功，清空文件
    if (is_file_clear)
    {
        clear_file_content(file_name);
    }
    lingxin_free(content_file_segment);
    return is_file_clear;
}

static void upload_memory_core_node_buffer()
{
    char *content_file_json = NULL;
    lxut_content_to_json(lingxin_ut->memory_buffer_core_node, lingxin_ut->offset_core_node_buffer, &content_file_json);
    if (content_file_json)
    {
        if (do_ut_upload(content_file_json))
        {
            lingxin_ut->offset_core_node_buffer = 0;
        }
        cJSON_free(content_file_json);
    }
}

static void upload_memory_will_cache_buffer()
{
    char *content_file_json = NULL;
    lxut_content_to_json(lingxin_ut->will_cache_buffer, lingxin_ut->offset_will_cache_buffer, &content_file_json);
    if (content_file_json)
    {
        if (do_ut_upload(content_file_json))
        {
            lingxin_ut->offset_will_cache_buffer = 0;
        }
        cJSON_free(content_file_json);
    }
}

static void upload_memory_recording_cache_buffer()
{
    char *content_file_json = NULL;
    lxut_content_to_json(lingxin_ut->recording_buffer, lingxin_ut->offset_recording_buffer, &content_file_json);
    if (content_file_json)
    {
        if (do_ut_upload(content_file_json))
        {
            lingxin_ut->offset_recording_buffer = 0;
        }
        cJSON_free(content_file_json);
    }
}

static bool parse_upload_switch_and_url(char *response, char **upload_url)
{
    if (!response)
    {
        lingxin_log_error("upload switch result null");
        return false;
    }
    cJSON *result_json = cJSON_Parse(response);
    if (!result_json)
    {
        lingxin_log_error("upload switch result parse error %s", response);
        return false;
    }
    cJSON *success_obj = cJSON_GetObjectItem(result_json, "success");
    bool switch_get_success = false;
    if (success_obj && cJSON_IsBool(success_obj))
    {
        switch_get_success = cJSON_IsTrue(success_obj);
    }
    if (!switch_get_success)
    {
        lingxin_log_error("success false");
        cJSON_Delete(result_json);
        return false;
    }
    cJSON *data_obj = cJSON_GetObjectItem(result_json, "data");
    if (!data_obj)
    {
        lingxin_log_error("upload switch result parse data error %s", response);
        cJSON_Delete(result_json);
        return false;
    }
    bool upload_switch = false;
    cJSON *enable_upload_obj = cJSON_GetObjectItem(data_obj, "enable_upload");
    if (enable_upload_obj && cJSON_IsBool(enable_upload_obj))
    {
        upload_switch = cJSON_IsTrue(enable_upload_obj);
    }
    else
    {
        lingxin_log_error("upload switch result parse switch error %s", response);
    }
    if (!upload_switch)
    {
        lingxin_log_error("upload switch is false");
        cJSON_Delete(result_json);
        return false;
    }
    cJSON *upload_url_obj = cJSON_GetObjectItem(data_obj, "upload_url");
    if (!upload_url_obj)
    {
        lingxin_log_error("upload switch result parse upload_url error %s", response);
    }
    if (cJSON_IsString(upload_url_obj) && upload_url_obj->valuestring)
    {
        *upload_url = lingxin_strdup(upload_url_obj->valuestring);
    }
    else
    {
        lingxin_log_error("upload switch result parse upload_url error %s", response);
    }
    cJSON_Delete(result_json);
    return upload_switch;
}

static bool local_file_check(char *file_name)
{
    if (!file_name)
    {
        lingxin_log_debug("file name null");
        return false;
    }
    bool fie_exist_result = lingxin_file_exist(file_name);
    if (fie_exist_result)
    {
        lingxin_log_debug("%s exist", file_name);
        return true;
    }
    bool create_result = lingxin_file_create(file_name, 0);
    if (!create_result)
    {
        lingxin_log_error("file_create func call fail");
        return false;
    }
    return true;
}

static bool memory_buffer_to_file(bool append, char *buffer, int buffer_size, char *file_name)
{
    if (!buffer || buffer_size <= 0)
    {
        return false;
    }
    return lingxin_file_write(file_name, append, buffer, buffer_size);
}

static bool if_ut_file_full()
{
    int file_length = get_ut_file_length(lingxin_ut->file_path_ut);
    bool result = file_length >= USER_TRACK_FILE_MAX_LENGTH;
    if (result)
    {
        lingxin_log_error("file length %ld > %d", file_length, USER_TRACK_FILE_MAX_LENGTH);
    }
    return result;
}

static void update_log_switch()
{
    lingxin_ut->last_switch_update_time = lingxin_get_timestamp_s();
    char *sn = lingxin_auth_sn_get();
    char body[64];
    snprintf(body, sizeof(body), "{\"sn\":\"%s\"}", sn);

    #ifdef ENV_DAILY
char* req_url = "math-daily.edu-aliyun.com";
#else
char* req_url = "eagent.edu-aliyun.com";
#endif

    HttpConfig *config_get_switch = createHttpConfig(lingxin_auth_appId_get(), sn, lingxin_auth_license_get(), req_url, LOG_UPLOAD_SWITCH_GET_PATH, body);
    if (!config_get_switch)
    {
        return;
    }
    char *post_result = NULL;
    http_post_without_callback(config_get_switch, &post_result);
    free_http_config(config_get_switch);
    if (!post_result)
    {
        lingxin_log_error("switch get result null");
        return;
    }
    char *url_to_upload = NULL;
    parse_upload_switch_and_url(post_result, &url_to_upload);
    lingxin_free(post_result);
    if (!url_to_upload)
    {
        return;
    }
    if(lingxin_ut->log_upload_host) {
        lingxin_free(lingxin_ut->log_upload_host);
    }
    if(lingxin_ut->log_upload_path) {
        lingxin_free(lingxin_ut->log_upload_path);
    }
    parse_host_path_from_url(url_to_upload, &lingxin_ut->log_upload_host, &lingxin_ut->log_upload_path);
    if (!lingxin_ut->log_upload_host || !lingxin_ut->log_upload_path)
    {
        lingxin_log_error("upload switch result parse url error, %s", url_to_upload);
    }
    lingxin_free(url_to_upload);
}

/**
 * 1分钟上传一次日志
 * 上传顺序：文件缓存 -> 待缓冲内存缓存 -> 正在记录的内存缓存
 */
static void try_to_upload()
{
    long now = lingxin_get_timestamp_s();
    // 1分钟上传执行一次
    if (now - lingxin_ut->last_upload_time < UPLOAD_TIME_INTERVAL)
    {
        return;
    }
    // 更新上传时间
    lingxin_ut->last_upload_time = now;
    // 上传开关未更新，则不进行上传
    if (!lingxin_ut->log_upload_host || !lingxin_ut->log_upload_path)
    {
        lingxin_log_warn("upload info null");
        return;
    }
    // 先上传核心节点记录
    if (!lingxin_ut->core_node_file_null)
    {
        lingxin_log_debug("upload core node file");

        if (upload_file_cache(lingxin_ut->file_path_core_node))
        {
            lingxin_ut->core_node_file_null = true;
        }
    }
    else if (lingxin_ut->offset_core_node_buffer > 0)
    {
        upload_memory_core_node_buffer();
    }

    lingxin_mutex_lock(lingxin_ut->mutex);
    lingxin_ut->can_record = false;
    lingxin_mutex_unlock(lingxin_ut->mutex);

    // 本地缓存文件不为空，则上传
    if (!lingxin_ut->ut_file_null)
    {
        lingxin_log_debug("upload ut file");
        bool upload_result = upload_file_cache(lingxin_ut->file_path_ut);
        if (upload_result)
        {
            lingxin_ut->ut_file_null = true;
        }
    }
    // 处理已满的缓冲区
    if (lingxin_ut->offset_will_cache_buffer > 0)
    {
        lingxin_log_debug("upload will cache");
        upload_memory_will_cache_buffer();
    }
    // 处理正在记录的缓冲区
    if (lingxin_ut->offset_recording_buffer > 0)
    {
        lingxin_log_debug("upload recording cache");
        upload_memory_recording_cache_buffer();
    }
    lingxin_mutex_lock(lingxin_ut->mutex);
    lingxin_ut->can_record = true;
    lingxin_mutex_unlock(lingxin_ut->mutex);
}

static void *ut_thread_routine(void *arg)
{
    // if (!local_file_check(lingxin_ut->file_path_core_node))
    // {
    //     lingxin_ut->memory_buffer_core_node = lingxin_calloc(1, USER_TRACK_MEMORY_BUFFER_SIZE);
    // }

    if (!local_file_check(lingxin_ut->file_path_ut))
    {
        lingxin_ut->enable_file_cache = false;
    }
    while (lingxin_ut->running)
    {
        // 仅当没有流式音频播放时，才执行文件缓存，避免造成播放卡顿
        // todo 录音时是否有影响？
        if (!is_state_audio_playing())
        {
            // 文件缓存可用，且有缓冲区已满，有，则将已满的缓冲区数据写入文件
            if (lingxin_ut->enable_file_cache && lingxin_ut->offset_will_cache_buffer > 0)
            {
                bool file_exceed = if_ut_file_full();
                // 文件大小超过限制，直接覆盖写
                bool result = memory_buffer_to_file(!file_exceed, lingxin_ut->will_cache_buffer, lingxin_ut->offset_will_cache_buffer, lingxin_ut->file_path_ut);
                if (result)
                {
                    lingxin_ut->ut_file_null = false;
                    // 写入成功后清空缓存缓冲区
                    lingxin_ut->offset_will_cache_buffer = 0;
                }
            }
        }
        // websocket空闲，则执行上传
        if (is_all_websocket_idle())
        {
            if (lingxin_get_timestamp_s() - lingxin_ut->last_switch_update_time > SWITCH_UPDATE_TIME_INTERVAL)
            {
                update_log_switch();
            }
            try_to_upload();
        }
        lingxin_thread_sleep(100); // 100ms
    }
    lingxin_log_debug("ut_thread_routine finish");
    return NULL;
}

static bool memory_buffer_init()
{
    // 分配两个缓冲区
    lingxin_ut->buffer_a = lingxin_calloc(1, USER_TRACK_MEMORY_BUFFER_SIZE);
    if (!lingxin_ut->buffer_a)
    {
        return false;
    }
    lingxin_ut->buffer_b = lingxin_calloc(1, USER_TRACK_MEMORY_BUFFER_SIZE);
    if (!lingxin_ut->buffer_b)
    {
        lingxin_free(lingxin_ut->buffer_a);
        return false;
    }
     lingxin_ut->memory_buffer_core_node = lingxin_calloc(1, USER_TRACK_MEMORY_BUFFER_SIZE / 4);

    // 初始化指针指向
    lingxin_ut->recording_buffer = lingxin_ut->buffer_a;
    lingxin_ut->will_cache_buffer = lingxin_ut->buffer_b;
    lingxin_ut->offset_recording_buffer = 0;
    lingxin_ut->offset_will_cache_buffer = 0;

    return true;
}

static char *append_file_path_name(char *flash_path, char *file_name)
{
    if (!flash_path)
    {
        lingxin_log_error("flash path null");
        return NULL;
    }
    int flash_path_len = strlen(flash_path);
    if (flash_path_len == 0)
    {
        lingxin_log_error("path len zero");
        return NULL;
    }
    int file_name_path = strlen(file_name);
    // 确保路径以'/'结尾
    bool need_slash = flash_path[flash_path_len - 1] != '/';
    int full_path_len = flash_path_len + (need_slash ? 1 : 0) + file_name_path + 1;

    char *final_path = (char *)lingxin_malloc(full_path_len);
    if (!final_path)
    {
        lingxin_log_error("failed to allocate memory for file path");
        return NULL;
    }
    if (need_slash)
    {
        snprintf(final_path, full_path_len, "%s/%s", flash_path, file_name);
    }
    else
    {
        snprintf(final_path, full_path_len, "%s%s", flash_path, file_name);
    }
    return final_path;
}

bool user_track_init(char *flash_path)
{
    lingxin_log_debug("begin");
    if (lingxin_ut)
    {
        lingxin_log_error("ut has inited");
        return false;
    }
    lingxin_ut = (lingxin_user_track *)lingxin_calloc(1, sizeof(lingxin_user_track));
    if (!lingxin_ut)
    {
        lingxin_log_error("failed to calloc lingxin ut memory");
        return false;
    }
    lingxin_ut->enable_file_cache = false;
    lingxin_ut->file_path_core_node = append_file_path_name(flash_path, CORE_NODE_FILE_NAME);
    lingxin_ut->file_path_ut = append_file_path_name(flash_path, USER_TRACK_FILE_NAME);
    lingxin_ut->can_record = true;
    lingxin_ut->ut_file_null = true;
    lingxin_ut->core_node_file_null = true;
    lingxin_ut->log_upload_path = NULL;
    lingxin_ut->last_upload_time = lingxin_get_timestamp_s();
    lingxin_ut->last_switch_update_time = 0;
    lingxin_ut->running = true;
    lingxin_ut->core_node_mutex = lingxin_mutex_create();
    if (!lingxin_ut->core_node_mutex)
    {
        lingxin_log_error("failed to initialize core_node_mutex");
    }
    lingxin_ut->mutex = lingxin_mutex_create();
    if (!lingxin_ut->mutex)
    {
        lingxin_log_error("failed to initialize mutex");
        lingxin_free(lingxin_ut);
        lingxin_ut = NULL;
        return false;
    }
    if (!memory_buffer_init())
    {
        lingxin_log_error("failed to malloc memory buffer");
         lingxin_mutex_destroy(lingxin_ut->core_node_mutex);
        lingxin_mutex_destroy(lingxin_ut->mutex);
        lingxin_free(lingxin_ut);
        lingxin_ut = NULL;
        return false;
    }
    lingxin_thread_param_t thread_param = {.name = "lx_ut_thread", .priority = 9, .stack_size = 4096};
    if (lingxin_thread_create(&lingxin_ut->thread_id, &thread_param, ut_thread_routine, NULL) != 0)
    {
        lingxin_log_error("failed to create ut thread");
        lingxin_free(lingxin_ut->buffer_a);
        lingxin_free(lingxin_ut->buffer_b);
        lingxin_free(lingxin_ut);
        lingxin_ut = NULL;
        return false;
    }
    return true;
}

void user_track_record(char *content)
{
    if (!content)
    {
        lingxin_log_error("content null");
        return;
    }
    if (!lingxin_ut)
    {
        lingxin_log_error("ut not init");
        return;
    }
    int content_len = strlen(content);
    int needed_space = MEMORY_LENGTH_FIELD_WIDTH + content_len;

    bool buffer_switched = false;
    int cached_bytes = 0;

    lingxin_mutex_lock(lingxin_ut->mutex);
    if (!lingxin_ut->can_record)
    {
        lingxin_log_debug("reading buffer, can not record");
        lingxin_mutex_unlock(lingxin_ut->mutex);
        return;
    }

    // 检查 recording_buffer 是否已满
    if ((lingxin_ut->offset_recording_buffer + needed_space) > USER_TRACK_MEMORY_BUFFER_SIZE)
    {
        // 检查 will_cache_buffer 是否不为空
        if (lingxin_ut->offset_will_cache_buffer > 0)
        {
            // 这个节点需要写入排查文档，改成warn
            lingxin_log_warn("buffer need change, but will_cache_buffer not empty, data overwritten");
        }
        // 缓冲区切换 - 交换指针而不是拷贝数据
        char *temp_buffer = lingxin_ut->recording_buffer;
        int temp_offset = lingxin_ut->offset_recording_buffer;

        lingxin_ut->recording_buffer = lingxin_ut->will_cache_buffer;
        lingxin_ut->offset_recording_buffer = 0;

        lingxin_ut->will_cache_buffer = temp_buffer;
        lingxin_ut->offset_will_cache_buffer = temp_offset;

        buffer_switched = true;
        cached_bytes = temp_offset;
    }

    // 使用固定宽度存储长度（例如4位数字，不足补0）
    if (content_len > 9999)
    {
        lingxin_log_error("Content too long: %d", content_len);
        lingxin_mutex_unlock(lingxin_ut->mutex);
        return;
    }
    // 内存中写入固定4位长度
    snprintf(lingxin_ut->recording_buffer + lingxin_ut->offset_recording_buffer, MEMORY_LENGTH_FIELD_WIDTH + 1, "%04d", content_len);

    // 内存中写入日志内容
    memcpy(lingxin_ut->recording_buffer + lingxin_ut->offset_recording_buffer + MEMORY_LENGTH_FIELD_WIDTH, content, content_len);

    lingxin_ut->offset_recording_buffer += needed_space;
    lingxin_mutex_unlock(lingxin_ut->mutex);
    // 在锁外记录缓冲区切换日志
    if (buffer_switched)
    {
        lingxin_log_debug("Buffer switched, cached %d bytes", cached_bytes);
    }
}

bool is_user_track_init()
{
    return lingxin_ut != NULL;
}

void core_node_record(char *node)
{
    if (!node)
    {
        lingxin_log_error("core node null");
        return;
    }
    if (!lingxin_ut || !lingxin_ut->core_node_mutex)
    {
        lingxin_log_error("user track not init");
        return;
    }
    lingxin_mutex_lock(lingxin_ut->core_node_mutex);
    static char key_pre[128];
    static int key_pre_len = -1;
    if (key_pre_len == -1)
    {
        char *app_name = get_lingxin_device_name();
        char *app_version = get_lingxin_device_version();
        if (app_name && app_version)
        {
            snprintf(key_pre, sizeof(key_pre), "app:%s,version:%s,CORE_NODE_", app_name, app_version);
        }
        else
        {
            snprintf(key_pre, sizeof(key_pre), "app:NONE,version:NONE,CORE_NODE_");
        }
        key_pre_len = strlen(key_pre);
    }

    int node_len = strlen(node);
    int content_len = key_pre_len + node_len;
    if (content_len < 0 || content_len > 9999) {
        lingxin_log_warn("Core node content too long, data discarded");
        lingxin_mutex_unlock(lingxin_ut->core_node_mutex);
        return;
    }
    int needed_space = MEMORY_LENGTH_FIELD_WIDTH + content_len;
    if (lingxin_ut->memory_buffer_core_node)
    {
        // 文件缓存不可用时，直接将数据按格式写入内存缓冲区
        if ((lingxin_ut->offset_core_node_buffer + needed_space) <= (USER_TRACK_MEMORY_BUFFER_SIZE / 4))
        {
            // 写入长度字段
            snprintf(lingxin_ut->memory_buffer_core_node + lingxin_ut->offset_core_node_buffer, MEMORY_LENGTH_FIELD_WIDTH + 1, "%04d", content_len);

            // 写入内容
            memcpy(lingxin_ut->memory_buffer_core_node + lingxin_ut->offset_core_node_buffer + MEMORY_LENGTH_FIELD_WIDTH, key_pre, key_pre_len);
            memcpy(lingxin_ut->memory_buffer_core_node + lingxin_ut->offset_core_node_buffer + MEMORY_LENGTH_FIELD_WIDTH + key_pre_len, node, node_len);

            lingxin_ut->offset_core_node_buffer += needed_space;
        }
        else
        {
            lingxin_log_warn("Memory buffer core node is full, data discarded");
        }
    }
    lingxin_mutex_unlock(lingxin_ut->core_node_mutex);
}