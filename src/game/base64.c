#include "mrock/game/base64.h"

#include <string.h>

static uint8_t decode_table[256];
static bool table_ready = false;

static void build_decode_table(void)
{
    //0xFF marks an invalid character, that is how bad input is caught | 0xFF geçersiz karakteri işaretliyor, bozuk girdi böyle yakalanıyor
    memset(decode_table, 0xFF, sizeof(decode_table));

    for(int i = 0; i < 26; i++)
        decode_table['A' + i] = (uint8_t)i;

    for(int i = 0; i < 26; i++)
        decode_table['a' + i] = (uint8_t)(i + 26);

    for(int i = 0; i < 10 ; i++)
        decode_table['0' + i] = (uint8_t)(i + 52);

    //Both alphabets are accepted, standard and url-safe | İki alfabe de kabul ediliyor, standart ve url-safe
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

    //Collect 6 bits per character and emit a byte whenever 8 have piled up | Karakter başına 6 bit toplanıyor, 8 bit birikince bir bayt yazılıyor
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

    //6 or more leftover bits means a dangling character that completes no byte | 6 veya daha fazla artık bit, hiçbir baytı tamamlamayan sarkan bir karakter demek
    if(bits >= 6)
        return -1;

    return (int)written;
}
