#ifndef MCROCK_GAME_PLAYER_H
#define MCROCK_GAME_PLAYER_H

#include "mrock/common.h"

#define MC_MAX_NAME_LEN 64
#define MC_MAX_UUID_LEN 40
#define MC_MAX_XUID_LEN 24

typedef struct
{
    char name[MC_MAX_NAME_LEN];
    char uuid[MC_MAX_UUID_LEN];
    char xuid[MC_MAX_XUID_LEN];
} mc_player_identity_t;

#endif
