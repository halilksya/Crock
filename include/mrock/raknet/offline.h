#ifndef MROCK_RAKNET_OFFLINE_H
#define MROCK_RAKNET_OFFLINE_H

#include "mrock/raknet/proto.h"
#include "mrock/net/addr.h"

struct raknet_server;

//Pre-connection messages: ping and the two open-connection steps, handled without a session | Bağlantı öncesi mesajlar: ping ve iki open-connection adımı, session olmadan işleniyor
//Returns 1 if the packet was consumed here, 0 if the caller should route it to a session | Paketi burada tükettiyse 1, session'a yönlendirilmesi gerekiyorsa 0 dönüyor
int raknet_offline_handle(struct raknet_server *server, const uint8_t *data, size_t len, const mr_addr_t *from);

#endif