#include "lingxin_mutex.h"
#include "lingxin_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stddef.h>

/* 最好不要依赖可重入锁 */
lingxin_mutex_t lingxin_mutex_create()
{
  SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == NULL)
  {
    lingxin_log_error("lingxin_mutex_create fail");
    return NULL;
  }
  return (lingxin_mutex_t)mutex;
}

void lingxin_mutex_lock(lingxin_mutex_t mutex)
{
  if (mutex == NULL)
  {
    lingxin_log_error("lingxin_mutex_lock: invalid mutex");
    return;
  }

  // 获取互斥锁，阻塞直到获取成功，可重入锁
  BaseType_t result = xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex, portMAX_DELAY);
  if (result != pdTRUE)
  {
    lingxin_log_error("lingxin_mutex_lock fail");
  }
}

void lingxin_mutex_unlock(lingxin_mutex_t mutex)
{
  if (mutex == NULL)
  {
    lingxin_log_error("lingxin_mutex_unlock: invalid mutex");
    return;
  }

  // 释放互斥锁
  BaseType_t result = xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex);
  if (result != pdTRUE)
  {
    lingxin_log_error("lingxin_mutex_unlock fail");
  }
}

void lingxin_mutex_destroy(lingxin_mutex_t mutex)
{
  if (mutex == NULL)
  {
    lingxin_log_error("lingxin_mutex_destroy: invalid mutex");
    return;
  }

  vSemaphoreDelete((SemaphoreHandle_t)mutex);
}
