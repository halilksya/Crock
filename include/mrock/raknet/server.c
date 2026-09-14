#include "mrock/raknet/server.h"
#include "mrock/raknet/offline.h"
#include "mrock/mc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TICK_MS 10
#define RECV_BUF_SIZE 2048

static uint64_t random_guid(void)
{
    uint64_t guid = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    
    if(f)
    {
        if(fread(&guid, sizeof(guid), 1, f) != sizeof(guid))
            guid = 0;
        fclose(f);
    }

    if(guid == 0)
        guid = ((uint64_t)mr_now_ms() << 20) ^ (uint64_t)getpid();

    return guid;
}
// Daha sonra devam edecem ^^'