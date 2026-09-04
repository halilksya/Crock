#include "mrock/util/bstream.h"

#include <stdlib.h>
#include <string.h> 

void bs_read_init(bs_t *s, const void *buf, size_t len)
{
    s->buf = (uint8_t *)buf;
    s->cap = len;
    s->len = len;
    s->pos = 0;
    s->err = 0;
    s->owned = 0;
}

void bs_write_init(bs_t *s, void *buf, size_t cap)
{
    s->buf = (uint8_t *)buf;
    s->cap = cap;
    s->len = 0;
    s->pos = 0;
    s->err = 0;
    s->owned = 0;
}

int bs_dyn_init(bs_t *s, size_t initial_cap)
{
    if(initial_cap <64) 
        initial_cap = 64;

    s->buf = (uint8_t *)malloc(initial_cap);
    if(!s->buf)
    {
        memset(s, 0, sizeof(*s));
        s->err = 1;
        return -1;
    }
    s->cap = initial_cap;
    s->len = 0;
    s->pos = 0;
    s->err = 0;
    s->owned = 1;
    return 0;
}

void bs_free(bs_t *s)
{
    if(s->owned && s->buf)
        free(s->buf);
    
    memset(s, 0, sizeof(*s));
}

static inline int rd_ok(bs_t *s, size_t n)
{
    if(s->err)
        return 0;
    
    if(s->pos + n > s->len)
    {
        s->err = 1;
        return 0;
    }
    
    return 1;
}

static int wr_ok(bs_t *s, size_t n)
{
    if(s->err)
        return 0;

    if(s->pos + n <= s->cap)
        return 1;

    if(!s->owned)
    {
        s->err = 1;
        return 0;
    }

    size_t need = s->pos + n;
    size_t cap = s->cap ? s->cap : 64;

    while(need > cap)
    {
        if(cap > SIZE_MAX / 2)
        {
            s->err = 1;
            return 0;
        }
        cap *= 2;
    }

    uint8_t *nb = (uint8_t *)realloc(s->buf, cap);
    
    if(!nb)
    {
        s->err = 1;
        return 0;
    }

    s->buf = nb;
    s->cap = cap;
    return 1;
}

static inline void wr_bump(bs_t *s)
{
    if(s->pos > s->len)
        s->len = s->pos;
}

uint8_t bs_ru8(bs_t *s)
{
    if(!rd_ok(s, 1))
        return 0;
    
    return s->buf[s->pos++];
}

bool bs_rbool(bs_t *s)
{
    return bs_ru8(s) != 0;
}

uint16_t bs_ru16be(bs_t *s)
{
    if(!rd_ok(s,2))
        return 0;

    uint16_t v = (uint16_t)((uint16_t)s->buf[s->pos] << 8 | s->buf[s->pos + 1]);
    s->pos += 2;
    return v;
}

uint32_t bs_ru32be(bs_t *s)
{
    if(!rd_ok(s,4))
        return 0;

    const uint8_t *p = s->buf + s->pos;
    uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    s->pos += 4;
    return v;
}

uint64_t bs_ru64be(bs_t *s)
{
    if(!rd_ok(s,8))
        return 0;

    const uint8_t *p = s->buf + s->pos;
    uint64_t v = 0;
    for(int i = 0; i < 8; i++)
    {
        v = (v << 8) | p[i];
    }
    s->pos += 8;
    return v;
}

uint32_t bs_ru24le(bs_t *s)
{
    if(!rd_ok(s,3))
        return 0;

    const uint8_t *p = s->buf + s->pos;
    uint32_t v = ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    s->pos += 3;
    return v;
}

uint16_t bs_ru16le(bs_t *s)
{
    if(!rd_ok(s,2))
        return 0;

    uint16_t v = (uint16_t)(s->buf[s->pos] | (uint16_t)s->buf[s->pos + 1] << 8);  
    s->pos += 2;
    return v;
}

uint32_t bs_ru32le(bs_t *s)
{
    if(!rd_ok(s,4))
        return 0;

    const uint8_t *p = s->buf + s->pos;
    uint32_t v = ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    s->pos += 4;
    return v;
}

uint64_t bs_ru64le(bs_t *s)
{
    if(!rd_ok(s,8))
        return 0;

    const uint8_t *p = s->buf + s->pos;
    uint64_t v = 0;
    for(int i = 7; i >= 0; i--)
    {
        v = (v << 8) | p[i];
    }
    s->pos += 8;
    return v;
}

