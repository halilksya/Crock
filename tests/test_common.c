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
        printf("FAIL %s:%d: ", __FILE__, __LINE__);                     \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    }                                                                   \
} while (0)

//Small values where the wrapping logic should behave exactly like plain comparison | Küçük değerler, sarmalı mantığın düz karşılaştırma gibi davranması gereken yer
static void test_normal(void)
{
    printf("triad: normal comparison tests\n");

    CHECK(triad_gt(5, 3), "5 > 3 should be true");
    CHECK(!triad_gt(3, 5), "3 >= 5 should be false");
    CHECK(!triad_gt(7, 7), "7 > 7 should be false");

    CHECK(triad_ge(5, 3),  "5 >= 3 should be true");
    CHECK(triad_ge(7, 7),  "7 >= 7 should be true");
    CHECK(!triad_ge(3, 5), "3 >= 5 should be false");

    CHECK(triad_lt(3, 5),  "3 < 5 should be true");
    CHECK(!triad_lt(5, 3), "5 < 3 should be false");
    CHECK(!triad_lt(7, 7), "7 < 7 should be false");

    CHECK(triad_le(3, 5),  "3 <= 5 should be true");
    CHECK(triad_le(7, 7),  "7 <= 7 should be true");
    CHECK(!triad_le(5, 3), "5 <= 3 should be false");
}

static void test_wrap(void)
{
    printf("triad: 0xFFFFFF -> 0 wraparound tests\n");

    CHECK(triad_gt(0, 0xFFFFFF), "0 should come after 0xFFFFFF");
    CHECK(!triad_gt(0xFFFFFF, 0), "0xFFFFFF should not come after 0");

    CHECK(triad_lt(0xFFFFFF, 0),  "0xFFFFFF should come before 0");
    CHECK(!triad_lt(0, 0xFFFFFF), "0 should not come before 0xFFFFFF");

    CHECK(triad_gt(5, 0xFFFFFE),        "5 should come after 0xFFFFFE");
    CHECK(triad_gt(0xFFFFFF, 0xFFFFFE), "0xFFFFFF should come after 0xFFFFFE");
    CHECK(triad_gt(1, 0),               "1 should come after 0");

    //Exactly half the range is the ambiguous point, and it is defined as not ahead | Aralığın tam yarısı belirsiz nokta, tanım olarak önde sayılmıyor
    CHECK(triad_gt(MR_TRIAD_HALF - 1, 0), "HALF-1 should be ahead");
    CHECK(!triad_gt(MR_TRIAD_HALF, 0),    "HALF should not be considered ahead");
}

static void test_diff(void)
{
    printf("triad: signed difference tests\n");

    CHECK(triad_diff(10, 3) == 7,  "diff(10,3)=%d", triad_diff(10, 3));
    CHECK(triad_diff(3, 10) == -7, "diff(3,10)=%d", triad_diff(3, 10));
    CHECK(triad_diff(7, 7) == 0,   "diff(7,7)=%d", triad_diff(7, 7));

    CHECK(triad_diff(0, 0xFFFFFF) == 1, "diff(0,MAX)=%d, should be 1", triad_diff(0, 0xFFFFFF));
    CHECK(triad_diff(0xFFFFFF, 0) == -1, "diff(MAX,0)=%d, should be -1", triad_diff(0xFFFFFF, 0));
    CHECK(triad_diff(5, 0xFFFFFE) == 7, "diff(5,0xFFFFFE)=%d, should be 7", triad_diff(5, 0xFFFFFE));

    //recv.c uses the sign and the magnitude interchangeably, so the two have to agree | recv.c işaret ile büyüklüğü birbirinin yerine kullanıyor, o yüzden ikisi uyuşmak zorunda
    CHECK((triad_diff(0, 0xFFFFFF) > 0) == triad_gt(0, 0xFFFFFF), "diff sign does not match triad_gt");
}

static void test_inc(void)
{
    printf("triad: increment tests\n");

    CHECK(triad_inc(0) == 1, "inc(0)=%u", triad_inc(0));
    CHECK(triad_inc(5) == 6, "inc(5)=%u", triad_inc(5));
    CHECK(triad_inc(0xFFFFFE) == 0xFFFFFF, "inc(0xFFFFFE)=%u", triad_inc(0xFFFFFE));
    CHECK(triad_inc(0xFFFFFF) == 0, "inc(MAX)=%u, should be 0", triad_inc(0xFFFFFF));
}

//Starts 16 below the top so the wrap lands in the middle of the loop | Tepeden 16 aşağıdan başlıyor, böylece sarma döngünün ortasına düşüyor
static void test_full_cycle(void)
{
    printf("triad: full cycle around wraparound tests\n");

    uint32_t start = 0xFFFFF0;
    uint32_t previous = start;
    int ok = 1;

    for (int step = 1; step <= 32; step++)
    {
        uint32_t current = triad_inc(previous);

        if (!triad_gt(current, previous))
        {
            printf("  step %d: %06x did not come after %06x\n", step, current, previous);
            ok = 0;
        }

        if (!triad_gt(current, start))
        {
            printf("  step %d: %06x did not come after start %06x\n", step, current, start);
            ok = 0;
        }

        if (triad_diff(current, start) != step)
        {
            printf("  step %d: diff was %d, should be %d\n", step, triad_diff(current, start), step);
            ok = 0;
        }
        previous = current;
    }

    CHECK(ok, "full cycle around wraparound failed");
    CHECK(previous == 0x10, "after 32 steps got %06x, should be 0x000010", previous);
}

static void test_clock(void)
{
    printf("clock: monotonic\n");

    uint64_t t1 = mr_now_ms();
    CHECK(t1 > 0, "mr_now_ms returned zero");

    //The volatile is what keeps this loop alive under -O2; without it the test passes for free | volatile bu döngüyü -O2 altında ayakta tutan şey; o olmasa test bedavaya geçiyor
    volatile uint64_t sink = 0;

    for(int i = 0; i < 5000000; i++)
    {
        sink += (uint64_t)i;
    }

    uint64_t t2 = mr_now_ms();
    CHECK(t2 > t1, "clock moved backward: %llu -> %llu", (unsigned long long)t1, (unsigned long long)t2);
}

int main(void)
{
    test_normal();
    test_wrap();
    test_diff();
    test_inc();
    test_full_cycle();
    test_clock();

    printf("Total tests: %d, Failed: %d\n", total, fails);
    return fails ? 1 : 0;
}
