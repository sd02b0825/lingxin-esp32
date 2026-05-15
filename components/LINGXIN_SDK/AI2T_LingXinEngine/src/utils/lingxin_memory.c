#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "lingxin_memory.h"
#include "lingxin_log.h"
#include "lingxin_common.h"
#include "lingxin_mutex.h"

// 内存跟踪结构体
typedef struct mem_track
{
    void *addr;
    int size;
    const char *file;
    int line;
    int freed;
    struct mem_track *next; // 链表指针
} mem_track_t;

static lingxin_mutex_t g_mem_tracker_lock = NULL; // 添加全局锁

static mem_track_t *g_mem_tracker_head = NULL; // 链表头指针
static int g_tracker_count = 0;
static int g_total_allocated = 0;
static int g_current_used = 0;
static bool g_memory_statistics_enabled = false;
static int g_file_strings_allocated = 0;

static bool add_node_to_track_list(void *address, int size, char module_name[64], int line)
{
    mem_track_t *new_node = (mem_track_t *)malloc(sizeof(mem_track_t));
    if (!new_node)
    {
        return false;
    }

    // 提前复制文件名，避免在锁内进行内存分配
    char *file_copy = strdup(module_name);
    if (!file_copy)
    {
        free(new_node);
        return false;
    }

    new_node->addr = address;
    new_node->size = size;
    new_node->file = file_copy;
    new_node->line = line;
    new_node->freed = 0;

    if (g_mem_tracker_lock)
    {
        lingxin_mutex_lock(g_mem_tracker_lock);
    }

    new_node->next = g_mem_tracker_head;
    g_mem_tracker_head = new_node;

    g_tracker_count++;
    g_total_allocated += new_node->size;
    g_current_used += new_node->size;
    g_file_strings_allocated += strlen(module_name) + 1;

    if (g_mem_tracker_lock)
    {
        lingxin_mutex_unlock(g_mem_tracker_lock);
    }

    return true;
}

// 内存分配统计函数实现
void *_lingxin_malloc_internal_(int size, const char *file_path, int line)
{
    void *ptr = malloc(size);
    if (!ptr)
    {
        char module_name[64] = {0};
        parse_file_name_from_path(module_name, file_path);
        lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_malloc_fail", " %s:%d malloc %p, size: %d", module_name, line, ptr, size);
    }
    if (g_memory_statistics_enabled && ptr)
    {
        char module_name[64] = {0};
        parse_file_name_from_path(module_name, file_path);

        // lingxin_log_debug("malloc(%p, %d) called from %s:%d", ptr, size, module_name, line);

        if (!add_node_to_track_list(ptr, size, module_name, line))
        {
            lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_malloc_add_node_fail", " %s:%d malloc %p, size: %d", module_name, line, ptr, size);
        }
    }
    return ptr;
}

void *_lingxin_calloc_internal_(int num, int size, const char *file_path, int line)
{
    void *ptr = calloc(num, size);
    int total_size = num * size;
    if (!ptr)
    {
        char module_name[64] = {0};
        parse_file_name_from_path(module_name, file_path);
        lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_calloc_fail", " %s:%d calloc %p, size: %d, num: %d, each size: %d", module_name, line, ptr, total_size, num, size);
    }
    if (g_memory_statistics_enabled && ptr)
    {
        char module_name[64] = {0};
        parse_file_name_from_path(module_name, file_path);

        // lingxin_log_debug("calloc(%p, %d) called from %s:%d", ptr, total_size, module_name, line);

        if (!add_node_to_track_list(ptr, total_size, module_name, line))
        {
            lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_calloca_dd_node_fail", " %s:%d calloc %p, size: %d, num: %d, each size: %d", module_name, line, ptr, total_size, num, size);
        }
    }

    return ptr;
}

