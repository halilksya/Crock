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
    printf("bstream: endian tests\n");

    uint8_t buf[64];
    bs_t s;
    bs_write_init(&s, buf, sizeof(buf));

    bs_wu16be(&s, 0x1234);
    bs_wu16le(&s, 0x1234);
    bs_wu32be(&s, 0xAABBCCDD);
    bs_wu32le(&s, 0xAABBCCDD);
    bs_wu24le(&s, 0x123456);

    CHECK(!s.err, "write error");

    //Checking the first and last byte of each field is enough to pin the order down | Her alanın ilk ve son baytını kontrol etmek sırayı saptamaya yetiyor
    CHECK(buf[0] == 0x12 && buf[1] == 0x34, "wrong u16be byte order");
    CHECK(buf[2] == 0x34 && buf[3] == 0x12, "wrong u16le byte order");
    CHECK(buf[4] == 0xAA && buf[7] == 0xDD, "wrong u32be byte order");
    CHECK(buf[8] == 0xDD && buf[11] == 0xAA, "wrong u32le byte order");
    CHECK(buf[12] == 0x56 && buf[13] == 0x34 && buf[14] == 0x12, "wrong triad byte order");

    bs_t r;
    bs_read_init(&r, buf, s.len);
    CHECK(bs_ru16be(&r) == 0x1234, "u16be readback");
    CHECK(bs_ru16le(&r) == 0x1234, "u16le readback");
    CHECK(bs_ru32be(&r) == 0xAABBCCDD, "u32be readback");
    CHECK(bs_ru32le(&r) == 0xAABBCCDD, "u32le readback");
    CHECK(bs_ru24le(&r) == 0x123456, "triad readback");
    CHECK(!r.err, "read error");
    CHECK(bs_remaining(&r) == 0, "bytes remain");
}

static void test_varint(void)
{
    printf("bstream: varint tests\n");

    //The last value is the ten byte worst case | Son değer on baytlık en kötü durum
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

    //The two extremes are where a zigzag mistake shows up | Zigzag hatası kendini bu iki uçta gösteriyor
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

    //This is the point of zigzag: a plain varint would spend five bytes on -1 | Zigzag'in bütün amacı bu: düz varint -1 için beş bayt harcardı
    uint8_t buff[16];
    bs_t s;
    bs_write_init(&s, buff, sizeof(buff));
    bs_wvari32(&s, -1);
    CHECK(s.len == 1 && buff[0] == 0x01, "zigzag should use one byte, %zu bytes", s.len);

    bs_write_init(&s, buff, sizeof(buff));
    bs_wvaru64(&s, 0);
    CHECK(s.len == 1 && buff[0] == 0x00, "varint(0) should use one byte, %zu bytes", s.len);
}

static void test_overflow(void)
{
    printf("bstream: overflow protection tests\n");

    uint8_t buf[4] = {1, 2, 3, 4};
    bs_t s;
    bs_read_init(&s, buf, sizeof(buf));

    bs_ru32be(&s);
    CHECK(!s.err, "4-byte read should succeed");

    bs_ru32be(&s);
    CHECK(s.err, "reading from an empty buffer should set err");

    //The sticky contract: once err is set nothing moves and nothing real comes back | Yapışkan sözleşme: err set olduktan sonra hiçbir şey ilerlemiyor ve gerçek bir değer dönmüyor
    CHECK(bs_ru64be(&s) == 0, "read after err should return 0");
    CHECK(bs_rbytes(&s, 100) == NULL, "read after err should return NULL");
    CHECK(s.pos == 4, "pos should not advance after err, pos=%zu", s.pos);

    //A fixed buffer must refuse rather than grow, and refuse without writing part of the value | Sabit buffer büyümek yerine reddetmek, hem de değerin bir kısmını yazmadan reddetmek zorunda
    uint8_t smallbuf[2];
    bs_t w;
    bs_write_init(&w, smallbuf, sizeof(smallbuf));
    bs_wu32be(&w, 0xDEADBEEF);
    CHECK(w.err, "fixed-buffer overflow should set err");
    CHECK(w.len == 0, "overflowing write should not change len, len=%zu", w.len);
}

static void test_dynamic(void)
{
    printf("bstream: growing buffer tests\n");

    //Starting at 8 bytes and writing 4000 forces several reallocs | 8 baytla başlayıp 4000 bayt yazmak birkaç realloc'u zorluyor
    bs_t s;
    CHECK(bs_dyn_init(&s, 8) == 0, "dyn_init failed");

    for(int i = 0; i < 1000; i++)
    {
        bs_wu32be(&s, (uint32_t)i);
    }

    CHECK(!s.err, "error while growing");
    CHECK(s.len == 4000, "wrong length while growing");

    bs_t r;
    bs_read_init(&r, s.buf, s.len);
    int ok = 1;

    for(int i = 0; i < 1000; i++)
    {
        if(bs_ru32be(&r) != (uint32_t)i)
        {
            ok = 0;
            break;
        }
    }
    CHECK(ok, "growing buffer contents were corrupted");

    bs_free(&s);
    CHECK(s.buf == NULL && s.owned == 0, "bs_free should clear the struct");
}

