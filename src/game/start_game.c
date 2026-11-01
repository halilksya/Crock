#include "mrock/game/start_game.h"
#include "mrock/game/packet_id.h"
#include "mrock/game/nbt.h"
#include "mrock/mc.h"

#include <stdio.h>
#include <string.h>

void mc_world_settings_default(mc_world_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));

    settings -> entity_unique_id = 1;
    settings -> entity_runtime_id = 1;

    settings -> player_gamemode = MC_GAME_MODE_SURVIVAL;
    settings -> world_gamemode = MC_GAME_MODE_SURVIVAL;

    settings -> spawn_x = 0.0f;
    settings -> spawn_y = 70.0f;
    settings -> spawn_z = 0.0f;
    settings -> pitch = 0.0f;
    settings -> yaw = 0.0f;

    settings -> world_seed = 0;
    settings -> spawn_biome_type = 0;
    settings -> biome_name = "";

    settings -> dimension = MC_DIMENSION_OVERWORLD;
    //What the reference servers send, regardless of the terrain we actually generate | Referans sunucuların gönderdiği değer, ürettiğimiz araziden bağımsız
    settings -> generator = MC_GENERATOR_OVERWORLD;
    settings -> difficulty = MC_DIFFICULTY_NORMAL;

    settings -> world_spawn_x = 0;
    settings -> world_spawn_y = 70;
    settings -> world_spawn_z = 0;

    settings -> player_permissions = MC_PERMISSION_MEMBER;
    settings -> chunk_tick_radius = 4;

    settings -> world_time = 0;

    settings -> level_id = "mrock";
    settings -> world_name = "mrock";

    //Shaped like a version 4 UUID; it only has to differ between sessions | Sürüm 4 UUID biçiminde; sadece oturumlar arası farklı olması yeterli
    snprintf(settings -> correlation_id, sizeof(settings -> correlation_id),
            "%08x-%04x-4%03x-8%03x-%012llx",
            (unsigned)(mr_now_ms() & 0xFFFFFFFFu),
            (unsigned)((mr_now_ms() >> 32) & 0xFFFFu),
            (unsigned)(mr_now_ms() & 0xFFFu),
            (unsigned)((mr_now_ms() >> 12) & 0xFFFu),
            (unsigned long long)(mr_now_ms() & 0xFFFFFFFFFFFFull));
}