void *_lingxin_realloc_internal_(void *ptr, int size, const char *file_path, int line)
{
    // 当 ptr 为 NULL 时，realloc 的行为等同于 malloc
    void *new_ptr = realloc(ptr, size);

    if (g_memory_statistics_enabled)
    {
        char module_name[64] = {0};
        parse_file_name_from_path(module_name, file_path);

        if (ptr == NULL)
        {
            // ptr 为 NULL，相当于 malloc 行为
            if (new_ptr)
            {
                if (!add_node_to_track_list(new_ptr, size, module_name, line))
                {
                    lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_realloc_add_node_fail", " %s:%d realloc(NULL) %p, size: %d (equivalent to malloc)", module_name, line, new_ptr, size);
                }
            }
        }
        else
        {
            // ptr 不为 NULL，处理正常的 realloc 逻辑
            if (new_ptr)
            {
                if (g_mem_tracker_lock)
                {
                    lingxin_mutex_lock(g_mem_tracker_lock);
                }

                // 查找原始内存块信息
                mem_track_t *current = g_mem_tracker_head;
                mem_track_t *prev = NULL;
                bool found = false;

                while (current)
                {
                    if (current->addr == ptr && !current->freed)
                    {
                        found = true;
                        // 更新内存跟踪信息
                        if (new_ptr == ptr)
                        {
                            // 原地调整大小
                            g_current_used = g_current_used - current->size + size;
                            g_total_allocated = g_total_allocated - current->size + size;
                            current->size = size;
                            lingxin_log_ut_with_args(LINGXIN_DEBUG, "lingxin_realloc", " %s:%d realloc %p, resized from %d to %d bytes", module_name, line, new_ptr, ptr, current->size, size);
                        }
                        else
                        {
                            // 分配了新内存块，需要更新跟踪信息
                            // 删除旧节点
                            if (prev)
                            {
                                prev->next = current->next;
                            }
                            else
                            {
                                g_mem_tracker_head = current->next;
                            }

                            g_current_used -= current->size;
                            g_tracker_count--;
                            if (current->file)
                            {
                                g_file_strings_allocated -= strlen(current->file) + 1;
                                free((void *)current->file);
                            }
                            free(current);

                            // 解锁后添加新节点，因为add_node_to_track_list内部有锁
                            if (g_mem_tracker_lock)
                            {
                                lingxin_mutex_unlock(g_mem_tracker_lock);
                            }

                            if (!add_node_to_track_list(new_ptr, size, module_name, line))
                            {
                                lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_realloc_add_node_fail", " %s:%d realloc %p -> %p, size: %d", module_name, line, ptr, new_ptr, size);
                            }
                        }
                        break;
                    }
                    prev = current;
                    current = current->next;
                }

                // 如果没找到原始内存块，创建新的跟踪节点
                if (!found && new_ptr != ptr)
                {
                    // 解锁后添加新节点，因为add_node_to_track_list内部有锁
                    if (g_mem_tracker_lock)
                    {
                        lingxin_mutex_unlock(g_mem_tracker_lock);
                    }

                    if (!add_node_to_track_list(new_ptr, size, module_name, line))
                    {
                        lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_realloc_add_node_fail", " %s:%d realloc %p, size: %d (new tracking entry)", module_name, line, new_ptr, size);
                    }
                }
                else
                {
                    // 只有在找到了原始节点且不是新增节点的情况下才解锁
                    if (found && new_ptr == ptr)
                    {
                        if (g_mem_tracker_lock)
                        {
                            lingxin_mutex_unlock(g_mem_tracker_lock);
                        }
                    }
                }
            }
            else if (size == 0)
            {
                if (g_mem_tracker_lock)
                {
                    lingxin_mutex_lock(g_mem_tracker_lock);
                }

                // realloc(ptr, 0) 相当于 free(ptr)
                // 查找并删除对应的内存跟踪节点
                mem_track_t *current = g_mem_tracker_head;
                mem_track_t *prev = NULL;

                while (current)
                {
                    if (current->addr == ptr && !current->freed)
                    {
                        // 删除节点
                        if (prev)
                        {
                            prev->next = current->next;
                        }
                        else
                        {
                            g_mem_tracker_head = current->next;
                        }

                        g_current_used -= current->size;
                        g_tracker_count--;
                        if (current->file)
                        {
                            g_file_strings_allocated -= strlen(current->file) + 1;
                            free((void *)current->file);
                        }
                        free(current);
                        break;
                    }
                    prev = current;
                    current = current->next;
                }
                if (g_mem_tracker_lock)
                {
                    lingxin_mutex_unlock(g_mem_tracker_lock);
                }
                lingxin_log_ut_with_args(LINGXIN_DEBUG, "lingxin_realloc", " %s:%d realloc %p with size 0, treated as free",
                                         module_name, line, ptr);
            }
            else
            {
                // realloc 失败且 size > 0
                lingxin_log_ut_with_args(LINGXIN_ERROR, "lingxin_realloc", " %s:%d realloc failed for ptr %p, size: %d",
                                         module_name, line, ptr, size);
            }
        }
    }

    return new_ptr;
}

