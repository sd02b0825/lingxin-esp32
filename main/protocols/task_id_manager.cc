#include "task_id_manager.h"

#include "settings.h"

#include <esp_log.h>

#define TAG "TaskId"

TaskIdManager::TaskIdManager(const std::string& sn) : sn_(sn) {
    Settings settings("lingxin", false);
    int32_t conversation_id = settings.GetInt("conv_id", 0);
    int32_t turn_id = settings.GetInt("turn_id", 0);
    if (conversation_id > 0 && turn_id > 0) {
        current_task_id_ = BuildTaskId(conversation_id, turn_id);
    }
}

std::string TaskIdManager::BeginNewConversation() {
    Settings settings("lingxin", true);
    int32_t conversation_id = settings.GetInt("conv_id", 0) + 1;
    int32_t turn_id = 1;

    current_task_id_ = BuildTaskId(conversation_id, turn_id);
    settings.SetInt("conv_id", conversation_id);
    settings.SetInt("turn_id", turn_id);
    settings.SetBool("task_inflight", true);
    ESP_LOGI(TAG, "Begin conversation task: %s", current_task_id_.c_str());
    return current_task_id_;
}

std::string TaskIdManager::BeginNextTurn() {
    Settings settings("lingxin", true);
    int32_t conversation_id = settings.GetInt("conv_id", 0);
    if (conversation_id <= 0) {
        conversation_id = 1;
    }
    int32_t turn_id = settings.GetInt("turn_id", 0) + 1;

    current_task_id_ = BuildTaskId(conversation_id, turn_id);
    settings.SetInt("conv_id", conversation_id);
    settings.SetInt("turn_id", turn_id);
    settings.SetBool("task_inflight", true);
    ESP_LOGI(TAG, "Begin turn task: %s", current_task_id_.c_str());
    return current_task_id_;
}

void TaskIdManager::MarkTaskClosed() {
    Settings settings("lingxin", true);
    settings.SetBool("task_inflight", false);
}

std::string TaskIdManager::BuildTaskId(int32_t conversation_id, int32_t turn_id) const {
    return sn_ + "^" + std::to_string(conversation_id) + "^" + std::to_string(turn_id);
}
