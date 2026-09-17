#ifndef MCROCK_RAKNET_FRAME_H
#define MCROCK_RAKNET_FRAME_H

#include "mrock/raknet/proto.h"
#include "mrock/util/bstream.h"

#define RAKNET_MAX_SPLIT_PART_COUNT 128

#define RAKNET_MAX_CONCURRENT_SPLITS 4

#define RAKNET_MAX_FRAME_PER_DATAGRAM 128

typedef struct
{
    uint8_t reliability;

    uint32_t reliable_index;
    uint32_t sequenced_index;
    uint32_t ordered_index;
    uint8_t order_channel;

    bool is_split;
    uint32_t split_count;
    uint16_t split_id;
    uint32_t split_index;

    uint8_t *payload;
    uint16_t payload_len;
    bool owns_payload;
} raknet_frame_t;

void raknet_frame_decode(bs_t *stream, raknet_frame_t *frame);

void raknet_frame_encode(bs_t *stream, const raknet_frame_t *frame);

size_t raknet_frame_size(const raknet_frame_t *frame);

size_t raknet_frame_header_size(uint8_t reliability, bool is_split);

int raknet_frame_own_payload(raknet_frame_t *frame);

void raknet_frame_free(raknet_frame_t *frame);

#endif