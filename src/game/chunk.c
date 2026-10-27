#include "mrock/game/chunk.h"
#include "mrock/game/packet_id.h"
#include "mrock/game/start_game.h"

int mc_send_chunk_radius_updated(mc_connection_t *connection, int32_t radius)
{
    uint8_t body[8];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wvari32(&stream, radius);

    if(stream.err)
    {
        log_debug("ChunkRadiusUpdated encode failed");
        return -1;
    }

    return mc_connection_send_packet(connection, MC_PACKET_CHUNK_RADIUS_UPDATE, stream.buf, stream.len);
}

//An empty column needs no block palette at all, only biome data | Boş kolon hiç blok palette'i istemiyor, sadece biyom verisi
static void write_empty_biomes(bs_t *stream)
{
    //bitsPerIndex 0 with the network flag, meaning one value for the whole section | Ağ bayrağıyla bitsPerIndex 0, yani bölümün tamamı tek değer
    bs_wu8(stream, 0x01);

    //The palette holds one biome id, and network palettes are zigzag varints | Palette tek bir biyom  id'si tutuyor, ağ palette'leri zigzag varint
    bs_wvari32(stream, 1);

    //0xFF means this sevtion repeats the previous one | 0xFF bu bölümün öncekini tekrarladığı anlamına geliyor
    for(int i = 1; i < MC_CHUNK_SECTION_COUNT; i++)
        bs_wu8(stream, 0xFF);

}

int mc_send_level_chunk(mc_connection_t *connection, int32_t chunk_x, int32_t chunk_z)
{
    uint8_t payload_buf[128];
    bs_t payload;
    bs_write_init(&payload, payload_buf, sizeof(payload_buf));

    write_empty_biomes(&payload);

    //Border block count, only education edition uses it | Sınır blok sayacı, sadece education sürümü kullanıyor
    bs_wu8(&payload, 0);

    if(payload.err)
    {
        log_debug("LevelChunk payload encode failed");
        return -1;
    }

    uint8_t body[256];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wvari32(&stream, chunk_x);
    bs_wvari32(&stream, chunk_z);
    bs_wvari32(&stream, MC_DIMENSION_OVERWORLD);

    //Zero sub_chunks, so the client draws an empty column | Sıfır alt-chunk, client boş bir kolon çiziyor
    bs_wvaru32(&stream, 0);

    bs_wbool(&stream, false); //sub chunk limit absent
    bs_wbool(&stream, false); //cache disabled
    bs_wvaru32(&stream, 0);   //blob has count
    
    bs_wvaru32(&stream, (uint32_t)payload.len);
    bs_wbytes(&stream, payload.buf, payload.len);

    if(stream.err)
    {
        log_debug("LevelChunk encode failed");
        return -1;
    }

    return mc_connection_send_packet(connection, MC_PACKET_LEVEL_CHUNK, stream.buf, stream.len);
}

//Tells the client which area around the player stays loaded | Client'a oyuncunun etrafındaki hangi alanın yüklü kalacağını söylüyor
int mc_send_network_chunk_publisher_update(mc_connection_t *connection, int32_t block_x, int32_t block_y, int32_t block_z, uint32_t block_radius)
{
    uint8_t body[16];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    //Position is a BlockPos, so three separate varints | Konum BlockPos, yani üç ayrı varint
    bs_wvari32(&stream, block_x);
    bs_wvari32(&stream, block_y);
    bs_wvari32(&stream, block_z);

    //This radius counts blocks, not chunks | Bu yarıçap blok sayıyor, chunk değil
    bs_wvaru32(&stream, block_radius);

    //Saved chunk count, a fixed uint32 rather than a varint | Kayıtlı chunk sayacı, varint değil sabit uint32
    bs_wu32le(&stream, 0);

    if(stream.err)
    {
        log_debug("NetworkChunkPublisherUpdate encode failed");
        return -1;
    }

    return mc_connection_send_packet(connection, MC_PACKET_NETWORK_CHUNK_PUBLISHER_UPDATE, stream.buf, stream.len);
}

int mc_send_spawn_chunks(mc_connection_t *connection)
{
    int32_t radius = connection -> chunk_radius;
    if(radius < 1)
        radius = 1;

    for(int32_t x = -radius; x <= radius; x++)
    {
        for(int32_t z = -radius; x <= radius; z++)
        {
            if(mc_send_level_chunk(connection, x, z) < 0)
                return -1;
        }
    }

    //The publisher radius is the chunk radius in blocks | Publisher yarıçapı blok cinsinden chunk yarıçapı
    return mc_send_network_chunk_publisher_update(connection, 0, 70, 0, (uint32_t)(radius * 16));
}