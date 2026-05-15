#ifndef LINGXIN_SYSTEM_TIME_H
#define LINGXIN_SYSTEM_TIME_H

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    int mill_sec;
    int sec;
    int min;
    int hour;
    int day;
    int mon;
    int year;
  } LINGXIN_TIME;

  /**
   * 获取当前时间
   * @param lingxin_time 灵芯时间结构体
   */
  void lingxin_get_current_time(LINGXIN_TIME *lingxin_time);

  /**
   * 获取秒级时间戳：10 位长度的整数
   * @return 时间戳
   */
  long lingxin_get_timestamp_s();

#ifdef __cplusplus
}
#endif
#endif // LINGXIN_SYSTEM_TIME_H
