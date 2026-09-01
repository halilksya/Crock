#include "mrock/common.h"
#include <stdio.h>

static int total = 0;
static int fails = 0;

#define CHECK(cond, ...) do                                             \
{                                                                       \
    total++;                                                            \
    if (!(cond))                                                        \
    {                                                                   \
        fails++;                                                        \
        printf("FAIL %s:%d: " __VA_ARGS__, __FILE__, __LINE__);         \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    }                                                                   \
} while (0);

static void test_normal(void)
{
    printf("triad: normal karşılaştırma testleri\n");

    CHECK(triad_gt(5, 3), "5 > 3 olmalı");
    CHECK(triad_gt(3, 5), "3 >= 5 olmamalı");
    CHECK(triad_gt(7, 7), "7 > 7 olmamalı");

    CHECK(triad_ge(5, 3),  "5 >= 3 olmali");
    CHECK(triad_ge(7, 7),  "7 >= 7 olmali");
    CHECK(!triad_ge(3, 5), "3 >= 5 olmamali");

    CHECK(triad_lt(3, 5),  "3 < 5 olmali");
    CHECK(!triad_lt(5, 3), "5 < 3 olmamali");
    CHECK(!triad_lt(7, 7), "7 < 7 olmamali");

    CHECK(triad_le(3, 5),  "3 <= 5 olmali");
    CHECK(triad_le(7, 7),  "7 <= 7 olmali");
    CHECK(!triad_le(5, 3), "5 <= 3 olmamali");
}

static void test_wrap(void)
{
    printf("triad: 0xFFFFFF -> 0 geçişi testleri\n");

    CHECK(triad_gt(0, 0xFFFFFF), "0, 0xFFFFFF'ten sonra gelmeli");
    CHECK(!triad_gt(0xFFFFFF, 0), "0xFFFFFF, 0'dan sonra gelmemeli");

    CHECK(triad_lt(0xFFFFFF, 0),  "0xFFFFFF, 0'dan once gelmeli");
    CHECK(!triad_lt(0, 0xFFFFFF), "0, 0xFFFFFF'ten once gelmemeli");

    CHECK(triad_gt(5, 0xFFFFFE),        "5, 0xFFFFFE'den sonra gelmeli");
    CHECK(triad_gt(0xFFFFFF, 0xFFFFFE), "0xFFFFFF, 0xFFFFFE'den sonra");
    CHECK(triad_gt(1, 0),               "1, 0'dan sonra");

    CHECK(triad_gt(MR_TRIAD_HALF - 1, 0), "HALF-1 ileride olmali");
    CHECK(!triad_gt(MR_TRIAD_HALF, 0),    "HALF ileride sayilmamali");
}

static void test_diff(void)
{
    printf("triad: işaretli fark testleri\n");

    CHECK(triad_diff(10, 3) == 7,  "diff(10,3)=%d", triad_diff(10, 3));
    CHECK(triad_diff(3, 10) == -7, "diff(3,10)=%d", triad_diff(3, 10));
    CHECK(triad_diff(7, 7) == 0,   "diff(7,7)=%d", triad_diff(7, 7));

    CHECK(triad_diff(0, 0xFFFFFF) == 1, "diff(0,MAX)=%d, 1 olmali", triad_diff(0, 0xFFFFFF));
    CHECK(triad_diff(0xFFFFFF, 0) == -1, "diff(MAX,0)=%d, -1 olmali", triad_diff(0xFFFFFF, 0));
    CHECK(triad_diff(5, 0xFFFFFE) == 7, "diff(5,0xFFFFFE)=%d, 7 olmali", triad_diff(5, 0xFFFFFE));

    CHECK((triad_diff(0, 0xFFFFFF) > 0) == triad_gt(0, 0xFFFFFF), "diff isareti triad_gt ile uyusmuyor");
}

static void test_inc(void)
{
    printf("triad: artış testleri\n");

    CHECK(triad_inc(0) == 1, "inc(0)=%d", triad_inc(0));
    CHECK(triad_inc(5) == 6, "inc(5)=%d", triad_inc(5));
    CHECK(triad_inc(0xFFFFFE) == 0xFFFFFF, "inc(0xFFFFFE)=%u", triad_inc(0xFFFFFE));
    CHECK(triad_inc(0xFFFFFF) == 0, "inc(MAX)=%u, 0 olmali", triad_inc(0xFFFFFF));
}

static void test_full_cycle(void)
{
    printf("triad: wrap etrafinda tam tur testleri\n");

    uint32_t start = 0xFFFFF0;
    uint32_t previous = start;
    int ok = 1;

    for (int step = 1; step <= 32; step++) 
    {
        uint32_t current = triad_inc(previous);

        if (!triad_gt(current, previous)) 
        {
            printf("  adim %d: %06x, %06x'ten ileride cikmadi\n", step, current, previous);
            ok = 0;
        }

        if (!triad_gt(current, start)) 
        {
            printf("  adim %d: %06x, baslangic %06x'ten ileride cikmadi\n",step, current, start);
            ok = 0;
        }

        if (triad_diff(current, start) != step) {

            printf("  adim %d: diff %d cikti, %d olmali\n", step, triad_diff(current, start), step);
            ok = 0;
        }
        previous = current;
    }

    CHECK(ok, "wrap etrafinda tam tur basarisiz");
    CHECK(previous == 0x10, "32 adim sonra %06x, 0x000010 olmali", previous);
}

static void test_clock(void)
{
    printf("saat: monotonik\n");

    uint64_t t1 = mr_now_ms();
    CHECK(t1 > 0, "mr_now_ms sıfır döndü");

    volatile uint64_t sink = 0;
    
    for(int i = 0; i < 5000000; i++) 
    {
        sink += i;
    }

    uint64_t t2 = mr_now_ms();
    CHECK(t2 > t1, "saat geri gitti: %llu -> %llu", (unsigned long long)t1, (unsigned long long)t2);
}

int main(void)
{
    test_normal();
    test_wrap();
    test_diff();
    test_inc();
    test_full_cycle();
    test_clock();

    printf("Toplam test: %d, Başarısız: %d\n", total, fails);
    return fails ? 1 : 0;
}