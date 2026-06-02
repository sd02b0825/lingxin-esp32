#include "env_sound_monitor.h"
#include "board.h"
#include "system_info.h"

#include <esp_log.h>
#include <esp_opus_enc.h>
#include <mbedtls/base64.h>
#include <cJSON.h>
#include <cstring>
#include <cstdio>

//==============================================================================
// AudioRingBuffer
//==============================================================================

AudioRingBuffer::AudioRingBuffer(size_t capacity)
    : buffer_(std::make_unique<int16_t[]>(capacity)), capacity_(capacity) {
}

AudioRingBuffer::~AudioRingBuffer() = default;

size_t AudioRingBuffer::Write(const int16_t* data, size_t count) {
    if (data == nullptr || count == 0) {
        return 0;
    }

    size_t write_idx = write_pos_.load(std::memory_order_relaxed);
    size_t read_idx = read_pos_.load(std::memory_order_acquire);

    size_t available;
    if (write_idx >= read_idx) {
        available = capacity_ - (write_idx - read_idx) - 1;
    } else {
        available = read_idx - write_idx - 1;
    }

    size_t to_write = (count < available) ? count : available;
    if (to_write == 0) {
        return 0;
    }

    size_t first_chunk = capacity_ - write_idx;
    if (first_chunk >= to_write) {
        memcpy(buffer_.get() + write_idx, data, to_write * sizeof(int16_t));
        write_idx += to_write;
    } else {
        memcpy(buffer_.get() + write_idx, data, first_chunk * sizeof(int16_t));
        memcpy(buffer_.get(), data + first_chunk,
               (to_write - first_chunk) * sizeof(int16_t));
        write_idx = to_write - first_chunk;
    }

    if (write_idx >= capacity_) {
        write_idx -= capacity_;
    }

    write_pos_.store(write_idx, std::memory_order_release);
    return to_write;
}

size_t AudioRingBuffer::ReadAvailable(std::vector<int16_t>& output, size_t max_samples) {
    size_t write_idx = write_pos_.load(std::memory_order_acquire);
    size_t read_idx = read_pos_.load(std::memory_order_relaxed);

    size_t available;
    if (write_idx >= read_idx) {
        available = write_idx - read_idx;
    } else {
        available = capacity_ - read_idx + write_idx;
    }

    if (available == 0) {
        output.clear();
        return 0;
    }

    size_t to_read = (max_samples > 0 && max_samples < available) ? max_samples : available;
    output.resize(to_read);

    size_t first_chunk = capacity_ - read_idx;
    if (first_chunk >= to_read) {
        memcpy(output.data(), buffer_.get() + read_idx, to_read * sizeof(int16_t));
    } else {
        memcpy(output.data(), buffer_.get() + read_idx, first_chunk * sizeof(int16_t));
        memcpy(output.data() + first_chunk, buffer_.get(),
               (to_read - first_chunk) * sizeof(int16_t));
    }

    size_t new_read_idx = read_idx + to_read;
    if (new_read_idx >= capacity_) {
        new_read_idx -= capacity_;
    }
    read_pos_.store(new_read_idx, std::memory_order_release);

    return to_read;
}

void AudioRingBuffer::Clear() {
    read_pos_.store(write_pos_.load(std::memory_order_relaxed),
                    std::memory_order_release);
}

size_t AudioRingBuffer::Size() const {
    size_t write_idx = write_pos_.load(std::memory_order_relaxed);
    size_t read_idx = read_pos_.load(std::memory_order_relaxed);
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return capacity_ - read_idx + write_idx;
    }
}

size_t AudioRingBuffer::Capacity() const {
    return capacity_;
}

//==============================================================================
// EnvSoundMonitor
//==============================================================================

EnvSoundMonitor::EnvSoundMonitor() {
    event_group_ = xEventGroupCreate();
}

EnvSoundMonitor::~EnvSoundMonitor() {
    Stop();
    {
        std::lock_guard<std::mutex> lock(encoder_mutex_);
        if (opus_encoder_ != nullptr) {
            esp_opus_enc_close(opus_encoder_);
            opus_encoder_ = nullptr;
        }
    }
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}

