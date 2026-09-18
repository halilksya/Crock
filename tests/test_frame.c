#include "mrock/raknet/frame.h"

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

static void test_roundtrip(void)
{
    printf("raknet: frame roundtrip tests(all reliability modes)\n");

    uint8_t payload[32];
    for(int i = 0; i < 32; i++)
        payload[i] = (uint8_t)(i * 3);

    for(uint8_t reliability = 0; reliability < 8; reliability++)
    {
        raknet_frame_t original;
        memset(&original, 0, sizeof(original));

        original.reliability = reliability;
        original.reliable_index = 0x123456;
        original.sequenced_index = 0xABCDEF;
        original.ordered_index = 0x000042;
        original.order_channel = 7;
        original.payload = payload;
        original.payload_len = sizeof(payload);

        uint8_t buf[256];
        bs_t writer;
        bs_write_init(&writer, buf, sizeof(buf));
        raknet_frame_encode(&writer, &original);
        CHECK(!writer.err, "reliability %u: encode error", reliability);

        bs_t reader;
        bs_read_init(&reader, buf, writer.len);
        raknet_frame_t decoded;
        raknet_frame_decode(&reader, &decoded);
        
        CHECK(!reader.err, "reliability %u: decode error", reliability);
        CHECK(decoded.reliability == reliability, "reliability %u -> %u", reliability, decoded.reliability);
        CHECK(decoded.payload_len == sizeof(payload), "reliability %u: payload_len %u", reliability, decoded.payload_len);
        CHECK(decoded.payload && memcmp(decoded.payload, payload, sizeof(payload)) == 0, "reliability %u: payload corrupted", reliability);
        CHECK(bs_remaining(&reader) == 0, "reliability %u: %zu bytes left over", reliability, bs_remaining(&reader));

        if(RAKNET_IS_RELIABLE(reliability))
            CHECK(decoded.reliable_index == 0x123456, "reliability %u: reliable_index %06x", reliability, decoded.reliable_index);

        if(RAKNET_IS_SEQUENCED(reliability))
            CHECK(decoded.sequenced_index == 0xABCDEF, "reliability %u: sequenced_index %06x", reliability, decoded.sequenced_index);

        if(RAKNET_IS_ORDERED(reliability))
        {
            CHECK(decoded.ordered_index == 0x000042, "reliability %u: ordered_index %06x", reliability, decoded.ordered_index);
            CHECK(decoded.order_channel == 7, "reliability %u: order_channel %u", reliability, decoded.order_channel);

        } 
    }
}

static void test_split(void)
{
    printf("raknet: frame split tests\n");

    uint8_t payload[16];
    memset(payload, 0x5A, sizeof(payload));

    raknet_frame_t original;
    memset(&original, 0, sizeof(original));

    original.reliability = RAKNET_RELIABLE_ORDERED;
    original.reliable_index = 100;
    original.ordered_index = 200;
    original.order_channel = 0;
    original.is_split = true;
    original.split_count = 5;
    original.split_id = 0x1234;
    original.split_index = 3;
    original.payload = payload;
    original.payload_len = sizeof(payload);

    uint8_t buf[256];
    bs_t writer;
    bs_write_init(&writer, buf, sizeof(buf));
    raknet_frame_encode(&writer, &original);
    CHECK(!writer.err, "split encode error");

    CHECK((buf[0] & RAKNET_FLAG_SPLIT) != 0, "split flag not set in encoded frame");

    bs_t reader;
    bs_read_init(&reader, buf, writer.len);
    raknet_frame_t decoded;
    raknet_frame_decode(&reader, &decoded);

    CHECK(!reader.err, "split decode error");
    CHECK(decoded.is_split, "decoded frame is not marked as split");
    CHECK(decoded.split_count == 5, "split_count %u", decoded.split_count);
    CHECK(decoded.split_id == 0x1234, "split_id %04x", decoded.split_id);
    CHECK(decoded.split_index == 3, "split_index %u", decoded.split_index);
    CHECK(bs_remaining(&reader) == 0, "split: %zu bytes left over", bs_remaining(&reader));
}

