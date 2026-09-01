#ifndef MCROCK_COMMON_H
#define MCROCK_COMMON_H

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

#define log_trace(...) log_write(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  log_write(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

uint64_t mr_now_ms(void);

#define MR_TRIAD_MAX 0x1000000u
#define MR_TRIAD_HALF 0x800000u

static inline bool triad_gt(uint32_t a, uint32_t b)
{
    uint32_t diff = a - b & 0xFFFFFFu;
    return diff != 0 && diff < MR_TRIAD_HALF;
}

static inline bool triad_ge(uint32_t a, uint32_t b)
{
    return ((a - b) & 0xFFFFFFu) < MR_TRIAD_HALF; 
}

static inline bool triad_lt(uint32_t a, uint32_t b)
{
    return triad_gt(b, a);
}

static inline bool triad_le(uint32_t a, uint32_t b)
{
    return triad_ge(b, a);
}

static inline int32_t triad_diff(uint32_t a, uint32_t b)
{
    uint32_t diff = (a - b) & 0xFFFFFFu;
    return diff < MR_TRIAD_HALF ? (int32_t)diff : (int32_t)(diff - MR_TRIAD_MAX);
}

static inline uint32_t triad_inc(uint32_t a)
{
    return (a + 1) & 0xFFFFFFu;
}

#endif // MCROCK_COMMON_H