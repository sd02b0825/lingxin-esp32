#include "lingxin_file.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "lingxin_log.h"
/**
 *  创建指定大小的文件
 *
 * @param file_path 文件名称
 * @param length 文件大小
 *
 * @return true:创建成功 false:创建失败
 */
bool lingxin_file_create(const char *file_path, int length)
{
    if (!file_path || length < 0)
    {
        lingxin_log_error("Invalid parameter, file_path=%s, length=%d", file_path, length);
        return false;
    }

    FILE *fp = fopen(file_path, "wb");
    if (!fp)
    {
        lingxin_log_error("Failed to open file, errno=%d, error=%s", errno, strerror(errno));
        return false;
    }

    // 使用 ftruncate 更高效，但 ESP-IDF 的 VFS 不一定支持（尤其 SPIFFS）
    // 所以我们手动写入零字节
    bool success = true;
    if (length > 0)
    {
        char zero = '\0';
        for (int i = 0; i < length; ++i)
        {
            if (fwrite(&zero, 1, 1, fp) != 1)
            {
                success = false;
                break;
            }
        }
    }

    fclose(fp);
    return success;
    // return false;
}

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
bool lingxin_file_read(const char *file_path, char *buffer, int offet, int length)
{
    if (!file_path || !buffer || offet < 0 || length <= 0)
    {
        return false;
    }

    FILE *fp = fopen(file_path, "rb");
    if (!fp)
    {
        return false;
    }

    if (fseek(fp, offet, SEEK_SET) != 0)
    {
        fclose(fp);
        return false;
    }

    size_t read_bytes = fread(buffer, 1, (size_t)length, fp);
    fclose(fp);

    return (read_bytes == (size_t)length);
    // return false;
}

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
bool lingxin_file_write(const char *file_path, bool is_append, const char *data, int length)
{
    if (!file_path || !data || length <= 0)
    {
        return false;
    }

    const char *mode = is_append ? "ab" : "wb";
    FILE *fp = fopen(file_path, mode);
    if (!fp)
    {
        return false;
    }

    size_t written = fwrite(data, 1, (size_t)length, fp);
    fclose(fp);

    return (written == (size_t)length);
}

/**
 *  检查指定名称的文件是否存在
 *
 * @param file_path 文件名称
 *
 * @return true:存在 false:不存在
 */

bool lingxin_file_exist(const char *file_path)
{
    if (!file_path)
        return false;
    struct stat st;
    return (stat(file_path, &st) == 0);
}

/**
 *  获取指定名称的文件内容长度（文件大小）
 *
 * @param file_path 文件名称
 *
 * @return 文件内容长度（文件大小）
 */

int lingxin_file_length(const char *file_path)
{
    if (!file_path)
        return -1;
    struct stat st;
    if (stat(file_path, &st) != 0)
    {
        return -1;
    }
    return (int)st.st_size;
}

/**
 *  删除指定名称的文件
 *
 * @param file_path 文件名称
 *
 * @return true:删除成功 false:删除失败
 */

bool lingxin_file_delete(const char *file_path)
{
    if (!file_path)
        return false;
    return (unlink(file_path) == 0);
}

/**
 *  清空指定名称的文件内容
 *
 * @param file_path 文件名称
 *
 * @return true:清理成功 false:清理失败
 */

bool lingxin_file_clear(const char *file_path)
{
    if (!file_path)
        return false;
    FILE *fp = fopen(file_path, "wb");
    if (!fp)
        return false;
    bool ok = (fclose(fp) == 0);
    return ok;
}