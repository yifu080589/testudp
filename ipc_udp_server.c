#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdlib.h>

#include "nbus.h"

#define PORT 37000
#define BUF_SIZE 2048

#define CMD_DISCOVER 'C'
#define CMD_INFO     'I'
#define CMD_GETCFG   'G'
#define CMD_CFGDATA  'K'
#define CMD_ERROR    'E'

/* 取得第一個 IPv4（避免寫死 eth0 / enp0s8） */
int get_first_ipv4(char *ifname, char *ip, char *mask)
{
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1)
        return -1;

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        struct sockaddr_in *a = (struct sockaddr_in*)ifa->ifa_addr;
        struct sockaddr_in *m = (struct sockaddr_in*)ifa->ifa_netmask;

        inet_ntop(AF_INET, &a->sin_addr, ip, 32);
        inet_ntop(AF_INET, &m->sin_addr, mask, 32);

        strcpy(ifname, ifa->ifa_name);

        freeifaddrs(ifaddr);
        return 0;
    }

    freeifaddrs(ifaddr);
    return -1;
}

int main()
{
    int sock;
    struct sockaddr_in local, client;
    socklen_t client_len = sizeof(client);

    char buf[BUF_SIZE];

    char ifname[32];
    char ip[32];
    char mask[32];

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0)
    {
        perror("bind");
        return -1;
    }

    printf("IPC running...\n");

    while (1)
    {
        int n = recvfrom(sock,
                         buf,
                         sizeof(buf) - 1,
                         0,
                         (struct sockaddr*)&client,
                         &client_len);

        if (n <= 0)
            continue;

        buf[n] = 0;

        /* =========================
           DISCOVER
           ========================= */
        if (buf[0] == CMD_DISCOVER)
        {
            if (get_first_ipv4(ifname, ip, mask) < 0)
            {
                printf("no network interface\n");
                continue;
            }

            char reply[256];
            reply[0] = CMD_INFO;

            snprintf(reply + 1,
                     sizeof(reply) - 1,
                     "{\"type\":\"ipc\",\"ip\":\"%s\"}",
                     ip);

            sendto(sock,
                   reply,
                   strlen(reply + 1) + 1,
                   0,
                   (struct sockaddr*)&client,
                   client_len);

            printf("[IPC] DISCOVER -> INFO (%s)\n", ip);
        }

        /* =========================
           GET CONFIG
           ========================= */
        else if (buf[0] == CMD_GETCFG)
        {
            char key[256];
            char value[256];

            memset(key, 0, sizeof(key));
            memset(value, 0, sizeof(value));

            strcpy(key, buf + 1);

            printf("[IPC] GETCFG: %s\n", key);

            nbus_initial();

            if (nbus_get(key,
                         value,
                         sizeof(value)) == NBUS_RESULT_OK)
            {
                char reply[512];

                reply[0] = CMD_CFGDATA;

                snprintf(reply + 1,
                         sizeof(reply) - 1,
                         "{\"key\":\"%s\",\"value\":\"%s\"}",
                         key,
                         value);

                sendto(sock,
                       reply,
                       strlen(reply + 1) + 1,
                       0,
                       (struct sockaddr*)&client,
                       client_len);

                printf("[IPC] SEND CFG OK\n");
            }
            else
            {
                char reply[128];

                reply[0] = CMD_ERROR;

                snprintf(reply + 1,
                         sizeof(reply) - 1,
                         "{\"error\":\"nbus_get failed\"}");

                sendto(sock,
                       reply,
                       strlen(reply + 1) + 1,
                       0,
                       (struct sockaddr*)&client,
                       client_len);

                printf("[IPC] GETCFG FAIL\n");
            }

            nbus_release();
        }

        /* =========================
           UNKNOWN
           ========================= */
        else
        {
            printf("[IPC] UNKNOWN CMD: %c\n", buf[0]);
        }
    }

    close(sock);
    return 0;
}