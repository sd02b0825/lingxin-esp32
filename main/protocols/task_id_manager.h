#ifndef TASK_ID_MANAGER_H
#define TASK_ID_MANAGER_H

#include <string>

class TaskIdManager {
public:
    explicit TaskIdManager(const std::string& sn);

    std::string BeginNewConversation();
    std::string BeginNextTurn();
    const std::string& Current() const { return current_task_id_; }
    void MarkTaskClosed();

private:
    std::string BuildTaskId(int32_t conversation_id, int32_t turn_id) const;

    std::string sn_;
    std::string current_task_id_;
};

#endif // TASK_ID_MANAGER_H
