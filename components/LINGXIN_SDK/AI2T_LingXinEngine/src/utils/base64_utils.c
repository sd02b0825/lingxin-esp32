#include "mbedtls/base64.h"
#include "lingxin_base64_utils.h"
#include "lingxin_memory.h"

char *lingxin_base64_encode(const char *input, size_t input_len)
{
    // 计算输出缓冲区大小（Base64编码后的长度大约是原长度的4/3倍，再加上填充和结束符）
    size_t output_size = (input_len + 2) / 3 * 4 + 1; // +1 for null terminator
    unsigned char *output = (unsigned char *)lingxin_malloc(output_size);
    if (!output)
    {
        return NULL;
    }
    size_t output_len;
    int ret = mbedtls_base64_encode(output, output_size, &output_len, (const unsigned char *)input, input_len);
    if (ret != 0)
    {
        lingxin_free(output);
        return NULL;
    }
    // 确保字符串以null结尾
    output[output_len] = '\0';
    return (char *)output;
}

char *lingxin_base64_decode(const char *input, size_t input_len, size_t *output_len)
{
    if (!input)
    {
        goto exit_decode;
    }
    // 计算输出缓冲区大小（解码后的长度大约是Base64长度的3/4倍）
    size_t output_size = input_len / 4 * 3 + 1; // +1 for null terminator
    unsigned char *output = (unsigned char *)lingxin_malloc(output_size);
    if (!output)
    {
        goto exit_decode;
    }
    size_t actual_len;
    int ret = mbedtls_base64_decode(output, output_size, &actual_len, (const unsigned char *)input, input_len);
    if (ret != 0)
    {
        lingxin_free(output);
        goto exit_decode;
    }
    // 确保字符串以null结尾
    output[actual_len] = '\0';
    if (output_len)
        *output_len = actual_len;
    return (char *)output;

exit_decode:
    if (output_len)
        *output_len = 0;
    return NULL;
}