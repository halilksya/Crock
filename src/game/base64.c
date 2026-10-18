#include "mrock/game/base64.h"

#include <string.h>

static uint8_t decode_table[256];
static bool table_ready = false;

static void build_decode_table(void)
{
    memset(decode_table, 0xFF, sizeof(decode_table));

    for(int i = 0; i < 26; i++)
        decode_table['A' + i] = (uint8_t)i;

    for(int i = 0; i < 26; i++)
        decode_table['a' + i] = (uint8_t)(i + 26);

    for(int i = 0; i < 10 ; i++)
        decode_table['0' + i] = (uint8_t)(i + 52);

    decode_table['+'] = 62;
    decode_table['-'] = 62;
    decode_table['/'] = 63;
    decode_table['_'] = 63;

    table_ready = true;
}

int mc_base64url_decode(const char *src, size_t src_len, uint8_t *out, size_t out_cap)
{
    if(!table_ready)
        build_decode_table();

    size_t written = 0;
    uint32_t accumulator = 0;
    int bits = 0;

    for(size_t i = 0; i < src_len; i++)
    {
        uint8_t c = (uint8_t)src[i];
        
        if(c == '=')
            break;
        
        uint8_t value = decode_table[c];
        if(value == 0xFF)
            return -1;

        accumulator = (accumulator << 6) | value;
        bits += 6;

        if(bits >= 8)
        {
            bits -= 8;

            if(written >= out_cap)
                return -1;

            out[written++] = (uint8_t)((accumulator >> bits) & 0xFF);
        }
    }

    if(bits >= 6)
        return -1;

    return (int)written;
}

