#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <openssl/md5.h>

#define TARGET_IP	"127.0.0.1"
//#define TARGET_IP	"192.168.43.10"
#define BUFFERS_LEN 1024

//#define TARGET_PORT 6666
#define LOCAL_PORT 6666

const int flag_name[] = {-1, -2};
const int flag_length[] = {-3, -4};
const int flag_hash[] = {-5, -6};
const int flag_data[] = {-7, -8};
const unsigned short CRC_length = 16;

unsigned short crc16ibm(const char *sdata, size_t len){
    const char CRCSIZE = 2; // in bytes
    const unsigned char* data = (const unsigned char*) sdata; // maybe for ANSI
    unsigned short pol = 0x8005; // polynomial
    unsigned char aux = 0;
    unsigned short crc = 0x0000; // init

    for(int i = 0; i < len+CRCSIZE; ++i){
        if (i < len) aux = data[i]; else aux = crc >> 8;
        for(int j = 0; j < 8; ++j){
            crc = (crc << 1) | (aux & 1);
            if(crc & 0x8000){
                crc ^= pol;
            }else{
                aux >>= 1;
            }
        }
    }

    return crc;
}

class Receiver {
private:
    struct sockaddr_in local, from;
    socklen_t fromlen;
    int sockfd;
    char buffer[BUFFERS_LEN];
    ssize_t recv_len;
    MD5_CTX md5_ctx;
    unsigned char beg_flags_len = 4;
    const unsigned short CRC_length = 16;

    inline short strip_CRC(ssize_t &recv_len){
        short crc;
        recv_len -= CRC_length;
        memcpy((void*)&crc, buffer+recv_len, CRC_length / 8);
        return crc;
    }
    inline bool check_CRC(const short crc, const int size){
        const short data_crc = crc16ibm(buffer, size);
        return crc == data_crc;
//        return true;
    }
    char *receive_name(){
        char *file_name = nullptr;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            if(!check_CRC(strip_CRC(recv_len), recv_len - CRC_length)) {
                sendto(sockfd, flag_name, sizeof(flag_name[1]), 0, (sockaddr *) &from, sizeof(from));
                continue;
            }
            sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));

            recv_len -= beg_flags_len;
            file_name = new char[recv_len + 1];
            file_name[recv_len] = 0;
            memcpy(file_name, buffer+beg_flags_len, recv_len);

            break;
        }
        return file_name;
    }
    int receive_length(){
        int size;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            if(!check_CRC(strip_CRC(recv_len), recv_len - CRC_length)) {
                sendto(sockfd, flag_length, sizeof(flag_length[1]), 0, (sockaddr *) &from, sizeof(from));
                continue;
            }
            sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));

            if(memcmp(buffer, flag_length, sizeof(flag_length[0])) != 0)
                continue;

            memcpy((void *) &size, buffer+beg_flags_len, 4);
            break;
        }
        return size;
    }
    void receive_data(FILE *file_out, int length){
        int pos = 0, pos_prev = -(BUFFERS_LEN - beg_flags_len - CRC_length), pos_exp = 0;
        do{
            while(true) {
                recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
                if(!check_CRC(strip_CRC(recv_len), recv_len - CRC_length)) {
                    sendto(sockfd, flag_data, sizeof(flag_data[1]), 0, (sockaddr *) &from, sizeof(from));
                    std::cout << "wrong CRC" << std::endl;
                    continue;
                }
                memcpy((void*)&pos, buffer, beg_flags_len);

                if(pos == flag_length[0]){
                    sendto(sockfd, flag_length, sizeof(flag_length[1]), 0, (sockaddr *) &from, sizeof(from));
                    continue;
                }
                if(pos < 0 || pos == pos_prev || pos != pos_prev + BUFFERS_LEN - beg_flags_len - CRC_length)
                    continue;

                sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));

                pos_prev = pos;

                pos_exp = pos + recv_len - beg_flags_len;
                std::cout << "received data " << pos << ", next exp: " << pos_exp << std::endl;
                fwrite(&buffer[beg_flags_len], recv_len - beg_flags_len, 1, file_out);
                break;
            }
        } while(pos_exp < length);
    }
    char *receive_hash(){
        char *hash;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            if(!check_CRC(strip_CRC(recv_len), recv_len - CRC_length)) {
                sendto(sockfd, flag_hash, sizeof(flag_hash[1]), 0, (sockaddr *) &from, sizeof(from));
                continue;
            }
            sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));

            if(memcmp(buffer, flag_hash, sizeof(flag_hash[0])) != 0)
                continue;

            recv_len -= beg_flags_len;
            hash = new char[recv_len + 1];
            hash[recv_len] = 0;
            memcpy(hash, buffer+beg_flags_len, recv_len);
            break;
        }
        return hash;
    }
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
    }
    ~Receiver(){
        close(sockfd);
    }
    void receive(){
        char *file_name = receive_name();
        std::cout << "name " << file_name << std::endl;

        int length = receive_length();
        std::cout << "length: " << length << std::endl;

        FILE *file_out = fopen(file_name, "w");
        receive_data(file_out, length);

        char *hash = receive_hash();
        std::cout << "hash: " << hash << std::endl;

        fclose(file_out);
    }
};

int main(int argc, char** argv) {
    Receiver receiver;
    receiver.receive();
    return 0;
}