void EnvSoundMonitor::Initialize() {
    upload_url_ = CONFIG_ENV_SOUND_MONITOR_URL;
    interval_sec_ = CONFIG_ENV_SOUND_MONITOR_INTERVAL_SEC;
    bitrate_kbps_ = CONFIG_ENV_SOUND_MONITOR_OPUS_BITRATE_KBPS;
    sample_rate_= CONFIG_ENV_SOUND_MONITOR_SAMPLE_RATE;
    frame_size_= sample_rate_ * OPUS_FRAME_DURATION_MS / 1000;

    upload_chunk_samples_ = (size_t)interval_sec_ * sample_rate_;

    ESP_LOGI(TAG, "Initialize: url=%s, interval=%ds, bitrate=%dkbps, frame_size=%d, chunk=%u samples",
             upload_url_.c_str(), interval_sec_, bitrate_kbps_, frame_size_,
             (unsigned int)upload_chunk_samples_);

    esp_opus_enc_config_t enc_cfg = {
        .sample_rate = sample_rate_,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = bitrate_kbps_ * 1000,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = true,
        .enable_vbr = true,
    };
    auto ret = esp_opus_enc_open(&enc_cfg, sizeof(enc_cfg), &opus_encoder_);
    if (opus_encoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create Opus encoder, ret=%d", ret);
        return;
    }
    esp_opus_enc_get_frame_size(opus_encoder_, &encoder_frame_size_, &encoder_outbuf_size_);
    encoder_frame_size_ = encoder_frame_size_ / sizeof(int16_t);
    ESP_LOGI(TAG, "Opus encoder: frame_size=%d samples, outbuf_size=%d bytes",
             encoder_frame_size_, encoder_outbuf_size_);

    size_t buffer_capacity = upload_chunk_samples_ * 3;
    ring_buffer_ = std::make_unique<AudioRingBuffer>(buffer_capacity);
}

void EnvSoundMonitor::Start() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (running_) {
        ESP_LOGW(TAG, "Already running");
        return;
    }
    if (upload_url_.length() < 9 ||
        (upload_url_.find("http://") != 0 && upload_url_.find("https://") != 0)) {
        ESP_LOGE(TAG, "Invalid URL: %s", upload_url_.c_str());
        return;
    }
    if (opus_encoder_ == nullptr) {
        ESP_LOGE(TAG, "Cannot start: encoder not initialized");
        return;
    }

    running_ = true;
    upload_count_ = 0;
    upload_error_count_ = 0;
    ring_buffer_->Clear();

    xEventGroupClearBits(event_group_, EVENT_TASK_EXITED);

    BaseType_t ret = xTaskCreate([](void* arg) {
        EnvSoundMonitor* monitor = (EnvSoundMonitor*)arg;
        monitor->UploadTask();
        vTaskDelete(NULL);
    }, "env_sound_up", UPLOAD_TASK_STACK_SIZE, this, UPLOAD_TASK_PRIORITY, &upload_task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create upload task");
        running_ = false;
        upload_task_handle_ = nullptr;
        return;
    }

    ESP_LOGI(TAG, "Started, buffer=%u samples (~%u sec), URL: %s",
             (unsigned int)ring_buffer_->Capacity(),
             (unsigned int)(ring_buffer_->Capacity() / sample_rate_),
             upload_url_.c_str());
}

void EnvSoundMonitor::Stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (!running_) {
        return;
    }
    running_ = false;

    if (upload_task_handle_ != nullptr) {
        EventBits_t bits = xEventGroupWaitBits(
            event_group_,
            EVENT_TASK_EXITED,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(STOP_TIMEOUT_MS));

        if (!(bits & EVENT_TASK_EXITED)) {
            ESP_LOGW(TAG, "Upload task did not exit in time, force deleting");
            vTaskDelete(upload_task_handle_);
        }
        upload_task_handle_ = nullptr;
    }

    ring_buffer_->Clear();

    ESP_LOGI(TAG, "Stopped (uploads: %u, errors: %u)",
             upload_count_.load(), upload_error_count_.load());
}

bool EnvSoundMonitor::IsRunning() const {
    return running_;
}

void EnvSoundMonitor::FeedPcm(const int16_t* data, size_t samples) {
    if (!running_ || data == nullptr || samples == 0) return;
    size_t written = ring_buffer_->Write(data, samples);
    if (written < samples) {
        ESP_LOGD(TAG, "Ring buffer full, dropped %u samples",
                 (unsigned int)(samples - written));
    }
}

void EnvSoundMonitor::UploadTask() {
    ESP_LOGI(TAG, "Upload task started");

    while (running_) {
        vTaskDelay(pdMS_TO_TICKS(UPLOAD_CHECK_INTERVAL_MS));

        if (!running_) {
            break;
        }

        size_t buffered = ring_buffer_->Size();
        if (buffered < upload_chunk_samples_) {
            continue;
        }

        std::vector<int16_t> data_to_upload;
        size_t samples = ring_buffer_->ReadAvailable(data_to_upload, upload_chunk_samples_);
        if (samples == 0) {
            continue;
        }

        ESP_LOGD(TAG, "Uploading %u samples (%.1f sec)",
                 (unsigned int)samples, (float)samples / sample_rate_);

        if (!running_) break;

        std::vector<uint8_t> opus_data;
        if (!EncodePcmToOpus(data_to_upload, opus_data)) {
            ESP_LOGW(TAG, "Opus encoding returned no data");
            continue;
        }

        if (!running_) break;

        HttpPostOpus(opus_data);
    }

    ESP_LOGI(TAG, "Upload task exiting");

    if (event_group_ != nullptr) {
        xEventGroupSetBits(event_group_, EVENT_TASK_EXITED);
    }
}