float bs_rf32le(bs_t *s)
{
    uint32_t bits = bs_ru32le(s);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

uint64_t bs_rvaru64(bs_t *s)
{
    uint64_t v = 0;
    for(int shift = 0; shift < 70; shift += 7)
    {   
        if(!rd_ok(s, 1))
            return 0;
        
        uint8_t b = s->buf[s->pos++];
        v |= (uint64_t)(b & 0x7F) << shift;
        
        if((b & 0x80) == 0)
            return v;
            
    }
    s->err = 1;
    return 0;
}

uint32_t bs_rvaru32(bs_t *s)
{
    uint32_t v = 0;

    for(int shift = 0; shift < 35; shift += 7)
    {      
        if(!rd_ok(s, 1))
            return 0;
        
        uint8_t b = s->buf[s->pos++];
        v |= (uint32_t)(b & 0x7F) << shift;
        
        if((b & 0x80) == 0)
            return v;
            
    }
    s->err = 1;
    return 0;
}

int32_t bs_rvari32(bs_t *s)
{
    uint32_t u = bs_rvaru32(s);
    
    return (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
}

int64_t bs_rvari64(bs_t *s)
{
    uint64_t u = bs_rvaru64(s);
    
    return (int64_t)(u >> 1) ^ -(int64_t)(u & 1);
}

const uint8_t *bs_rbytes(bs_t *s, size_t n)
{
    if(!rd_ok(s, n))
        return NULL;

    const uint8_t *p = s->buf + s->pos;
    s->pos += n;
    return p;
}

void bs_rcopy(bs_t *s, void *dst, size_t n)
{
    const uint8_t *p = bs_rbytes(s, n);
    if(p)
        memcpy(dst, p, n);
    else
        memset(dst, 0, n);

}

static size_t read_str(bs_t *s, size_t n, char *dst, size_t dst_cap)
{
    if(dst_cap == 0)
        return 0;

    const uint8_t *p = bs_rbytes(s, n);
    
    if(!p)
    {
        dst[0] = '\0';
        return 0;
    }

    size_t copy = n < dst_cap - 1 ? n : dst_cap - 1;
    memcpy(dst, p, copy);
    dst[copy] = '\0';
    return copy;
}

size_t bs_rstr_be(bs_t *s, char *dst, size_t dst_cap)
{
    return read_str(s, bs_ru16be(s), dst, dst_cap);
}

size_t bs_rstr_var(bs_t *s, char *dst, size_t dst_cap)
{
    return read_str(s, bs_rvaru32(s), dst, dst_cap);
}

uint8_t *bs_wreserve(bs_t *s, size_t n)
{
    if(!wr_ok(s, n))
        return NULL;

    uint8_t *p = s->buf + s->pos;
    s->pos += n;
    wr_bump(s);
    return p;
}

void bs_wu8(bs_t *s, uint8_t v)
{
    if(!wr_ok(s, 1))
        return;
    
    s->buf[s->pos++] = v;
    wr_bump(s);
}

void bs_wbool(bs_t *s, bool v)
{
    bs_wu8(s, v ? 1 : 0);
}

void bs_wu16be(bs_t *s, uint16_t v)
{
    if(!wr_ok(s, 2))
        return;

    s->buf[s->pos++] = (uint8_t)(v >> 8);
    s->buf[s->pos++] = (uint8_t)(v & 0xFF);
    wr_bump(s);
}

void bs_wu32be(bs_t *s, uint32_t v)
{
    if(!wr_ok(s, 4))
        return;

    s->buf[s->pos++] = (uint8_t)(v >> 24);
    s->buf[s->pos++] = (uint8_t)((v >> 16) & 0xFF);
    s->buf[s->pos++] = (uint8_t)((v >> 8) & 0xFF);
    s->buf[s->pos++] = (uint8_t)v;
    wr_bump(s);
}

void bs_wu64be(bs_t *s, uint64_t v)
{
    if(!wr_ok(s, 8))
        return;

    for(int i = 7; i >= 0; i--)
    {
        s->buf[s->pos++] = (uint8_t)(v >> (i * 8));
    }
    wr_bump(s);
}

void bs_wu24le(bs_t *s, uint32_t v)
{
    if(!wr_ok(s, 3))
        return;

    s->buf[s->pos++] = (uint8_t)v;
    s->buf[s->pos++] = (uint8_t)(v >> 8);
    s->buf[s->pos++] = (uint8_t)(v >> 16);
    wr_bump(s);
}

void bs_wu16le(bs_t *s, uint16_t v)
{
    if(!wr_ok(s, 2))
        return;

    s->buf[s->pos++] = (uint8_t)v;
    s->buf[s->pos++] = (uint8_t)(v >> 8);
    wr_bump(s);
}

void bs_wu32le(bs_t *s, uint32_t v)
{
    if(!wr_ok(s, 4))
        return;

    s->buf[s->pos++] = (uint8_t)v;
    s->buf[s->pos++] = (uint8_t)(v >> 8);
    s->buf[s->pos++] = (uint8_t)(v >> 16);
    s->buf[s->pos++] = (uint8_t)(v >> 24);
    wr_bump(s);
}

void bs_wu64le(bs_t *s, uint64_t v)
{
    if(!wr_ok(s, 8))
        return;

    for(int i = 0; i < 8; i++)
    {
        s->buf[s->pos++] = (uint8_t)(v >> (i * 8));
    }
    wr_bump(s);
}

void bs_wf32le(bs_t *s, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    bs_wu32le(s, bits);
}

void bs_wvaru64(bs_t *s, uint64_t v)
{
    do
    {
        uint8_t byte = (uint8_t)(v & 0x7F);
        v >>= 7;
        if(v != 0)
            byte |= 0x80;
        bs_wu8(s, byte);
    } while(v != 0 && !s->err);
}

void bs_wvaru32(bs_t *s, uint32_t v)
{
    bs_wvaru64(s, v);
}

void bs_wvari32(bs_t *s, int32_t v)
{
    bs_wvaru32(s, ((uint32_t)v << 1) ^ (uint32_t)(v >> 31));
}

void bs_wvari64(bs_t *s, int64_t v)
{
    bs_wvaru64(s, ((uint64_t)v << 1) ^ (uint64_t)(v >> 63));
}

void bs_wbytes(bs_t *s, const void *data, size_t n)
{
    if(n == 0)
        return;
    
    if(!wr_ok(s, n))
        return;

    memcpy(s->buf + s->pos, data, n);
    s->pos += n;
    wr_bump(s);
}

void bs_wstr_be(bs_t *s, const char *str)
{
    size_t n = strlen(str);
    if(n > 0xFFFF)
    {
        s->err = 1;
        return;
    }

    bs_wu16be(s, (uint16_t)n);
    bs_wbytes(s, str, n);
}

void bs_wstr_var(bs_t *s, const char *str)
{
    size_t n = strlen(str);
    bs_wvaru32(s, (uint32_t)n);
    bs_wbytes(s, str, n);
}