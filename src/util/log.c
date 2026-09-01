#include "mrock/common.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static log_level_t g_level = LOG_INFO;

void log_set_level(log_level_t level)
{
    g_level = level;
}

// YORULDUM YARIN EKLERIM DEVAMINI BONİ ^^'