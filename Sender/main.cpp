
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <sys/types.h>          /* See NOTES */
#include <stdlib.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <fstream>
#include <iostream>

#define TARGET_IP	"127.0.0.1"
//#define TARGET_IP	"192.168.43.10"
#define BUFFERS_LEN 1024

#define TARGET_PORT 6666
#define LOCAL_PORT 5555

int send(const char *name){
    int sockfd;

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_port = htons(LOCAL_PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (bind(sockfd, (sockaddr *) &local, sizeof(local)) != 0) {
        perror("bind");
        printf("Binding error!\n");
        return 1;
    }

    char buffer_tx[BUFFERS_LEN];
    sockaddr_in addrDest;
    addrDest.sin_family = AF_INET;
    addrDest.sin_port = htons(TARGET_PORT);
    inet_pton(AF_INET, TARGET_IP, &addrDest.sin_addr.s_addr);

//    const char *name = argv[1];
    FILE* file_in = fopen(name, "rb");
    strncpy(buffer_tx, name, BUFFERS_LEN);
    sendto(sockfd, buffer_tx, strlen(buffer_tx), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    fseek(file_in, 0L, SEEK_END);
    unsigned int pom = ftell(file_in);
    char size[4];
    memcpy(size, (void*)&pom, 4);
    strncpy(buffer_tx, size, BUFFERS_LEN);
    sendto(sockfd, buffer_tx, 4, 0, (sockaddr*)&addrDest, sizeof(addrDest));

    size_t length;
    int pos = 0;
    rewind(file_in);
    while((length = fread(&buffer_tx[4], 1, BUFFERS_LEN - 4, file_in)) > 0) {
        if (length+4 < sizeof(buffer_tx)) buffer_tx[length+4] = 0;
        memcpy((void*)&buffer_tx, (void*)&pos, 4);
        sendto(sockfd, buffer_tx, 4 + length, 0, (sockaddr *) &addrDest, sizeof(addrDest));
        pos += length;
    }

    close(sockfd);
    fclose(file_in);
}

int main(int argc, char** argv) {
    if(argc < 2){
        std::cout << "please provide filename" << std::endl;
        return 1;
    }
    send(argv[1]);
    return 0;
}