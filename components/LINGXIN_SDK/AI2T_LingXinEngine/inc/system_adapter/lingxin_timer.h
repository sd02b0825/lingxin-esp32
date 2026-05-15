#ifndef LINGXIN_TIMER_H
#define LINGXIN_TIMER_H
#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>
#define INVALID_TIMER_ID (-1)

// 周期性定时器

    /**
     * @brief sys_timer定时扫描增加接口，创建一个周期性定时器
     * @param priv 定时器回调函数func的私有参数
     * @param func 超时扫描回调函数
     * @param msec 超时时间， 单位：毫秒
     * @return 定时器分配的id号, 创建失败时返回INVALID_TIMER_ID
     */
    int lingxin_sys_timer_add(void *priv, void (*func)(void *priv), long msec);


    /**
     * @brief sys_timer定时扫描删除接口
     * @param timer_id sys_timer_add分配的id号
     * @return 删除结果（0-成功，-1-失败）
     */
    int lingxin_sys_timer_del(int timer_id);


    /**
     * @brief sys_timer定时扫描重置接口
     * @param timer_id sys_timer_add分配的id号
     * @return 重置结果（0-成功，-1-失败）
     */
    int lingxin_sys_timer_re_run(int timer_id);



// 一次性定时器

    /**
    * @brief 创建一个一次性定时器
    * @param priv 定时器回调函数func的私有参数
    * @param func 定时器回调函数
    * @param countdown 超时时间， 单位：毫秒，值可能为0
    * @return 定时器分配的id号, 创建失败时返回INVALID_TIMER_ID
    */
    int lingxin_one_shot_timer_create(void *priv, void (*func)(void *priv), long countdown);

    /**
    * @brief 删除指定的一次性定时器
    * @param timerId 定时器ID
    * @return 删除结果（0-成功，-1-失败）
    */
    int lingxin_one_shot_timer_delete(int timerId);

#ifdef __cplusplus
}
#endif
#endif