#include "mrock/raknet/ack.h"

#include <string.h>
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

static size_t roundtrip(const uint32_t *input, size_t count, uint32_t *output, size_t out_capacity, bool is_ack, size_t *encoded_len)
{
    uint8_t buf[2048];
    bs_t writer;
    bs_write_init(&writer, buf, sizeof(buf));

    size_t consumed = raknet_ack_encode(&writer, input, count, is_ack, sizeof(buf));
    if(encoded_len)
        *encoded_len = writer.len;
    
    if(consumed != count)
        return 0;

    bs_t reader;
    bs_read_init(&reader, buf, writer.len);
    bs_ru8(&reader);

    return raknet_ack_decode(&reader, output, out_capacity);
}

static void test_roundtrip(void)
{
    printf("raknet: ack roundtrip tests\n");

    uint32_t input[] = { 1, 5, 9, 100, 1000};
    uint32_t output[16];

    size_t n = roundtrip(input, 5, output, 16, true, NULL);
    CHECK(n == 5, "decoded %zu sequences, expected 5", n);
    CHECK(memcmp(input, output, sizeof(input)) == 0, "sequences corrupted");
}

static void test_range_compression(void)
{
    printf("raknet: ack range compression tests\n");

    uint32_t input[100];
    for(uint32_t i = 0; i < 100; i++)
        input[i] = i;

    uint32_t output[100];
    size_t encoded_len = 0;

    size_t n = roundtrip(input, 100, output, 128, true, &encoded_len);

    CHECK(n == 100, "decoded %zu sequences, expected 100", n);
    CHECK(memcmp(input, output, sizeof(input)) == 0, "sequences corrupted");
    CHECK(encoded_len == 10, "encoded to %zu bytes, expected 10", encoded_len);
}

static void test_mixed_records(void)
{
    printf("raknet: ack mixed records tests\n");

    uint32_t input[] = {5, 6, 7, 8, 12, 20, 21};
    uint32_t output[16];

    uint8_t buf[256];
    bs_t writer;
    bs_write_init(&writer, buf, sizeof(buf));

    size_t consumed = raknet_ack_encode(&writer, input, 7, true, sizeof(buf));
    CHECK(consumed == 7, "consumed %zu sequences, expected 7", consumed);
    CHECK(!writer.err, "encode error");

    uint16_t record_count = (uint16_t)((buf[1] << 8) | buf[2]);
    CHECK(record_count == 3, "record count %u, expected 3", record_count);

    bs_t reader;
    bs_read_init(&reader, buf, writer.len);
    bs_ru8(&reader);
    size_t n = raknet_ack_decode(&reader, output, 16);

    CHECK(n == 7, "decoded %zu sequences, expected 7", n);
    CHECK(memcmp(input, output, sizeof(input)) == 0, "sequences corrupted");
}

static void test_wraparound(void)
{
    printf("raknet: ack wraparound tests\n");

    uint32_t input[] = { 0xFFFFFE, 0xFFFFFF, 0x000000, 0x000001};
    uint32_t output[16];

    uint8_t buf[256];
    bs_t writer;
    bs_write_init(&writer, buf, sizeof(buf));

    size_t consumed = raknet_ack_encode(&writer, input, 4, true, sizeof(buf));
    CHECK(consumed == 4, "consumed %zu sequences, expected 4", consumed);
    
    uint16_t record_count = (uint16_t)((buf[1] << 8) | buf[2]);
    CHECK(record_count == 1, "record count %u, expected 1 (wraparound)", record_count);

    bs_t reader;
    bs_read_init(&reader, buf, writer.len);
    bs_ru8(&reader);
    size_t n = raknet_ack_decode(&reader, output, 16);

    CHECK(n == 4, "decoded %zu expected 4", n);
    CHECK(memcmp(input, output, sizeof(input)) == 0, "wraparound sequences corrupted");
}

static void test_mtu_limit(void)
{
    printf("raknet: ack mtu limit tests\n");

    uint32_t input[1000];
    for(uint32_t i = 0; i < 1000; i++)
        input[i] = i * 2;
    
    uint8_t buf[2048];
    bs_t writer;
    bs_write_init(&writer, buf, sizeof(buf));

    size_t consumed = raknet_ack_encode(&writer, input, 1000, true, 1400);
    
    CHECK(consumed > 0, "nothing consumed");
    CHECK(consumed < 1000, "consumed %zu, should have been truncated", consumed);
    CHECK(writer.len <= 1400, "encoded %zu bytes, exceeds MTU 1400", writer.len);
    CHECK(!writer.err, "encode error");

    bs_t writer2;
    bs_write_init(&writer2, buf, sizeof(buf));
    size_t consumed2 = raknet_ack_encode(&writer2, input + consumed, 1000 - consumed, true, 1400);
    CHECK(consumed2 > 0, "second call consumed nothing");
}   

static void test_malformed(void)
{
    printf("raknet: ack malformed tests\n");

    uint32_t output[64];
    uint8_t buf[] = { 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x0A, 0x00, 0x00 };
    bs_t reader;
    bs_read_init(&reader, buf, sizeof(buf));
    raknet_ack_decode(&reader, output, 64);
    CHECK(reader.err, "start > end should be rejected");

    uint8_t buf2[] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF };
    bs_read_init(&reader, buf2, sizeof(buf2));
    raknet_ack_decode(&reader, output, 64);
    CHECK(reader.err, "huge range should be rejected");

    uint8_t buf3[] = { 0x00, 0x0A, 0x01, 0x05, 0x00, 0x00 };
    bs_read_init(&reader, buf3, sizeof(buf3));
    size_t n = raknet_ack_decode(&reader, output, 64);
    CHECK(reader.err, "truncated record should be rejected");
    CHECK(n == 1, "decoded %zu before running out, expected 1", n);
}

static void test_flags(void)
{
    printf("raknet: ack vs nack flags tests\n");

    uint32_t input[] = { 1, 2, 3};
    uint8_t buf[64];

    bs_t writer;
    bs_write_init(&writer, buf, sizeof(buf));
    raknet_ack_encode(&writer, input, 3, true, sizeof(buf));
    CHECK(buf[0] == (RAKNET_FLAG_VALID | RAKNET_FLAG_ACK), "ACK flag = 0x%02x, expected 0xC0", buf[0]);

    bs_write_init(&writer, buf, sizeof(buf));
    raknet_ack_encode(&writer, input, 3, false, sizeof(buf));
    CHECK(buf[0] == (RAKNET_FLAG_VALID | RAKNET_FLAG_NAK), "NAK flag = 0x%02x, expected 0xA0", buf[0]);
}

int main(void)
{
    test_roundtrip();
    test_range_compression();
    test_mixed_records();
    test_wraparound();
    test_mtu_limit();
    test_malformed();
    test_flags();

    printf("\nTotal: %d, Failed: %d\n", total, fails);
    return fails ? 1 : 0;
}
