#include "mrock/common.h"
#include "mrock/mc.h"
#include "mrock/raknet/server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Global because the signal handler has no other way to reach it | Global, çünkü sinyal işleyicisinin ona başka türlü erişmesi mümkün değil
static raknet_server_t g_server;

//Only flips a flag; the main loop notices on its next pass | Sadece bir bayrak çeviriyor; ana döngü sonraki turunda farkediyor
static void on_signal(int sig)
{
    (void)sig;
    g_server.running = false;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [-p port] [-n name] [-v] \n"
        " -p UDP port to listen on (default: 19132)\n"
        " -n server name shown in the MOTD\n"
        " -v enable debug logging\n", prog);
}

int main(int argc, char **argv)
{
    uint16_t port = 19132;
    const char *name = "mrock";

    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "-p") && i + 1 < argc)
            port = (uint16_t)atoi(argv[++i]);
        else if(!strcmp(argv[i], "-n") && i + 1 < argc)
            name = argv[++i];
        else if(!strcmp(argv[i], "-v"))
            log_set_level(LOG_DEBUG);
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    //UDP should never raise it, but ignoring turns a write failure into EPIPE instead of a kill | UDP'de çıkmaması lazım, ama yok saymak yazma hatasını süreci öldürmek yerine EPIPE'a çeviriyor
    signal(SIGPIPE, SIG_IGN);

    if(raknet_server_init(&g_server, port) < 0)
        return 1;

    //init already built a MOTD with the default name, this one applies -n | init varsayılan isimle bir MOTD kurdu, bu çağrı -n değerini uyguluyor
    raknet_server_build_motd(&g_server, name, name, 0, 20);

    log_info("mrock - target Minecraft %s (protocol %d)", MCROCK_VERSION_STRING, MCROCK_VERSION);

    raknet_server_run(&g_server);

    log_info("shutting down. rx=%llu tx=%llu bad=%llu",
            (unsigned long long)g_server.rx_datagrams,
            (unsigned long long)g_server.tx_datagrams,
            (unsigned long long)g_server.bad_packets);

    raknet_server_close(&g_server);
    return 0;
}