static void test_strings(void)
{
    printf("bstream: string tests\n");

    //The be form is what the MOTD uses, the var form what the game layer uses | be biçimi MOTD'un kullandığı, var biçimi oyun katmanının kullandığı
    uint8_t buf[256];
    bs_t s;
    bs_write_init(&s, buf, sizeof(buf));
    bs_wstr_be(&s, "MCPE;mrock;2193;1.26.51;0;20;");
    bs_wstr_var(&s, "merhaba");

    bs_t r;
    bs_read_init(&r, buf, s.len);
    char s1[64], s2[64];
    bs_rstr_be(&r, s1, sizeof(s1));
    bs_rstr_var(&r, s2, sizeof(s2));

    CHECK(strcmp(s1, "MCPE;mrock;2193;1.26.51;0;20;") == 0, "str_be: '%s'", s1);
    CHECK(strcmp(s2, "merhaba") == 0, "str_var: '%s'", s2);
    CHECK(!r.err, "string read error");

    //A short destination has to truncate and still terminate | Kısa hedef kırpmak ve yine de sonlandırmak zorunda
    bs_read_init(&r, buf, s.len);
    char tiny[5];
    memset(tiny, 'X', sizeof(tiny));
    bs_rstr_be(&r, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 4, "small buffer should truncate, len=%zu", strlen(tiny));
    CHECK(tiny[4] == '\0', "truncated string should be NUL-terminated");

    bs_write_init(&s, buf, sizeof(buf));
    bs_wstr_var(&s, "");
    bs_read_init(&r, buf, s.len);
    char empty[8];
    bs_rstr_var(&r, empty, sizeof(empty));
    CHECK(empty[0] == '\0' && !r.err, "empty string read");
}

static void test_bytes(void)
{
    printf("bstream: block read tests\n");

    uint8_t src[16];
    for(int i = 0; i < 16; i++)
        src[i] = (uint8_t)(i * 7);

    uint8_t buf[64];
    bs_t w;
    bs_write_init(&w, buf, sizeof(buf));
    bs_wu8(&w , 0xAA);
    bs_wbytes(&w, src, 16);

    bs_t r;
    bs_read_init(&r, buf, w.len);
    CHECK(bs_ru8(&r) == 0xAA, "prefix byte");

    //The pointer identity is the actual contract; the whole frame layer depends on it | Pointer kimliği asıl sözleşme; bütün frame katmanı buna dayanıyor
    const uint8_t *p = bs_rbytes(&r, 16);
    CHECK(p != NULL, "bs_rbytes returned NULL");
    CHECK(p == buf + 1, "bs_rbytes should not copy; it should return a pointer into the source buffer");
    CHECK(p && memcmp(p, src, 16) == 0, "block contents were corrupted");

    //Zeroing on failure means a caller that forgets to check err reads zeros, not stack garbage | Hata halinde sıfırlamak, err'i kontrol etmeyi unutan çağıranın çöp değil sıfır okuması demek
    bs_read_init(&r, buf, 4);
    uint8_t small[16];
    memset(small, 0xFF, sizeof(small));
    bs_rcopy(&r, small, 16);
    CHECK(r.err, "insufficient data should set err");
    CHECK(small[0] == 0 && small[15] == 0, "bs_rcopy should zero dst on error");
}

//Mirrors what ack.c does: reserve a slot, write the body, then fill the slot in | ack.c'nin yaptığının aynısı: yer ayır, gövdeyi yaz, sonra ayrılan yeri doldur
static void test_reserve(void)
{
    printf("bstream: wreserve tests\n");

    uint8_t buf[64];
    bs_t s;
    bs_write_init(&s, buf, sizeof(buf));

    size_t len_offset = s.pos;
    uint8_t *slot = bs_wreserve(&s, 2);
    CHECK(slot != NULL, "wreserve returned NULL");

    size_t start = s.pos;
    bs_wu32be(&s, 0xDEADBEEF);
    bs_wu8(&s, 0x42);
    size_t body = s.pos - start;

    CHECK(body == 5, "body should be 5 bytes, %zu", body);

    buf[len_offset] = (uint8_t)(body >> 8);
    buf[len_offset + 1] = (uint8_t)body;

    bs_t r;
    bs_read_init(&r, buf, s.len);
    CHECK(bs_ru16be(&r) == 5, "backfilled length");
    CHECK(bs_ru32be(&r) == 0xDEADBEEF, "first body field");
    CHECK(bs_ru8(&r) == 0x42, "second body field");
    CHECK(s.len == 7, "total length should be 7 bytes, %zu", s.len);
}

int main(void)
{
    test_endian();
    test_varint();
    test_overflow();
    test_dynamic();
    test_strings();
    test_bytes();
    test_reserve();

    printf("Total tests: %d, Failed: %d\n", total, fails);
    return fails ? 1 : 0;
}
