#include "mrock/game/connection.h"

#include <string.h>

void mc_connection_init(mc_connection_t *connection, void *transport, mc_send_callback_t send, mc_close_callback_t close)
{
    memset(connection, 0, sizeof(*connection));
    connection -> transport = transport;
    connection -> send = send;
    connection -> close = close;
    connection -> state = MC_STATE_HANDSHAKE;
    connection -> compression_enabled = false;
}

static void on_packet(void *context, uint32_t packet_id, const uint8_t *body, size_t body_len)
{
    mc_connection_t *connection = (mc_connection_t *)context;
    mc_handle_packet(connection, packet_id, body, body_len);
}

void mc_connection_on_batch(mc_connection_t *connection, const uint8_t *data, size_t len)
{
    if(mc_batch_decode(data, len, connection -> compression_enabled, on_packet, connection) < 0)
    {
        log_debug("malformed batch (%zu bytes), closing connection", len);
        connection -> close(connection -> transport, "malformed batch");
    }
}

int mc_connection_send_packet(mc_connection_t *connection, uint32_t packet_id, const uint8_t *body, size_t body_len)
{
    mc_batch_t batch;
    if(mc_batch_init(&batch) < 0)
        return -1;

    if(mc_batch_add(&batch, packet_id, body, body_len) < 0)
    {
        mc_batch_free(&batch);
        return -1;
    }

    bs_t out;
    if(bs_dyn_init(&out, 512) < 0)
    {
        mc_batch_free(&batch);
        return -1;
    }

    int result = mc_batch_finish(&batch, connection -> compression_enabled, MC_COMPRESSION_THRESHOLD, &out);
    
    mc_batch_free(&batch);

    if(result == 0)
        result = connection -> send(connection -> transport, out.buf, out.len);

    bs_free(&out);
    return result;

}