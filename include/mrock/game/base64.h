#ifndef MROCK_GAME_BASE64_H
#define MROCK_GAME_BASE64_H

#include "mrock/common.h"

#define MC_BASE64_DECODED_SIZE(encoded_len) (((encoded_len) / 4 + 1) * 3)

int mc_base64url_decode(const char *src, size_t src_len, uint8_t *out, size_t out_cap);

#endif