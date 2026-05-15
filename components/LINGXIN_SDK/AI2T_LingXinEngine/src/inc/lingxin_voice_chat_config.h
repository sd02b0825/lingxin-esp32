#ifndef LINGXIN_VOICE_CHAT_CONFIG_H
#define LINGXIN_VOICE_CHAT_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "chat_api.h"

/**
 * @brief appId是企业在灵芯的唯一标识，在灵芯工作台获取，https://eagent.edu-aliyun.com/console/device/#/tenant/developer
 * @return appId
 */
char* lingxin_auth_appId_get();

/**
 * @brief License是灵芯平台颁发的 License
 * @return License
 */
char* lingxin_auth_license_get();

/**
 * @brief sn是设备在灵芯平台上的唯一 id，一般用设备唯一编号
 * @return sn
 */
char* lingxin_auth_sn_get();

/**
 * @brief appCode是灵芯工作台中创建的app应用的唯一标识
 * @return appCode
 */
char* lingxin_auth_appCode_get();

/**
 * @brief device_code是设备型号
 * @return device_code
 */
char* lingxin_device_code_get();

/**
 * @brief 内部注册动态获取appId、license、sn、appCode的函数
 * @param auth_app_id_get_func appId获取函数
 * @param auth_license_get_func license获取函数
 * @param auth_sn_get_func sn获取函数
 * @param auth_app_code_get_func appCode获取函数
 * @param chat_biz_parameter_get_func 获取业务参数的方法
 * @param chat_custom_parameter_get_func 获取自定义参数的方法
 * @param chat_flow_control_parameter_get_func 获取流控参数的方法
 * @param websocket_check_interval 检测websocket连接状态的间隔时间
 * @param websocket_check_timeout 检测websocket连接状态的超时时间
 */
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
);

int websocket_check_interval_get();

int websocket_check_timeout_get();

char* lingxin_chat_biz_parameter_get();

char* lingxin_chat_custom_parameter_get();

char* lingxin_chat_flow_control_parameter_get();

typedef struct {
  char* input_format;       // 输入音频格式
  int input_sample_rate;    // 输入音频采样率
  char* output_format;      // 输出音频格式
  int output_sample_rate;   // 输出音频采样率
  bool enable_schedule_task;// 是否支持定时任务
  bool enable_log_upload;   // 是否支持日志上传
} LingxinServerConfig;

#ifdef __cplusplus
}
#endif

#endif // LINGXIN_VOICE_CHAT_CONFIG_H