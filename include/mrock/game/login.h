#ifndef MCROCK_GAME_LOGIN_H
#define MCROCK_GAME_LOGIN_H

#include "mrock/game/player.h"
#include "mrock/util/bstream.h"

int mc_login_parse(const uint8_t *body, size_t body_len, mc_player_identity_t *identity);

#endif