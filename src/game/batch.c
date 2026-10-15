#include "mrock/game/batch.h"
#include "mrock/game/compress.h"

#include <string.h>

static int parse_packets(const uint8_t *data, size_t data_len, mc_packet_handler_t handler, void *context)
{
    bs_t stream;
    bs_read_init(&stream, data, data_len);
    size_t packet_count = 0;

    while(bs_remaining(&stream) > 0)
    {
        if(packet_count >= MC_MAX_PACKETS_PER_BATCH)
            return -1;

        uint32_t packet_len = bs_rvaru32(&stream);
        if(stream.err || packet_len == 0)
            return -1;

        const uint8_t *packet = bs_rbytes(&stream, packet_len);
        if(!packet)
            return -1;

        bs_t packet_stream;
        bs_read_init(&packet_stream, packet, packet_len);

        uint32_t header = bs_rvaru32(&packet_stream);
        if(packet_stream.err)
            return -1;

        uint32_t packet_id = header & 0x3FF;

        handler(context, packet_id, packet + packet_stream.pos, bs_remaining(&packet_stream));
        packet_count++;
    }
    return 0;
}

int mc_batch_decode(const uint8_t *data, size_t data_len, bool compression_enabled, mc_packet_handler_t handler, void *context)
{
    if(!compression_enabled)
        return parse_packets(data, data_len, handler, context);

    if(data_len < 1)
        return -1;

    uint8_t algorithm = data[0];
    const uint8_t *body = data + 1;
    size_t body_len = data_len - 1;

    if(algorithm == MC_COMPRESSION_NONE)
        return parse_packets(body, body_len, handler, context);

    if(algorithm != MC_COMPRESSION_ZLIB)
        return -1;

    bs_t inflated;
    if(bs_dyn_init(&inflated, body_len * 4) < 0)
        return -1;

    if(mc_compress_inflate(body, body_len, &inflated, MC_MAX_DECOMPRESSED_SIZE) < 0)
    {
        bs_free(&inflated);
        return -1;
    }

    int result = parse_packets(inflated.buf, inflated.len, handler, context);
    bs_free(&inflated);
    return result;  
}

int mc_batch_init(mc_batch_t *batch)
{
    batch -> packet_count = 0;
    return bs_dyn_init(&batch -> packets, 256);
}

void mc_batch_free(mc_batch_t *batch)
{
    bs_free(&batch -> packets);
    batch -> packet_count = 0;
}

int mc_batch_add(mc_batch_t *batch, uint32_t packet_id, const uint8_t *body, size_t body_len)
{
    if(packet_id > 0x3FF || batch -> packet_count >= MC_MAX_PACKETS_PER_BATCH)
        return -1;

    uint8_t header_buf[5];
    bs_t header_stream;
    bs_write_init(&header_stream, header_buf, sizeof(header_buf));
    bs_wvaru32(&header_stream, packet_id);

    size_t packet_len = header_stream.len + body_len;
    if(packet_len > UINT32_MAX)
        return -1;

    bs_wvaru32(&batch -> packets, (uint32_t)packet_len);
    bs_wbytes(&batch -> packets, header_stream.buf, header_stream.len);
    bs_wbytes(&batch -> packets, body, body_len);

    if(batch -> packets.err)
        return -1;
    
    batch -> packet_count++;
    return 0;
}

int mc_batch_finish(mc_batch_t *batch, bool compression_enabled, size_t threshold, bs_t *out)
{
    if(batch -> packet_count == 0 || batch -> packets.err)
        return -1;

    bs_wu8(out, MC_BATCH_HEADER);

    if(!compression_enabled)
    {
        bs_wbytes(out, batch -> packets.buf, batch -> packets.len);
        return out -> err ? -1 : 0;
    }

    if(batch -> packets.len < threshold)
    {
        bs_wu8(out, MC_COMPRESSION_NONE);
        bs_wbytes(out, batch -> packets.buf, batch -> packets.len);
        return out -> err ? -1 : 0;
    }

    bs_wu8(out, MC_COMPRESSION_ZLIB);
    if(out -> err)
        return -1;

    return mc_compress_deflate(batch -> packets.buf, batch -> packets.len, out);
}

