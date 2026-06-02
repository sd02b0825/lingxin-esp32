#ifndef ENV_SOUND_MONITOR_H
#define ENV_SOUND_MONITOR_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacity);
    ~AudioRingBuffer();

    size_t Write(const int16_t* data, size_t count);
    size_t ReadAvailable(std::vector<int16_t>& output, size_t max_samples = 0);
    void Clear();
    size_t Size() const;
    size_t Capacity() const;

private:
    std::unique_ptr<int16_t[]> buffer_;
    size_t capacity_;
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
};

class EnvSoundMonitor {
public:
    EnvSoundMonitor();
    ~EnvSoundMonitor();

    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const;

    void FeedPcm(const int16_t* data, size_t samples);

private:
    static constexpr const char* TAG = "EnvSoundMonitor";
    static constexpr int OPUS_FRAME_DURATION_MS = 60;
    static constexpr int UPLOAD_TASK_STACK_SIZE = 2048 * 10;
    static constexpr int UPLOAD_TASK_PRIORITY = 2;
    static constexpr int UPLOAD_CHECK_INTERVAL_MS = 100;
    static constexpr int HTTP_TIMEOUT_MS = 10000;
    static constexpr int STOP_TIMEOUT_MS = 15000;
    static constexpr uint32_t EVENT_TASK_EXITED = (1 << 0);

    std::string upload_url_;
    int interval_sec_;
    int sample_rate_ = 16000;
    int bitrate_kbps_ = 16;
    int frame_size_ = 0;

    size_t upload_chunk_samples_ = 0;

    void* opus_encoder_ = nullptr;
    int encoder_frame_size_ = 0;
    int encoder_outbuf_size_ = 0;
    std::mutex encoder_mutex_;

    std::unique_ptr<AudioRingBuffer> ring_buffer_;
    std::mutex state_mutex_;

    TaskHandle_t upload_task_handle_ = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    std::atomic<bool> running_{false};

    std::atomic<uint32_t> upload_count_{0};
    std::atomic<uint32_t> upload_error_count_{0};

    void UploadTask();
    bool EncodePcmToOpus(const std::vector<int16_t>& pcm, std::vector<uint8_t>& opus_data);
    bool HttpPostOpus(const std::vector<uint8_t>& opus_data);
};

#endif