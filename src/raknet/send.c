#include "mrock/raknet/session.h"
#include "mrock/raknet/server.h"

#include <stdlib.h>
#include <string.h>

static int split_message(raknet_session_t *session, const uint8_t *payload, size_t len, uint8_t reliability, uint8_t order_channel, uint64_t now);
static int queue_frame(raknet_session_t *session, raknet_frame_t *frame, uint64_t now);
static void flush_queue(raknet_session_t *session, uint64_t now);
static void cache_datagram(raknet_session_t *session, uint32_t sequence, uint64_t now);

static inline size_t cache_slot(uint32_t sequence)
{
    return sequence & (RAKNET_SEND_CACHE_CAPACITY - 1);
}

static void assign_indices(raknet_send_layer_t *send, raknet_frame_t *frame)
{
    if(RAKNET_IS_RELIABLE(frame -> reliability))
    {
        frame -> reliable_index = send -> next_reliable_index;
        send -> next_reliable_index = triad_inc(send -> next_reliable_index);
    }

    if(RAKNET_IS_SEQUENCED(frame -> reliability))
    {
        uint8_t channel = frame -> order_channel;
        frame -> sequenced_index = send -> next_sequenced[channel];
        send -> next_sequenced[channel] = triad_inc(send -> next_sequenced[channel]);
    }

    if(RAKNET_IS_ORDERED(frame -> reliability))
    {
        uint8_t channel = frame -> order_channel;
        frame -> ordered_index = send -> next_ordered[channel];

        if(!RAKNET_IS_SEQUENCED(frame -> reliability))
            send -> next_ordered[channel] = triad_inc(send -> next_ordered[channel]);
    }
}

int raknet_send_message(raknet_session_t *session, const uint8_t *payload, size_t len, uint8_t reliability, uint8_t order_channel)
{
    if(len == 0 || order_channel >= RAKNET_MAX_ORDER_CHANNELS)
        return -1;

    raknet_send_layer_t *send = &session -> send;
    uint64_t now = mr_now_ms();

    size_t header = raknet_frame_header_size(reliability, false);
    size_t max_payload = send -> usable_mtu - 4 - header;

    if(len <= max_payload)
    {
        raknet_frame_t frame;
        memset(&frame, 0, sizeof(frame));

        frame.reliability = reliability;
        frame.order_channel = order_channel;
        frame.payload = (uint8_t *)payload;
        frame.payload_len = (uint16_t)len;
        frame.owns_payload = false;

        assign_indices(send, &frame);

        if(raknet_frame_own_payload(&frame) < 0)
            return -1;

        if(queue_frame(session, &frame, now) < 0)
        {
            raknet_frame_free(&frame);
            return -1;
        }
        return 0;
    }
    return split_message(session, payload, len, reliability, order_channel, now);
}

static int split_message(raknet_session_t *session, const uint8_t *payload, size_t len, uint8_t reliability, uint8_t order_channel, uint64_t now)
{
    raknet_send_layer_t *send = &session -> send;

    size_t header = raknet_frame_header_size(reliability, true);
    size_t max_payload = send -> usable_mtu - 4 - header;

    size_t part_count = (len + max_payload - 1) / max_payload;

    if(part_count > RAKNET_MAX_SPLIT_PART_COUNT)
    {
        log_debug("message too large to split: %zu bytes", len);
        return -1;
    }

    uint16_t split_id = send -> next_split_id++;

    uint32_t ordered_index = 0;
    uint32_t sequenced_index = 0;

    if(RAKNET_IS_ORDERED(reliability))
    {
        ordered_index = send -> next_ordered[order_channel];
        if(!RAKNET_IS_SEQUENCED(reliability))
            send -> next_ordered[order_channel] = triad_inc(send -> next_ordered[order_channel]);
    }

    if(RAKNET_IS_SEQUENCED(reliability))
    {
        sequenced_index = send -> next_sequenced[order_channel];
        send -> next_sequenced[order_channel] = triad_inc(send -> next_sequenced[order_channel]);
    }

    size_t offset = 0;

    for(uint32_t part = 0; part < part_count; part++)
    {
        size_t chunk = len - offset;
        if(chunk > max_payload)
            chunk = max_payload;

        raknet_frame_t frame;
        memset(&frame, 0, sizeof(frame));

        frame.reliability = reliability;
        frame.order_channel = order_channel;
        frame.ordered_index = ordered_index;
        frame.sequenced_index = sequenced_index;

        if(RAKNET_IS_RELIABLE(reliability))
        {
            frame.reliable_index = send -> next_reliable_index;
            send -> next_reliable_index = triad_inc(send -> next_reliable_index);
        }

        frame.is_split = true;
        frame.split_count = (uint32_t)part_count;
        frame.split_id = split_id;
        frame.split_index = part;

        frame.payload = (uint8_t *)(payload + offset);
        frame.payload_len = (uint16_t)chunk;
        frame.owns_payload = false;

        if(raknet_frame_own_payload(&frame) < 0)
            return -1;

        if(queue_frame(session, &frame, now) < 0)
        {
            raknet_frame_free(&frame);
            return -1;
        }

        offset += chunk;
    }
    return 0;
}

static int queue_frame(raknet_session_t *session, raknet_frame_t *frame, uint64_t now)
{
    raknet_send_layer_t *send = &session -> send;
    size_t frame_size = raknet_frame_size(frame);

    if(send -> send_queue_bytes + frame_size + 4 > send -> mtu)
        flush_queue(session, now);

    if(send -> send_queue_count >= RAKNET_SEND_QUEUE_CAPACITY)
    {
        flush_queue(session, now);
        if(send -> send_queue_count >= RAKNET_SEND_QUEUE_CAPACITY)
            return -1;
    }

    send -> send_queue[send -> send_queue_count++] = *frame;
    send -> send_queue_bytes += frame_size;
    return 0;
}