bool EnvSoundMonitor::EncodePcmToOpus(const std::vector<int16_t>& pcm,
                                       std::vector<uint8_t>& opus_data) {
    std::lock_guard<std::mutex> lock(encoder_mutex_);
    if (opus_encoder_ == nullptr) {
        return false;
    }

    std::vector<uint8_t> temp_buf(encoder_outbuf_size_);
    size_t pos = 0;

    while (pos + encoder_frame_size_ <= pcm.size()) {
        esp_audio_enc_in_frame_t in = {
            .buffer = (uint8_t*)(pcm.data() + pos),
            .len = (uint32_t)(encoder_frame_size_ * sizeof(int16_t)),
        };
        esp_audio_enc_out_frame_t out = {
            .buffer = temp_buf.data(),
            .len = (uint32_t)encoder_outbuf_size_,
            .encoded_bytes = 0,
        };
        auto ret = esp_opus_enc_process(opus_encoder_, &in, &out);
        if (ret == ESP_AUDIO_ERR_OK && out.encoded_bytes > 0) {
            uint16_t frame_len = (uint16_t)out.encoded_bytes;
            opus_data.push_back((uint8_t)(frame_len & 0xFF));
            opus_data.push_back((uint8_t)((frame_len >> 8) & 0xFF));
            opus_data.insert(opus_data.end(),
                            temp_buf.data(), temp_buf.data() + out.encoded_bytes);
        }
        pos += encoder_frame_size_;
    }

    return !opus_data.empty();
}

bool EnvSoundMonitor::HttpPostOpus(const std::vector<uint8_t>& opus_data) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGW(TAG, "Network not available, skip upload");
        upload_error_count_++;
        return false;
    }

    auto http = network->CreateHttp();
    if (http == nullptr) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        upload_error_count_++;
        return false;
    }
    http->SetTimeout(HTTP_TIMEOUT_MS);

    size_t b64_len = 0;
    mbedtls_base64_encode(nullptr, 0, &b64_len, opus_data.data(), opus_data.size());
    std::string b64_data(b64_len, '\0');
    int ret = mbedtls_base64_encode((unsigned char*)b64_data.data(), b64_len,
                                     &b64_len, opus_data.data(), opus_data.size());
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encode failed: %d", ret);
        http->Close();
        upload_error_count_++;
        return false;
    }
    b64_data.resize(b64_len);

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        http->Close();
        upload_error_count_++;
        return false;
    }
    cJSON_AddStringToObject(root, "client_id", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root, "data", b64_data.c_str());
    cJSON_AddStringToObject(root, "type", "opus");
    cJSON_AddNumberToObject(root, "sample_rate", sample_rate_);
    cJSON_AddNumberToObject(root, "channels", 1);
    cJSON_AddNumberToObject(root, "frame_size", frame_size_);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "POST to %s, json_len=%u, opus_len=%u",
             upload_url_.c_str(), (unsigned int)json.size(), (unsigned int)opus_data.size());

    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("User-Agent", SystemInfo::GetUserAgent().c_str());
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetContent(std::move(json));

    if (!http->Open("POST", upload_url_)) {
        ESP_LOGE(TAG, "HTTP request failed, error: 0x%x", http->GetLastError());
        http->Close();
        upload_error_count_++;
        return false;
    }

    int status_code = http->GetStatusCode();
    if (status_code == 200) {
        std::string response = http->ReadAll();
        cJSON* resp_root = cJSON_Parse(response.c_str());
        if (resp_root != nullptr) {
            cJSON* success = cJSON_GetObjectItem(resp_root, "success");
            if (cJSON_IsBool(success) && cJSON_IsTrue(success)) {
                upload_count_++;
                ESP_LOGI(TAG, "Upload success: %u bytes",
                         (unsigned int)opus_data.size());
            } else {
                cJSON* message = cJSON_GetObjectItem(resp_root, "message");
                ESP_LOGW(TAG, "Upload rejected: %s",
                         cJSON_IsString(message) ? message->valuestring : "unknown");
                upload_error_count_++;
            }
            cJSON_Delete(resp_root);
        } else {
            ESP_LOGW(TAG, "Failed to parse response: %.100s", response.c_str());
            upload_error_count_++;
        }
    } else {
        ESP_LOGW(TAG, "HTTP error: status=%d", status_code);
        upload_error_count_++;
    }

    http->Close();
    return (status_code == 200);
}