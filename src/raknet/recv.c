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

static void decode_frames(raknet_session_t *session, bs_t *stream);
static void handle_frame(raknet_session_t *session, raknet_frame_t *frame);
static void dispatch_frame(raknet_session_t *session, raknet_frame_t *frame);
static void handle_split(raknet_session_t *session, raknet_frame_t *frame);
static void deliver_ordered(raknet_session_t *session, raknet_frame_t *frame);

void raknet_send_on_ack(raknet_session_t *session, uint32_t sequence);
void raknet_send_on_nack(raknet_session_t *session, uint32_t sequence);

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

    int32_t offset = triad_diff(index, recv -> reliable_window_start);

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

static void handle_frame(raknet_session_t *session, raknet_frame_t *frame)
{
    raknet_recv_layer_t *recv = &session -> recv;

    if(RAKNET_IS_RELIABLE(frame -> reliability))
    {
        if(reliable_is_duplicate(recv, frame -> reliable_index))
            return;
    }

    if(frame -> is_split)
    {
        handle_split(session, frame);
        return;
    }

    dispatch_frame(session, frame);
}

static void dispatch_frame(raknet_session_t *session, raknet_frame_t *frame)
{
    raknet_recv_layer_t *recv = &session -> recv;

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

        if(!recv -> order_buffer[channel])
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

static raknet_split_assembly_t *find_split(raknet_session_t *session, uint16_t split_id, uint32_t part_count, uint64_t now)
{
    for(size_t i = 0; i < RAKNET_MAX_CONCURRENT_SPLITS; i++)
    {
        raknet_split_assembly_t *assembly = &session -> splits[i];

        if(assembly -> in_use && assembly -> split_id == split_id)
        {
            if(assembly -> part_count != part_count)
                return NULL;
            return assembly;
        }
    }

    for(size_t i = 0; i < RAKNET_MAX_CONCURRENT_SPLITS; i++)
    {
        raknet_split_assembly_t *assembly = &session -> splits[i];
        if(assembly -> in_use)
            continue;

        assembly -> parts = (raknet_frame_t *)calloc(part_count, sizeof(raknet_frame_t));
        assembly -> part_present = (bool *)calloc(part_count, sizeof(bool));

        if(!assembly -> parts || !assembly -> part_present)
        {
            free(assembly -> parts);
            free(assembly -> part_present);
            assembly -> parts = NULL;
            assembly -> part_present = NULL;
            return NULL;
        }

        assembly -> in_use = true;
        assembly -> split_id = split_id;
        assembly -> part_count = part_count;
        assembly -> parts_received = 0;
        assembly -> started_ms = now;
        return assembly;
    }
    return NULL;
}

static void split_reset(raknet_split_assembly_t *assembly)
{
    if(assembly -> parts)
    {
        for(uint32_t i = 0; i < assembly -> part_count; i++)
        {
            if(assembly -> part_present[i])
                raknet_frame_free(&assembly -> parts[i]);
        }
        free(assembly -> parts);
        free(assembly -> part_present);
    }
    memset(assembly, 0, sizeof(*assembly));
}

static void handle_split(raknet_session_t *session, raknet_frame_t *frame)
{
    raknet_split_assembly_t *assembly = find_split(session, frame -> split_id, frame -> split_count, mr_now_ms());
    if(!assembly)
        return;

    if(assembly -> part_present[frame -> split_index])
        return;

    if(raknet_frame_own_payload(frame) < 0)
        return;

    assembly -> parts[frame -> split_index] = *frame;
    assembly -> part_present[frame -> split_index] = true;
    assembly -> parts_received++;

    if(assembly -> parts_received < assembly -> part_count)
        return;

    size_t total = 0;
    for(uint32_t i = 0; i < assembly -> part_count; i++)
        total += assembly -> parts[i].payload_len;

    if(total == 0 || total > RAKNET_MAX_SPLIT_PART_COUNT * RAKNET_MAX_MTU)
    {
        split_reset(assembly);
        return;
    }

    uint8_t *merged = (uint8_t *)malloc(total);
    if(!merged)
    {
        split_reset(assembly);
        return;
    }

    size_t offset = 0;
    for(uint32_t i = 0; i < assembly -> part_count; i++)
    {
        memcpy(merged + offset, assembly -> parts[i].payload, assembly -> parts[i].payload_len);
        offset += assembly -> parts[i].payload_len;
    }

    raknet_frame_t whole = assembly -> parts[0];
    whole.is_split = false;
    whole.payload = merged;
    whole.payload_len = (uint16_t)total;
    whole.owns_payload = false;

    split_reset(assembly);
    dispatch_frame(session, &whole);
    free(merged);
}

void raknet_recv_on_ack(raknet_session_t *session, bs_t *stream)
{
    uint32_t sequence[RAKNET_MAX_ACK_SEQUENCES];
    size_t count = raknet_ack_decode(stream, sequence, RAKNET_MAX_ACK_SEQUENCES);

    for(size_t i = 0; i < count; i++)
        raknet_send_on_ack(session, sequence[i]);
}

void raknet_recv_on_nack(raknet_session_t *session, bs_t *stream)
{
    uint32_t sequence[RAKNET_MAX_ACK_SEQUENCES];
    size_t count = raknet_ack_decode(stream, sequence, RAKNET_MAX_ACK_SEQUENCES);

    for(size_t i = 0; i < count; i++)
        raknet_send_on_nack(session, sequence[i]);
}

void raknet_recv_expire_splits(raknet_session_t *session, uint64_t now)
{
    for(size_t i = 0; i < RAKNET_MAX_CONCURRENT_SPLITS; i++)
    {
        raknet_split_assembly_t *assembly = &session -> splits[i];
        if(!assembly -> in_use)
            continue;

        if(now - assembly -> started_ms < 30000)
            continue;

        log_debug("expiring incomplete split %u (%u/%u parts)", assembly -> split_id, assembly -> parts_received, assembly -> part_count);
        split_reset(assembly);
    }
}