#ifndef LINGXIN_USER_TRACK_H
#define LINGXIN_USER_TRACK_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

bool user_track_init(char* flash_path);

void user_track_record(char* log);

bool is_user_track_init();

void core_node_record(char* node);

#ifdef __cplusplus
}
#endif
#endif // LINGXIN_USER_TRACK_H