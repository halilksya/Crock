#include "mrock/game/connection.h"
#include "mrock/game/packet_id.h"
#include "mrock/mc.h"
#include "mrock/game/login.h"

#include <string.h>

static void handle_request_network_settings(mc_connection_t *connection, const uint8_t *body, size_t body_len);

static void handle_login(mc_connection_t *connection, const uint8_t *body, size_t body_len);

static void handle_resource_pack_response(mc_connection_t *connection, const uint8_t *body, size_t body_len);

static void send_resource_packs_info(mc_connection_t *connection);

static void send_resource_pack_stack(mc_connection_t *connection);

void mc_handle_packet(mc_connection_t *connection, uint32_t packet_id, const uint8_t *body, size_t body_len)
{
    switch(packet_id)
    {
        case MC_PACKET_REQUEST_NETWORK_SETTINGS:
            handle_request_network_settings(connection, body, body_len);
            break;

        case MC_PACKET_LOGIN:
            handle_login(connection, body, body_len);
            break;

        case MC_PACKET_RESOURCE_PACK_RESPONSE:
            handle_resource_pack_response(connection, body, body_len);
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

static void handle_login(mc_connection_t *connection, const uint8_t *body, size_t body_len)
{
    if(connection -> state != MC_STATE_LOGIN)
    {
        log_debug("Login in wrong state (%d)", connection -> state);
        return;
    }

    if(mc_login_parse(body, body_len, &connection -> player) < 0)
    {
        log_debug("failed to parse Login packet (%zu bytes)", body_len);
        connection -> close(connection -> transport, "malformed Login");
        return;
    }

    log_info("player logged in: %s (uuid %s)", connection -> player.name, connection -> player.uuid);

    send_play_status(connection, MC_PLAY_STATUS_LOGIN_SUCCESS);
    send_resource_packs_info(connection);

    connection -> state = MC_STATE_RESOURCE_PACKS;
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

    if(!stream.err)
        mc_connection_send_packet(connection, MC_PACKET_PLAY_STATUS, body, stream.len);
}

static void send_resource_packs_info(mc_connection_t *connection)
{
    uint8_t body[64];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wbool(&stream, false);
    bs_wbool(&stream, false);
    bs_wbool(&stream, false);

    uint8_t zero_uuid[16];
    memset(zero_uuid, 0, sizeof(zero_uuid));
    bs_wbytes(&stream, zero_uuid, sizeof(zero_uuid));

    bs_wstr_var(&stream, "");

    bs_wbool(&stream, false);

    bs_wvaru32(&stream, 0);

    if(stream.err)
    {
        log_debug("ResourcePacksInfo encode failed");
        return;
    }

    mc_connection_send_packet(connection, MC_PACKET_RESOURCE_PACKS_INFO, stream.buf, stream.len);

    int result = mc_connection_send_packet(connection, MC_PACKET_RESOURCE_PACKS_INFO,
                                           stream.buf, stream.len);
    log_debug("ResourcePacksInfo sent: %d bytes, result %d", stream.len, result);
}

static void send_resource_pack_stack(mc_connection_t *connection)
{
    uint8_t body[64];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wbool(&stream, false);

    bs_wvaru32(&stream, 0);
    
    bs_wstr_var(&stream, MCROCK_VERSION_STRING);

    bs_wu32le(&stream, 0);

    bs_wbool(&stream, false);
    bs_wbool(&stream, false);

    if(stream.err)
    {
        log_debug("ResourcePackStack encode failed");
        return;
    }

    mc_connection_send_packet(connection, MC_PACKET_RESOURCE_PACK_STACK, stream.buf, stream.len);
}

#define MC_RESOURCE_PACK_REFUSED 1
#define MC_RESOURCE_PACK_SEND_PACKS 2
#define MC_RESOURCE_PACK_HAVE_ALL 3
#define MC_RESOURCE_PACK_COMPLETED 4

static void handle_resource_pack_response(mc_connection_t *connection, const uint8_t *body, size_t body_len)
{
    if(connection -> state != MC_STATE_RESOURCE_PACKS)
    {
        log_debug("ResourcePackResponse in wrong state (%d)", connection -> state);
        return;
    }

    bs_t stream;
    bs_read_init(&stream, body, body_len);

    uint8_t status = bs_ru8(&stream);
    if(stream.err)
        return;

    switch(status)
    {
        case MC_RESOURCE_PACK_HAVE_ALL:
            send_resource_pack_stack(connection);
            break;

        case MC_RESOURCE_PACK_COMPLETED:
            log_info("resource pack negotiation complete for %s", connection -> player.name);
            connection -> state = MC_STATE_SPAWNING;
            break;

        case MC_RESOURCE_PACK_REFUSED:
            log_debug("client refused resource packs");
            connection -> close(connection -> transport, "resource packs refused");
            break;

    }
    
}
