#include "mrock/raknet/session.h"
#include "mrock/raknet/server.h"
#include "mrock/raknet/ack.h"
#include "mrock/util/bstream.h"
#include "mrock/game/connection.h"

#include <stdlib.h>
#include <string.h>

void raknet_recv_on_frame_set(raknet_session_t *session, bs_t *stream);
void raknet_recv_on_ack(raknet_session_t *session, bs_t *stream);
void raknet_recv_on_nack(raknet_session_t *session, bs_t *stream);
void raknet_recv_expire_splits(raknet_session_t *session, uint64_t now);

void raknet_send_update(raknet_session_t *session, uint64_t now);

static void send_connected_ping(raknet_session_t *session, uint64_t now);
static void send_connected_pong(raknet_session_t *session, uint64_t ping_time);
static void send_connection_request_accepted(raknet_session_t *session, uint64_t request_time);

static int game_send(void *transport, const uint8_t *data, size_t len)
{
    raknet_session_t *session = (raknet_session_t *)transport;
    return raknet_send_message(session, data, len, RAKNET_RELIABLE_ORDERED, 0);
}

static void game_close(void *transport, const char *reason)
{
    raknet_session_t *session = (raknet_session_t *)transport;
    log_debug("game connection closed: %s", reason);
    raknet_session_close(session, RAKNET_DISCONNECT_PROTOCOL);
}

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

    mc_connection_init(&session -> game, session, game_send, game_close);
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
        if(!assembly -> parts)
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
        raknet_recv_on_ack(session, &stream);
    else if(flags & RAKNET_FLAG_NAK)
        raknet_recv_on_nack(session, &stream);
    else if(flags & RAKNET_FLAG_VALID)
        raknet_recv_on_frame_set(session, &stream);
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
    log_info("session closed: %s (%s) rx=%llu tx=%llu resends=%llu",
             address_text,
             disconnect_reason_text(reason),
             (unsigned long long)session -> rx_datagrams,
             (unsigned long long)session -> tx_datagrams,
             (unsigned long long)session -> resends);

    session -> state = RAKNET_STATE_DEAD;
}

static void flush_acknowledgements(raknet_session_t *session)
{
    raknet_recv_layer_t *recv = &session -> recv;
    uint8_t buffer[RAKNET_MAX_MTU];

    size_t written = 0;
    while(written < recv -> ack_count)
    {
        bs_t stream;
        bs_write_init(&stream, buffer, session -> send.mtu);

        size_t n = raknet_ack_encode(&stream, recv -> ack_queue + written, recv -> ack_count - written, true, session -> send.mtu);

        if(n == 0)
            break;

        raknet_server_send(session -> server, &session -> address, stream.buf, stream.len);
        written += n;
    }
    recv -> ack_count = 0;

    written = 0;
    while(written < recv -> nack_count)
    {
        bs_t stream;
        bs_write_init(&stream, buffer, session -> send.mtu);

        size_t n = raknet_ack_encode(&stream, recv -> nack_queue + written, recv -> nack_count - written, false, session -> send.mtu);

        if(n == 0)
            break;

        raknet_server_send(session -> server, &session -> address, stream.buf, stream.len);
        written += n;
    }
    recv -> nack_count = 0;
}

void raknet_session_tick(raknet_session_t *session, uint64_t now_ms)
{
    if(session -> state == RAKNET_STATE_DEAD)
        return;

    flush_acknowledgements(session);
    raknet_send_update(session, now_ms);

    if(session -> state == RAKNET_STATE_CONNECTED && now_ms - session -> last_ping_ms >= RAKNET_PING_INTERVAL_MS)
    {
        send_connected_ping(session, now_ms);
        session -> last_ping_ms = now_ms;
    }

    raknet_recv_expire_splits(session, now_ms);

    if(now_ms - session -> last_recv_ms >= RAKNET_SESSION_TIMEOUT_MS)
        raknet_session_close(session, RAKNET_DISCONNECT_TIMEOUT);
}