//81 fields, order taken from gophertunnel for protocol 2193; one wrong type shifts everything after it | 81 alan, sıra protokol 2193 için gophertunnel'dan alındı; tek bir yanlış tip sonrasının tamamını kaydırıyor
int mc_send_start_game(mc_connection_t *connection, const mc_world_settings_t *settings)
{
    uint8_t zero_uuid[16];
    memset(zero_uuid, 0, sizeof(zero_uuid));

    uint8_t body[MC_START_GAME_MAX_SIZE];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wvari64(&stream, settings -> entity_unique_id);
    bs_wvaru64(&stream, settings -> entity_runtime_id);
    bs_wvari32(&stream, settings -> player_gamemode);

    bs_wf32le(&stream, settings -> spawn_x);
    bs_wf32le(&stream, settings -> spawn_y);
    bs_wf32le(&stream, settings -> spawn_z);
    bs_wf32le(&stream, settings -> pitch);
    bs_wf32le(&stream, settings -> yaw);

    //Seed and biome type are fixed-width little-endian, not varints | Seed ve biyom tipi sabit genişlikli little-endian, varint değil
    bs_wu64le(&stream, (uint64_t)settings -> world_seed);
    bs_wu16le(&stream, (uint16_t)settings -> spawn_biome_type);
    bs_wstr_var(&stream, settings -> biome_name);

    bs_wvari32(&stream, settings -> dimension);
    bs_wvari32(&stream, settings -> generator);
    bs_wvari32(&stream, settings -> world_gamemode);
    bs_wbool(&stream, false);                       //hardcore
    bs_wvari32(&stream, settings -> difficulty);

    //World spawn is a BlockPos, so three separate varints | Dünya spawn'ı BlockPos, yani üç ayrı varint
    bs_wvari32(&stream, settings -> world_spawn_x);
    bs_wvari32(&stream, settings -> world_spawn_y);
    bs_wvari32(&stream, settings -> world_spawn_z);

    bs_wbool(&stream, true);                        //achievements disabled
    bs_wvari32(&stream, 0);                         //editor world type
    bs_wbool(&stream, false);                       //created in editor
    bs_wbool(&stream, false);                       //exported from editor
    bs_wvari32(&stream, 0);                         //day cycle lock time

    bs_wvaru32(&stream, 0);                         //education edition offer
    bs_wbool(&stream, false);                       //education features enabled
    bs_wstr_var(&stream, "");                       //education product id

    bs_wf32le(&stream, 0.0f);                       //rain level
    bs_wf32le(&stream, 0.0f);                       //lightning level

    bs_wbool(&stream, false);                       //confirmed platform locked content
    bs_wbool(&stream, true);                        //multiplayer game
    bs_wbool(&stream, true);                        //lan broadcast enabled
    bs_wvari32(&stream, 0);                         //xbl broadcast mode
    bs_wvari32(&stream, 0);                         //platform broadcast mode
    bs_wbool(&stream, true);                        //commands enabled
    bs_wbool(&stream, false);                       //texture pack required

    bs_wvaru32(&stream, 0);                         //game rule count
    //The experiment count is a fixed uint32 while the other list counts are varints | Experiment sayacı sabit uint32, diğer liste sayaçları varint
    bs_wu32le(&stream, 0);                          //experiment count
    bs_wbool(&stream, false);                       //experiments previously toggled
    bs_wbool(&stream, false);                       //bonus chest enabled
    bs_wbool(&stream, false);                       //start with map enabled

    bs_wu8(&stream, settings -> player_permissions);
    bs_wu32le(&stream, (uint32_t)settings -> chunk_tick_radius);

    bs_wbool(&stream, false);                       //has locked behaviour pack
    bs_wbool(&stream, false);                       //has locked texture pack
    bs_wbool(&stream, false);                       //from locked world template
    bs_wbool(&stream, false);                       //msa gamer tags only
    bs_wbool(&stream, false);                       //from world template
    bs_wbool(&stream, false);                       //world template settings locked
    bs_wbool(&stream, false);                       //only spawn v1 villagers
    bs_wbool(&stream, false);                       //persona disabled
    bs_wbool(&stream, false);                       //custom skins disabled
    bs_wbool(&stream, false);                       //emote chat muted

    bs_wstr_var(&stream, MCROCK_VERSION_STRING);    //base game version
    bs_wu32le(&stream, 0);                          //limited world width
    bs_wu32le(&stream, 0);                          //limited world depth
    bs_wbool(&stream, true);                        //new nether

    bs_wstr_var(&stream, "");                       //education resource button name
    bs_wstr_var(&stream, "");                       //education resource link uri

    //Optional field: the flag says there is no value, so nothing follows | Opsiyonel alan: bayrak değer olmadığını söylüyor, arkasından bir şey gelmiyor
    bs_wbool(&stream, false);                       //force experimental gameplay

    bs_wu8(&stream, 0);                             //chat restriction level
    bs_wbool(&stream, false);                       //disable player interactions
    bs_wvari32(&stream, 0);                         //server editor connection policy
    bs_wbool(&stream, false);                       //allow anonymous block drops in editor worlds

    bs_wstr_var(&stream, settings -> level_id);
    bs_wstr_var(&stream, settings -> world_name);
    bs_wstr_var(&stream, "");                       //template content identity
    bs_wbool(&stream, false);                       //trial

    //Movement settings, only two fields in this protocol version | Hareket ayarları, bu protokol sürümünde sadece iki alan
    bs_wvari32(&stream, 0);                         //rewind history size
    bs_wbool(&stream, false);                       //server authoritative block breaking

    bs_wu64le(&stream, (uint64_t)settings -> world_time);
    bs_wvari32(&stream, 0);                         //enchantment seed

    bs_wvaru32(&stream, 0);                         //custom block count
    bs_wstr_var(&stream, settings -> correlation_id);
    bs_wbool(&stream, true);                        //server authoritative inventory
    bs_wstr_var(&stream, MCROCK_VERSION_STRING);    //game version

    //Player property data, an empty NBT compound | Oyuncu property verisi, boş bir NBT compound
    mc_nbt_empty_compound(&stream);

    //Zero tells the client to skip block state verification | Sıfır, client'a blok durumu doğrulamasını atlamasını söylüyor
    bs_wu64le(&stream, 0);                          //server block state checksum
    bs_wbytes(&stream, zero_uuid, sizeof(zero_uuid));   //world template id

    bs_wbool(&stream, false);                       //client side generation
    //False means the chunk palette uses indices, true would mean name hashes | False, chunk palette'inin indeks kullandığı anlamına geliyor; true isim hash'i demek
    bs_wbool(&stream, false);                       //use block network id hashes
    bs_wbool(&stream, false);                       //server authoritative sound
    bs_wbool(&stream, false);                       //server join information absent

    bs_wstr_var(&stream, "");                       //server id
    bs_wstr_var(&stream, "");                       //scenario id
    bs_wstr_var(&stream, "");                       //world id
    bs_wstr_var(&stream, "");                       //owner id

    if(stream.err)
    {
        log_debug("StartGame encode failed");
        return -1;
    }

    return mc_connection_send_packet(connection, MC_PACKET_START_GAME, stream.buf, stream.len);
}

