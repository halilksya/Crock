#ifndef MCROCK_RAKNET_SERVER_H
#define MCROCK_RAKNET_SERVER_H

#include "mrock/raknet/proto.h"
#include "mrock/net/addr.h"

#define RAKNET_MOTD_MAX 256

//Must stay a power of two, lookup masks with BUCKETS - 1 | İkinin kuvveti kalmak zorunda, arama BUCKETS - 1 ile maskeliyor
#define RAKNET_SESSION_BUCKETS 512
#define RAKNET_MAX_SESSIONS 100

struct raknet_session;

typedef struct raknet_server
{
    int fd;
    //AF_INET6 for dual-stack, AF_INET when IPv6 is unavailable; send path checks this | Dual-stack ise AF_INET6, IPv6 yoksa AF_INET; gönderim yolu buna bakıyor
    int family;
    uint16_t port;
    uint64_t guid;
    uint64_t start_ms;

    char motd[RAKNET_MOTD_MAX];

    //Intrusive hash table, chained through session -> hash_next | Intrusive hash tablosu, session -> hash_next üzerinden zincirleniyor
    struct raknet_session *sessions[RAKNET_SESSION_BUCKETS];
    size_t session_count;

    uint64_t rx_datagrams;
    uint64_t tx_datagrams;
    uint64_t bad_packets;

    bool running;
} raknet_server_t;

struct raknet_session *raknet_server_find_session(raknet_server_t *server, const mr_addr_t *addr);

int raknet_server_add_session(raknet_server_t *server, struct raknet_session *session);
void raknet_server_remove_session(raknet_server_t *server, struct raknet_session *session);

int raknet_server_init(raknet_server_t *server, uint16_t port);
void raknet_server_close(raknet_server_t *server);

//Blocks until running goes false | running false olana kadar bloklanıyor
void raknet_server_run(raknet_server_t *server);

int raknet_server_send(raknet_server_t *server, const mr_addr_t *to, const void *buf, size_t len);

//Semicolon-separated MOTD; a missing field hides the server from the client's list | Noktalı virgülle ayrılmış MOTD; eksik alan sunucuyu client'ın listesinde göstermiyor
void raknet_server_build_motd(raknet_server_t *server, const char *name, const char *sub_name, int online, int max_players);

#endif