#include "mrock/raknet/frame.h"

#include <string.h>
#include <stdlib.h>

size_t raknet_frame_header_size(uint8_t reliability, bool is_split)
{
    size_t size = 1 + 2;

    if(RAKNET_IS_RELIABLE(reliability))
        size += 3;

    if(RAKNET_IS_SEQUENCED(reliability))
        size += 3;

    if(RAKNET_IS_ORDERED(reliability))
        size += 3 + 1;

    if(is_split)
        size += 4 + 2 + 4;

    return size;
}

size_t raknet_frame_size(const raknet_frame_t *frame)
{
    return raknet_frame_header_size(frame -> reliability, frame -> is_split) + frame -> payload_len;
}

void raknet_frame_decode(bs_t *stream, raknet_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));

    uint8_t flags = bs_ru8(stream);
    frame -> reliability = (uint8_t)(flags >> 5);
    frame -> is_split = (flags & RAKNET_FLAG_SPLIT) != 0;

    uint16_t bit_len = bs_ru16be(stream);
    size_t byte_len = ((size_t)bit_len + 7) / 8;

    if(byte_len == 0 || byte_len > RAKNET_MAX_MTU)
    {
        stream -> err = 1;
        return;
    }
    frame -> payload_len = (uint16_t)byte_len;

    if(RAKNET_IS_RELIABLE(frame -> reliability))
        frame -> reliable_index = bs_ru24le(stream);

    if(RAKNET_IS_SEQUENCED(frame -> reliability))
        frame -> sequenced_index = bs_ru24le(stream);

    if(RAKNET_IS_ORDERED(frame -> reliability))
    {
        frame -> ordered_index = bs_ru24le(stream);
        frame -> order_channel = bs_ru8(stream);
        
            if(frame -> order_channel >= RAKNET_MAX_ORDER_CHANNELS)
        {
            stream -> err = 1;
            return;
        }

    }

    if(frame -> is_split)
    {
        frame -> split_count = bs_ru32be(stream);
        frame -> split_id = bs_ru16be(stream);
        frame -> split_index = bs_ru32be(stream);

        if(frame -> split_count == 0 || frame -> split_count > RAKNET_MAX_SPLIT_PART_COUNT || frame -> split_index >= frame -> split_count)
        {
            stream -> err = 1;
            return;
        }
    }

    frame -> payload = (uint8_t *)bs_rbytes(stream, byte_len);
    frame -> owns_payload = false;

    if(!frame -> payload)
        stream -> err = 1;

}

void raknet_frame_encode(bs_t *stream, const raknet_frame_t *frame)
{
    uint8_t flags = (uint8_t)(frame -> reliability << 5);
    if(frame -> is_split)
        flags |= RAKNET_FLAG_SPLIT;

    if(frame -> payload_len == 0 || frame -> payload_len > 8191)
    {
        stream -> err = 1;
        return;
    }
    
    bs_wu8(stream, flags);
    bs_wu16be(stream, (uint16_t)(frame -> payload_len * 8));

    if(RAKNET_IS_RELIABLE(frame -> reliability))
        bs_wu24le(stream, frame -> reliable_index);

    if(RAKNET_IS_SEQUENCED(frame -> reliability))
        bs_wu24le(stream, frame -> sequenced_index);

    if(RAKNET_IS_ORDERED(frame -> reliability))
    {
        bs_wu24le(stream, frame -> ordered_index);
        bs_wu8(stream, frame -> order_channel);
    }

    if(frame -> is_split)
    {
        bs_wu32be(stream, frame -> split_count);
        bs_wu16be(stream, frame -> split_id);
        bs_wu32be(stream, frame -> split_index);
    }

    bs_wbytes(stream, frame -> payload, frame -> payload_len);
}

int raknet_frame_own_payload(raknet_frame_t *frame)
{
    if(frame -> owns_payload)
        return 0;

    if(frame -> payload_len == 0)
    {
        frame -> payload = NULL;
        frame -> owns_payload = false;
        return 0;
    }

    uint8_t *copy = (uint8_t *)malloc(frame -> payload_len);
    if(!copy)
        return -1;

    memcpy(copy, frame -> payload, frame -> payload_len);
    frame -> payload = copy;
    frame -> owns_payload = true;
    return 0;
}

void raknet_frame_free(raknet_frame_t *frame)
{
    if(frame -> owns_payload)
        free(frame -> payload);

    frame -> payload = NULL;
    frame -> payload_len = 0;
    frame -> owns_payload = false;
}