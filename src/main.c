#include "mrock/common.h"
#include "mrock/mc.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Global because the signal handler has no other way to reach it | Global, çünkü sinyal işleyicisinin ona başka türlü erişmesi mümkün değil
static volatile sig_atomic_t g_running = 1;

//Only flips a flag; the main loop notices on its next pass | Sadece bir bayrak çeviriyor; ana döngü sonraki turunda farkediyor
static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [-p port] [-n name] [-v] \n"
        " -p port to listen on (default: 19132)\n"
        " -n server name shown to clients\n"
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
    //Ignoring turns a write to a closed socket into EPIPE instead of killing us | Yok saymak, kapalı sokete yazmayı süreci öldürmek yerine EPIPE'a çeviriyor
    signal(SIGPIPE, SIG_IGN);

    log_info("mrock - target Minecraft %s (protocol %d)", MCROCK_VERSION_STRING, MCROCK_VERSION);
    log_warn("no transport yet, nothing is listening (port %u, name %s)", port, name);

    (void)g_running;
    return 0;
}