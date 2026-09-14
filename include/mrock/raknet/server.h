#ifndef MCROCK_RAKNET_SERVER_H
#define MCROCK_RAKNET_SERVER_H

#include "mrock/raknet/proto.h"
#include "mrock/net/addr.h"

#define RAKNET_MOTD_MAX 256

typedef struct raknet_server
{
    int fd;
    int family;
    uint16_t port;
    uint64_t guid;
    uint64_t start_ms;

    char motd[RAKNET_MOTD_MAX];

    uint64_t rx_datagrams;
    uint64_t tx_datagrams;
    uint64_t bad_packets;

    bool running;
} raknet_server_t;

int raknet_server_init(raknet_server_t *server, int family, uint16_t port);
void raknet_server_close(raknet_server_t *server);
void raknet_server_run(raknet_server_t *server);

int raknet_server_send(raknet_server_t *server, const mr_addr_t *to, const void *buf, size_t len);

void raknet_server_build_motd(raknet_server_t *server, const char *name, const char *sub_name, int online, int max_players);

#endif