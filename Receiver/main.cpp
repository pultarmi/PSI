
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

#define TARGET_IP	"127.0.0.1"
//#define TARGET_IP	"192.168.43.10"
#define BUFFERS_LEN 1024

//#define TARGET_PORT 6666
#define LOCAL_PORT 6666

const char *ACK_length[] = {"LENG+", "LENG-"};
const char *ACK_name[] = {"NAME+", "NAME-"};
const char *ACK_data[] = {"DATA+", "DATA-"};
const char *ACK_hash[] = {"HASH+", "HASH-"};
const unsigned short CRC_length = 32;

class Receiver {
private:
    struct sockaddr_in local, from;
    socklen_t fromlen;
    int sockfd;
    char buffer[BUFFERS_LEN], buffer_old[BUFFERS_LEN], CRC[CRC_length];
    ssize_t recv_len;
    MD5_CTX md5_ctx();
public:
    Receiver(){
        local.sin_family = AF_INET;
        local.sin_port = htons(LOCAL_PORT);
        local.sin_addr.s_addr = INADDR_ANY;

        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (bind(sockfd, (sockaddr *) &local, sizeof(local)) != 0) {
            printf("Binding error!\n");
            return;
        }
        fromlen = sizeof(from);
    };
    ~Receiver(){
        close(sockfd);
    }

    inline void strip_CRC(ssize_t &recv_len){
        recv_len -= CRC_length;
        memcpy(CRC, buffer+recv_len, CRC_length);
//        buffer[recv_len] = 0;
    }
    inline bool check_CRC(const unsigned int recv_len){
        return true;
    }

    char *receive_name(){
        char *file_name;
        while(1) {
            recv_len = recvfrom(sockfd, buffer_old, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            if(recv_len == -1)
                continue;
            if(strncmp(buffer, buffer_old, sizeof(buffer)) == 0)
                continue;
            memcpy(buffer, buffer_old, sizeof(buffer));
            strip_CRC(recv_len);

            if(!check_CRC(recv_len)) {
                sendto(sockfd, ACK_name, strlen(ACK_name[1]), 0, (sockaddr *) &from, sizeof(from));
                continue;
            }

            file_name = new char[recv_len + 1];
            file_name[recv_len] = 0;
            memcpy(file_name, buffer, recv_len);

            sendto(sockfd, ACK_name[0], strlen(ACK_name[0]), 0, (sockaddr *) &from, sizeof(from));
            break;
        }

        return file_name;
    }

    unsigned int receive_length(){
//        buffer[0] = buffer[1] = buffer[2] = buffer[3] = 0;
        unsigned int size;
        while(1) {
            recv_len = recvfrom(sockfd, buffer_old, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            if(recv_len == -1)
                continue;
            if(strncmp(buffer, buffer_old, sizeof(buffer)) == 0)
                continue;
            memcpy(buffer, buffer_old, sizeof(buffer));

            strip_CRC(recv_len);

            memcpy((void *) &size, buffer, 4);

            sendto(sockfd, ACK_length[0], strlen(ACK_length[0]), 0, (sockaddr *) &from, sizeof(from));
            break;
        }

        return size;
    }

    void receive_data(FILE *file_out, unsigned int length){
        unsigned int pos = 0;
        for (; pos < length; pos += recv_len - 4) {
            while(1) {
                recv_len = recvfrom(sockfd, buffer_old, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
                if(recv_len == -1)
                    continue;
                if(strncmp(buffer, buffer_old, sizeof(buffer)) == 0)
                    continue;
                memcpy(buffer, buffer_old, sizeof(buffer));

                strip_CRC(recv_len);

                sendto(sockfd, ACK_data[0], strlen(ACK_data[0]), 0, (sockaddr *) &from, sizeof(from));

                memcpy((void *) &pos, buffer, 4);
                fwrite(&buffer[4], recv_len - 4, 1, file_out);
                break;
            }
        }
    }

    char *receive_hash(){
        char *hash;
        while(1) {
            recv_len = recvfrom(sockfd, buffer_old, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            if(recv_len == -1)
                continue;
            if(strncmp(buffer, buffer_old, sizeof(buffer)) == 0)
                continue;
            memcpy(buffer, buffer_old, sizeof(buffer));

            strip_CRC(recv_len);
            hash = new char[recv_len + 1];

            hash[recv_len] = 0;
            memcpy(hash, buffer, recv_len);

            sendto(sockfd, ACK_hash[0], strlen(ACK_hash[0]), 0, (sockaddr *) &from, sizeof(from));
            break;
        }
        return hash;
    }

    void receive(){
        char *file_name = receive_name();
        std::cout << "name " << file_name << std::endl;

        unsigned int length = receive_length();
        std::cout << "length: " << length << std::endl;

        FILE *file_out = fopen(file_name, "w");
        receive_data(file_out, length);

        char *hash = receive_hash();
        std::cout << "HASH: " << hash << std::endl;

        fclose(file_out);
    }
};

int main(int argc, char** argv) {
    Receiver receiver;
    receiver.receive();
    return 0;
}