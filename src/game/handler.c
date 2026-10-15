#include "mrock/game/connection.h"
#include "mrock/game/packet_id.h"
#include "mrock/mc.h"

#include <string.h>

static void handle_request_network_settings(mc_connection_t *connection, const uint8_t *body, size_t body_len);

void mc_handle_packet(mc_connection_t *connection, uint32_t packet_id, const uint8_t *body, size_t body_len)
{
    switch(packet_id)
    {
        case MC_PACKET_REQUEST_NETWORK_SETTINGS:
            handle_request_network_settings(connection, body, body_len);
            break;
        default:
            log_debug("unhandled game packet 0x%02x (%zu bytes) in state %d", packet_id, body_len, connection -> state);
            break;
    }
}

static void send_network_settings(mc_connection_t *connection);
static void send_play_status(mc_connection_t *connection, int32_t status);

static void handle_request_network_settings(mc_connection_t *connection, const uint8_t *body, size_t body_len)
{
    if(connection -> state != MC_STATE_HANDSHAKE)
    {
        log_debug("RequestNetworkSettings in wrong state");
        return;
    }

    bs_t stream;
    bs_read_init(&stream, body, body_len);

    uint32_t protocol = bs_ru32be(&stream);
    if(stream.err)
    {
        connection -> close(connection -> transport, "truncated RequestNetworkSettings");
        return;
    }

    connection -> client_protocol = protocol;
    log_debug("client protocol: %u (ours %d)", protocol, MCROCK_VERSION);

    if(protocol != (uint32_t)MCROCK_VERSION)
    {
        int32_t status = (protocol < (uint32_t)MCROCK_VERSION) ? MC_PLAY_STATUS_FAILED_CLIENT : MC_PLAY_STATUS_FAILED_SERVER;

        send_play_status(connection, status);
        connection -> close(connection -> transport, "protocol mismatch");
        return;
    }

    send_network_settings(connection);
    connection ->   compression_enabled = true;
    connection -> state = MC_STATE_LOGIN;
}

static void send_network_settings(mc_connection_t *connection)
{
    uint8_t body[16];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wu16le(&stream, MC_COMPRESSION_THRESHOLD);
    bs_wu16le(&stream, MC_COMPRESSION_ZLIB);
    bs_wbool(&stream, false);
    bs_wu8(&stream, 0);
    bs_wf32le(&stream, 0.0f);

    if(stream.err)
    {
        log_debug("NetworkSettings encode failed");
        return;
    }

    if(mc_connection_send_packet(connection, MC_PACKET_NETWORK_SETTINGS, body, stream.len) < 0)
    {
        log_debug("NetworkSettings send failed");
    }
}

static void send_play_status(mc_connection_t *connection, int32_t status)
{
    uint8_t body[4];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wu32be(&stream, (uint32_t)status);

    if(stream.err)
        mc_connection_send_packet(connection, MC_PACKET_PLAY_STATUS, body, stream.len);
}