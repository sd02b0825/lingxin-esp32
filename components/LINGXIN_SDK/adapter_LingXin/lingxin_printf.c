#include "lingxin_printf.h"
#include <stdio.h>

void lingxin_printf(char *message)
{
    if (!message)
    {
        printf("[%s] [%s] message is null", __FILE__, __FUNCTION__);
        return;
    }
    printf("%s\n", message);
}