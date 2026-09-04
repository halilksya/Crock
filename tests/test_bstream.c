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
    CHECK(bs_remaining(&r) == 0, "artık byte kaldı");
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

static void test_overflow(void)
{
    printf("bstream: taşma koruması testleri\n");

    uint8_t buf[4] = {1, 2, 3, 4};
    bs_t s;
    bs_read_init(&s, buf, sizeof(buf));

    bs_ru32be(&s);
    CHECK(!s.err, "4 byte okuma başarılı olmalı");

    bs_ru32be(&s);
    CHECK(s.err, "boş buffer'dan okuma err set etmeli");

    CHECK(bs_ru64be(&s) == 0, "err sonrası okuma 0 dönmeli");
    CHECK(bs_rbytes(&s, 100) == NULL, "err sonrası okuma NULL dönmeli");
    CHECK(s.pos == 4, "err sonrası pos ilerlememeli, pos=%zu", s.pos);

    uint8_t smallbuf[2];
    bs_t w;
    bs_write_init(&w, smallbuf, sizeof(smallbuf));
    bs_wu32be(&w, 0xDEADBEEF);
    CHECK(w.err, "sabit buffer taşması err set etmeli");
    CHECK(w.len == 0, "taşan yazma len'i 0 değiştirmemeli, len=%zu", w.len);
}

static void test_dynamic(void)
{
    printf("bstream: büyüyen buffer testleri\n");

    bs_t s;
    CHECK(bs_dyn_init(&s, 8) == 0, "dyn_init başarısız");

    for(int i = 0; i < 1000; i++)
    {
        bs_wu32be(&s, (uint32_t)i);
    }

    CHECK(!s.err, "büyüme sırasında hata");
    CHECK(s.len == 4000, "büyüme sırasında hata");

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
    CHECK(ok, "büyüyen buffer içeriği bozuldu.");

    bs_free(&s);
    CHECK(s.buf == NULL && s.owned == 0, "bs_free struct'i temizlemeli");
}

static void test_strings(void)
{
    printf("bstream: string testleri\n");

    uint8_t buf[256];
    bs_t s;
    bs_write_init(&s, buf, sizeof(buf));
    bs_wstr_be(&s, "MCPE;mrock;2168;1.26.44;0;20;");
    bs_wstr_var(&s, "merhaba");

    bs_t r;
    bs_read_init(&r, buf, s.len);
    char s1[64], s2[64];
    bs_rstr_be(&r, s1, sizeof(s1));
    bs_rstr_var(&r, s2, sizeof(s2));

    CHECK(strcmp(s1, "MCPE;mrock;2168;1.26.44;0;20;") == 0, "str_be: '%s'", s1);
    CHECK(strcmp(s2, "merhaba") == 0, "str_var: '%s'", s2);
    CHECK(!r.err, "string okuma hatası");

    bs_read_init(&r, buf, s.len);
    char tiny[5];
    memset(tiny, 'X', sizeof(tiny));
    bs_rstr_be(&r, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 4, "küçük buffer kırpılmalı, len=%zu", strlen(tiny));
    CHECK(tiny[4] == '\0', "kırpılan string NUL ile sonlanmalı");

    bs_write_init(&s, buf, sizeof(buf));
    bs_wstr_var(&s, "");
    bs_read_init(&r, buf, s.len);
    char empty[8];
    bs_rstr_var(&r, empty, sizeof(empty));
    CHECK(empty[0] == '\0' && !r.err, "boş string okuma");
}

static void test_bytes(void)
{
    printf("bstream: blok okuma testleri\n");

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
    CHECK(bs_ru8(&r) == 0xAA, "önek byte");

    const uint8_t *p = bs_rbytes(&r, 16);
    CHECK(p != NULL, "bs_rbytes NULL döndü");
    CHECK(p == buf + 1, "bs_rbytes kopya yapmamalı, kaynak buffer'a pointer vermeli");
    CHECK(p && memcmp(p, src, 16) == 0, "blok içeriği bozuldu");

    bs_read_init(&r, buf, 4);
    uint8_t small[16];
    memset(small, 0xFF, sizeof(small));
    bs_rcopy(&r, small, 16);
    CHECK(r.err, "yetersiz veri err set etmeli");
    CHECK(small[0] == 0 && small[15] == 0, "bs_rcopy hata durumunda dst'i sıfırlamalı");
}

static void test_reserve(void)
{
    printf("bstream: wreserve testleri\n");

    uint8_t buf[64];
    bs_t s;
    bs_write_init(&s, buf, sizeof(buf));

    size_t len_offset = s.len;
    uint8_t *slot = bs_wreserve(&s, 2);
    CHECK(slot != NULL, "wreserve NULL döndü");

    size_t start = s.pos;
    bs_wu32be(&s, 0xDEADBEEF);
    bs_wu8(&s, 0x42);
    size_t body = s.pos - start;

    CHECK(body == 5, "gövde 5 byte olmalı, %zu", body);

    buf[len_offset] = (uint8_t)(body >> 8);
    buf[len_offset + 1] = (uint8_t)body;

    bs_t r;
    bs_read_init(&r, buf, s.len);
    CHECK(bs_ru16be(&r) == 5, "geri doldurulan uzunluk");
    CHECK(bs_ru32be(&r) == 0xDEADBEEF, "gövde ilk alan");
    CHECK(bs_ru8(&r) == 0x42, "gövde ikinci alan");
    CHECK(s.len == 7, "toplam uzunluk 7 byte olmalı, %zu", s.len);
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

    printf("Toplam test: %d, Başarısız: %d\n", total, fails);
    return fails ? 1 : 0;
}
