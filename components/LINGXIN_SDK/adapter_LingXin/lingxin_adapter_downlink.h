#ifndef LINGXIN_ADAPTER_DOWNLINK_H
#define LINGXIN_ADAPTER_DOWNLINK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse terminal/config/get JSON body and cache output_format / output_sample_rate.
 * Called from lingxin_http.c on successful config/get response.
 */
void lingxin_adapter_cache_terminal_config_response(const char *json_body);

void lingxin_adapter_downlink_get(char *output_format, size_t output_format_len, int *output_sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* LINGXIN_ADAPTER_DOWNLINK_H */
