#ifndef MCROCK_UTIL_BSTREAM_H
#define MCROCK_UTIL_BSTREAM_H

#include "mrock/common.h"

typedef struct
{
    uint8_t *buf;
    size_t   cap;
    size_t  len;
    size_t  pos;
    int     err;
    int    owned;
} bs_t;

void bs_read_init(bs_t *s, const void *buf, size_t len);
void bs_write_init(bs_t *s, void *buf, size_t cap);
int bs_dyn_int(bs_t *s, size_t initial_cap);
void bs_free(bs_t *s);

static inline size_t bs_remaining(const bs_t *s)
{
    return s->pos < s->len ? s->len - s->pos : 0;
}

static inline void bs_reset(bs_t *s)
{
    s->pos = 0;
    s->err = 0;
}

uint8_t  bs_ru8(bs_t *s);
uint16_t bs_ru16be(bs_t *s);
uint32_t bs_ru32be(bs_t *s);
uint64_t bs_ru64be(bs_t *s);
bool bs_rbool(bs_t *s);

uint32_t bs_ru24le(bs_t *s);

uint16_t bs_ru16le(bs_t *s);
uint32_t bs_ru32le(bs_t *s);
uint64_t bs_ru64le(bs_t *s);
float bs_rf32le(bs_t *s);

uint32_t bs_rvaru32(bs_t *s);
int32_t bs_rvari32(bs_t *s);
uint64_t bs_rvaru64(bs_t *s);
int64_t bs_rvari64(bs_t *s);

const uint8_t *bs_rbytes(bs_t *s, size_t n);
void bs_rcopy(bs_t *s, void *dst, size_t n);

size_t bs_rstr_be(bs_t *s, char *dst, size_t dst_cap);
size_t bs_rstr_var(bs_t *s, char *dst, size_t dst_cap);

void bs_wu8(bs_t *s, uint8_t v);
void bs_wu16be(bs_t *s, uint16_t v);
void bs_wu32be(bs_t *s, uint32_t v);
void bs_wu64be(bs_t *s, uint64_t v);
void bs_wbool(bs_t *s, bool v);

void bs_wu24le(bs_t *s, uint32_t v);

void bs_wu16le(bs_t *s, uint16_t v);
void bs_wu32le(bs_t *s, uint32_t v);
void bs_wu64le(bs_t *s, uint64_t v);
void bs_wf32le(bs_t *s, float v);

void bs_wvaru32(bs_t *s, uint32_t v);
void bs_wvari32(bs_t *s, int32_t v);
void bs_wvaru64(bs_t *s, uint64_t v);
void bs_wvari64(bs_t *s, int64_t v);

void bs_wbytes(bs_t *s, const void *src, size_t n);
void bs_wstr_be(bs_t *s, const char *src);
void bs_wstr_var(bs_t *s, const char *src);

uint8_t *bs_wreserve(bs_t *s, size_t n);

#endif