#include "mrock/raknet/session.h"
#include "mrock/raknet/server.h"
#include "mrock/raknet/ack.h"

#include <stdlib.h>
#include <string.h>

static inline bool bit_get(const uint8_t *bits, size_t offset)
{
    return (bits[offset / 8] & (uint8_t)(1u << (offset % 8))) != 0;
}

static inline void bit_set(uint8_t *bits, size_t offset)
{
    bits[offset / 8] |= (uint8_t)(1u << (offset % 8));
}

static inline void bit_clear(uint8_t *bits, size_t offset)
{
    bits[offset / 8] &= (uint8_t)~(1u << (offset % 8));
}

static void slide_window(raknet_recv_layer_t *recv)
{
    while(bit_get(recv -> seen_bits, 0))
    {
        bit_clear(recv -> seen_bits, 0);

        for(size_t i = 1; i < RAKNET_RECV_WINDOW; i++)
        {
            if(bit_get(recv -> seen_bits, i))
            {
                bit_set(recv -> seen_bits, i - 1);
                bit_clear(recv -> seen_bits, i);
            }
        }

        recv -> window_start = triad_inc(recv -> window_start);
        recv -> window_end = triad_inc(recv -> window_end);
    }
}

void raknet_recv_on_frame_set(raknet_session_t *session, bs_t *stream)
{
    raknet_recv_layer_t *recv = &session -> recv;

    uint32_t sequence = bs_ru24le(stream);
    if(stream -> err)
        return;

    if(triad_lt(sequence, recv -> window_start))
        return;

    if(triad_ge(sequence, recv -> window_end))
    {
        stream -> err = 1;
        return;
    }

    size_t offset = (size_t)triad_diff(sequence, recv -> window_start);

    if(bit_get(recv -> seen_bits, offset))
        return;

    bit_set(recv -> seen_bits, offset);

    if(recv -> ack_count < RAKNET_RECV_WINDOW)
        recv -> ack_queue[recv -> ack_count++] = sequence;

    if(triad_gt(sequence, recv -> highest_seen))
    {
        uint32_t missing = recv -> highest_seen;
        while(triad_lt(missing, sequence))
        {
            if(recv -> nack_count < RAKNET_RECV_WINDOW)
                recv -> nack_queue[recv -> nack_count++] = missing;
            missing = triad_inc(missing);
        }
    }

    if(triad_ge(sequence, recv -> highest_seen))
        recv -> highest_seen = triad_inc(sequence);

    decode_frames(session, stream);

    slide_window(recv);  
}

static void handle_frame(raknet_session_t *session, raknet_frame_t *frame);

static void decode_frames(raknet_session_t *session, bs_t *stream)
{
    size_t frame_count = 0;

    while(bs_remaining(stream) > 0 && frame_count < RAKNET_MAX_FRAME_PER_DATAGRAM)
    {
        raknet_frame_t frame;
        raknet_frame_decode(stream, &frame);
        if(stream -> err)
            break;

        handle_frame(session, &frame);
        frame_count++;
    }
}

static bool reliable_is_duplicate(raknet_recv_layer_t *recv, uint32_t index)
{
    if(triad_lt(index, recv -> reliable_window_start))
        return true;

    uint32_t offset = triad_diff(index, recv -> reliable_window_start);

    if((size_t)offset >= RAKNET_RECV_WINDOW)
    {
        uint32_t slide = (uint32_t)((size_t)offset - RAKNET_RECV_WINDOW + 1);

        if(slide >= RAKNET_RECV_WINDOW)
        {
            memset(recv -> reliable_seen_bits, 0, sizeof(recv -> reliable_seen_bits));
            recv -> reliable_window_start = index;
            bit_set(recv -> reliable_seen_bits, 0);
            return false;
        }
        
        for(uint32_t i = 0; i < slide; i++)
        {
            bit_clear(recv -> reliable_seen_bits, 0);
            for(size_t j = 1; j < RAKNET_RECV_WINDOW; j++)
            {
                if(bit_get(recv -> reliable_seen_bits, j))
                {
                    bit_set(recv -> reliable_seen_bits, j - 1);
                    bit_clear(recv -> reliable_seen_bits, j);
                }
            }
            recv -> reliable_window_start = triad_inc(recv -> reliable_window_start);
        }
        offset = triad_diff(index, recv -> reliable_window_start);
    }
    
    if(bit_get(recv -> reliable_seen_bits, (size_t)offset))
        return true;

    bit_set(recv -> reliable_seen_bits, (size_t)offset);
    return false;
}

