#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <errno.h>

#include "nbus.h"

#define PORT 37000
#define BUF_SIZE 2048

#define CMD_DISCOVER    'C'
#define CMD_INFO        'I'
#define CMD_GETCFG      'G'
#define CMD_SETCFG      'P'
#define CMD_CFGDATA     'K'
#define CMD_BUNDLE      'B'
#define CMD_PING        'H'
#define CMD_PONG        'O'
#define CMD_NOTIFY      'N'
#define CMD_ERROR       'E'
#define CMD_GETSTREAM1  'S'

static int g_sock = -1;
static struct sockaddr_in g_last_client;
static socklen_t g_last_client_len = 0;
static int g_has_client = 0;
static int g_notify_registered = 0;

static const char *g_stream1_keys[] = {
    "encode.stream1.codec",
    "encode.stream1.resolution",
    "encode.stream1.bit_rate",
    "encode.stream1.frame_rate",
    "encode.stream1.gop",
    "encode.stream1.rate_control",
    "encode.stream1.profile",
    "encode.stream1.quality",
    "encode.stream1.jpeg_snapshot",
    NULL
};

/* -------------------------
 * JSON helpers (no library)
 * ------------------------- */
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

static int reply_packet_len(const char *reply)
{
    return 1 + (int)strlen(reply + 1);
}

/* -------------------------
 * nbus get (empty on fail)
 * ------------------------- */
static void nbus_get_cfg(const char *key, char *out, int size)
{
    out[0] = '\0';
    if (nbus_get(key, out, size) != NBUS_RESULT_OK)
        out[0] = '\0';
}

static void remember_client(const struct sockaddr_in *client, socklen_t client_len)
{
    memcpy(&g_last_client, client, sizeof(g_last_client));
    g_last_client_len = client_len;
    g_has_client = 1;
}

static void send_reply(const char *reply, int reply_len,
                       const struct sockaddr_in *client, socklen_t client_len)
{
    int sent = (int)sendto(g_sock, reply, (size_t)reply_len, 0,
                           (struct sockaddr *)client, client_len);

    if (sent < 0)
        perror("[IPC] sendto");
    else
        printf("[IPC] SEND cmd=%c len=%d to %s:%d\n",
               reply[0], sent,
               inet_ntoa(client->sin_addr),
               ntohs(client->sin_port));
}

static void on_config_change(const char *name, const char *value)
{
    char reply[512];

    if (!g_has_client || g_sock < 0)
        return;

    reply[0] = CMD_NOTIFY;
    snprintf(reply + 1, sizeof(reply) - 1,
             "{\"event\":\"config_changed\",\"key\":\"%s\",\"value\":\"%s\"}",
             name, value);

    send_reply(reply, reply_packet_len(reply), &g_last_client, g_last_client_len);
    printf("[IPC] NOTIFY %s = %s\n", name, value);
}

static void register_notify_listeners(void)
{
    int i;

    if (g_notify_registered)
        return;

    for (i = 0; g_stream1_keys[i] != NULL; i++)
        nbus_listen(g_stream1_keys[i], on_config_change);

    g_notify_registered = 1;
}

static void nbus_session_end(void)
{
    nbus_release();
    g_notify_registered = 0;
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
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (strcmp(ifa->ifa_name, "lo") == 0)
            continue;

        {
            struct sockaddr_in *a = (struct sockaddr_in *)ifa->ifa_addr;
            struct sockaddr_in *m = (struct sockaddr_in *)ifa->ifa_netmask;

            inet_ntop(AF_INET, &a->sin_addr, ip, 32);
            inet_ntop(AF_INET, &m->sin_addr, mask, 32);
            strcpy(ifname, ifa->ifa_name);
            freeifaddrs(ifaddr);
            return 0;
        }
    }

    freeifaddrs(ifaddr);
    return -1;
}

static void handle_discover(const struct sockaddr_in *client, socklen_t client_len)
{
    char ifname[32];
    char ip[32];
    char mask[32];
    char model[128];
    char fw[128];
    char reply[512];

    remember_client(client, client_len);

    if (get_first_ipv4(ifname, ip, mask) < 0)
    {
        printf("[IPC] No Network Interface\n");
        return;
    }

    model[0] = '\0';
    fw[0] = '\0';

    nbus_initial();
    nbus_get_cfg("system.info.model_name", model, sizeof(model));
    nbus_get_cfg("system.info.fw_version", fw, sizeof(fw));
    register_notify_listeners();
    nbus_session_end();

    reply[0] = CMD_INFO;
    snprintf(reply + 1, sizeof(reply) - 1,
             "{\"type\":\"ipc\",\"ip\":\"%s\",\"model\":\"%s\",\"fw\":\"%s\"}",
             ip, model, fw);

    send_reply(reply, reply_packet_len(reply), client, client_len);
    printf("[IPC] DISCOVER -> %s model=%s fw=%s\n", ip, model, fw);
}

