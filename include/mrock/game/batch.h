#ifndef MCROCK_GAME_BATCH_H
#define MCROCK_GAME_BATCH_H

#include "mrock/util/bstream.h"

#define MC_BATCH_HEADER 0xFE

#define MC_COMPRESSION_ZLIB 0x00
#define MC_COMPRESSION_SNAPPY 0x01
#define MC_COMPRESSION_NONE 0xFF

#define MC_MAX_PACKETS_PER_BATCH 512

typedef void (*mc_packet_handler_t)(void *context, uint32_t packet_id, const uint8_t *body, size_t body_len);

int mc_batch_decode(const uint8_t *data, size_t data_len, bool compression_enabled, mc_packet_handler_t handler, void *context);

typedef struct
{
    bs_t packets;
    size_t packet_count;
} mc_batch_t;

int mc_batch_init(mc_batch_t *batch);
void mc_batch_free(mc_batch_t *batch);

int mc_batch_add(mc_batch_t *batch, uint32_t packet_id, const uint8_t *body, size_t body_len);

int mc_batch_finish(mc_batch_t *batch, bool compression_enabled, size_t threshold, bs_t *out);

#endif