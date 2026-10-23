#include "mrock/net/addr.h"

#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

int mr_addr_from_sockaddr(mr_addr_t *addr, const struct sockaddr_storage *ss)
{
    memset(addr, 0, sizeof(*addr));

    if(ss -> ss_family == AF_INET)
    {
        const struct sockaddr_in *in = (const struct sockaddr_in *)ss;
        addr -> version = 4;
        addr -> port = ntohs(in -> sin_port);
        memcpy(addr -> ip, &in -> sin_addr, sizeof(in -> sin_addr));
        return 0;
    }

    if(ss -> ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)ss;

        //The dual-stack socket hands IPv4 clients over like this; treating them as v6 would break them | Dual-stack soket IPv4 client'ları böyle veriyor; v6 sanmak onları koparır
        if(IN6_IS_ADDR_V4MAPPED(&in6 -> sin6_addr))
        {
            addr -> version = 4;
            addr -> port = ntohs(in6 -> sin6_port);
            memcpy(addr -> ip, ((const uint8_t *)&in6 -> sin6_addr) + 12, 4);
            return 0;
        }

        addr -> version = 6;
        addr -> port = ntohs(in6 -> sin6_port);
        addr -> flowinfo = ntohl(in6 -> sin6_flowinfo);
        //scope_id is already in host byte order, unlike flowinfo | scope_id zaten host byte order'da, flowinfo'nun aksine
        addr -> scope_id = in6 -> sin6_scope_id;
        memcpy(addr -> ip, &in6 -> sin6_addr, 16);
        return 0;
    }
    return -1;
}

int mr_addr_to_sockaddr(const mr_addr_t *addr, struct sockaddr_storage *ss, socklen_t *ss_len)
{
    memset(ss, 0, sizeof(*ss));

    if(addr -> version == 4)
    {
        struct sockaddr_in *in = (struct sockaddr_in *)ss;
        in -> sin_family = AF_INET;
        in -> sin_port = htons(addr -> port);
        memcpy(&in -> sin_addr.s_addr, addr -> ip, 4);
        *ss_len = sizeof(*in);
        return 0;
    }

    if(addr -> version == 6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ss;
        in6 -> sin6_family = AF_INET6;
        in6 -> sin6_port = htons(addr -> port);
        in6 -> sin6_flowinfo = htonl(addr -> flowinfo);
        in6 -> sin6_scope_id = addr -> scope_id;
        memcpy(&in6 -> sin6_addr, addr -> ip, 16);
        *ss_len = sizeof(*in6);
        return 0;
    }
    return -1;
}

void mr_addr_read(bs_t *s, mr_addr_t *addr)
{
    memset(addr, 0, sizeof(*addr));
    addr -> version = bs_ru8(s);

    if(addr -> version == 4)
    {
        //RakNet stores IPv4 bytes inverted; the cast is needed because ~ promotes to int | RakNet IPv4 baytlarını ters çevirerek tutuyor; cast gerekli çünkü ~ int'e yükseltiyor
        for(int i = 0; i < 4; i++)
            addr -> ip[i] = (uint8_t)(~bs_ru8(s));

        addr -> port = bs_ru16be(s);
        return;
    }

    if(addr -> version == 6)
    {
        //The AF_INET6 constant, we already know what it is | AF_INET6 sabiti, ne olduğunu zaten biliyoruz
        (void)bs_ru16le(s);
        addr -> port = bs_ru16be(s);
        addr -> flowinfo = bs_ru32be(s);
        bs_rcopy(s, addr -> ip, 16);
        addr -> scope_id = bs_ru32be(s);
        return;
    }
    s -> err = 1;
}

void mr_addr_write(bs_t *s, const mr_addr_t *addr)
{
    if(addr -> version == 4)
    {
        bs_wu8(s, 4);

        for(int i = 0; i < 4; i++)
            bs_wu8(s, (uint8_t)(~addr -> ip[i]));

        bs_wu16be(s, addr -> port);
        return;
    }

    if(addr -> version == 6)
    {
        bs_wu8(s, 6);
        //Windows' value, written little-endian; the platform's own constant would be wrong here | Windows'un değeri, little-endian yazılıyor; platformun kendi sabiti burada yanlış olur
        bs_wu16le(s, MR_AF_INET6_RAKNET);
        bs_wu16be(s, addr -> port);
        bs_wu32be(s, addr -> flowinfo);
        bs_wbytes(s, addr -> ip, 16);
        bs_wu32be(s, addr -> scope_id);
        return;
    }
    s -> err = 1;
}

bool mr_addr_equal(const mr_addr_t *a, const mr_addr_t *b)
{
    if(a -> version != b -> version || a -> port != b -> port)
        return false;

    size_t n = (a -> version == 4) ? 4 : 16;
    return memcmp(a -> ip, b -> ip, n) == 0;
}

//FNV-1a over the address bytes and the port | Adres baytları ve port üzerinde FNV-1a
uint32_t mr_addr_hash(const mr_addr_t *addr)
{
    uint32_t hash = 2166136261u;
    size_t n = (addr -> version == 4) ? 4 : 16;

    for(size_t i = 0; i < n; i++)
    {
        hash ^= addr -> ip[i];
        hash *= 16777619u;
    }

    hash ^= (uint32_t)(addr -> port & 0xFF);
    hash *= 16777619u;
    hash ^= (uint32_t)((addr -> port >> 8) & 0xFF);
    hash *= 16777619u;

    return hash;
}

void mr_addr_to_string(const mr_addr_t *addr, char *buf, size_t buf_len)
{
    char ip_str[INET6_ADDRSTRLEN] = "?";

    if(addr -> version == 4)
    {
        inet_ntop(AF_INET, addr -> ip, ip_str, sizeof(ip_str));
        snprintf(buf, buf_len, "%s:%u", ip_str, addr -> port);
    }
    else
    {
        inet_ntop(AF_INET6, addr -> ip, ip_str, sizeof(ip_str));
        snprintf(buf, buf_len, "[%s]:%u", ip_str, addr -> port);
    }
}