static void handle_getcfg(const char *payload,
                          const struct sockaddr_in *client, socklen_t client_len)
{
    char key[128];
    char value[128];
    char reply[512];

    remember_client(client, client_len);
    memset(key, 0, sizeof(key));
    memset(value, 0, sizeof(value));
    strncpy(key, payload, sizeof(key) - 1);

    printf("[IPC] GETCFG key=%s\n", key);

    nbus_initial();
    register_notify_listeners();

    if (nbus_get(key, value, sizeof(value)) == NBUS_RESULT_OK)
    {
        reply[0] = CMD_CFGDATA;
        snprintf(reply + 1, sizeof(reply) - 1,
                 "{\"key\":\"%s\",\"value\":\"%s\"}",
                 key, value);
        send_reply(reply, reply_packet_len(reply), client, client_len);
    }
    else
    {
        reply[0] = CMD_ERROR;
        snprintf(reply + 1, sizeof(reply) - 1,
                 "{\"error\":\"nbus_get failed\"}");
        send_reply(reply, reply_packet_len(reply), client, client_len);
        printf("[IPC] GETCFG failed: %s\n", key);
    }

    nbus_session_end();
}

static void handle_setcfg(const char *payload,
                          const struct sockaddr_in *client, socklen_t client_len)
{
    char key[128];
    char value[128];
    char reply[128];

    remember_client(client, client_len);
    memset(key, 0, sizeof(key));
    memset(value, 0, sizeof(value));

    if (json_get_string(payload, "key", key, sizeof(key)) != 0 ||
        json_get_string(payload, "value", value, sizeof(value)) != 0)
    {
        reply[0] = CMD_ERROR;
        snprintf(reply + 1, sizeof(reply) - 1,
                 "{\"error\":\"invalid setcfg json\"}");
        send_reply(reply, reply_packet_len(reply), client, client_len);
        return;
    }

    nbus_initial();
    register_notify_listeners();

    if (nbus_set(key, value) == NBUS_RESULT_OK)
    {
        reply[0] = CMD_CFGDATA;
        snprintf(reply + 1, sizeof(reply) - 1, "{\"result\":\"ok\"}");
        send_reply(reply, reply_packet_len(reply), client, client_len);
        printf("[IPC] SETCFG %s = %s OK\n", key, value);
    }
    else
    {
        reply[0] = CMD_ERROR;
        snprintf(reply + 1, sizeof(reply) - 1, "{\"result\":\"fail\"}");
        send_reply(reply, reply_packet_len(reply), client, client_len);
        printf("[IPC] SETCFG %s = %s FAIL\n", key, value);
    }

    nbus_session_end();
}

static void handle_bundle(const struct sockaddr_in *client, socklen_t client_len)
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
    char reply[1024];

    remember_client(client, client_len);

    nbus_initial();
    register_notify_listeners();

    nbus_get_cfg("encode.stream1.codec", codec, sizeof(codec));
    nbus_get_cfg("encode.stream1.resolution", resolution, sizeof(resolution));
    nbus_get_cfg("encode.stream1.bit_rate", bitrate, sizeof(bitrate));
    nbus_get_cfg("encode.stream1.frame_rate", framerate, sizeof(framerate));
    nbus_get_cfg("encode.stream1.gop", gop, sizeof(gop));
    nbus_get_cfg("encode.stream1.rate_control", rate_control, sizeof(rate_control));
    nbus_get_cfg("encode.stream1.profile", profile, sizeof(profile));
    nbus_get_cfg("encode.stream1.quality", quality, sizeof(quality));
    nbus_get_cfg("encode.stream1.jpeg_snapshot", snapshot, sizeof(snapshot));

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

    send_reply(reply, reply_packet_len(reply), client, client_len);
    printf("[IPC] STREAM1 BUNDLE SENT\n");

    nbus_session_end();
}

static void handle_ping(const struct sockaddr_in *client, socklen_t client_len)
{
    char reply[2];

    remember_client(client, client_len);
    reply[0] = CMD_PONG;
    reply[1] = '\0';
    send_reply(reply, 1, client, client_len);
}

/* =========================
 * MAIN
 * ========================= */
int main(void)
{
    struct sockaddr_in local, client;
    socklen_t client_len;
    char buf[BUF_SIZE];

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0)
    {
        perror("socket");
        return -1;
    }

    {
        int opt = 1;
        setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_sock, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
        perror("bind");
        return -1;
    }

    printf("IPC UDP Server Running on port %d...\n", PORT);

    while (1)
    {
        int n;

        client_len = sizeof(client);
        n = recvfrom(g_sock, buf, sizeof(buf) - 1, 0,
                     (struct sockaddr *)&client, &client_len);

        if (n <= 0)
        {
            if (n < 0)
                perror("[IPC] recvfrom");
            continue;
        }

        buf[n] = '\0';

        printf("[IPC] RECV cmd=%c len=%d from %s:%d\n",
               buf[0], n,
               inet_ntoa(client.sin_addr),
               ntohs(client.sin_port));

        if (buf[0] == CMD_DISCOVER)
        {
            handle_discover(&client, client_len);
        }
        else if (buf[0] == CMD_GETCFG)
        {
            handle_getcfg(buf + 1, &client, client_len);
        }
        else if (buf[0] == CMD_SETCFG)
        {
            handle_setcfg(buf + 1, &client, client_len);
        }
        else if (buf[0] == CMD_BUNDLE || buf[0] == CMD_GETSTREAM1)
        {
            handle_bundle(&client, client_len);
        }
        else if (buf[0] == CMD_PING)
        {
            handle_ping(&client, client_len);
        }
        else
        {
            printf("[IPC] UNKNOWN CMD: %c (0x%02x)\n",
                   buf[0], (unsigned char)buf[0]);
        }
    }

    close(g_sock);
    return 0;
}
