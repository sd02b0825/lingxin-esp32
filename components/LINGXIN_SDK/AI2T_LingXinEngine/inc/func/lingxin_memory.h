#ifndef LINGXIN_MEMORY_H
#define LINGXIN_MEMORY_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "lingxin_memory.h"

/**
 * 申请内存
 * @param size 申请内存大小，单位是字节
 *  * @return 返回指向分配内存的指针，内存已清零；如果分配失败则返回 NULL
 */
#define lingxin_malloc(size) _lingxin_malloc_internal_((size), __FILE__, __LINE__)

/**
 * 申请内存并清零申请的内存
 * @param num 申请的内存块数量
 * @param size 每个内存块的大小，单位是字节
 * @return 返回指向分配内存的指针，内存已清零；如果分配失败则返回 NULL
 */
#define lingxin_calloc(num, size) _lingxin_calloc_internal_((num), (size), __FILE__, __LINE__)

/**
 * 重新分配内存
 */
#define lingxin_realloc(ptr, size) _lingxin_realloc_internal_((void *)(ptr), (size), __FILE__, __LINE__)

/**
 * 释放内存
 * @param ptr 要释放的内存指针
 */
#define lingxin_free(ptr) _lingxin_free_internal_((void *)(ptr), __FILE__, __LINE__)

/**
 * 申请字符串内存并复制字符串
 */
#define lingxin_strdup(message) _lingxin_strdup_internal_((char *)(message), __FILE__, __LINE__)

    /**
     * 启用内存统计功能
     */
    void lingxin_memory_enable_statistics();

    /**
     * 打印内存使用情况
     */
    void lingxin_memory_print_statistics();

    /*
     * 销毁内存统计数据
     */
    void lingxin_memory_destroy_statistics();

    /**
     * 私有函数实现，不对外
     */
    void *_lingxin_malloc_internal_(int size, const char *file_path, int line);
    void *_lingxin_calloc_internal_(int num, int size, const char *file_path, int line);
    void *_lingxin_realloc_internal_(void *ptr, int size, const char *file_path, int line);
    void _lingxin_free_internal_(void *ptr, const char *file_path, int line);
    char *_lingxin_strdup_internal_(char *message, const char *file_path, int line);

#ifdef __cplusplus
}
#endif
#endif // LINGXIN_MEMORY_H