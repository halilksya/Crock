#ifndef MCROCK_GAME_PLAYER_H
#define MCROCK_GAME_PLAYER_H

#include "mrock/common.h"

#define MC_MAX_NAME_LEN 64
#define MC_MAX_UUID_LEN 40
#define MC_MAX_XUID_LEN 24

//Filled from the Login chain JWT. With PlayFab auth uuid is the 'sub' claim, not a dashed UUID | Login chain JWT'sinden doldurulur; PlayFab kimliğinde uuid 'sub' alanı oluyor, tireli UUID değil
typedef struct
{
    char name[MC_MAX_NAME_LEN];
    char uuid[MC_MAX_UUID_LEN];
    char xuid[MC_MAX_XUID_LEN];
} mc_player_identity_t;

#endif