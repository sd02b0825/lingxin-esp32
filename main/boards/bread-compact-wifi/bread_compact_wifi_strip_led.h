#ifndef _BREAD_COMPACT_WIFI_STRIP_LED_H_
#define _BREAD_COMPACT_WIFI_STRIP_LED_H_

#include "led/led.h"
#include "led/circular_strip.h"
#include <driver/gpio.h>
#include <esp_timer.h>

class BreadCompactWifiStripLed : public Led {
public:
    BreadCompactWifiStripLed(gpio_num_t gpio, uint16_t count);
    ~BreadCompactWifiStripLed() override;

    void OnStateChanged() override;

private:
    CircularStrip strip_;
    esp_timer_handle_t breathe_timer_ = nullptr;
    uint16_t led_count_ = 0;
    float breathe_phase_ = 0.0f;
    float breathe_phase_step_ = 0.0f;

    void SetRainbow(uint8_t brightness);
    void ShowRainbowStatic();
    void StopBreatheTimer();
    void StartBreatheTimer(float speed_multiplier = 1.0f);
    void OnBreatheTimer();
};

#endif // _BREAD_COMPACT_WIFI_STRIP_LED_H_
