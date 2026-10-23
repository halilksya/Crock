#ifndef MROCK_GAME_COMPRESS_H
#define MROCK_GAME_COMPRESS_H

#include "mrock/util/bstream.h"

//Compression bomb guard | Sıkıştırma bombası koruması
#define MC_MAX_DECOMPRESSED_SIZE (8 * 1024 * 1024)

//Raw deflate (windowBits -15), no zlib header. Both append at out -> pos | Ham deflate (windowBits -15), zlib başlığı yok. İkisi de out -> pos'a ekliyor
int mc_compress_inflate(const uint8_t *src, size_t src_len, bs_t *out, size_t max_out);
int mc_compress_deflate(const uint8_t *src, size_t src_len, bs_t *out);

#endif