static void hadle_split(raknet_session_t *session, raknet_frame_t *frame);
static void deliver_ordered(raknet_session_t *session, raknet_frame_t *frame);

static void handle_frame(raknet_session_t *session, raknet_frame_t *frame)
{
    raknet_recv_layer_t *recv = &session -> recv;

    if(RAKNET_RELIABILITY_IS_RELIABLE(frame -> reliability))
    {
        if(reliable_is_duplicate(recv, frame -> reliable_index))
            return;
    }

    if(frame -> is_split)
    {
        hadle_split(session, frame);
        return;
    }

    if(RAKNET_IS_SEQUENCED(frame -> reliability))
    {
        uint8_t channel = frame -> order_channel;

        if(triad_lt(frame -> sequenced_index, recv -> highest_sequenced[channel]))
            return;

        recv -> highest_sequenced[channel] = triad_inc(frame -> sequenced_index);
        raknet_session_on_message(session, frame -> payload, frame -> payload_len);
        return;
    }

    if(RAKNET_IS_ORDERED(frame -> reliability))
    {
        deliver_ordered(session, frame);
        return;
    }

    raknet_session_on_message(session, frame -> payload, frame -> payload_len);
}

static bool ensure_order_buffer(raknet_recv_layer_t *recv, uint8_t channel)
{
    if(recv -> order_buffer[channel])
        return true;

    recv -> order_buffer[channel] = (raknet_frame_t *)calloc(RAKNET_ORDER_BUFFER_CAPACITY, sizeof(raknet_frame_t));
    recv -> order_slot_used[channel] = (bool *)calloc(RAKNET_ORDER_BUFFER_CAPACITY, sizeof(bool));

    if(!recv -> order_buffer[channel] || !recv -> order_slot_used[channel])
    {
        free(recv -> order_buffer[channel]);
        free(recv -> order_slot_used[channel]);
        recv -> order_buffer[channel] = NULL;
        recv -> order_slot_used[channel] = NULL;
        return false;
    }
    return true;
}

static void deliver_ordered(raknet_session_t *session, raknet_frame_t *frame)
{
    raknet_recv_layer_t *recv = &session -> recv;
    uint8_t channel = frame -> order_channel;

    if(frame -> ordered_index == recv -> next_ordered[channel])
    {
        raknet_session_on_message(session, frame -> payload, frame -> payload_len);
        recv -> next_ordered[channel] = triad_inc(recv -> next_ordered[channel]);

        if(!ensure_order_buffer(recv, channel))
            return;

        for(;;)
        {
            uint32_t wanted = recv -> next_ordered[channel];
            size_t slot = wanted % RAKNET_ORDER_BUFFER_CAPACITY;

            if(!recv -> order_slot_used[channel][slot])
                break;

            raknet_frame_t *buffered = &recv -> order_buffer[channel][slot];
            if(buffered -> ordered_index != wanted)
                break;

            raknet_session_on_message(session, buffered -> payload, buffered -> payload_len);
            raknet_frame_free(buffered);
            recv -> order_slot_used[channel][slot] = false;
            recv -> next_ordered[channel] = triad_inc(wanted);
        }
        return;
    } 

    if(triad_lt(frame -> ordered_index, recv -> next_ordered[channel]))
        return;

    int32_t ahead = triad_diff(frame -> ordered_index, recv -> next_ordered[channel]);
    if(ahead < 0 || (size_t)ahead >= RAKNET_ORDER_BUFFER_CAPACITY)
    {
        log_debug("ordered index too far ahead (%d), dropping", ahead);
        return;
    }

    if(!ensure_order_buffer(recv, channel))
        return;

    size_t slot = frame -> ordered_index % RAKNET_ORDER_BUFFER_CAPACITY;

    if(recv -> order_slot_used[channel][slot])
        return;

    if(raknet_frame_own_payload(frame) < 0)
        return;
    
    recv -> order_buffer[channel][slot] = *frame;
    recv -> order_slot_used[channel][slot] = true;
}

// I'm tired. ^^' 
