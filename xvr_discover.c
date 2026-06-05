#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>

#define PORT        37000
#define BUF_SIZE    2048
#define DISCOVER_RETRY_SEC  2

#define CMD_DISCOVER    'C'
#define CMD_INFO        'I'
#define CMD_GETCFG      'G'
#define CMD_CFGDATA     'K'
#define CMD_BUNDLE      'B'
#define CMD_PING        'H'
#define CMD_PONG        'O'
#define CMD_NOTIFY      'N'
#define CMD_ERROR       'E'

static int json_get_string(const char *json, const char *field,
                           char *out, int outsize)
{
    char pattern[64];
    const char *start;
    const char *end;
    int len;

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", field);
    start = strstr(json, pattern);
    if (!start)
        return -1;

    start += strlen(pattern);
    end = strchr(start, '"');
    if (!end)
        return -1;

    len = (int)(end - start);
    if (len >= outsize)
        len = outsize - 1;

    memcpy(out, start, (size_t)len);
    out[len] = '\0';
    return 0;
}

static int is_bundle_json(const char *json)
{
    return strstr(json, "\"codec\":") != NULL;
}

static void print_stream1_config(const char *json)
{
    char codec[64];
    char resolution[64];
    char bit_rate[64];
    char frame_rate[64];
    char gop[64];
    char rate_control[64];
    char profile[64];
    char quality[64];
    char jpeg_snapshot[64];

    codec[0] = '\0';
    resolution[0] = '\0';
    bit_rate[0] = '\0';
    frame_rate[0] = '\0';
    gop[0] = '\0';
    rate_control[0] = '\0';
    profile[0] = '\0';
    quality[0] = '\0';
    jpeg_snapshot[0] = '\0';

    json_get_string(json, "codec", codec, sizeof(codec));
    json_get_string(json, "resolution", resolution, sizeof(resolution));
    json_get_string(json, "bit_rate", bit_rate, sizeof(bit_rate));
    json_get_string(json, "frame_rate", frame_rate, sizeof(frame_rate));
    json_get_string(json, "gop", gop, sizeof(gop));
    json_get_string(json, "rate_control", rate_control, sizeof(rate_control));
    json_get_string(json, "profile", profile, sizeof(profile));
    json_get_string(json, "quality", quality, sizeof(quality));
    json_get_string(json, "jpeg_snapshot", jpeg_snapshot, sizeof(jpeg_snapshot));

    printf("\n=================================\n");
    printf("STREAM1 CONFIG\n");
    printf("==============\n\n");
    printf("codec         : %s\n", codec);
    printf("resolution    : %s\n", resolution);
    printf("bit_rate      : %s\n", bit_rate);
    printf("frame_rate    : %s\n", frame_rate);
    printf("gop           : %s\n", gop);
    printf("rate_control  : %s\n", rate_control);
    printf("profile       : %s\n", profile);
    printf("quality       : %s\n", quality);
    printf("jpeg_snapshot : %s\n", jpeg_snapshot);
    printf("=================\n");
}

static void print_single_config(const char *json)
{
    char key[128];
    char value[128];

    key[0] = '\0';
    value[0] = '\0';

    json_get_string(json, "key", key, sizeof(key));
    json_get_string(json, "value", value, sizeof(value));

    printf("\n=================================\n");
    printf("CONFIG RESPONSE\n");
    printf("key   : %s\n", key);
    printf("value : %s\n", value);
    printf("=================================\n");
}

static int get_broadcast_addr(struct sockaddr_in *bcast)
{
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    memset(bcast, 0, sizeof(*bcast));
    bcast->sin_family = AF_INET;
    bcast->sin_port = htons(PORT);

    if (getifaddrs(&ifaddr) == -1)
        return -1;

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next)
    {
        struct sockaddr_in *addr;
        struct sockaddr_in *mask;
        uint32_t ip;
        uint32_t netmask;
        uint32_t bcast_ip;

        if (!ifa->ifa_addr || !ifa->ifa_netmask)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (strcmp(ifa->ifa_name, "lo") == 0)
            continue;

        addr = (struct sockaddr_in *)ifa->ifa_addr;
        mask = (struct sockaddr_in *)ifa->ifa_netmask;

        ip = ntohl(addr->sin_addr.s_addr);
        netmask = ntohl(mask->sin_addr.s_addr);
        bcast_ip = htonl(ip | ~netmask);

        bcast->sin_addr.s_addr = bcast_ip;
        found = 1;
        break;
    }

    freeifaddrs(ifaddr);

    if (!found)
        bcast->sin_addr.s_addr = inet_addr("255.255.255.255");

    return 0;
}

static void send_discover(int sock, const struct sockaddr_in *bcast)
{
    char discover = CMD_DISCOVER;
    int sent;

    sent = (int)sendto(sock, &discover, 1, 0,
                       (struct sockaddr *)bcast, sizeof(*bcast));
    if (sent < 0)
        perror("[XVR] sendto DISCOVER");
    else
        printf("[XVR] Send DISCOVER to %s\n", inet_ntoa(bcast->sin_addr));
}

