#ifndef MROCK_RAKNET_OFFLINE_H
#define MROCK_RAKNET_OFFLINE_H

#include "mrock/raknet/proto.h"
#include "mrock/net/addr.h"

struct raknet_server;

int raknet_offline_handle(struct raknet_server *server, const uint8_t *data, size_t len, const mr_addr_t *from);

#endif