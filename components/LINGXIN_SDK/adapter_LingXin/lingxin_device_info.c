/**
 * lingxin_device_info.c - v2.6.6 adapter
 *
 * Adapted to provide device info from v2.6.6 Board/Settings.
 * Falls back to static strings if bridge functions are unavailable.
 */

#include "lingxin_device_info.h"

/* Bridge function implemented in lingxin_sdk_bridge.cc */
extern const char *lingxin_bridge_get_device_name(void);
extern const char *lingxin_bridge_get_device_version(void);

char *get_lingxin_device_name()
{
    const char *name = lingxin_bridge_get_device_name();
    return (char *)(name ? name : "XIAOZHI_ESP32S3");
}

char *get_lingxin_device_version()
{
    const char *version = lingxin_bridge_get_device_version();
    return (char *)(version ? version : "2.2.6");
}
