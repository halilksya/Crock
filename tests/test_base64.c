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

static void expect_text(const char *encoded, const char *expected)
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

    //The unpadded tails are the interesting cases, JWTs never carry padding | Dolgusuz kuyruklar asıl ilginç olanlar, JWT'lerde hiç dolgu olmuyor
    expect_text("YWJj", "abc");
    expect_text("YWI", "ab");
    expect_text("YQ", "a");
    expect_text("SGVsbG8", "Hello");
    expect_text("SGVsbG8sIHdvcmxkIQ", "Hello, world!");
}

static void test_url_alphabet(void)
{
    printf("base64: url-safe alphabet\n");

    uint8_t out[16];

    //'-' is 62, '_' is 63 and '8' is 60, which lays down 0xFB 0xFF and two spare bits | '-' 62, '_' 63, '8' 60; bu da 0xFB 0xFF ile iki artık bit üretiyor
    int n = mc_base64url_decode("-_8", 3, out, sizeof(out));
    CHECK(n == 2, "'-_8' -> %d bytes, expected 2", n);
    CHECK(n == 2 && out[0] == 0xFB && out[1] == 0xFF, "'-_8' -> %02x %02x, expected fb ff", out[0], out[1]);

    //Both alphabets map to the same values, so the result has to match | İki alfabe de aynı değerlere eşleniyor, yani sonuç aynı çıkmak zorunda
    n = mc_base64url_decode("+/8", 3, out, sizeof(out));
    CHECK(n == 2 && out[0] == 0xFB && out[1] == 0xFF, "'+/8' should decode identically to '-_8'");

    //Padding is optional but must still be accepted | Dolgu opsiyonel ama yine de kabul edilmek zorunda
    expect_text("YQ==", "a");
    expect_text("YWI=", "ab");
}

static void test_invalid_input(void)
{
    printf("base64: invalid input rejection\n");

    uint8_t out[64];

    CHECK(mc_base64url_decode("ab!c", 4, out, sizeof(out)) < 0, "'!' should be rejected");
    CHECK(mc_base64url_decode("ab cd", 5, out, sizeof(out)) < 0, " space should be rejected");
    //Five characters leave six spare bits, which cannot complete a byte | Beş karakter altı artık bit bırakıyor, bu bir baytı tamamlayamıyor
    CHECK(mc_base64url_decode("abcde", 5, out, sizeof(out)) < 0, "length 4k+1 should be rejected");
    //A byte above the alphabet has no table entry | Alfabenin üstündeki bir baytın tabloda karşılığı yok
    CHECK(mc_base64url_decode("abc\xC5=", 5, out, sizeof(out)) < 0, "high byte should be rejected");
}

static void test_output_bounds(void)
{
    printf("base64: output buffer bounds\n");

    //Seven characters decode to five bytes, so a two byte buffer has to fail | Yedi karakter beş bayta açılıyor, yani iki baytlık buffer başarısız olmak zorunda
    uint8_t small[2];
    memset(small, 0xAA, sizeof(small));

    int n = mc_base64url_decode("SGVsbG8", 7, small, sizeof(small));
    CHECK(n < 0, "should reject when output does not fit, got %d", n);

    uint8_t exact[3];
    n = mc_base64url_decode("YWJj", 4, exact, sizeof(exact));
    CHECK(n == 3, "exact fit should work, got %d", n);
    CHECK(n == 3 && memcmp(exact, "abc", 3) == 0, "exact fit content wrong");

    //The macro gives 15 for an 18 character input while 13 are needed, so it over-allocates as intended | Makro 18 karakter için 15 veriyor, gereken 13; yani amaçlandığı gibi fazla ayırıyor
    const char *encoded = "SGVsbG8sIHdvcmxkIQ";
    uint8_t sized[MC_BASE64_DECODED_SIZE(18)];
    n = mc_base64url_decode(encoded, 18, sized, sizeof(sized));
    CHECK(n == 13, "macro-sized buffer should fit, got %d", n);
}

static void test_jwt_header(void)
{
    printf("base64: real JWT header\n");

    //A real Bedrock login JWT header, the shape login.c has to chew through | Gerçek bir Bedrock login JWT başlığı, login.c'nin çiğnemesi gereken biçim
    const char *header = "eyJhbGciOiJFUzM4NCIsIng1dSI6ImFiYyJ9";

    uint8_t out[MC_BASE64_DECODED_SIZE(64)];
    int n = mc_base64url_decode(header, strlen(header), out, sizeof(out));

    CHECK(n > 0, "JWT header decode failed");

    if(n > 0)
    {
        CHECK(out[0] == '{', "decoded JWT header should start with '{'");
        CHECK(out[n-1] == '}', "decoded JWT header should end with '}'");

        //Room for the terminator exists because the macro over-allocates | Sonlandırıcı için yer var, çünkü makro fazla ayırıyor
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
