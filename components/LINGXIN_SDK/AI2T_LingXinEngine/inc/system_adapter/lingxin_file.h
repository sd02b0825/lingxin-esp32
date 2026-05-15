#ifndef LINGXIN_FILE_H
#define LINGXIN_FILE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

    /**
     *  创建指定大小的文件
     *
     * @param file_path 文件名称
     * @param length 文件大小
     *
     * @return true:创建成功 false:创建失败
     */
    bool lingxin_file_create(const char *file_path, int length);

    /**
     *  根据指定名称的文件，从指定的起始位置开始，读取一定大小的文件内容到缓冲区中
     *
     * @param file_path 文件名称
     * @param buffer 要写入的缓冲区
     * @param offet  需要读取的文件内容偏移位置
     * @param length 需要读取的文件内容长度
     *
     * @return true:读取成功 false:读取失败
     */
    bool lingxin_file_read(const char *file_path, char *buffer, int offet, int length);

    /**
     *  向指定名称的文件中写入一定长度的内容
     *
     * @param file_path 文件名称
     * @param is_append 是否以追加模式写入文件
     * @param data 要写入的内容
     * @param length 要写入的内容长度
     *
     * @return true:写入成功 false:写入失败
     */
    bool lingxin_file_write(const char *file_path, bool is_append, const char *data, int length);

    /**
     *  检查指定名称的文件是否存在
     *
     * @param file_path 文件名称
     *
     * @return true:存在 false:不存在
     */

    bool lingxin_file_exist(const char *file_path);

    /**
     *  获取指定名称的文件内容长度（文件大小）
     *
     * @param file_path 文件名称
     *
     * @return 文件内容长度（文件大小）
     */

    int lingxin_file_length(const char *file_path);

    /**
     *  删除指定名称的文件
     *
     * @param file_path 文件名称
     *
     * @return true:删除成功 false:删除失败
     */

    bool lingxin_file_delete(const char *file_path);

    /**
     *  清空指定名称的文件内容
     *
     * @param file_path 文件名称
     *
     * @return true:清理成功 false:清理失败
     */

    bool lingxin_file_clear(const char *file_path);

#ifdef __cplusplus
}
#endif
#endif // LINGXIN_FILE_H
