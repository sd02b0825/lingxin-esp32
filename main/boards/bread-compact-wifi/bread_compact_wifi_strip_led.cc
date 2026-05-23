#include "bread_compact_wifi_strip_led.h"

#include "application.h"
#include "device_state.h"

#include <esp_log.h>
#include <cmath>
#include <vector>

#define TAG "BreadCompactWifiStripLed"

#define STRIP_BRIGHTNESS 48
#define STRIP_LOW_BRIGHTNESS 4

#define IDLE_BREATHE_BRIGHTNESS_HIGH 72
#define IDLE_BREATHE_BRIGHTNESS_LOW 0
#define IDLE_BREATHE_CONTRAST_EXP 2.0f
#define IDLE_BREATHE_PHASE_STEP 0.04f
#define IDLE_BREATHE_INTERVAL_MS 50
#define LISTEN_BREATHE_SPEED_MULTIPLIER 4.0f

static StripColor HsvToStripColor(float hue, uint8_t value) {
    float h = hue / 60.0f;
    int i = static_cast<int>(h);
    float f = h - i;
    float p = 0.0f;
    float q = value * (1.0f - f);
    float t = value * f;

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    switch (i % 6) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }

    return {
        static_cast<uint8_t>(r),
        static_cast<uint8_t>(g),
        static_cast<uint8_t>(b),
    };
}

BreadCompactWifiStripLed::BreadCompactWifiStripLed(gpio_num_t gpio, uint16_t count)
    : strip_(gpio, count), led_count_(count) {
    esp_timer_create_args_t breathe_timer_args = {
        .callback = [](void* arg) {
            static_cast<BreadCompactWifiStripLed*>(arg)->OnBreatheTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "strip_breathe_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&breathe_timer_args, &breathe_timer_));

    ShowRainbowStatic();
}

BreadCompactWifiStripLed::~BreadCompactWifiStripLed() {
    StopBreatheTimer();
    if (breathe_timer_ != nullptr) {
        esp_timer_delete(breathe_timer_);
    }
}

void BreadCompactWifiStripLed::SetRainbow(uint8_t brightness) {
    std::vector<StripColor> colors(led_count_);
    for (int i = 0; i < static_cast<int>(colors.size()); ++i) {
        float hue = (static_cast<float>(i) * 360.0f) / colors.size();
        colors[i] = HsvToStripColor(hue, brightness);
    }
    strip_.SetMultiColors(colors);
}

void BreadCompactWifiStripLed::ShowRainbowStatic() {
    SetRainbow(STRIP_BRIGHTNESS);
}

void BreadCompactWifiStripLed::OnBreatheTimer() {
    breathe_phase_ += breathe_phase_step_;
    float factor = (std::sin(breathe_phase_) + 1.0f) * 0.5f;
    factor = std::pow(factor, IDLE_BREATHE_CONTRAST_EXP);
    uint8_t brightness = IDLE_BREATHE_BRIGHTNESS_LOW +
        static_cast<uint8_t>((IDLE_BREATHE_BRIGHTNESS_HIGH - IDLE_BREATHE_BRIGHTNESS_LOW) * factor);
    SetRainbow(brightness);
}

void BreadCompactWifiStripLed::StopBreatheTimer() {
    if (breathe_timer_ != nullptr) {
        esp_timer_stop(breathe_timer_);
    }
}

void BreadCompactWifiStripLed::StartBreatheTimer(float speed_multiplier) {
    StopBreatheTimer();
    breathe_phase_ = 0.0f;
    breathe_phase_step_ = IDLE_BREATHE_PHASE_STEP * speed_multiplier;
    OnBreatheTimer();
    esp_timer_start_periodic(breathe_timer_, IDLE_BREATHE_INTERVAL_MS * 1000);
}

void BreadCompactWifiStripLed::OnStateChanged() {
    auto device_state = Application::GetInstance().GetDeviceState();
    StopBreatheTimer();

    switch (device_state) {
        case kDeviceStateStarting:
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
            ShowRainbowStatic();
            break;
        case kDeviceStateWifiConfiguring: {
            StripColor color = {STRIP_LOW_BRIGHTNESS, STRIP_LOW_BRIGHTNESS, STRIP_BRIGHTNESS};
            strip_.Blink(color, 500);
            break;
        }
        case kDeviceStateIdle:
            StartBreatheTimer();
            break;
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            StartBreatheTimer(LISTEN_BREATHE_SPEED_MULTIPLIER);
            break;
        case kDeviceStateSpeaking:
            ShowRainbowStatic();
            break;
        case kDeviceStateUpgrading: {
            StripColor color = {STRIP_LOW_BRIGHTNESS, STRIP_BRIGHTNESS, STRIP_LOW_BRIGHTNESS};
            strip_.Blink(color, 100);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown led strip event: %d", device_state);
            break;
    }
}
