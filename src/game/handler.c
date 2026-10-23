#include "mrock/game/connection.h"
#include "mrock/game/packet_id.h"
#include "mrock/mc.h"
#include "mrock/game/login.h"
#include "mrock/game/start_game.h"

#include <string.h>

//Login flow: RequestNetworkSettings, Login, then the resource pack exchange, then StartGame | Giriş akışı: RequestNetworkSettings, Login, ardından resource pack görüşmesi, sonra StartGame

//0-based in protocol 2193; older references list these as 1 to 4 | Protokol 2193'te 0 tabanlı; eski kaynaklar 1-4 olarak gösteriyor
#define MC_RESOURCE_PACK_REFUSED 0
#define MC_RESOURCE_PACK_SEND_PACKS 1
#define MC_RESOURCE_PACK_HAVE_ALL 2
#define MC_RESOURCE_PACK_COMPLETED 3

static void handle_request_network_settings(mc_connection_t *connection, const uint8_t *body, size_t body_len);

static void handle_login(mc_connection_t *connection, const uint8_t *body, size_t body_len);

static void handle_resource_pack_response(mc_connection_t *connection, const uint8_t *body, size_t body_len);

static void send_network_settings(mc_connection_t *connection);

static void send_play_status(mc_connection_t *connection, int32_t status);

static void send_resource_packs_info(mc_connection_t *connection);

static void send_resource_pack_stack(mc_connection_t *connection);

static void start_spawn_sequence(mc_connection_t *connection);

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

        //Swallowed on purpose, we do not use the chunk cache | Bilerek yutuluyor, chunk cache kullanmıyoruz
        case MC_PACKET_CLIENT_CACHE_STATUS:
            break;

        default:
            log_debug("unhandled game packet 0x%02x (%zu bytes) in state %d", packet_id, body_len, connection -> state);
            break;
    }
}

static void handle_request_network_settings(mc_connection_t *connection, const uint8_t *body, size_t body_len)
{
    if(connection -> state != MC_STATE_HANDSHAKE)
    {
        log_debug("RequestNetworkSettings in wrong state");
        return;
    }

    bs_t stream;
    bs_read_init(&stream, body, body_len);

    //Big-endian int32, unlike most game layer fields | Big-endian int32, oyun katmanının çoğu alanının aksine
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

    //NetworkSettings itself goes out uncompressed, so the flag flips only afterwards | NetworkSettings'in kendisi sıkıştırılmadan gidiyor, o yüzden bayrak ancak sonrasında açılıyor
    send_network_settings(connection);
    connection -> compression_enabled = true;
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
    bs_wbool(&stream, false);   //client throttle
    bs_wu8(&stream, 0);         //throttle threshold
    bs_wf32le(&stream, 0.0f);   //throttle scalar

    if(stream.err)
    {
        log_debug("NetworkSettings encode failed");
        return;
    }

    if(mc_connection_send_packet(connection, MC_PACKET_NETWORK_SETTINGS, stream.buf, stream.len) < 0)
    {
        log_debug("NetworkSettings send failed");
    }
}

static void send_play_status(mc_connection_t *connection, int32_t status)
{
    uint8_t body[4];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    //The status field is big-endian | Durum alanı big-endian
    bs_wu32be(&stream, (uint32_t)status);

    if(!stream.err)
        mc_connection_send_packet(connection, MC_PACKET_PLAY_STATUS, stream.buf, stream.len);
}

static void send_resource_packs_info(mc_connection_t *connection)
{
    uint8_t body[64];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wbool(&stream, false);   //must accept
    bs_wbool(&stream, false);   //has addons
    bs_wbool(&stream, false);   //has scripts
    bs_wbool(&stream, false);   //force disable vibrant visuals

    uint8_t zero_uuid[16];
    memset(zero_uuid, 0, sizeof(zero_uuid));
    bs_wbytes(&stream, zero_uuid, sizeof(zero_uuid));   //world template id

    bs_wstr_var(&stream, "");   //world template version

    bs_wvaru32(&stream, 0);     //texture pack count

    if(stream.err)
    {
        log_debug("ResourcePacksInfo encode failed");
        return;
    }

    mc_connection_send_packet(connection, MC_PACKET_RESOURCE_PACKS_INFO, stream.buf, stream.len);
}

static void send_resource_pack_stack(mc_connection_t *connection)
{
    uint8_t body[64];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wbool(&stream, false);   //must accept

    bs_wvaru32(&stream, 0);     //texture pack count

    bs_wstr_var(&stream, MCROCK_VERSION_STRING);   //base game version

    //The experiment count is a fixed uint32, not a varint | Experiment sayacı sabit uint32, varint değil
    bs_wu32le(&stream, 0);

    bs_wbool(&stream, false);   //experiments previously toggled
    bs_wbool(&stream, false);   //include editor packs

    if(stream.err)
    {
        log_debug("ResourcePackStack encode failed");
        return;
    }

    mc_connection_send_packet(connection, MC_PACKET_RESOURCE_PACK_STACK, stream.buf, stream.len);
}

//Entity IDs come from the defaults and are both 1; a second player will need a counter | Entity ID'leri varsayılandan geliyor ve ikisi de 1; ikinci oyuncu için sayaç gerekecek
static void start_spawn_sequence(mc_connection_t *connection)
{
    mc_world_settings_t settings;
    mc_world_settings_default(&settings);

    if(mc_send_start_game_sequence(connection, &settings) < 0)
    {
        log_debug("failed to send StartGame sequence");
        connection -> close(connection -> transport, "failed to send StartGame sequence");
        return;
    }

    log_info("StartGame sequence sent to %s", connection -> player.name);
}

static void handle_resource_pack_response(mc_connection_t *connection, const uint8_t *body, size_t body_len)
{
    if(connection -> state != MC_STATE_RESOURCE_PACKS)
    {
        log_debug("ResourcePackResponse in wrong state (%d)", connection -> state);
        return;
    }

    bs_t stream;
    bs_read_init(&stream, body, body_len);

    //A response name string follows the status, we do not need it | Durumun ardından bir isim string'i geliyor, ona ihtiyacımız yok
    uint32_t status = bs_rvaru32(&stream);
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
            start_spawn_sequence(connection);
            break;

        case MC_RESOURCE_PACK_REFUSED:
            log_debug("client refused resource packs");
            connection -> close(connection -> transport, "resource packs refused");
            break;

        default:
            log_debug("unexpected resource pack status %u", status);
            break;
    }
}
