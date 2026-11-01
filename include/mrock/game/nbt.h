#ifndef MCROCK_GAME_NBT_H
#define MCROCK_GAME_NBT_H

#include "mrock/util/bstream.h"

//Network little-endian NBT, not the standard variant | Standart NBT değil, ağ varyantı
//String lengths are varuint32 and int32 values are zigzag varint | String uzunlukları varuint32, int32 değerler zigzag varint

#define MC_NBT_TAG_END 0x00
#define MC_NBT_TAG_BYTE 0x01
#define MC_NBT_TAG_SHORT 0x02
#define MC_NBT_TAG_INT 0x03
#define MC_NBT_TAG_LONG 0x04
#define MC_NBT_TAG_FLOAT 0x05
#define MC_NBT_TAG_DOUBLE 0x06
#define MC_NBT_TAG_BYTE_ARRAY 0x07
#define MC_NBT_TAG_STRING 0x08
#define MC_NBT_TAG_LIST 0x09
#define MC_NBT_TAG_COMPOUND 0x0A

//Must be paired | İkisi birlikte kullanılmalı
void mc_nbt_compound_start(bs_t *stream, const char *name);
void mc_nbt_compound_end(bs_t *stream);

void mc_nbt_empty_compound(bs_t *stream);

//An empty list still declares its element type | Boş liste de eleman tipini bildiriyor
void mc_nbt_empty_list(bs_t *stream, const char *name, uint8_t element_type);

#endif