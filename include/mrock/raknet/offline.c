#include "mrock/raknet/offline.h"
#include "mrock/raknet/server.h"
#include "mrock/util/bstream.h"

#include <string.h>

const uint8_t RAKNET_MAGIC[RAKNET_MAGIC_LEN] = 
{
    0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
    0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78
};

static bool read_magic(bs_t *s)
{
    const uint8_t *magic = bs_rbytes(s, RAKNET_MAGIC_LEN);
    if(!magic || memcmp(magic, RAKNET_MAGIC, RAKNET_MAGIC_LEN) != 0)
    {
        s -> err = 1;
        return false;
    }
    return true;
}

static void write_magic(bs_t *s)
{
    bs_wbytes(s, RAKNET_MAGIC, RAKNET_MAGIC_LEN);
}

static void handle_ping(raknet_server_t *server, bs_t *in, const mr_addr_t *from)
{
    uint64_t send_time = bs_ru64be(in);

    if(!read_magic(in))
        return;

    (void)bs_ru64be(in);
    if(in -> err)
        return;

    uint8_t out_buf[1024];
    bs_t out;
    bs_write_init(&out, out_buf, sizeof(out_buf));

    bs_wu8(&out, RAKNET_ID_UNCONNECTED_PONG);
    bs_wu64be(&out, send_time);
    bs_wu64be(&out, server -> guid);
    write_magic(&out);
    bs_wstr_be(&out, server -> motd); 
    
    if(!out.err)
        raknet_server_send(server, from, out.buf, out.len);
}

static void handle_open1(raknet_server_t *server, bs_t *in, const mr_addr_t *from, size_t datagram_len)
{
    if(!read_magic(in))
        return;

    uint8_t protocol = bs_ru8(in);
    if(in -> err)
        return;

    if(protocol != RAKNET_PROTOCOL_VERSION)
    {
        uint8_t out_buf[64];
        bs_t out;
        bs_write_init(&out, out_buf, sizeof(out_buf));  

        bs_wu8(&out, RAKNET_ID_INCOMPATIBLE_PROTOCOL);
        bs_wu8(&out, RAKNET_PROTOCOL_VERSION);
        write_magic(&out);
        bs_wu64be(&out, server -> guid);

        if(!out.err)
            raknet_server_send(server, from, out.buf, out.len);

        log_debug("incompatible raknet protocol: client=%u ours=%u", protocol, RAKNET_PROTOCOL_VERSION);
        return;
    }

    size_t mtu = datagram_len + RAKNET_UDP_HEADER_SIZE;
    
    if(mtu < RAKNET_MIN_MTU)
        mtu = RAKNET_MIN_MTU;
    else if(mtu > RAKNET_MAX_MTU)
        mtu = RAKNET_MAX_MTU;
    
    uint8_t out_buf[64];
    bs_t out;
    bs_write_init(&out, out_buf, sizeof(out_buf));

    bs_wu8(&out, RAKNET_ID_OPEN_CONNECTION_REPLY_1);
    write_magic(&out);
    bs_wu64be(&out, server -> guid);
    bs_wbool(&out, false);
    bs_wu16be(&out, (uint16_t)mtu);

    if(!out.err)
        raknet_server_send(server, from, out.buf, out.len);

    log_debug("OPEN_CONNECTION_1 -> mtu=%zu", mtu);
}

static void handle_open2(raknet_server_t *server, bs_t *in, const mr_addr_t *from)
{
    if(!read_magic(in))
        return;

    mr_addr_t server_addr;
    mr_addr_read(in, &server_addr);

    uint16_t mtu = bs_ru16be(in);
    uint64_t client_guid = bs_ru64be(in);

    if(in -> err)
        return;
    
    if(mtu < RAKNET_MIN_MTU)
        mtu = RAKNET_MIN_MTU;
    
    if(mtu > RAKNET_MAX_MTU)
        mtu = RAKNET_MAX_MTU;

    uint8_t out_buf[128];
    bs_t out;
    bs_write_init(&out, out_buf, sizeof(out_buf));

    bs_wu8(&out, RAKNET_ID_OPEN_CONNECTION_REPLY_2);
    write_magic(&out);
    bs_wu64be(&out, server -> guid);
    mr_addr_write(&out, from);
    bs_wu16be(&out, mtu);
    bs_wbool(&out, false);

    if(!out.err)
        raknet_server_send(server, from, out.buf, out.len);

    char addr_str[64];
    mr_addr_to_str(from, addr_str, sizeof(addr_str));
    log_info("opening session: %s guid=%016llx mtu=%u", addr_str, (unsigned long long)client_guid, mtu);

    //TODO: raknet_session_create(server, from, client_guid, mtu);

}

int raknet_offline_handle(raknet_server_t *server, const uint8_t *data, size_t len, const mr_addr_t *from)
{
    if(len < 1)
        return 0;

    bs_t in;
    bs_read_init(&in, data, len);
    uint8_t id = bs_ru8(&in);

    switch(id)
    {
        case RAKNET_ID_UNCONNECTED_PING:
        case RAKNET_ID_UNCONNECTED_PING_OPEN_CONNECTIONS:
            handle_ping(server, &in, from);
            break;
        case RAKNET_ID_OPEN_CONNECTION_REQUEST_1:
            handle_open1(server, &in, from, len);
            break;
        case RAKNET_ID_OPEN_CONNECTION_REQUEST_2:
            handle_open2(server, &in, from);
            break;
        default:
            return 0;
    }

    if(in.err)
    {
        server -> bad_packets++;
        log_debug("malformed offline packet, id=0x%02x len=%zu", id, len);
    }
    return 1;
}