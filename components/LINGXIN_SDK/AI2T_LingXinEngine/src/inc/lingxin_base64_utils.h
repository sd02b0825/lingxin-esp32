#ifndef LINGXIN_BASE64_UTIL_H
#define LINGXIN_BASE64_UTIL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

/**
 * Base64编码函数
 * @param input 输入字符串
 * @param input_len 输入字符串长度
 * @return 返回编码后的字符串，失败返回NULL
 * @note 【注意】调用者需要自行使用lingxin_free释放返回的字符串！！！！！！！！！
 */
    char* lingxin_base64_encode(const char *input, size_t input_len);

    /**
 * Base64解码函数
 * @param input 输入字符串
 * @param input_len 输入字符串长度
 * @return 返回解码后的字符串，失败返回NULL
 * @note 【注意】调用者需要自行使用lingxin_free释放返回的字符串！！！！！！！！！
 */
    char *lingxin_base64_decode(const char *input, size_t input_len, size_t *output_len);

#ifdef __cplusplus
}
#endif
#endif // LINGXIN_BASE64_UTIL_H