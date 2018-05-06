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

/*
//#define TARGET_IP	"192.168.0.157"
#define TARGET_IP "192.168.43.138"
//#define TARGET_IP	"192.168.0.129"
//#define TARGET_IP	"192.168.43.10"
*/

#define BUFFERS_LEN 1024

#define TARGET_PORT 6666
#define LOCAL_PORT 5555

#define SENDRATE 20
#define SENDRATEMOD 100

//TODO selective repeat

const int flag_name[] = {-1, -2};
const int flag_length[] = {-3, -4};
const int flag_hash[] = {-5, -6};
const int flag_data[] = {-7, -8};

#define	CRC_START_16 0x0000
#define	CRC_POLY_16	0xA001
static bool crc_tab16_init = false;
static uint16_t crc_tab16[256];
static void init_crc16_tab(void) {
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
    if (!crc_tab16_init) init_crc16_tab();
    crc = CRC_START_16;
    ptr = input_str;
    if (ptr != NULL) for (a=0; a<num_bytes; a++) {
            crc = (crc >> 8) ^ crc_tab16[(crc ^ (uint16_t) *ptr++) & 0x00FF ];
        }
    return crc;
}  /* crc_16 */

class Sender{
private:
    int sockfd;
    struct sockaddr_in local, addrDest;
    socklen_t fromlen;
    char buffer[BUFFERS_LEN], buffer_backup[BUFFERS_LEN];
    MD5_CTX md5_ctx;
    static const unsigned int beg_flags_len = 4;
    static const unsigned short CRC_length = 16;
    unsigned int recv_max_rate;

    inline void random_distort(const int size, const short crc){
        if(rand() % 200 < 5) {
            int pos = rand() % size;
            buffer[pos] ^= 1UL << (rand() % 8); // modify one bit
        }
    }

    inline short count_crc(const int size){
        return crc_16((unsigned char*)buffer, size);
    }

    inline void send_datagram(unsigned int size){
        assert(size + CRC_length <= BUFFERS_LEN);

        short crc = count_crc(size);
        memcpy(buffer+size, (void*)&crc, CRC_length);

        memcpy(buffer_backup, buffer, BUFFERS_LEN);
        while(true){
            memcpy(buffer, buffer_backup, BUFFERS_LEN);
            random_distort(size, crc);

            int rnd = rand() % SENDRATEMOD;
            if(rnd < SENDRATE) sendto(sockfd, buffer, size+CRC_length, 0, (sockaddr*)&addrDest, sizeof(addrDest));

            while(recvfrom(sockfd, buffer+beg_flags_len, sizeof(buffer)-beg_flags_len, 0, (sockaddr *) &addrDest, &fromlen) != -1){
                if(memcmp(buffer+beg_flags_len, buffer_backup, beg_flags_len) == 0) {
                    if (memcmp(buffer, flag_name, sizeof(flag_name[0])) == 0) {
                        std::cout << "received NAME ACK" << std::endl;
                        memcpy((void*)&recv_max_rate, buffer+beg_flags_len+4, 4);
                        std::cout << "received rate " << recv_max_rate << " packets/s" << std::endl;
                    }
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

    inline void send_data(FILE* file_in, unsigned int maxrate){
        maxrate = std::min(maxrate, recv_max_rate);
        std::cout << "using rate " << maxrate << " packets/s" << std::endl;

        size_t chars_read;
        int pos = 0;
        clock_t oldclock = clock(); // used for flow control
        clock_t newclock = clock();
        unsigned int sent = 0;
        std::cout << "Sending data";
        while((chars_read = fread(buffer+beg_flags_len, 1, BUFFERS_LEN - beg_flags_len - CRC_length, file_in)) > 0) {
            MD5_Update(&md5_ctx, buffer + 4, chars_read);

            memcpy(buffer, (void*)&pos, beg_flags_len);
            send_datagram(beg_flags_len+chars_read);
            pos += chars_read;
            std::cout << ".";
            std::flush(std::cout);

            ++sent;
            newclock = clock();
//            std::cout << "interval " << ((1/(double)maxrate)*CLOCKS_PER_SEC) << std::endl;
            unsigned int int_length = (1/(double)maxrate)*CLOCKS_PER_SEC;
            if ((newclock - oldclock) < int_length && maxrate != 0){
//                usleep(CLOCKS_PER_SEC - newclock + oldclock);
                unsigned int remaining_clocks_to_wait = (int_length - (newclock - oldclock));
                unsigned int mics_to_wait = (double)1000000*remaining_clocks_to_wait / ((double)CLOCKS_PER_SEC);
//                std::cout << mics_to_wait << std::endl;
                usleep(mics_to_wait);
                // assuming CLOCKS_PER_SEC to be 1000000
            }
            oldclock = clock();

        }
        std::cout << std::endl;
    }

    inline void send_hash(){
        MD5_Final((unsigned char*)buffer + beg_flags_len, &md5_ctx);
        memcpy(buffer, flag_hash, sizeof(flag_hash[0]));
        send_datagram(sizeof(flag_hash[0])+4);
    }

public:
    Sender(char* target_ip){
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
        inet_pton(AF_INET, target_ip, &addrDest.sin_addr.s_addr);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

        MD5_Init(&md5_ctx);

        srand(time(NULL));
    }

    void send(const char *name, int maxrate){
        send_name(name);
        FILE* file_in = fopen(name, "rb");
        send_length(file_in);
        send_data(file_in, maxrate);
        send_hash();
        close(sockfd);
        fclose(file_in);
    }
};


int main(int argc, char** argv) {
    if(argc < 3){
        std::cout << "Usage: ./" << argv[0] << " <Target IP> <Relative Path> <Maximum Packet Rate>" << std::endl;
        return 1;
    }
    Sender sender(argv[1]);
    unsigned int maxrate = 0;
    if(argc > 3) maxrate = atoi(argv[3]);
    sender.send(argv[2], maxrate);

    return 0;
}
