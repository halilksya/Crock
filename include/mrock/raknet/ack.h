#ifndef MCROCK_RAKNET_ACK_H
#define MCROCK_RAKNET_ACK_H

#include "mrock/raknet/proto.h"
#include "mrock/util/bstream.h"

#define RAKNET_MAX_ACK_SEQUENCES 2048

size_t raknet_ack_encode(bs_t *stream, const uint32_t *sequences, size_t count, bool is_ack, size_t mtu);
size_t raknet_ack_decode(bs_t *stream, uint32_t *out_sequences, size_t out_capacity);

#endif
