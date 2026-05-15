#ifndef LINGXIN_CHIP_INFO_H
#define LINGXIN_CHIP_INFO_H

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * 获取设备对应的芯片名称
     * 举例： LINGXIN_芯片名称
     */
    char *get_lingxin_device_name();

    /**
     * 获取设备对应的灵芯版本
     * 举例：0.0.1
     */
    char *get_lingxin_device_version();

#ifdef __cplusplus
}
#endif
#endif // LINGXIN_CHIP_INFO_H