//Jigsaw rules for structure generation; we define none but the packet is still required | Yapı üretimi için jigsaw kuralları; hiçbirini tanımlamıyoruz ama paket yine de zorunlu
int mc_send_jigsaw_structure_data(mc_connection_t *connection)
{
    uint8_t body[128];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    //Four empty compound lists, what a vanilla server sends with no jigsaw rules defined | Dört boş compound listesi, hiç jigsaw kuralı olmayan bir vanilla sunucunun gönderdiği şey
    mc_nbt_compound_start(&stream, "");
    mc_nbt_empty_list(&stream, "processors", 0x0A);
    mc_nbt_empty_list(&stream, "template_pools", 0x0A);
    mc_nbt_empty_list(&stream, "jigsaws", 0x0A);
    mc_nbt_empty_list(&stream, "structure_sets", 0x0A);
    mc_nbt_compound_end(&stream);

    if(stream.err)
    {
        log_debug("JigsawStructureData encode failed");
        return -1;
    }

    return mc_connection_send_packet(connection, MC_PACKET_JIGSAW_STRUCTURE_DATA, stream.buf, stream.len);
}

//Collision shapes for custom blocks, empty since we add none | Özel bloklar için çarpışma şekilleri, hiç eklemediğimiz için boş
int mc_send_voxel_shapes(mc_connection_t *connection)
{
    uint8_t body[16];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wvaru32(&stream, 0);     //shape count
    bs_wvaru32(&stream, 0);     //name map count
    bs_wu16le(&stream, 0);      //custom shape count, fixed uint16

    if(stream.err)
    {
        log_debug("VoxelShapes encode failed");
        return -1;
    }

    return mc_connection_send_packet(connection, MC_PACKET_VOXEL_SHAPES, stream.buf, stream.len);
}

//An empty list is valid on the wire but mobile clients may crash without the real item table | Boş liste wire açısından geçerli ama mobil client'lar gerçek item tablosu olmadan çökebiliyor
int mc_send_item_registry(mc_connection_t *connection)
{
    uint8_t body[8];
    bs_t stream;
    bs_write_init(&stream, body, sizeof(body));

    bs_wvaru32(&stream, 0);     //item count

    if(stream.err)
        {
        log_debug("ItemRegistry encode failed");
        return -1;
        }

    return mc_connection_send_packet(connection, MC_PACKET_ITEM_REGISTRY, stream.buf, stream.len);
}

//The client builds the world on StartGame, so the shape and structure definitions go out first | Client dünyayı StartGame ile kuruyor, o yüzden şekil ve yapı tanımları önce gidiyor
int mc_send_start_game_sequence(mc_connection_t *connection, const mc_world_settings_t *settings)
{
    if(mc_send_jigsaw_structure_data(connection) < 0)
        return -1;

    if(mc_send_voxel_shapes(connection) < 0)
        return -1;

    if(mc_send_start_game(connection, settings) < 0)
        return -1;

    if(mc_send_item_registry(connection) < 0)
        return -1;

    return 0;
}