void raknet_send_flush(raknet_session_t *session, uint64_t now_ms)
{
    flush_queue(session, now_ms);
}

static void flush_queue(raknet_session_t *session, uint64_t now)
{
    raknet_send_layer_t *send = &session -> send;

    if(send -> send_queue_count == 0)
        return;

    uint8_t buffer[RAKNET_MAX_MTU];
    bs_t stream;
    bs_write_init(&stream, buffer, send -> usable_mtu);

    uint32_t sequence = send -> next_datagram_sequence;
    send -> next_datagram_sequence = triad_inc(send -> next_datagram_sequence);

    bs_wu8(&stream, RAKNET_FLAG_VALID);
    bs_wu24le(&stream, sequence);

    for(size_t i = 0; i < send -> send_queue_count; i++)
        raknet_frame_encode(&stream, &send -> send_queue[i]);

    if(stream.err)
    {
        log_debug("datagram encode overflow, dropping %zu frames", send -> send_queue_count);

        for(size_t i = 0; i < send -> send_queue_count; i++)
            raknet_frame_free(&send -> send_queue[i]);
        send -> send_queue_count = 0;
        send -> send_queue_bytes = 0;
        return;
    }

    raknet_server_send(session -> server, &session -> address, stream.buf, stream.len);
    session -> tx_datagrams++;

    bool needs_cache = false;

    for(size_t i = 0; i < send -> send_queue_count; i++)
    {
        if(RAKNET_IS_RELIABLE(send -> send_queue[i].reliability))
        {
            needs_cache = true;
            break;
        }
    }

    if(!needs_cache)
    {
        for(size_t i = 0; i < send -> send_queue_count; i++)
            raknet_frame_free(&send -> send_queue[i]);
        send -> send_queue_count = 0;
        send -> send_queue_bytes = 0;
        return;
    }

    cache_datagram(session, sequence, now);

    send -> send_queue_count = 0;
    send -> send_queue_bytes = 0;
}

static void cache_free_entry(raknet_cached_datagram_t *entry)
{
    if(!entry -> in_use)
        return;

    for(size_t i = 0; i < entry -> frame_count; i++)
        raknet_frame_free(&entry -> frames[i]);

    free(entry -> frames);
    entry -> frames = NULL;
    entry -> frame_count = 0;
    entry -> in_use = false;
}

static void cache_datagram(raknet_session_t *session, uint32_t sequence, uint64_t now)
{
    raknet_send_layer_t *send = &session -> send;
    raknet_cached_datagram_t *entry = &send -> cache[cache_slot(sequence)];

    if(entry -> in_use)
    {
        log_debug("send cache overwrite at sequence %u (old sequence: %u)", sequence, entry -> sequence);
        cache_free_entry(entry);
    }

    entry -> frames = (raknet_frame_t *)malloc(send -> send_queue_count * sizeof(raknet_frame_t));

    if(!entry -> frames)
    {
        for(size_t i = 0; i < send -> send_queue_count; i++)
            raknet_frame_free(&send -> send_queue[i]);
        return;
    }

    memcpy(entry -> frames, send -> send_queue, send -> send_queue_count * sizeof(raknet_frame_t));

    entry -> frame_count = send -> send_queue_count;
    entry -> sequence = sequence;
    entry -> sent_at_ms = now;
    entry -> in_use = true;
}

void raknet_send_on_ack(raknet_session_t *session, uint32_t sequence)
{
    raknet_send_layer_t *send = &session -> send;
    raknet_cached_datagram_t *entry = &send -> cache[cache_slot(sequence)];

    if(!entry -> in_use || entry -> sequence != sequence)
        return;

    cache_free_entry(entry);
}

void raknet_send_on_nack(raknet_session_t *session, uint32_t sequence)
{
    raknet_send_layer_t *send = &session -> send;
    raknet_cached_datagram_t *entry = &send -> cache[cache_slot(sequence)];

    if(!entry -> in_use || entry -> sequence != sequence)
        return;

    for(size_t i = 0; i < send -> resend_count; i++)
    {
        if(send -> resend_queue[i] == sequence)
            return;
    }

    if(send -> resend_count < RAKNET_SEND_CACHE_CAPACITY)
        send -> resend_queue[send -> resend_count++] = sequence;
}

void raknet_send_update(raknet_session_t *session, uint64_t now)
{
    raknet_send_layer_t *send = &session -> send;

    for(size_t i = 0; i < RAKNET_SEND_CACHE_CAPACITY; i++)
    {
        raknet_cached_datagram_t *entry = &send -> cache[i];

        if(!entry -> in_use)
            continue;

        if(now - entry -> sent_at_ms < RAKNET_RESEND_TIMEOUT_MS)
            continue;

        raknet_send_on_nack(session, entry -> sequence);
    }

    for(size_t q = 0; q < send -> resend_count; q++)
    {
        uint32_t old_sequence = send -> resend_queue[q];
        raknet_cached_datagram_t *entry = &send -> cache[cache_slot(old_sequence)];

        if(!entry -> in_use || entry -> sequence != old_sequence)
            continue;

        for(size_t i = 0; i < entry -> frame_count; i++)
        {
            raknet_frame_t frame = entry -> frames[i];
            frame.owns_payload = false;

            if(raknet_frame_own_payload(&frame) < 0)
                break;

            if(queue_frame(session, &frame, now) < 0)
            {
                raknet_frame_free(&frame);
                break;
            }
        }
        cache_free_entry(entry);
        session -> resends++;
    }
    send -> resend_count = 0;

    flush_queue(session, now);
}