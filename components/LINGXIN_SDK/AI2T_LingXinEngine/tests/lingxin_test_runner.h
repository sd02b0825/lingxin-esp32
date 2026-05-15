#ifndef __LINGXIN_TEST_RUNNER_H__
#define __LINGXIN_TEST_RUNNER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "chat_api.h"

void lingxin_test_runner_init(VoiceChatInitProps *props);
void lingxin_test_runner_wakeup();

void lingxin_checkpoint_report(char* checkpoint_name); 
void lingxin_checkpoint_report_with_int(char* checkpoint_name, int value);
void lingxin_checkpoint_report_with_string(char* checkpoint_name, const char* value); 

#ifdef __cplusplus
}
#endif

#endif // __LINGXIN_TEST_RUNNER_H__