static void test_size(void)
{
    printf("raknet: frame size calculation tests\n");

    uint8_t payload[50];
    memset(payload, 0, sizeof(payload));

    for(uint8_t reliability = 0; reliability < 8; reliability++)
    {
        for(int with_split = 0; with_split < 2; with_split++)
        {
            raknet_frame_t frame;
            memset(&frame, 0, sizeof(frame));

            frame.reliability = reliability;
            frame.is_split = (with_split != 0);
            frame.split_count = 4;
            frame.split_index = 1;
            frame.payload = payload;
            frame.payload_len = sizeof(payload);

            uint8_t buf[256];
            bs_t writer;
            bs_write_init(&writer, buf, sizeof(buf));
            raknet_frame_encode(&writer, &frame);

            size_t predicted_size = raknet_frame_size(&frame);
            CHECK(!writer.err && writer.len == predicted_size, "reliability %u, split %d: predicted size %zu, actual size %zu", reliability, with_split, predicted_size, writer.len);

            size_t header_size = raknet_frame_header_size(reliability, frame.is_split);
            CHECK(header_size + sizeof(payload) == predicted_size, "reliability %u: header_size inconsistent", reliability);
        }
    }
}

static void test_malformed(void)
{
    printf("raknet: malformed frame tests\n");

    uint8_t buf[] =
    {
        0x70,
        0x00, 0x40,
        0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        1, 2, 3, 4, 5, 6, 7, 8
    };
    bs_t reader;
    bs_read_init(&reader, buf, sizeof(buf));
    raknet_frame_t frame;
    raknet_frame_decode(&reader, &frame);
    CHECK(reader.err, "split_count=0 should be rejected");

    uint8_t buf2[] =
    {
        0x60,
        0x00, 0x40,
        0x01, 0x00, 0x00,
        0x00, 0x00, 0x00,
        200,
        1, 2, 3, 4, 5, 6, 7, 8
    };
    bs_read_init(&reader, buf2, sizeof(buf2));
    raknet_frame_t frame2;
    raknet_frame_decode(&reader, &frame2);
    CHECK(reader.err, "order_channel=200 should be rejected");

    uint8_t buf3[] ={ 0x60, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    bs_read_init(&reader, buf3, sizeof(buf3));
    raknet_frame_t frame3;
    raknet_frame_decode(&reader, &frame3);
    CHECK(reader.err, "zero-lenght payload should be rejected");

    uint8_t buf4[] = { 0x60, 0x03, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 1, 2, 3, 4 };
    bs_read_init(&reader, buf4, sizeof(buf4));
    raknet_frame_t frame4;
    raknet_frame_decode(&reader, &frame4);
    CHECK(reader.err, "truncated payload should be rejected");
}

static void test_ownership(void)
{
    printf("frame: payload ownership tests\n");

    uint8_t source[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    raknet_frame_t frame;
    memset(&frame, 0 ,sizeof(frame));
    frame.payload = source;
    frame.payload_len = sizeof(source);
    frame.owns_payload = false;

    CHECK(raknet_frame_own_payload(&frame) == 0, "own_payload failed");
    CHECK(frame.owns_payload, "owns_payload not set");
    CHECK(frame.payload != source, "own_payload must copy, not alias");

    memset(source, 0xFF, sizeof(source));
    CHECK(frame.payload[0] == 1 && frame.payload[7] == 8, "payload was not really copied");

    uint8_t *before = frame.payload;
    CHECK(raknet_frame_own_payload(&frame) == 0, "second own_payload failed");
    CHECK(frame.payload == before, "own_payload must be idempotent");

    raknet_frame_free(&frame);
    CHECK(frame.payload == NULL, "free must null the pointer");
    CHECK(!frame.owns_payload, "free must clear owns_payload");

    raknet_frame_free(&frame);
    raknet_frame_t borrowed;
    memset(&borrowed, 0, sizeof(borrowed));
    borrowed.payload = source;
    borrowed.payload_len = sizeof(source);
    borrowed.owns_payload = false;
    raknet_frame_free(&borrowed);
    CHECK(borrowed.payload == NULL, "free must null even borrowed payload");
}

int main(void)
{
    test_roundtrip();
    test_split();
    test_size();
    test_malformed();
    test_ownership();

    printf("\nTotal: %d, Failed: %d\n", total, fails);
    return fails ? 1 : 0;
}