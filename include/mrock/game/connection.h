#ifndef MCROCK_GAME_CONNECTION_H
#define MCROCK_GAME_CONNECTION_H

#include "mrock/game/batch.h"
#include "mrock/game/player.h"

#define MC_COMPRESSION_THRESHOLD 1

typedef enum
{
    MC_STATE_HANDSHAKE = 0,
    MC_STATE_LOGIN,
    MC_STATE_RESOURCE_PACKS,
    MC_STATE_SPAWNING,
    MC_STATE_PLAYING,
} mc_connection_state_t;

typedef int(*mc_send_callback_t)(void *transport, const uint8_t *data, size_t len);

typedef void(*mc_close_callback_t)(void *transport, const char *reason);

typedef struct
{
    void *transport;

    mc_send_callback_t send;
    mc_close_callback_t close;
    mc_connection_state_t state;

    bool compression_enabled;
    uint32_t client_protocol;
    mc_player_identity_t player;
}   mc_connection_t;

void mc_connection_init(mc_connection_t *connection, void *transport, mc_send_callback_t send, mc_close_callback_t close);

void mc_connection_on_batch(mc_connection_t *connection, const uint8_t *data, size_t len);

int mc_connection_send_packet(mc_connection_t *connection, uint32_t packet_id, const uint8_t *body, size_t body_len);

void mc_handle_packet(mc_connection_t *connection, uint32_t packet_id, const uint8_t *body, size_t body_len);

#endif