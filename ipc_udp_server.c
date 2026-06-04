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

#define CMD_DISCOVER    'C'
#define CMD_INFO        'I'
#define CMD_GETCFG      'G'
#define CMD_CFGDATA     'K'
#define CMD_ERROR       'E'
#define CMD_GETSTREAM1  'S'
#define CMD_BUNDLE      'B'

/* -------------------------
 * safe get (industrial)
 * ------------------------- */
static void get_cfg(const char *key, const char *def, char *out, int size)
{
    char tmp[128] = {0};

    if (nbus_get(key, tmp, sizeof(tmp)) == NBUS_RESULT_OK && tmp[0] != '\0')
        strncpy(out, tmp, size - 1);
    else
        strncpy(out, def, size - 1);

    out[size - 1] = 0;
}

/* -------------------------
 * get ip
 * ------------------------- */
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

        struct sockaddr_in *a = (struct sockaddr_in *)ifa->ifa_addr;
        struct sockaddr_in *m = (struct sockaddr_in *)ifa->ifa_netmask;

        inet_ntop(AF_INET, &a->sin_addr, ip, 32);
        inet_ntop(AF_INET, &m->sin_addr, mask, 32);

        strcpy(ifname, ifa->ifa_name);

        freeifaddrs(ifaddr);
        return 0;
    }

    freeifaddrs(ifaddr);
    return -1;
}

/* =========================
 * MAIN
 * ========================= */
int main(void)
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

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
        perror("bind");
        return -1;
    }

    printf("IPC UDP Server Running...\n");

    while (1)
    {
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *)&client, &client_len);

        if (n <= 0)
            continue;

        buf[n] = 0;

        /* =========================
         * DISCOVER
         * ========================= */
        if (buf[0] == CMD_DISCOVER)
        {
            if (get_first_ipv4(ifname, ip, mask) < 0)
            {
                printf("No Network Interface\n");
                continue;
            }

            char reply[256];
            reply[0] = CMD_INFO;

            snprintf(reply + 1, sizeof(reply) - 1,
                     "{\"type\":\"ipc\",\"ip\":\"%s\"}", ip);

            sendto(sock, reply, strlen(reply + 1) + 1, 0,
                   (struct sockaddr *)&client, client_len);

            printf("[IPC] DISCOVER -> %s\n", ip);
        }

        /* =========================
         * SINGLE CONFIG
         * ========================= */
        else if (buf[0] == CMD_GETCFG)
        {
            char key[128] = {0};
            char value[128] = {0};

            strncpy(key, buf + 1, sizeof(key) - 1);

            nbus_initial();

            if (nbus_get(key, value, sizeof(value)) == NBUS_RESULT_OK)
            {
                char reply[512];

                reply[0] = CMD_CFGDATA;

                snprintf(reply + 1, sizeof(reply) - 1,
                         "{\"key\":\"%s\",\"value\":\"%s\"}",
                         key, value);

                sendto(sock, reply, strlen(reply + 1) + 1, 0,
                       (struct sockaddr *)&client, client_len);
            }
            else
            {
                char reply[128];
                reply[0] = CMD_ERROR;

                snprintf(reply + 1, sizeof(reply) - 1,
                         "{\"error\":\"nbus_get failed\"}");

                sendto(sock, reply, strlen(reply + 1) + 1, 0,
                       (struct sockaddr *)&client, client_len);
            }

            nbus_release();
        }

        /* =========================
         * STREAM1 BUNDLE (BEST)
         * ========================= */
        else if (buf[0] == CMD_BUNDLE || buf[0] == CMD_GETSTREAM1)
        {
            char codec[64];
            char resolution[64];
            char bitrate[64];
            char framerate[64];
            char gop[64];
            char rate_control[64];
            char profile[64];
            char quality[64];
            char snapshot[64];

            nbus_initial();

            get_cfg("encode.stream1.codec", "h265", codec, sizeof(codec));
            get_cfg("encode.stream1.resolution", "1920x1080", resolution, sizeof(resolution));
            get_cfg("encode.stream1.bit_rate", "2048", bitrate, sizeof(bitrate));
            get_cfg("encode.stream1.frame_rate", "30", framerate, sizeof(framerate));
            get_cfg("encode.stream1.gop", "100", gop, sizeof(gop));
            get_cfg("encode.stream1.rate_control", "vbr", rate_control, sizeof(rate_control));
            get_cfg("encode.stream1.profile", "main", profile, sizeof(profile));
            get_cfg("encode.stream1.quality", "50", quality, sizeof(quality));
            get_cfg("encode.stream1.jpeg_snapshot", "", snapshot, sizeof(snapshot));

            char reply[1024];
            reply[0] = CMD_CFGDATA;

            snprintf(reply + 1, sizeof(reply) - 1,
                     "{"
                     "\"codec\":\"%s\","
                     "\"resolution\":\"%s\","
                     "\"bit_rate\":\"%s\","
                     "\"frame_rate\":\"%s\","
                     "\"gop\":\"%s\","
                     "\"rate_control\":\"%s\","
                     "\"profile\":\"%s\","
                     "\"quality\":\"%s\","
                     "\"jpeg_snapshot\":\"%s\""
                     "}",
                     codec, resolution, bitrate, framerate,
                     gop, rate_control, profile, quality, snapshot);

            sendto(sock, reply, strlen(reply + 1) + 1, 0,
                   (struct sockaddr *)&client, client_len);

            printf("[IPC] STREAM1 BUNDLE SENT\n");

            nbus_release();
        }

        /* =========================
         * UNKNOWN
         * ========================= */
        else
        {
            printf("[IPC] UNKNOWN CMD: %c\n", buf[0]);
        }
    }

    close(sock);
    return 0;
}