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

    setsockopt(sock,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    setsockopt(sock,
               SOL_SOCKET,
               SO_BROADCAST,
               &opt,
               sizeof(opt));

    memset(&local_addr, 0, sizeof(local_addr));

    local_addr.sin_family      = AF_INET;
    local_addr.sin_port        = htons(PORT);
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

    /* 發送 Discover */

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

        /* IPC INFO */

        if (buf[0] == CMD_INFO)
        {
            char ipc_ip[32];

            strcpy(ipc_ip,
                   inet_ntoa(recv_addr.sin_addr));

            printf("\n");
            printf("=================================\n");
            printf("IPC FOUND\n");
            printf("IP : %s\n", ipc_ip);
            printf("INFO : %s\n", buf + 1);
            printf("=================================\n");

            char msg[256];

            memset(msg, 0, sizeof(msg));

            snprintf(msg,
                     sizeof(msg),
                     "Gencode.stream1.codec");

            sendto(sock,
                   msg,
                   strlen(msg),
                   0,
                   (struct sockaddr *)&recv_addr,
                   recv_len);

            printf("[XVR] GETCFG encode.stream1.codec\n");
        }

        /* CONFIG DATA */

        else if (buf[0] == CMD_CFGDATA)
        {
            printf("\n");
            printf("=================================\n");
            printf("CONFIG RESPONSE\n");
            printf("%s\n", buf + 1);
            printf("=================================\n");
        }

        /* ERROR */

        else if (buf[0] == CMD_ERROR)
        {
            printf("\n");
            printf("=================================\n");
            printf("IPC ERROR\n");
            printf("%s\n", buf + 1);
            printf("=================================\n");
        }
    }

    close(sock);

    return 0;
}