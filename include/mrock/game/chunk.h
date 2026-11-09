#ifndef MCROCK_GAME_CHUNK_H
#define MCROCK_GAME_CHUNK_H

#include "mrock/game/connection.h"

//The largest radius we serve, whatever the client asks for | Client ne isterse istesin hizmet ettiğimiz en büyük yarıçap
#define MC_MAX_CHUNK_RADIUS 8

//Overworld spans -64 to 320, which is 24 sections of 16 blocks | Overworld -64 ile 320 arasında, yani 16 bloktan 24 bölüm
#define MC_CHUNK_SECTION_COUNT 24

int mc_send_chunk_radius_updated(mc_connection_t *connection, int32_t radius);

int mc_send_level_chunk(mc_connection_t *connection, int32_t chunk_x, int32_t chunk_z);

int mc_send_network_chunk_publisher_update(mc_connection_t *connection, int32_t block_x, int32_t block_y, int32_t block_z, uint32_t block_radius);

int mc_send_spawn_chunks(mc_connection_t *connection);

#endif