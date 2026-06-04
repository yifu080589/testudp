#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT        37000
#define BUF_SIZE    2048

#define CMD_DISCOVER    'C'
#define CMD_INFO        'I'
#define CMD_GETCFG      'G'
#define CMD_CFGDATA     'K'
#define CMD_ERROR       'E'
#define CMD_GETSTREAM1  'S'

static int stream1_done = 0;

int main(void)
{
    int sock;

    struct sockaddr_in local_addr;
    struct sockaddr_in broadcast_addr;
    struct sockaddr_in recv_addr;

    socklen_t recv_len = sizeof(recv_addr);

    char buf[BUF_SIZE];

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        perror("socket");
        return -1;
    }

    int opt = 1;

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    memset(&local_addr, 0, sizeof(local_addr));

    local_addr.sin_family = AF_INET;
    local_addr.sin_port   = htons(PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock,
             (struct sockaddr *)&local_addr,
             sizeof(local_addr)) < 0)
    {
        perror("bind");
        return -1;
    }

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));

    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port   = htons(PORT);
    broadcast_addr.sin_addr.s_addr =
        inet_addr("255.255.255.255");

    printf("=================================\n");
    printf("XVR UDP Client Start\n");
    printf("PORT = %d\n", PORT);
    printf("=================================\n");

    /* =========================
     * DISCOVER IPC
     * ========================= */
    char discover = CMD_DISCOVER;

    sendto(sock,
           &discover,
           1,
           0,
           (struct sockaddr *)&broadcast_addr,
           sizeof(broadcast_addr));

    printf("[XVR] Send DISCOVER\n");

    while (1)
    {
        int n = recvfrom(sock,
                         buf,
                         sizeof(buf) - 1,
                         0,
                         (struct sockaddr *)&recv_addr,
                         &recv_len);

        if (n <= 0)
            continue;

        buf[n] = 0;

        /* =========================
         * IPC FOUND
         * ========================= */
        if (buf[0] == CMD_INFO)
        {
            char ipc_ip[32];

            strcpy(ipc_ip, inet_ntoa(recv_addr.sin_addr));

            printf("\n=================================\n");
            printf("IPC FOUND\n");
            printf("IP   : %s\n", ipc_ip);
            printf("INFO : %s\n", buf + 1);
            printf("=================================\n");

            /* =========================
             * 單參數查詢（保留）
             * ========================= */
            char single_cfg[256];

            snprintf(single_cfg,
                     sizeof(single_cfg),
                     "Gencode.stream1.codec");

            sendto(sock,
                   single_cfg,
                   strlen(single_cfg),
                   0,
                   (struct sockaddr *)&recv_addr,
                   recv_len);

            printf("[XVR] GETCFG encode.stream1.codec\n");

            /* =========================
             * STREAM1 BUNDLE（工業級）
             * ========================= */
            char stream_cmd = CMD_GETSTREAM1;

            sendto(sock,
                   &stream_cmd,
                   1,
                   0,
                   (struct sockaddr *)&recv_addr,
                   recv_len);

            printf("[XVR] GET STREAM1 BUNDLE\n");

            stream1_done = 0;
        }

        /* =========================
         * CONFIG RESPONSE
         * ========================= */
        else if (buf[0] == CMD_CFGDATA)
        {
            printf("\n=================================\n");
            printf("CONFIG RESPONSE\n");
            printf("%s\n", buf + 1);
            printf("=================================\n");
        }

        /* =========================
         * ERROR
         * ========================= */
        else if (buf[0] == CMD_ERROR)
        {
            printf("\n=================================\n");
            printf("IPC ERROR\n");
            printf("%s\n", buf + 1);
            printf("=================================\n");
        }

        /* =========================
         * UNKNOWN（過濾掉 S 重複問題）
         * ========================= */
        else
        {
            /* 避免 S 尚未支援時一直噴 */
            if (buf[0] != CMD_GETSTREAM1)
            {
                printf("[XVR] UNKNOWN RESPONSE: %c\n", buf[0]);
            }
        }
    }

    close(sock);
    return 0;
}