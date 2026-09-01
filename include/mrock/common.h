#ifndef MCROCK_COMMON_H
#define MCROCK_COMMON_H

#endif // MCROCK_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum 
{
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level_t;

void log_set_level(log_level_t level);
void log_write(log_level_t level, const char *file, int line, const char *format, ...);
void log_hexdump(log_level_t level, const char *tag, const void *data, size_t len);

