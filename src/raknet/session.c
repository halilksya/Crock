#include "mrock/raknet/session.h"
#include "mrock/raknet/server.h"
#include "mrock/raknet/ack.h"
#include "mrock/util/bstream.h"

#include <stdlib.h>
#include <string.h>

raknet_session_t *raknet_session_create(struct raknet_server *server, const mr_addr_t *address, uint64_t guid, uint16_t mtu)
{
    raknet_session_t *session = (raknet_session_t *)calloc(1, sizeof(*session));
    if(!session)
        return NULL;

    session -> server = server;
    session -> address = *address;
    session -> guid = guid;
    session -> state = RAKNET_STATE_CONNECTING;

    session -> send.mtu = mtu;

    session -> recv.window_start = 0;
    session -> recv.window_end = RAKNET_RECV_WINDOW;
    session -> recv.highest_seen = 0;
    session -> recv.reliable_window_start = 0;

    uint64_t now = mr_now_ms();
    session -> created_ms = now;
    session -> last_recv_ms = now;
    session -> last_ping_ms = now;

    return session;
}

static void free_order_buffers(raknet_recv_layer_t *recv)
{
    for(size_t i = 0; i < RAKNET_MAX_ORDER_CHANNELS; i++)
    {
        if(!recv -> order_buffer[i])
            continue;

        for(size_t j = 0; j < RAKNET_ORDER_BUFFER_CAPACITY; j++)
        {
            if(recv -> order_slot_used[i][j])
                raknet_frame_free(&recv -> order_buffer[i][j]);
        }

        free(recv -> order_buffer[i]);
        free(recv -> order_slot_used[i]);
        recv -> order_buffer[i] = NULL;
        recv -> order_slot_used[i] = NULL;
    }
}

static void free_split_assemblies(raknet_session_t *session)
{
    for(size_t i = 0; i < RAKNET_MAX_CONCURRENT_SPLITS; i++)
    {
        raknet_split_assembly_t *assembly = &session -> splits[i];
        if(!assembly -> in_use)
            continue;

        for(uint32_t j = 0; j < assembly -> part_count; j++)
        {
            if(assembly -> part_present[j])
                raknet_frame_free(&assembly -> parts[j]);
        }

        free(assembly -> parts);
        free(assembly -> part_present);
        assembly -> parts = NULL;
        assembly -> part_present = NULL;
        assembly -> in_use = false;
    }
}

static void free_send_cache(raknet_send_layer_t *send)
{
    for(size_t i = 0; i < RAKNET_SEND_CACHE_CAPACITY; i++)
    {
        raknet_cached_datagram_t *entry = &send -> cache[i];
        if(!entry -> in_use)
            continue;

        for(size_t j = 0; j < entry -> frame_count; j++)
            raknet_frame_free(&entry -> frames[j]);

        free(entry -> frames);
        entry -> frames = NULL;
        entry -> frame_count = 0;
        entry -> in_use = false;
    }
}

void raknet_session_destroy(raknet_session_t *session)
{
    if(!session)
        return;

    free_order_buffers(&session -> recv);
    free_split_assemblies(session);
    free_send_cache(&session -> send);

    for(size_t i = 0; i < session -> send.send_queue_count; i++)
        raknet_frame_free(&session -> send.send_queue[i]);

    free(session);
}

void raknet_recv_on_frame_set(raknet_session_t *session, bs_t *stream);
void raknet_send_on_ack(raknet_session_t *session, bs_t *stream);
void raknet_send_on_nack(raknet_session_t *session, bs_t *stream);

void raknet_session_on_datagram(raknet_session_t *session, const uint8_t *data, size_t len)
{
    if(session -> state == RAKNET_STATE_DEAD)
        return;

    session -> last_recv_ms = mr_now_ms();
    session -> rx_datagrams++;

    bs_t stream;
    bs_read_init(&stream, data, len);

    uint8_t flags = bs_ru8(&stream);
    if(stream.err)
        return;

    if(flags & RAKNET_FLAG_ACK)
        raknet_send_on_ack(session, &stream);
    else if(flags & RAKNET_FLAG_NAK)
        raknet_send_on_nack(session, &stream);
    else
        stream.err = 1;

    if(stream.err)
    {
        session -> server -> bad_packets++;
        log_debug("malformed datagram, flags=0x%02x len=%zu", flags, len);
    }
}

static const char *disconnect_reason_text(raknet_disconnect_reason_t reason)
{
    switch(reason)
    {
        case RAKNET_DISCONNECT_CLIENT: return "client request";
        case RAKNET_DISCONNECT_TIMEOUT: return "timeout";
        case RAKNET_DISCONNECT_PROTOCOL: return "protocol error";
        default: return "server shutdown";
    }
}

void raknet_session_close(raknet_session_t *session, raknet_disconnect_reason_t reason)
{
    if(session -> state == RAKNET_STATE_DEAD)
        return;

    char address_text[64];
    mr_addr_to_string(&session -> address, address_text, sizeof(address_text));
    log_info("session closed: %s (%s) rx=%llu tx=%llu resends=%llu reason=%s",
             address_text, 
             (unsigned long long)session -> guid,
             (unsigned long long)session -> rx_datagrams, 
             (unsigned long long)session -> tx_datagrams, 
             (unsigned long long)session -> resends);

    session -> state = RAKNET_STATE_DEAD;
}



