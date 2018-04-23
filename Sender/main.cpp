
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
#include <openssl/md5.h>

#define TARGET_IP "192.168.43.138"
//#define TARGET_IP	"192.168.0.129"
//#define TARGET_IP	"192.168.43.10"
#define BUFFERS_LEN 1024

#define TARGET_PORT 6666
#define LOCAL_PORT 5555

const char *ACK_length[] = {"LENG+", "LENG-"};
const char *ACK_name[] = {"NAME+", "NAME-"};
const char *ACK_data[] = {"DATA+", "DATA-"};
const char *ACK_hash[] = {"HASH+", "HASH-"};
const unsigned short CRC_length = 32;

class Sender{
private:
    int sockfd;
    struct sockaddr_in local, addrDest;
    socklen_t fromlen;
    char buffer[BUFFERS_LEN], CRC[CRC_length];
    ssize_t recv_len;
    MD5_CTX md5_ctx();
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

//        MD5_Init(md5_ctx);
    }
    inline short count_crc(const int size){
        return 0;
    }
    inline void send_datagram(unsigned int size){
        if(size + CRC_length > BUFFERS_LEN)
            throw("Too big payload, CRC cant fit");
        while(1){
            count_crc(size);
            memcpy(buffer+size, CRC, CRC_length);

            sendto(sockfd, buffer, size+CRC_length, 0, (sockaddr*)&addrDest, sizeof(addrDest));

            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &addrDest, &fromlen);
            if(recv_len < BUFFERS_LEN)
                buffer[recv_len] = 0;
            if(recv_len != -1){
                if(strcmp(buffer, ACK_name[0]) == 0){
                    std::cout << "received NAME ACK" << std::endl;
                    break;
                }
                else if(strcmp(buffer, ACK_length[0]) == 0){
                    std::cout << "received LENG ACK" << std::endl;
                    break;
                }
                else if(strcmp(buffer, ACK_data[0]) == 0)
                    break;
                else if(strcmp(buffer, ACK_hash[0]) == 0){
                    std::cout << "received HASH ACK" << std::endl;
                    break;
                }
            }
        }
    }
    int send(const char *name){
        strncpy(buffer, name, BUFFERS_LEN);
        send_datagram(strlen(buffer));

        FILE* file_in = fopen(name, "rb");
        fseek(file_in, 0L, SEEK_END);
        unsigned int file_size = ftell(file_in);
        memcpy(buffer, (void*)&file_size, 4);
        send_datagram(4);

        size_t length;
        int pos = 0;
        rewind(file_in);
        while((length = fread(&buffer[4], 1, BUFFERS_LEN - 4 - CRC_length, file_in)) > 0) {
//            MD5_Update(md5_ctx, buffer + 4, length);

            memcpy((void*)&buffer, (void*)&pos, 4);
            send_datagram(4+length);
            pos += length;
        }

//        MD5_Final((unsigned char*)buffer, md5_ctx);
        char MD5[] = "DUMMY MD5";
        memcpy(buffer, MD5, strlen(MD5));
        send_datagram(strlen(MD5));

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