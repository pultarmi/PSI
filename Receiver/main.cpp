
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
#define LOCAL_PORT 6666

class Receiver {
private:
    struct sockaddr_in local, from;
    socklen_t fromlen;
    int sockfd;
    char buffer_rx[BUFFERS_LEN];
    ssize_t recv_len;

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

    char *receive_name(){
        recv_len = recvfrom(sockfd, buffer_rx, sizeof(buffer_rx), 0, (sockaddr *) &from, &fromlen);
        if (recv_len < sizeof(buffer_rx)) buffer_rx[recv_len] = 0;
        char *file_name = new char[recv_len+1];
        strncpy(file_name, buffer_rx, recv_len+1);
        return file_name;
    }

    unsigned int receive_length(){
        buffer_rx[0] = buffer_rx[1] = buffer_rx[2] = buffer_rx[3] = 0;
        recv_len = recvfrom(sockfd, buffer_rx, sizeof(buffer_rx), 0, (sockaddr *) &from, &fromlen);
        unsigned int size;
        memcpy((void *) &size, buffer_rx, 4);
        return size;
    }

    void receive_data(FILE *file_out, unsigned int length){
        unsigned int pos = 0;
        for (; pos < length; pos += recv_len - 4) {
            recv_len = recvfrom(sockfd, buffer_rx, sizeof(buffer_rx), 0, (sockaddr *) &from, &fromlen);
            memcpy((void *) &pos, buffer_rx, 4);
            fwrite(&buffer_rx[4], recv_len - 4, 1, file_out);
        }
    }

    void receive(){
        char *file_name = receive_name();
        std::cout << "name " << file_name << std::endl;

        unsigned int length = receive_length();
        std::cout << "length: " << length << std::endl;

        FILE *file_out = fopen(file_name, "w");
        receive_data(file_out, length);
        fclose(file_out);
    }
};

int main(int argc, char** argv) {
    Receiver receiver;
    receiver.receive();
    return 0;
}