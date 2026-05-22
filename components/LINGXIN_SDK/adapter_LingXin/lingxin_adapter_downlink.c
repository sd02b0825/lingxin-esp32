#include "lingxin_adapter_downlink.h"
#include "cJSON.h"
#include "lingxin_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define LINGXIN_OUTPUT_FORMAT_MAX_LEN 16

static portMUX_TYPE s_downlink_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_output_format[LINGXIN_OUTPUT_FORMAT_MAX_LEN] = "";
static int s_output_sample_rate = 0;

void lingxin_adapter_cache_terminal_config_response(const char *json_body)
{
    if (!json_body || json_body[0] == '\0') {
        return;
    }

    cJSON *json_obj = cJSON_Parse(json_body);
    if (!json_obj) {
        lingxin_log_error("terminal config cache: JSON parse failed");
        return;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json_obj, "data");
    if (!data || !cJSON_IsObject(data)) {
        cJSON_Delete(json_obj);
        return;
    }

    cJSON *output_format = cJSON_GetObjectItemCaseSensitive(data, "output_format");
    cJSON *output_sample_rate = cJSON_GetObjectItemCaseSensitive(data, "output_sample_rate");

    portENTER_CRITICAL(&s_downlink_lock);
    if (output_format && cJSON_IsString(output_format) && output_format->valuestring) {
        strncpy(s_output_format, output_format->valuestring, sizeof(s_output_format) - 1);
        s_output_format[sizeof(s_output_format) - 1] = '\0';
    }

    if (output_sample_rate && cJSON_IsNumber(output_sample_rate)) {
        s_output_sample_rate = output_sample_rate->valueint;
    }
    int cached_sample_rate = s_output_sample_rate;
    char cached_format[LINGXIN_OUTPUT_FORMAT_MAX_LEN];
    strncpy(cached_format, s_output_format, sizeof(cached_format) - 1);
    cached_format[sizeof(cached_format) - 1] = '\0';
    portEXIT_CRITICAL(&s_downlink_lock);

    cJSON_Delete(json_obj);

    lingxin_log_debug("terminal config cached: output=%s, sample_rate=%d",
                      cached_format[0] ? cached_format : "(none)",
                      cached_sample_rate);
}

void lingxin_adapter_downlink_get(char *output_format, size_t output_format_len, int *output_sample_rate)
{
    portENTER_CRITICAL(&s_downlink_lock);
    if (output_format && output_format_len > 0) {
        strncpy(output_format, s_output_format, output_format_len - 1);
        output_format[output_format_len - 1] = '\0';
    }
    if (output_sample_rate) {
        *output_sample_rate = s_output_sample_rate;
    }
    portEXIT_CRITICAL(&s_downlink_lock);
}

