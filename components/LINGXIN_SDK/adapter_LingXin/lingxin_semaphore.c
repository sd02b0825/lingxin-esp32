#include "lingxin_semaphore.h"
#include "lingxin_log.h"
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"

lingxin_semaphore_t lingxin_semaphore_create(uint32_t cnt)
{
    SemaphoreHandle_t sem = xSemaphoreCreateCounting(0xFFFF, cnt);
    return (lingxin_semaphore_t)sem;
}

void lingxin_semaphore_pend(lingxin_semaphore_t sem, uint32_t timeout_ms)
{
    if (sem == NULL)
    {
        lingxin_log_error("lingxin_semaphore_pend: semaphore is NULL");
        return;
    }

    xSemaphoreTake((SemaphoreHandle_t)sem, timeout_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
}

void lingxin_semaphore_post(lingxin_semaphore_t sem)
{
    if (sem == NULL)
        return;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // 检查是否在中断上下文中调用
    if (xPortInIsrContext())
    {
        xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else
    {
        xSemaphoreGive((SemaphoreHandle_t)sem);
    }
}
// static portMUX_TYPE semaphore_mux = portMUX_INITIALIZER_UNLOCKED;

void lingxin_semaphore_set_value(lingxin_semaphore_t sem, uint32_t cnt)
{
    if (sem == NULL)
        return;
    SemaphoreHandle_t handle = (SemaphoreHandle_t)sem;
    // 先尝试清空信号量：不断 take 直到失败
    while (xSemaphoreTake(handle, 0) == pdTRUE)
    {
        // 消耗一个信号量
    }
    // 再 give cnt 次，达到目标值
    for (uint32_t i = 0; i < cnt; i++)
    {
        xSemaphoreGive(handle);
    }
}

void lingxin_semaphore_destroy(lingxin_semaphore_t sem)
{
    if (sem == NULL)
        return;
    vSemaphoreDelete((SemaphoreHandle_t)sem);
}