static void send_connection_request_accepted(raknet_session_t *session, uint64_t request_time)
{
    uint8_t payload[512];
    bs_t stream;
    bs_write_init(&stream, payload, sizeof(payload));

    bs_wu8(&stream, RAKNET_ID_CONNECTION_REQUEST_ACCEPTED);
    mr_addr_write(&stream, &session -> address);
    bs_wu16be(&stream, 0);

    mr_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.version = 4;
    dummy.ip[0] = dummy.ip[1] = dummy.ip[2] = dummy.ip[3] = 255;
    dummy.port = 0;

    for(size_t i = 0; i < 20; i++)
        mr_addr_write(&stream, &dummy);

    bs_wu64be(&stream, request_time);
    bs_wu64be(&stream, mr_now_ms());

    if(stream.err)
    {
        log_debug("CONNECTION_REQUEST_ACCEPTED encode failed");
        return;
    }

    raknet_send_message(session, stream.buf, stream.len, RAKNET_RELIABLE_ORDERED, 0);
}

static void send_connected_ping(raknet_session_t *session, uint64_t now)
{
    uint8_t payload[16];
    bs_t stream;
    bs_write_init(&stream, payload, sizeof(payload));

    bs_wu8(&stream, RAKNET_ID_CONNECTED_PING);
    bs_wu64be(&stream, now);

    if(!stream.err)
        raknet_send_message(session, stream.buf, stream.len, RAKNET_UNRELIABLE, 0);
}

static void send_connected_pong(raknet_session_t *session, uint64_t ping_time)
{
    uint8_t payload[32];
    bs_t stream;
    bs_write_init(&stream, payload, sizeof(payload));

    bs_wu8(&stream, RAKNET_ID_CONNECTED_PONG);
    bs_wu64be(&stream, ping_time);
    bs_wu64be(&stream, mr_now_ms());

    if(!stream.err)
        raknet_send_message(session, stream.buf, stream.len, RAKNET_UNRELIABLE, 0);
}

static void handle_connection_request(raknet_session_t *session, bs_t *stream)
{
    if(session -> state != RAKNET_STATE_CONNECTING)
    {
        log_debug("CONNECTION_REQUEST in wrong state");
        return;
    }

    uint64_t client_guid = bs_ru64be(stream);
    uint64_t request_time = bs_ru64be(stream);
    if(stream -> err)
        return;

    session -> guid = client_guid;
    send_connection_request_accepted(session, request_time);
}

static void handle_new_incoming_connection(raknet_session_t *session)
{
    if(session -> state != RAKNET_STATE_CONNECTING)
        return;

    session -> state = RAKNET_STATE_CONNECTED;

    char address_text[64];
    mr_addr_to_string(&session -> address, address_text, sizeof(address_text));
    log_info("session connected: %s guid=%016llx", address_text, (unsigned long long)session -> guid);
}

void raknet_session_on_message(raknet_session_t *session, const uint8_t *payload, size_t len)
{
    if(len == 0)
        return;

    bs_t stream;
    bs_read_init(&stream, payload, len);
    uint8_t message_id = bs_ru8(&stream);

    switch(message_id)
    {
        case RAKNET_ID_CONNECTION_REQUEST:
            handle_connection_request(session, &stream);
            break;

        case RAKNET_ID_NEW_INCOMING_CONNECTION:
            handle_new_incoming_connection(session);
            break;

        case RAKNET_ID_CONNECTED_PING:
        {
            uint64_t ping_time = bs_ru64be(&stream);
            if(!stream.err)
                send_connected_pong(session, ping_time);
            break;
        }

        case RAKNET_ID_CONNECTED_PONG:
            break;

        case RAKNET_ID_DISCONNECTION_NOTIFICATION:
            raknet_session_close(session, RAKNET_DISCONNECT_CLIENT);
            break;

        case RAKNET_ID_GAME_PACKET:
            mc_connection_on_batch(&session -> game, payload + 1, len - 1);
            break;

        default:
            log_debug("unhandled message id 0x%02x", message_id);
            break;
    }
}