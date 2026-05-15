#include "lingxin_system.h"
#include "esp_system.h"
void lingxin_system_abort() {
    esp_restart();
}