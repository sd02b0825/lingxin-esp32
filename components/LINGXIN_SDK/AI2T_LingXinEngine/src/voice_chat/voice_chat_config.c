#include "lingxin_voice_chat_config.h"
#include "cJSON.h"
#include <string.h>
#include "lingxin_log.h"
#include "lingxin_memory.h"

static AuthAppIdGetFunc inner_lingxin_auth_app_id_get = NULL;
static AuthLicenseGetFunc inner_lingxin_auth_license_get = NULL;
static AuthSnGetFunc inner_lingxin_auth_sn_get = NULL;
static AuthAppCodeGetFunc inner_lingxin_auth_app_code_get = NULL;
static DeviceCodeGetFunc inner_lingxin_device_code_get = NULL;
static ChatBizParameterGetFunc inner_lingxin_chat_biz_parameter_get = NULL;
static ChatCustomParameterGetFunc inner_lingxin_chat_custom_parameter_get = NULL;
static ChatFlowControlParameterGetFunc inner_lingxin_chat_flow_control_parameter_get = NULL;
static int inner_lingxin_websocket_check_interval = 0;
static int inner_lingxin_websocket_check_timeout = 0;

void module_voice_chat_config_init(
    AuthAppIdGetFunc auth_app_id_get_func, 
    AuthLicenseGetFunc auth_license_get_func, 
    AuthSnGetFunc auth_sn_get_func, 
    AuthAppCodeGetFunc auth_app_code_get_func,
    DeviceCodeGetFunc device_code_get_func,
    ChatBizParameterGetFunc chat_biz_parameter_get_func,
    ChatCustomParameterGetFunc chat_custom_parameter_get_func,
    ChatFlowControlParameterGetFunc chat_flow_control_parameter_get_func,
    int websocket_check_interval,
    int websocket_check_timeout
) {
    inner_lingxin_auth_app_id_get = auth_app_id_get_func;
    inner_lingxin_auth_license_get = auth_license_get_func;
    inner_lingxin_auth_sn_get = auth_sn_get_func;
    inner_lingxin_auth_app_code_get = auth_app_code_get_func;
    inner_lingxin_device_code_get = device_code_get_func;
    inner_lingxin_chat_custom_parameter_get = chat_custom_parameter_get_func;
    inner_lingxin_chat_biz_parameter_get = chat_biz_parameter_get_func;
    inner_lingxin_chat_flow_control_parameter_get = chat_flow_control_parameter_get_func;
    inner_lingxin_websocket_check_interval = websocket_check_interval;
    inner_lingxin_websocket_check_timeout = websocket_check_timeout;
}

int websocket_check_interval_get() {
    return inner_lingxin_websocket_check_interval;
}

int websocket_check_timeout_get() {
    return inner_lingxin_websocket_check_timeout;
}

static char* appId = NULL;
static char* license = NULL;
static char* sn = NULL;
char* lingxin_auth_appId_get() {
    if (inner_lingxin_auth_app_id_get) {
        if (!appId) {
            appId = inner_lingxin_auth_app_id_get();
        }
        return appId;
    } else {
        return NULL;
    }
}

char* lingxin_auth_license_get() {
    if (inner_lingxin_auth_license_get) {
        if (!license) {
            license = inner_lingxin_auth_license_get();
        }
        return license;
    } else {
        return NULL;
    }
}
char* lingxin_auth_sn_get() {
    if (inner_lingxin_auth_sn_get) {
        if (!sn) {
            sn = inner_lingxin_auth_sn_get();
        }
        return sn;
    } else {
        return NULL;
    }
}
char* lingxin_auth_appCode_get() {
    if (inner_lingxin_auth_app_code_get) {
        return inner_lingxin_auth_app_code_get();
    } else {
        return NULL;
    }
}

char* lingxin_device_code_get() {
    if (inner_lingxin_device_code_get) {
        return inner_lingxin_device_code_get();
    } else {
        return NULL;
    }
}

static bool is_valid_json_string(char *str) {
    if (!str) {
        return false;
    }
    cJSON *jsonObj = cJSON_Parse(str);
    if (!jsonObj) {
        const char *error_ptr = cJSON_GetErrorPtr();
        lingxin_log_error("Error json: %s", str);
        lingxin_log_error("Error error_ptr: %s", error_ptr);
        return false;
    } else {
        cJSON_Delete(jsonObj);
        return true;
    }
}

char* lingxin_chat_biz_parameter_get() {
    char* device_code = lingxin_device_code_get();
    if (inner_lingxin_chat_biz_parameter_get) {
        char* biz_parameter_str = inner_lingxin_chat_biz_parameter_get();
        if (device_code && strlen(device_code)) {
            if (!biz_parameter_str) {
                goto no_biz_parameter;
            }
            cJSON *jsonObj = cJSON_Parse(biz_parameter_str);
            if (!jsonObj) {
                const char *error_ptr = cJSON_GetErrorPtr();
                lingxin_log_error("Error json: %s", biz_parameter_str);
                lingxin_log_error("Error error_ptr: %s", error_ptr);
                goto no_biz_parameter;
            } else {
                cJSON *inner_device_code = cJSON_GetObjectItemCaseSensitive(jsonObj, "device_code");
                if (inner_device_code && cJSON_IsString(inner_device_code) 
                && inner_device_code->valuestring && strlen(inner_device_code->valuestring)) {
                    cJSON_Delete(jsonObj);
                    return lingxin_strdup(biz_parameter_str);
                } else {
                    cJSON_AddStringToObject(jsonObj, "device_code", device_code);
                    char* result = cJSON_PrintUnformatted(jsonObj);
                    char* result_copy = lingxin_strdup(result);
                    cJSON_free(result);
                    cJSON_Delete(jsonObj);
                    return result_copy;
                }
            }
        } else if (is_valid_json_string(biz_parameter_str)) {
            return lingxin_strdup(biz_parameter_str);
        } else {
            return lingxin_strdup("{}");
        }
    }

no_biz_parameter:
    if (device_code && strlen(device_code)) {
        lingxin_log_debug("device_code: %s", device_code);
        cJSON *jsonObj = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonObj, "device_code", device_code);
        char* result = cJSON_PrintUnformatted(jsonObj);
        char* result_copy = lingxin_strdup(result);
        cJSON_free(result);
        cJSON_Delete(jsonObj);
        return result_copy;
    } else {
        return lingxin_strdup("{}");
    }
}

char* lingxin_chat_custom_parameter_get() {
    if (inner_lingxin_chat_custom_parameter_get) {
        char* custom_parameter_str = inner_lingxin_chat_custom_parameter_get();
        if (is_valid_json_string(custom_parameter_str)) {
            return custom_parameter_str;
        } else {
            return "{}";
        }
    } else {
        return "{}";
    }
}

char* lingxin_chat_flow_control_parameter_get() {
    if (inner_lingxin_chat_flow_control_parameter_get) {
        char* flow_control_parameter_str = inner_lingxin_chat_flow_control_parameter_get();
        if (is_valid_json_string(flow_control_parameter_str)) {
            return flow_control_parameter_str;
        } else {
            return "{}";
        }
    } else {
        return "{}";
    }
}
