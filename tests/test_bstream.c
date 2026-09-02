#include "mrock/util/bstream.h"

#include <stdio.h>
#include <string.h>

static int total = 0;
static int fails = 0;

#define CHECK(cond, ...) do                                             \
{                                                                       \
    total++;                                                            \
    if (!(cond))                                                        \
    {                                                                   \
        fails++;                                                        \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);                     \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    }                                                                   \
} while (0)

static void test_endian(void)
{
    printf("bstream: endian testleri\n");

    uint8_t buf[64];
    bs_t s;
    bs_write_init(&s, buf, sizeof(buf));

    bs_wu16be(&s, 0x1234);
    bs_wu16le(&s, 0x1234);
    bs_wu32be(&s, 0xAABBCCDD);
    bs_wu32le(&s, 0xAABBCCDD);
    bs_wu24le(&s, 0x123456);

    CHECK(!s.err, "yazma hatası");

    CHECK(buf[0] == 0x12 && buf[1] == 0x34, "u16be byte sırası yanlış");
    CHECK(buf[2] == 0x34 && buf[3] == 0x12, "u16le byte sırası yanlış");
    CHECK(buf[4] == 0xAA && buf[7] == 0xDD, "u32be byte sırası yanlış");
    CHECK(buf[8] == 0xDD && buf[11] == 0xAA, "u32le byte sırası yanlış");
    CHECK(buf[12] == 0x56 && buf[13] == 0x34 && buf[14] == 0x12, "triad byte sırası yanlış");

    bs_t r;
    bs_read_init(&r, buf, s.len);
    CHECK(bs_ru16be(&r) == 0x1234, "u16be geri okuma");
    CHECK(bs_ru16le(&r) == 0x1234, "u16le geri okuma");
    CHECK(bs_ru32be(&r) == 0xAABBCCDD, "u32be geri okuma");
    CHECK(bs_ru32le(&r) == 0xAABBCCDD, "u32le geri okuma");
    CHECK(bs_ru24le(&r) == 0x123456, "triad geri okuma");
    CHECK(!r.err, "okuma hatasi");
    CHECK(bs_remaining(&r) == 0, "artik byte kaldi");
}

static void test_varint(void)
{
    printf("bstream: varint testleri\n");

    uint64_t values[] = { 0, 1, 127, 128, 255, 300, 0xFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };

    for(size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++)
    {
        uint8_t buf[16];
        bs_t s;
        bs_write_init(&s, buf, sizeof(buf));
        bs_wvaru64(&s, values[i]);

        bs_t r;
        bs_read_init(&r, buf, s.len);
        uint64_t got = bs_rvaru64(&r);

        CHECK(got == values[i] && !r.err, "varu64 %llu -> %llu", (unsigned long long)values[i], (unsigned long long)got);
    }
    
    int32_t svalues[] = { 0, -1, 1, -2, 2, -64, 63, INT32_MIN, INT32_MAX };

    for(size_t i = 0; i < sizeof(svalues)/sizeof(svalues[0]); i++)
    {
        uint8_t buf[16];
        bs_t s;
        bs_write_init(&s, buf, sizeof(buf));
        bs_wvari32(&s, svalues[i]);

        bs_t r;
        bs_read_init(&r, buf, s.len);
        int32_t got = bs_rvari32(&r);

        CHECK(got == svalues[i] && !r.err, "vari32 %d -> %d", svalues[i], got);
    }

    uint8_t buff[16];
    bs_t s;
    bs_write_init(&s, buff, sizeof(buff));
    bs_wvari32(&s, -1);
    CHECK(s.len == 1 && buff[0] == 0x01, "zigzag tek byte olmalı, %zu byte",s.len);

    bs_write_init(&s, buff, sizeof(buff));
    bs_wvaru64(&s, 0);
    CHECK(s.len == 1 && buff[0] == 0x00, "varint(0) tek byte olmalı, %zu byte",s.len);
}
// YETER YETER YORULDUM YETER KOD BITMIYOR YETER BONI YARDIM ET NOLUR ;<