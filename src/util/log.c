#include "mrock/common.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

//The -v flag lowers this to LOG_DEBUG | -v bayrağı bunu LOG_DEBUG'a indiriyor
static log_level_t g_level = LOG_INFO;

void log_set_level(log_level_t level)
{
    g_level = level;
}

static const char *level_name(log_level_t level)
{
    switch (level)
    {
        case LOG_TRACE: return "TRC";
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        default:        return "ERR";
    }
}

void log_write(log_level_t level, const char *file, int line, const char *format, ...)
{
    if(level < g_level)
        return;

    //__FILE__ carries the whole path, only the file name is wanted | __FILE__ bütün yolu taşıyor, istenen sadece dosya adı
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;

    //Wall clock on purpose, since a log line is read by a human; mr_now_ms is monotonic for the protocol timers | Bilerek duvar saati, çünkü log satırını insan okuyor; mr_now_ms protokol sayaçları için monotonik
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm broken_down;
    localtime_r(&ts.tv_sec, &broken_down);

    //Everything goes to stderr so it never mixes with program output | Her şey stderr'e gidiyor, program çıktısıyla karışmasın
    fprintf(stderr, "%02d:%02d:%02d.%03ld [%s] %s:%d: ", broken_down.tm_hour, broken_down.tm_min, broken_down.tm_sec, ts.tv_nsec / 1000000L, level_name(level), base, line);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fputc('\n', stderr);
}

void log_hexdump(log_level_t level, const char *tag, const void *data, size_t len)
{
    if(level < g_level)
        return;

    const uint8_t *bytes = (const uint8_t *)data;
    fprintf(stderr, "--- %s (%zu bytes) ---\n", tag, len);

    for(size_t offset = 0; offset < len; offset += 16)
    {
        fprintf(stderr, "%04zx  ", offset);
        for(size_t i = 0; i < 16; ++i)
        {
            if(offset + i < len)
                fprintf(stderr, "%02x ", bytes[offset + i]);
            else
                fputs("   ", stderr);

            //Extra gap in the middle, the usual hex dump layout | Ortada fazladan boşluk, alışılmış hex dump düzeni
            if(i == 7)
                fputc(' ', stderr);
        }
        fputs(" |", stderr);
        for(size_t i = 0; i < 16 && offset + i < len; ++i)
        {
            uint8_t c = bytes[offset + i];
            fputc((c >= 0x20 && c <= 0x7e) ? c : '.', stderr);
        }
        fputs("|\n", stderr);
    }
}

//Monotonic, because every timeout and resend timer is a difference of two of these | Monotonik, çünkü her timeout ve resend sayacı bunlardan ikisinin farkı
uint64_t mr_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}
