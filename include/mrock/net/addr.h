#ifndef MCROCK_NET_ADDR_H
#define MCROCK_NET_ADDR_H

#include "mrock/common.h"
#include "mrock/util/bstream.h"

#include <netinet/in.h>
#include <sys/socket.h>

//RakNet writes Windows' AF_INET6 value on the wire, not the platform's own | RakNet wire üzerinde Windows'un AF_INET6 değerini yazıyor, platformun kendi değerini değil
#define MR_AF_INET6_RAKNET 23

typedef struct
{
    uint8_t version; // 4 or 6 | 4 veya 6
    uint8_t ip[16];  //Only the first 4 bytes are used for IPv4 | IPv4 için yalnızca ilk 4 bayt kullanılıyor
    uint16_t port;
    uint32_t flowinfo;
    uint32_t scope_id;
} mr_addr_t;

//Normalizes IPv4-mapped IPv6 (::ffff:a.b.c.d) down to IPv4 | IPv4-mapped IPv6 adreslerini (::ffff:a.b.c.d) IPv4'e indiriyor
int mr_addr_from_sockaddr(mr_addr_t *addr, const struct sockaddr_storage *ss);
int mr_addr_to_sockaddr(const mr_addr_t *addr, struct sockaddr_storage *ss, socklen_t *ss_len);

//RakNet wire format: IPv4 address bytes are bit-inverted (~b) | RakNet wire formatı: IPv4 adres baytları ters çevrilerek (~b) yazılıyor
void mr_addr_read(bs_t *bs, mr_addr_t *addr);
void mr_addr_write(bs_t *bs, const mr_addr_t *addr);

bool mr_addr_equal(const mr_addr_t *a, const mr_addr_t *b);
uint32_t mr_addr_hash(const mr_addr_t *addr);

void mr_addr_to_string(const mr_addr_t *addr, char *buf, size_t buf_len);

#endif