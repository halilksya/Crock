#ifndef MCROCK_GAME_START_GAME_H
#define MCROCK_GAME_START_GAME_H

#include "mrock/game/connection.h"

// 3 and 4 are spectator variants; 5 tells the client to fall back to world_gamemode | 3 ve 4 izleyici varyantları; 5 client'a world_gamemode'a dönmesini söyler
#define MC_GAME_MODE_SURVIVAL 0
#define MC_GAME_MODE_CREATIVE 1
#define MC_GAME_MODE_ADVENTURE 2
#define MC_GAME_MODE_DEFAULT 5

#define MC_DIMENSION_OVERWORLD 0
#define MC_DIMENSION_NETHER 1
#define MC_DIMENSION_THE_END 2

#define MC_GENERATOR_LEGACY 0
#define MC_GENERATOR_OVERWORLD 1
#define MC_GENERATOR_FLAT 2
#define MC_GENERATOR_NETHER 3
#define MC_GENERATOR_THE_END 4
#define MC_GENERATOR_VOID 5

#define MC_DIFFICULTY_PEACEFUL 0
#define MC_DIFFICULTY_EASY 1
#define MC_DIFFICULTY_NORMAL 2
#define MC_DIFFICULTY_HARD 3

#define MC_PERMISSION_VISITOR 0
#define MC_PERMISSION_MEMBER 1
#define MC_PERMISSION_OPERATOR 2
#define MC_PERMISSION_CUSTOM 3

//Actual packet is around 250 bytes with empty lists | Boş listelerle paket 250 bayt civarında
#define MC_START_GAME_MAX_SIZE 1024

//Holds only the StartGame fields that vary; the rest are constants in start_game.c | StartGame'in sadece değişken alanlarını tutuyor; gerisi start_game.c'de sabit
typedef struct
{
    //unique is a zigzag varint, runtime a plain varuint; hence the signedness | unique zigzag varint, runtime düz varuint; tiplerin işareti bu yüzden farklı
    int64_t entity_unique_id;
    uint64_t entity_runtime_id;

    int32_t player_gamemode;

    //Where the player appears | Oyuncunun spawn olduğu yer
    float spawn_x;
    float spawn_y;
    float spawn_z;
    float pitch;
    float yaw;

    int64_t world_seed;
    int16_t spawn_biome_type;
    const char *biome_name;

    int32_t dimension;
    int32_t generator;
    int32_t world_gamemode;
    int32_t difficulty;

    //Compass target and respawn point, independent of the spawn above | Pusula hedefi ve yeniden doğma noktası, yukarıdaki spawn'dan bağımsız
    int32_t world_spawn_x;
    int32_t world_spawn_y;
    int32_t world_spawn_z;

    uint8_t player_permissions;
    int32_t chunk_tick_radius;

    int64_t world_time;

    //Random UUID string per connection, 36 chars + NUL | Her bağlantı için rastgele UUID string'i, 36 karakter + NUL
    char correlation_id[40];

    const char *level_id;
    const char *world_name;
} mc_world_settings_t;

void mc_world_settings_default(mc_world_settings_t *settings);

int mc_send_start_game(mc_connection_t *connection, const mc_world_settings_t *settings);

int mc_send_jigsaw_structure_data(mc_connection_t *connection);

int mc_send_voxel_shapes(mc_connection_t *connection);

int mc_send_item_registry(mc_connection_t *connection);

//Sends the four packets in the order the client expects | Dört paketi client'ın beklediği sırayla gönderir
int mc_send_start_game_sequence(mc_connection_t *connection, const mc_world_settings_t *settings);

#endif