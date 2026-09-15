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

int raknet_server_init(struct raknet_server *server, uint16_t port)
{
    memset(server, 0, sizeof(*server));
    server -> port = port;
    server -> guid = random_guid();
    server -> start_ms = mr_now_ms();
    server -> family = AF_INET6;
    server -> fd = socket(AF_INET6, SOCK_DGRAM, 0);

    if(server -> fd >= 0)
    {
        int off = 0;
        if(setsockopt(server -> fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)) < 0)
        {
            log_warn("Dualstack unavailable, falling back to IPv4 only: %s", strerror(errno));
            close(server -> fd);
            server -> fd = -1;
        }
    }

    if(server -> fd < 0)
    {
        server -> family = AF_INET;
        server -> fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(server -> fd < 0)
        {
            log_error("Failed to create socket: %s", strerror(errno));
            return -1;
        }
    }
    log_warn("IPv6 unavailable, using IPv4 only");

    int on = 1;
    setsockopt(server -> fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    int rcvbuf = 1 << 20;
    setsockopt(server -> fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    int flags = fcntl(server -> fd, F_GETFL, 0);
    fcntl(server -> fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_storage sa;
    socklen_t sa_len;
    memset(&sa, 0, sizeof(sa));

    if(server -> family == AF_INET6)
    {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&sa;
        s6 -> sin6_family = AF_INET6;
        s6 -> sin6_addr = in6addr_any;
        s6 -> sin6_port = htons(port);
        sa_len = sizeof(*s6);
    }
    else
    {
        struct sockaddr_in *s4 = (struct sockaddr_in *)&sa;
        s4 -> sin_family = AF_INET;
        s4 -> sin_addr.s_addr = INADDR_ANY;
        s4 -> sin_port = htons(port);
        sa_len = sizeof(*s4);
    }

    if(bind(server -> fd, (struct sockaddr *)&sa, sa_len) < 0)
    {
        log_error("bind %u: %s", port, strerror(errno));
        close(server -> fd);
        server -> fd = -1;
        return -1;
    }

    raknet_server_build_motd(server, "mrock", "mrock", 0, 20);
    log_info("RakNet server listening on port %u guid=%016llx", port, (unsigned long long)server -> guid);
    return 0;
}

void raknet_server_close(raknet_server_t *server)
{
    if(server -> fd >= 0)
        close(server -> fd);
    
    server -> fd = -1;
}

int raknet_server_send(raknet_server_t *server, const mr_addr_t *to, const void *buf, size_t len)
{
    struct sockaddr_storage ss;
    socklen_t ss_len;

    if(mr_addr_to_sockaddr(to, &ss, &ss_len) < 0)
        return -1;

    if(server -> family == AF_INET6 && ss.ss_family == AF_INET)
    {
        const struct sockaddr_in *in = (const struct sockaddr_in *)&ss;
        struct sockaddr_in6 mapped;

        memset(&mapped, 0, sizeof(mapped));
        mapped.sin6_family = AF_INET6;
        mapped.sin6_port = in -> sin_port;

        uint8_t *mapped_ip = (uint8_t *)&mapped.sin6_addr;
        mapped_ip[10] = 0xff;
        mapped_ip[11] = 0xff;
        memcpy(mapped_ip + 12, &in -> sin_addr.s_addr, 4);
        memcpy(&ss, &mapped, sizeof(mapped));
        ss_len = sizeof(mapped);
    }

    ssize_t sent = sendto(server -> fd, buf, len, 0, (struct sockaddr *)&ss, ss_len);

    if(sent < 0)
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            log_debug("sendto %s", strerror(errno));
        
        return -1;
    }

    server -> tx_datagrams++;
    return 0;
}

void raknet_server_build_motd(raknet_server_t *server, const char *name, const char *sub_name, int online, int max_players)
{
    snprintf(server -> motd, sizeof(server -> motd), 
         "MCPE;%s;%d;%s;%d;%d;%llu;%s;Survival;1;%u;%u;",
         name,
         MCROCK_VERSION,
         MCROCK_VERSION_STRING,
         online,
         max_players,
         (unsigned long long)server->guid,
         sub_name,
         server->port,
         server->port);
}

void raknet_server_run(raknet_server_t *server)
{
    uint8_t recv_buf[RECV_BUF_SIZE];
    uint64_t next_tick = mr_now_ms();

    server -> running = true;

    while(server -> running)
    {
        for(;;)
        {
            struct sockaddr_storage ss;
            socklen_t ss_len = sizeof(ss);
            ssize_t recv_len = recvfrom(server -> fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&ss, &ss_len);
            
            if(recv_len < 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                
                if(errno == EINTR)
                    continue;
                
                log_debug("recvfrom %s", strerror(errno));
                break;
            }

            if(recv_len == 0)
                continue;

            server -> rx_datagrams++;
            mr_addr_t from;

            if(mr_addr_from_sockaddr(&from, &ss) < 0)
                continue;

            if(raknet_offline_handle_packet(server, recv_buf, (size_t)recv_len, &from))
                continue;

            server -> bad_packets++;
        }

        next_tick += TICK_MS;
        uint64_t now = mr_now_ms();
        int wait_ms = (next_tick > now) ? (int)(next_tick - now) : 0;

        if(wait_ms == 0)
            next_tick = now;

        struct pollfd pfd = { .fd = server -> fd, .events = POLLIN };
        poll(&pfd, 1, wait_ms);
    }
}