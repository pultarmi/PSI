#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <cassert>
#include <ctime>
#include <cstdlib>
#include <openssl/md5.h>

//#define TARGET_IP "192.168.43.138"
#define TARGET_IP	"192.168.0.129"
//#define TARGET_IP	"192.168.43.10"
#define BUFFERS_LEN 1024

#define TARGET_PORT 6666
#define LOCAL_PORT 5555

const int flag_name[] = {-1, -2};
const int flag_length[] = {-3, -4};
const int flag_hash[] = {-5, -6};
const int flag_data[] = {-7, -8};

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

class Sender{
private:
    int sockfd;
    struct sockaddr_in local, addrDest;
    socklen_t fromlen;
    char buffer[BUFFERS_LEN], buffer_backup[BUFFERS_LEN];
//    ssize_t recv_len;
    MD5_CTX md5_ctx;
    unsigned int beg_flags_len = 4;
    const unsigned short CRC_length = 16;

    inline void random_distort(const int size, const short crc){
        if(rand() % 200 < 5) {
            int pos = rand() % size;
            buffer[pos] ^= 1UL << (rand() % 8); // modify one bit
//            short crc_check = crc16ibm(buffer, size);
//            std::cout << (crc != crc_check) << std::endl;
            //assert(crc != crc_check);
                    // assertion above FAILS, which is wrong because single error should be detected
        }
    }
    inline short count_crc(const int size){
        return crc16ibm(buffer, size);
    }
    inline void send_datagram(unsigned int size){
        assert(size + CRC_length <= BUFFERS_LEN);

        short crc = count_crc(size);
        memcpy(buffer+size, (void*)&crc, CRC_length);

        memcpy(buffer_backup, buffer, BUFFERS_LEN);
        while(true){
            memcpy(buffer, buffer_backup, BUFFERS_LEN);
            random_distort(size, crc);

            int rnd = rand() % 20;
            if(rnd < 2)
                sendto(sockfd, buffer, size+CRC_length, 0, (sockaddr*)&addrDest, sizeof(addrDest));
            if(rnd < 6)
                sendto(sockfd, buffer, size+CRC_length, 0, (sockaddr*)&addrDest, sizeof(addrDest));
            if(rnd < 15)
                sendto(sockfd, buffer, size+CRC_length, 0, (sockaddr*)&addrDest, sizeof(addrDest));

            while(recvfrom(sockfd, buffer+beg_flags_len, sizeof(buffer)-beg_flags_len, 0, (sockaddr *) &addrDest, &fromlen) != -1){
                if(memcmp(buffer+beg_flags_len, buffer_backup, beg_flags_len) == 0) {
                    if (memcmp(buffer, flag_name, sizeof(flag_name[0])) == 0)
                        std::cout << "received NAME ACK" << std::endl;
                    else if (memcmp(buffer, flag_length, sizeof(flag_length[0])) == 0)
                        std::cout << "received LENG ACK" << std::endl;
                    else if (memcmp(buffer, flag_hash, sizeof(flag_hash[0])) == 0)
                        std::cout << "received HASH ACK" << std::endl;
                    return;
                }
            }
        }
    }
    inline void send_name(const char *name){
        memcpy(buffer, (void*)&flag_name, sizeof(flag_name[0]));
        memcpy(buffer+sizeof(flag_name[0]), name, BUFFERS_LEN);
        send_datagram(sizeof(flag_name[0])+strlen(name));
    }
    inline void send_length(FILE* file_in){
        fseek(file_in, 0L, SEEK_END);
        long file_size = ftell(file_in);
        memcpy(buffer, (void*)&flag_length, sizeof(flag_length[0]));
        memcpy(buffer+sizeof(flag_length[0]), (void*)&file_size, sizeof(long));
        send_datagram(sizeof(flag_length[0])+sizeof(long));
        rewind(file_in);
    }
    inline void send_data(FILE* file_in){
        size_t chars_read;
        int pos = 0;
        while((chars_read = fread(buffer+beg_flags_len, 1, BUFFERS_LEN - beg_flags_len - CRC_length, file_in)) > 0) {
            MD5_Update(&md5_ctx, buffer + 4, chars_read);

            memcpy(buffer, (void*)&pos, beg_flags_len);
            send_datagram(beg_flags_len+chars_read);
            pos += chars_read;
        }
    }
    inline void send_hash(){
        MD5_Final((unsigned char*)buffer + beg_flags_len, &md5_ctx);
        //char MD5[] = "DUMMY MD5";
        memcpy(buffer, flag_hash, sizeof(flag_hash[0]));
        //memcpy(buffer+beg_flags_len, MD5, strlen(MD5));
        send_datagram(sizeof(flag_hash[0])+4);
    }
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
        tv.tv_usec = 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

        MD5_Init(&md5_ctx);

        srand(time(NULL));
    }
    int send(const char *name){
        send_name(name);
        FILE* file_in = fopen(name, "rb");
        send_length(file_in);
        send_data(file_in);
        send_hash();
        close(sockfd);
        fclose(file_in);
    }
};

int main(int argc, char** argv) {
    Sender sender;
    if(argc < 2){
        std::cout << "please provide filename" << std::endl;
        return 1;
    }
    sender.send(argv[1]);
    return 0;
}