void _lingxin_free_internal_(void *ptr, const char *file_path, int line)
{
    if (ptr)
    {
        if (g_memory_statistics_enabled)
        {
            char module_name[64] = {0};
            parse_file_name_from_path(module_name, file_path);

            if (g_mem_tracker_lock)
            {
                lingxin_mutex_lock(g_mem_tracker_lock);
            }

            // 在链表中查找并标记为已释放
            bool found = false;
            mem_track_t *current = g_mem_tracker_head;
            mem_track_t *prev = NULL;

            while (current)
            {
                if (current->addr == ptr && !current->freed)
                {
                    if (prev)
                    {
                        prev->next = current->next;
                    }
                    else
                    {
                        g_mem_tracker_head = current->next;
                    }
                    g_current_used -= current->size;
                    g_tracker_count--;
                    if (current->file)
                    {
                        g_file_strings_allocated -= strlen(current->file) + 1;
                        free((void *)current->file);
                    }
                    free(current);
                    found = true;
                    break;
                }
                prev = current;
                current = current->next;
            }

            if (g_mem_tracker_lock)
            {
                lingxin_mutex_unlock(g_mem_tracker_lock);
            }
            // 如果没有找到对应的内存记录，说明这块内存没有被统计过
            if (!found)
            {
                lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_free", "%s:%d free %p - memory not tracked by statistics", module_name, line, ptr);
            }
        }

        // 真正释放内存
        free(ptr);
    }
}

char *_lingxin_strdup_internal_(char *message, const char *file_path, int line)
{
    if (!message)
    {
        return NULL;
    }
    char *copy = strdup(message);
    if (!copy)
    {
        char module_name[64] = {0};
        parse_file_name_from_path(module_name, file_path);
        lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_strdup_fail", " %s:%d strdup %p", module_name, line, copy);
    }
    if (g_memory_statistics_enabled && copy)
    {
        char module_name[64] = {0};
        parse_file_name_from_path(module_name, file_path);

        // lingxin_log_debug("calloc(%p, %d) called from %s:%d", copy, strlen(message) + 1, module_name, line);

        if (!add_node_to_track_list(copy, strlen(message) + 1, module_name, line))
        {
            lingxin_log_ut_with_args(LINGXIN_WARN, "lingxin_strdup_add_node_fail", " %s:%d strdup %p", module_name, line, copy);
        }
    }
    return copy;
}

// 打印内存统计信息
void lingxin_memory_print_statistics()
{
    if (!g_memory_statistics_enabled)
    {
        return;
    }

    if (g_mem_tracker_lock)
    {
        lingxin_mutex_lock(g_mem_tracker_lock);
    }

    // 计算链表本身占用的内存大小
    size_t list_memory_size = g_tracker_count * sizeof(mem_track_t);
    size_t total_tracking_overhead = list_memory_size + g_file_strings_allocated;

    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "=== lingxin Memory Statistics Report ===");
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "Total allocations: %d", g_tracker_count);
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "Total allocated: %d bytes", g_total_allocated);
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "Currently used: %d bytes", g_current_used);
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "Memory tracking list size: %d bytes", list_memory_size);
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "File strings memory: %d bytes", g_file_strings_allocated);
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "Total tracking overhead: %d bytes", total_tracking_overhead);

    // 统计未释放的内存
    int unfreed_count = 0;
    int unfreed_size = 0;
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "=== Unfreed Memory Blocks ===");

    mem_track_t *current = g_mem_tracker_head;
    int index = 1;
    while (current)
    {
        if (!current->freed)
        {
            unfreed_count++;
            unfreed_size += current->size;
            lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "[%d] Address: %p, Size: %d bytes, Location: %s:%d\n",
                                     index,
                                     current->addr,
                                     current->size,
                                     current->file,
                                     current->line);
        }
        current = current->next;
        index++;
    }
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "Total unfreed blocks: %d", unfreed_count);
    lingxin_log_ut_with_args(LINGXIN_DEBUG, "print_lingxin_memory", "Total unfreed memory: %d bytes", unfreed_size);

    if (g_mem_tracker_lock)
    {
        lingxin_mutex_unlock(g_mem_tracker_lock);
    }
}

void lingxin_memory_enable_statistics()
{
    g_memory_statistics_enabled = true;
    if (!g_mem_tracker_lock)
    {
        g_mem_tracker_lock = lingxin_mutex_create();
    }
}

// 释放所有内存跟踪节点
void lingxin_memory_destroy_statistics()
{
    if (g_mem_tracker_lock)
    {
        lingxin_mutex_lock(g_mem_tracker_lock);
    }

    mem_track_t *current = g_mem_tracker_head;
    while (current)
    {
        mem_track_t *temp = current;
        current = current->next;

        // 释放文件名内存
        if (temp->file)
        {
            free((void *)temp->file);
        }
        // 释放节点内存
        free(temp);
    }

    // 重置统计变量
    g_mem_tracker_head = NULL;
    g_tracker_count = 0;
    g_total_allocated = 0;
    g_current_used = 0;
    g_file_strings_allocated = 0;
    g_memory_statistics_enabled = false;
    if (g_mem_tracker_lock)
    {
        lingxin_mutex_unlock(g_mem_tracker_lock);
    }
    lingxin_mutex_destroy(g_mem_tracker_lock);
}