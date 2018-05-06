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

#define		CRC_START_16		0x0000
#define		CRC_POLY_16		0xA001
static void             init_crc16_tab( void );
static bool             crc_tab16_init          = false;
static uint16_t         crc_tab16[256];
static void init_crc16_tab( void ) {
    uint16_t i;
    uint16_t j;
    uint16_t crc;
    uint16_t c;
    for (i=0; i<256; i++) {
        crc = 0;
        c = i;
        for (j = 0; j < 8; j++) {
            if ((crc ^ c) & 0x0001) crc = (crc >> 1) ^ CRC_POLY_16;
            else crc = crc >> 1;
            c = c >> 1;
        }
        crc_tab16[i] = crc;
    }
    crc_tab16_init = true;
}  /* init_crc16_tab */
uint16_t crc_16( const unsigned char *input_str, size_t num_bytes ) {
    uint16_t crc;
    const unsigned char *ptr;
    size_t a;
    if ( ! crc_tab16_init ) init_crc16_tab();
    crc = CRC_START_16;
    ptr = input_str;
    if ( ptr != NULL ) for (a=0; a<num_bytes; a++) {
            crc = (crc >> 8) ^ crc_tab16[ (crc ^ (uint16_t) *ptr++) & 0x00FF ];
        }
    return crc;
}  /* crc_16 */


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
    short count_crc(const int size){
        return crc_16((unsigned char*)buffer, size);
    }
    inline bool check_CRC(const short crc, const int size){
        const short data_crc = count_crc(size);
        return crc == data_crc;
    }
    char *receive_name(unsigned int recv_max_rate){
        char *file_name = nullptr;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            short crc = strip_CRC(recv_len);
            if(!check_CRC(crc, recv_len)) {
                sendto(sockfd, flag_name, sizeof(flag_name[1]), 0, (sockaddr *) &from, sizeof(from));
                continue;
            }
            recv_len -= beg_flags_len;
            file_name = new char[recv_len + 1];
            file_name[recv_len] = 0;
            memcpy(file_name, buffer+beg_flags_len, recv_len);

            memcpy(buffer+4, (void*)&recv_max_rate, 4);
            sendto(sockfd, buffer, beg_flags_len + 4, 0, (sockaddr *) &from, sizeof(from));

            break;
        }
        return file_name;
    }
    int receive_length(){
        int size;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            short crc = strip_CRC(recv_len);
            if(!check_CRC(crc, recv_len)) {
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
                short crc = strip_CRC(recv_len);
                if(!check_CRC(crc, recv_len)) {
                    sendto(sockfd, flag_data, sizeof(flag_data[1]), 0, (sockaddr *) &from, sizeof(from));
                    std::cout << "wrong CRC" << std::endl;
                    continue;
                }
                sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));
                memcpy((void*)&pos, buffer, beg_flags_len);

                if(pos == flag_length[0]) {
//                    sendto(sockfd, flag_length, sizeof(flag_length[1]), 0, (sockaddr *) &from, sizeof(from));
                    continue;
                }
                if(pos < 0 || pos == pos_prev || pos != pos_exp) {
                    continue;
                }

                sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));

                pos_prev = pos;

                pos_exp = pos + recv_len - beg_flags_len;
                std::cout << "received data " << pos << ", next exp: " << pos_exp << std::endl;
                fwrite(buffer + beg_flags_len, recv_len - beg_flags_len, 1, file_out);
                MD5_Update(&md5_ctx, buffer + 4, recv_len - beg_flags_len);
                break;
            }
        } while(pos_exp < length);
    }
    char *receive_hash(){
        char *hash;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            short crc = strip_CRC(recv_len);
            if(!check_CRC(crc, recv_len)) {
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

        MD5_Final((unsigned char*)buffer, &md5_ctx);
        if(memcmp(hash, buffer, 4) == 0)
            std::cout << "Hash is OK" << std::endl;
        if(memcmp(hash, buffer, 4) != 0)
            std::cout << "Hash does not match" << std::endl;

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

        MD5_Init(&md5_ctx);
    }
    ~Receiver(){
        close(sockfd);
    }
    void receive(unsigned int recv_max_rate){
        char *file_name = receive_name(recv_max_rate);
        std::cout << "name " << file_name << std::endl;

        int length = receive_length();
        std::cout << "length: " << length << std::endl;

        FILE *file_out = fopen(file_name, "w");
        receive_data(file_out, length);

        char *hash = receive_hash();

        fclose(file_out);
    }
};

int main(int argc, char** argv) {
    Receiver receiver;
    if(argc > 1) {
        receiver.receive(atoi(argv[1]));
    }
    else receiver.receive(INT32_MAX);
    return 0;
}