static void query_ipc(int sock, const struct sockaddr_in *ipc_addr, socklen_t ipc_len)
{
    char single_cfg[256];
    char bundle_cmd = CMD_BUNDLE;
    int ret;

    ret = connect(sock, (struct sockaddr *)ipc_addr, ipc_len);
    if (ret < 0)
        perror("[XVR] connect IPC");
    else
        printf("[XVR] Connected to IPC %s:%d\n",
               inet_ntoa(ipc_addr->sin_addr),
               ntohs(ipc_addr->sin_port));

    snprintf(single_cfg, sizeof(single_cfg), "Gencode.stream1.codec");
    sendto(sock, single_cfg, strlen(single_cfg), 0,
           (struct sockaddr *)ipc_addr, ipc_len);
    printf("[XVR] GETCFG encode.stream1.codec\n");

    usleep(100000);

    sendto(sock, &bundle_cmd, 1, 0,
           (struct sockaddr *)ipc_addr, ipc_len);
    printf("[XVR] GET STREAM1 BUNDLE\n");
}

int main(void)
{
    int sock;
    struct sockaddr_in local_addr;
    struct sockaddr_in broadcast_addr;
    struct sockaddr_in recv_addr;
    struct sockaddr_in ipc_addr;
    socklen_t recv_len;
    socklen_t ipc_len = 0;
    int ipc_found = 0;
    char buf[BUF_SIZE];
    struct timeval tv;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    {
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("bind");
        return -1;
    }

    get_broadcast_addr(&broadcast_addr);

    printf("=================================\n");
    printf("XVR UDP Client Start\n");
    printf("PORT = %d\n", PORT);
    printf("=================================\n");

    send_discover(sock, &broadcast_addr);

    while (1)
    {
        int n;
        fd_set rfds;
        int sel;

        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        tv.tv_sec = ipc_found ? 30 : DISCOVER_RETRY_SEC;
        tv.tv_usec = 0;

        sel = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0)
        {
            perror("[XVR] select");
            continue;
        }

        if (sel == 0)
        {
            if (!ipc_found)
            {
                printf("[XVR] Retry DISCOVER...\n");
                send_discover(sock, &broadcast_addr);
            }
            continue;
        }

        recv_len = sizeof(recv_addr);
        n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                     (struct sockaddr *)&recv_addr, &recv_len);

        if (n <= 0)
        {
            if (n < 0)
                perror("[XVR] recvfrom");
            continue;
        }

        buf[n] = '\0';

        printf("[XVR] RECV cmd=%c len=%d from %s:%d\n",
               buf[0], n,
               inet_ntoa(recv_addr.sin_addr),
               ntohs(recv_addr.sin_port));

        if (buf[0] == CMD_INFO)
        {
            char ipc_ip[32];

            strcpy(ipc_ip, inet_ntoa(recv_addr.sin_addr));
            memcpy(&ipc_addr, &recv_addr, sizeof(ipc_addr));
            ipc_len = recv_len;
            ipc_found = 1;

            printf("\n=================================\n");
            printf("IPC FOUND\n");
            printf("IP   : %s\n", ipc_ip);
            printf("INFO : %s\n", buf + 1);
            printf("=================================\n");

            query_ipc(sock, &ipc_addr, ipc_len);
        }
        else if (buf[0] == CMD_CFGDATA)
        {
            if (is_bundle_json(buf + 1))
                print_stream1_config(buf + 1);
            else if (strstr(buf + 1, "\"result\":") != NULL)
            {
                printf("\n=================================\n");
                printf("SETCFG RESPONSE\n");
                printf("%s\n", buf + 1);
                printf("=================================\n");
            }
            else
                print_single_config(buf + 1);
        }
        else if (buf[0] == CMD_ERROR)
        {
            printf("\n=================================\n");
            printf("IPC ERROR\n");
            printf("%s\n", buf + 1);
            printf("=================================\n");
        }
        else if (buf[0] == CMD_PONG)
        {
            printf("[XVR] PING -> PONG\n");
        }
        else if (buf[0] == CMD_NOTIFY)
        {
            char event[64];
            char key[128];
            char value[128];

            event[0] = '\0';
            key[0] = '\0';
            value[0] = '\0';

            json_get_string(buf + 1, "event", event, sizeof(event));
            json_get_string(buf + 1, "key", key, sizeof(key));
            json_get_string(buf + 1, "value", value, sizeof(value));

            printf("\n=================================\n");
            printf("IPC NOTIFY\n");
            printf("event : %s\n", event);
            printf("key   : %s\n", key);
            printf("value : %s\n", value);
            printf("=================================\n");
        }
        else if (buf[0] == CMD_DISCOVER)
        {
            /* ignore own broadcast */
        }
        else
        {
            printf("[XVR] UNKNOWN RESPONSE: %c (0x%02x)\n",
                   buf[0], (unsigned char)buf[0]);
        }
    }

    close(sock);
    return 0;
}
