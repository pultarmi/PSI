
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

#define TARGET_IP	"192.168.0.129"
//#define TARGET_IP	"192.168.43.10"
#define BUFFERS_LEN 1024

#define TARGET_PORT 6666
#define LOCAL_PORT 5555

class Sender{
private:
    int sockfd;
    struct sockaddr_in local, addrDest;
    socklen_t fromlen;
    char buffer[BUFFERS_LEN];
    ssize_t recv_len;
public:
    Sender(){
        local.sin_family = AF_INET;
        local.sin_port = htons(LOCAL_PORT);
        local.sin_addr.s_addr = INADDR_ANY;

        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (bind(sockfd, (sockaddr *) &local, sizeof(local)) != 0) {
            perror("bind");
            printf("Binding error!\n");
            exit(1);
        }

        addrDest.sin_family = AF_INET;
        addrDest.sin_port = htons(TARGET_PORT);
        inet_pton(AF_INET, TARGET_IP, &addrDest.sin_addr.s_addr);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    }
    inline void send_datagram(unsigned int size){
        while(1){
            sendto(sockfd, buffer, size, 0, (sockaddr*)&addrDest, sizeof(addrDest));

            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &addrDest, &fromlen);
            if(recv_len < BUFFERS_LEN)
                buffer[recv_len] = 0;
            if(recv_len != -1){
                if(strcmp(buffer, "NAME") == 0){
                    std::cout << "received NAME ACK" << std::endl;
                    break;
                }
                else if(strcmp(buffer, "LENG") == 0){
                    std::cout << "received LENG ACK" << std::endl;
                    break;
                }
                else if(strcmp(buffer, "DATA") == 0)
                    break;
            }
        }
    }
    int send(const char *name){
        strncpy(buffer, name, BUFFERS_LEN);
        send_datagram(strlen(buffer));

        FILE* file_in = fopen(name, "rb");
        fseek(file_in, 0L, SEEK_END);
        unsigned int file_size = ftell(file_in);
        char size[4];
        memcpy(size, (void*)&file_size, 4);
        memcpy(buffer, size, 4);
        send_datagram(4);

        size_t length;
        int pos = 0;
        rewind(file_in);
        while((length = fread(&buffer[4], 1, BUFFERS_LEN - 4, file_in)) > 0) {
            if (length+4 < sizeof(buffer)) buffer[length+4] = 0;
            memcpy((void*)&buffer, (void*)&pos, 4);
            send_datagram(4+length);
            pos += length;
        }

        close(sockfd);
        fclose(file_in);
    }
};

int main(int argc, char** argv) {
    Sender sender;

//    if(argc < 2){
//        std::cout << "please provide filename" << std::endl;
//        return 1;
//    }
//    sender.send(argv[1]);
    sender.send("vit_normal.ppm");
    return 0;
}