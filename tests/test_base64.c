#include "mrock/game/base64.h"

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

static void except_text(const char *encoded, const char *expected)
{
    uint8_t out[256];
    int n = mc_base64url_decode(encoded, strlen(encoded), out, sizeof(out));

    if(n < 0)
    {
        CHECK(0, "'%s' decode failed", encoded);
        return;
    }

    CHECK((size_t)n == strlen(expected),
          "'%s' decode length %d != expected %zu", encoded, n, strlen(expected));

    CHECK(memcmp(out, expected, (size_t)n) == 0,
            "'%s' -> wrong content", encoded);
}

static void test_known_values(void)
{
    printf("base64: known values\n");

    except_text("YWJj", "abc");
    except_text("YWI", "ab");
    except_text("YQ", "a");
    except_text("SGVsbG8", "Hello");
    except_text("SGVsbG8sIHdvcmxkIQ==", "Hello, world!");
}

static void test_url_alphabet(void)
{
    printf("base64: url-safe alphabet\n");

    uint8_t out[16];

    int n = mc_base64url_decode("-_8", 3, out, sizeof(out));
    CHECK(n == 2, "'-_8' -> %d bytes, expected 2", n);
    CHECK(n == 2 && out[0] == 0xFB && out[1] == 0xFF, "'-_8' -> %02x %02x, excepted fb ff", out[0], out[1]);

    n = mc_base64url_decode("+/8", 3, out, sizeof(out));
    CHECK(n == 2 && out[0] == 0xFB && out[1] == 0xFF, "'+/8' should decode identically to '-_8'");

    except_text("YQ==", "a");
    except_text("YWI=", "ab");
}

static void test_invalid_input(void)
{
    printf("base64: invalid input rejection\n");

    uint8_t out[64];

    CHECK(mc_base64url_decode("ab!c", 4, out, sizeof(out)) < 0, "'!' should be rejected");
    CHECK(mc_base64url_decode("ab cd", 5, out, sizeof(out)) < 0, " space should be rejected");
    CHECK(mc_base64url_decode("abcde", 5, out, sizeof(out)) < 0, "lenght 4k+1 should be rejected");
    CHECK(mc_base64url_decode("abcş=", 5, out, sizeof(out)) < 0, "high byte should be rejected");
}

static void test_output_bounds(void)
{
    printf("base64: output buffer bounds\n");

    uint8_t small[2];
    memset(small, 0xAA, sizeof(small));

    int n = mc_base64url_decode("SGVsbG8", 7, small, sizeof(small));
    CHECK(n < 0, "should reject when output does not fit, got %d", n);

    uint8_t exact[3];
    n = mc_base64url_decode("YWJj", 4, exact, sizeof(exact));
    CHECK(n == 3, "exact fit should work, got %d", n);
    CHECK(n == 3 && memcmp(exact, "abc", 3) == 0, "exact fit content wrong");

    const char *encoded = "SGVsbG8sIHdvcmxkIQ";
    uint8_t sized[MC_BASE64_DECODED_SIZE(18)];
    n = mc_base64url_decode(encoded, 18, sized, sizeof(sized));
    CHECK(n == 13, "macro-sized buffer should fit, got %d", n);
}

static void test_jwt_header(void)
{
    printf("base64: real JWT header\n");

    const char *header = "eyJhbGciOiJFUzM4NCIsIng1dSI6ImFiYyJ9";

    uint8_t out[MC_BASE64_DECODED_SIZE(64)];
    int n = mc_base64url_decode(header, strlen(header), out, sizeof(out));

    CHECK(n > 0, "JWT header decode failed");

    if(n > 0)
    {
        CHECK(out[0] == '{', "decoded JWT header should start with '{'");
        CHECK(out[n-1] == '}', "decoded JWT header should end with '}'");

        out[n] = '\0';
        CHECK(strstr((char *)out, "ES384") != NULL, "decoded header should contain ES384: %s", (char *)out);
    }
}

int main(void)
{
    test_known_values();
    test_url_alphabet();
    test_invalid_input();
    test_output_bounds();
    test_jwt_header();

    printf("\nTotal tests: %d, failures: %d\n", total, fails);
    return fails ? 1 : 0;
}