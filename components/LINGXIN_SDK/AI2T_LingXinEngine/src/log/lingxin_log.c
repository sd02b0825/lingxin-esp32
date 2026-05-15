#include "lingxin_log.h"
#include <stdarg.h>
#include <stdio.h>
#include "lingxin_version.h"
#include "lingxin_common.h"
#include <string.h>
#include "lingxin_system_time.h"
#include "lingxin_user_track.h"
#include "lingxin_device_info.h"
#include "lingxin_printf.h"
#include "lingxin_thread.h"

static void _log_internal(char *log_buffer, size_t log_buffer_size, int level, const char *file_path, int line, const char *node, const char *format, va_list args)
{
    char moduleName[64] = {0};
    parse_file_name_from_path(moduleName, file_path);

    char *level_str = "";
    if (level == LINGXIN_DEBUG)
    {
        level_str = "D";
    }
    else if (level == LINGXIN_ERROR)
    {
        level_str = "E";
    }
    else if (level == LINGXIN_WARN)
    {
        level_str = "W";
    }

    static char *app_name = NULL;
    static char *app_version = NULL;

    if (!app_name)
    {
        char *temp_name = get_lingxin_device_name();
        app_name = temp_name ? temp_name : "NULL";
    }
    if (!app_version)
    {
        char *temp_version = get_lingxin_device_version();
        app_version = temp_version ? temp_version : "NULL";
    }
    // 格式化时间字符串,[mm-dd hh:mm:ss.milliseconds]
    LINGXIN_TIME lingxin_time = {0};
    lingxin_get_current_time(&lingxin_time);

    char *temp_name = lingxin_get_current_thread_name();
    char *thread_name = temp_name ? temp_name : "";

    const char *format_template_with_node = "[%02d-%02d %02d:%02d:%02d.%03d] [%s_%s] [%s] [%s:%d] [%s] [%s] ";
    const char *format_template_without_node = "[%02d-%02d %02d:%02d:%02d.%03d] [%s_%s] [%s] [%s:%d] [%s] ";
    // 格式化完整日志到静态缓冲区
    int prefix_len = -1;
    if (node)
    {
        prefix_len = snprintf(log_buffer, log_buffer_size, format_template_with_node, lingxin_time.mon, lingxin_time.day, lingxin_time.hour, lingxin_time.min, lingxin_time.sec, lingxin_time.mill_sec, app_name, app_version, level_str, moduleName, line, thread_name, node);
    }
    else
    {
        prefix_len = snprintf(log_buffer, log_buffer_size, format_template_without_node, lingxin_time.mon, lingxin_time.day, lingxin_time.hour, lingxin_time.min, lingxin_time.sec, lingxin_time.mill_sec, app_name, app_version, level_str, moduleName, line, thread_name);
    }

    if (prefix_len >= 0 && prefix_len < (int)log_buffer_size)
    {
        // 添加实际日志内容
        vsnprintf(log_buffer + prefix_len, log_buffer_size - prefix_len, format, args);
    }
    // 输出到控制台
    lingxin_printf(log_buffer);
}

void _lingxin_log_print_internal_(int level, int is_ut, const char *file_path, int line, const char *node, const char *format, ...)
{

    char log_buffer[512];
    va_list args;
    va_start(args, format);
    _log_internal(log_buffer, sizeof(log_buffer), level, file_path, line, node, format, args);
    va_end(args);
    if (is_user_track_init() && is_ut == 1)
    {
        user_track_record(log_buffer);
    }
}