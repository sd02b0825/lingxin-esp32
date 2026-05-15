#include "lingxin_system_time.h"
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_log.h"
#include "lingxin_log.h"

static const char *TAG = "lingxin_adapter_sys_time";
static bool sntp_initialized = false;

// 只初始化SNTP客户端（不等待同步完成）
void sntp_client_init(void)
{
    if (sntp_initialized) {
        return;
    }
        

    ESP_LOGI(TAG, "Initializing SNTP client");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    // 设置时区
    setenv("TZ", "CST-8", 1);
    tzset();

    sntp_initialized = true;
    ESP_LOGI(TAG, "SNTP client initialized");
}

void lingxin_get_current_time(LINGXIN_TIME *lingxin_time)
{
    if (lingxin_time == NULL)
    {
        return; // 安全检查
    }
    // 如果还没初始化SNTP，至少初始化一下避免出错
    if (!sntp_initialized)
    {
        sntp_client_init();
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    struct tm local_time_info;
    localtime_r(&tv_now.tv_sec, &local_time_info);

    lingxin_time->year = local_time_info.tm_year + 1900; // tm_year 是自 1900 起的年数
    lingxin_time->mon = local_time_info.tm_mon + 1;      // tm_mon 从 0 开始（0=Jan）
    lingxin_time->day = local_time_info.tm_mday;         // 1-31
    lingxin_time->hour = local_time_info.tm_hour;        // 0-23
    lingxin_time->min = local_time_info.tm_min;          // 0-59
    lingxin_time->sec = local_time_info.tm_sec;          // 0-60（含闰秒）
    lingxin_time->mill_sec = tv_now.tv_usec / 1000;      // 微秒转毫秒 (0-999)
};


/**
 * 获取秒级时间戳：10 位长度的整数
 * @return 时间戳
 */
long lingxin_get_timestamp_s()
{
    if (!sntp_initialized) {
        sntp_client_init();
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // 检查时间是否合理（避免返回1970年的时间）
    // if (tv.tv_sec < 1000000000) {  // 2001年之前的时间可能是未同步的时间
    //     ESP_LOGW(TAG, "Current time may not be synced: %lld", tv.tv_sec);
    // }
    return (long)tv.tv_sec;
}