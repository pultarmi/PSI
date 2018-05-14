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
#include <deque>
#include <openssl/md5.h>

#define BUFFERS_LEN 1024

#define TARGET_PORT 6666
#define LOCAL_PORT 5555

#define SENDRATE 90
#define SENDRATEMOD 100
#define WINDOW_SIZE 8

const int flag_name[] = {-1, -2};
const int flag_length[] = {-3, -4};
const int flag_hash[] = {-5, -6};
const int flag_data[] = {-7, -8};
const int flag_end[] = {-9, -10};

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
}

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
}

class Packet{
public:
    int flag, size;
    char data[BUFFERS_LEN];
    Packet(int flag, int size, char *data){
        this->flag = flag;
        this->size = size;
        for(int i=0; i < size; i++)
            this->data[i] = data[i];
    }
};

class Sender{
private:
    int sockfd;
    struct sockaddr_in local, addrDest;
    socklen_t fromlen;
    MD5_CTX md5_ctx;
    static const unsigned int beg_flags_len = 4;
    static const unsigned short CRC_LEN_bits = 16;
    static const unsigned short CRC_LEN_bytes = CRC_LEN_bits / 8;
    unsigned int glob_max_rate;
    std::deque<Packet> sent_packets;
    clock_t oldclock;

    inline void random_distort(char *buffer, const int size){
        if(rand() % 200 < 5) {
            int pos = rand() % size;
            buffer[pos] ^= 1UL << (rand() % 8); // modify one bit
        }
    }

    inline short count_crc(char *buffer, const int size){
        return crc_16((unsigned char*)buffer, size);
    }


    inline void recv_acks(){
        char buffer[BUFFERS_LEN];
        // receive response
        while(recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &addrDest, &fromlen) != -1) {
            // whether response matches the sent flag (or position in case of data sending),
            // save the received flags to the position behind the sent flags

            std::deque<Packet> filtered_sent_flags;
            for (unsigned int i = 0; i < sent_packets.size(); i++) {

                if (memcmp(buffer, (void *) &sent_packets.at(i), beg_flags_len) == 0) {
//                    std::cout << "A" << std::endl;

                    if (memcmp(buffer, flag_name, sizeof(flag_name[0])) == 0) {
                        std::cout << "received NAME ACK" << std::endl;
                        unsigned int recv_max_rate;
                        memcpy((void *) &recv_max_rate, buffer + beg_flags_len, 4);
                        std::cout << "received rate " << recv_max_rate << " packets/s" << std::endl;
                        glob_max_rate = std::min(recv_max_rate, glob_max_rate);
                    } else if (memcmp(buffer, flag_length, sizeof(flag_length[0])) == 0)
                        std::cout << "received LENG ACK" << std::endl;
                    else if (memcmp(buffer, flag_hash, sizeof(flag_hash[0])) == 0)
                        std::cout << "received HASH ACK" << std::endl;
                } else filtered_sent_flags.push_back(sent_packets.at(i));

            }
            sent_packets = move(filtered_sent_flags);
        }
    }

    inline void send_with_rate(char *buffer, int size){
        clock_t newclock;

        newclock = clock();
        unsigned int int_length = (1/(double)glob_max_rate)*CLOCKS_PER_SEC;
        if ( (newclock - oldclock) < int_length ){
            unsigned int remaining_clocks_to_wait = (int_length - (newclock - oldclock));
            unsigned int mics_to_wait = (double)1000000*remaining_clocks_to_wait / ((double)CLOCKS_PER_SEC);
            usleep(mics_to_wait);
            // assuming CLOCKS_PER_SEC to be 1000000
        }
        oldclock = clock();

        sendto(sockfd, buffer, size, 0, (sockaddr *) &addrDest, sizeof(addrDest));
    }

    inline void wait_till_sent_num_at_max(int threshold){
        if(sent_packets.size() == WINDOW_SIZE)
            std::cout << "Wfull" << std::endl;
        while (sent_packets.size() > threshold) {
//            sendto(sockfd, sent_packets.front().data, sent_packets.front().size, 0, (sockaddr *) &addrDest, sizeof(addrDest));
            send_with_rate( sent_packets.front().data, sent_packets.front().size );
            recv_acks();
        }
    }

    inline void finalize_sending(bool send_end_flag=false){
        char buffer[BUFFERS_LEN];
        std::cout << "clearing window..." << std::endl;
        wait_till_sent_num_at_max(0);

        if(send_end_flag){
            memcpy(buffer, flag_end, sizeof(flag_end[0]));
            short crc = count_crc(buffer, beg_flags_len);
            memcpy(buffer+beg_flags_len, (void*)&crc, CRC_LEN_bytes);
            std::cout << "signaling end" << std::endl;
            sent_packets.emplace_back(Packet(flag_end[0], beg_flags_len+CRC_LEN_bytes, buffer));
//            sendto(sockfd, buffer, beg_flags_len+CRC_LEN_bytes, 0, (sockaddr*)&addrDest, sizeof(addrDest));

            send_with_rate( buffer, beg_flags_len+CRC_LEN_bytes );

            wait_till_sent_num_at_max(0);
        }
    }

    inline void send_datagram(char *buffer, unsigned int size){
        if(size + CRC_LEN_bytes > BUFFERS_LEN)
            std::cout << "assertion fault" << std::endl;

        short crc = count_crc(buffer, size);
        //append crc to the end
        memcpy(buffer+size, (void*)&crc, CRC_LEN_bytes);

        int sent_flag;
        memcpy((void*)&sent_flag, buffer, beg_flags_len);
        sent_packets.emplace_back(Packet(sent_flag, size+CRC_LEN_bytes, buffer));

        wait_till_sent_num_at_max(WINDOW_SIZE - 1);

        // backup_buffer before the distortion
        char buffer_backup[BUFFERS_LEN];
        memcpy(buffer_backup, buffer, BUFFERS_LEN);
        //restore backup
        memcpy(buffer, buffer_backup, BUFFERS_LEN);
        // randomly change one bit
        random_distort(buffer, size);

        // randomly forget to send
        int rnd = rand() % SENDRATEMOD;
        if(rnd < SENDRATE) {
            std::flush(std::cout);
//            sendto(sockfd, buffer, size + CRC_LEN_bytes, 0, (sockaddr *) &addrDest, sizeof(addrDest));
            send_with_rate( buffer, size + CRC_LEN_bytes );
        }

        std::flush(std::cout);
        recv_acks();
    }

    inline void send_name(const char *name){
        char buffer[BUFFERS_LEN];
        memcpy( buffer, (void*)&flag_name, sizeof(flag_name[0]) );
        memcpy( buffer+sizeof(flag_name[0]), name, BUFFERS_LEN );
        send_datagram( buffer, sizeof(flag_name[0])+strlen(name) );
    }

    inline void send_length(FILE* file_in){
        char buffer[BUFFERS_LEN];
        fseek(file_in, 0L, SEEK_END);
        //get size of the file
        long file_size = ftell(file_in);
        // copy length flag to the beggining of the buffer
        memcpy( buffer, (void*)&flag_length, sizeof(flag_length[0]) );
        // copy the length to behind the flags
        memcpy( buffer+sizeof(flag_length[0]), (void*)&file_size, sizeof(long) );
        // send the buffer
        send_datagram( buffer, sizeof(flag_length[0])+sizeof(long) );
        rewind(file_in);
    }

    inline void send_data(FILE* file_in, unsigned int maxrate){
        size_t chars_read;
        int pos = 0;
        clock_t oldclock = clock(); // used for flow control
        clock_t newclock;
        unsigned int sent = 0;
        std::cout << "Sending data";

        char buffer[BUFFERS_LEN];
        while((chars_read = fread(buffer+beg_flags_len, 1, BUFFERS_LEN - beg_flags_len - CRC_LEN_bytes, file_in)) > 0) {
//            maxrate = std::min(maxrate, recv_max_rate);

            MD5_Update(&md5_ctx, buffer + 4, chars_read);

            memcpy(buffer, (void*)&pos, beg_flags_len);
            send_datagram( buffer, beg_flags_len+chars_read );
            pos += chars_read;
            std::cout << ".";
            std::flush(std::cout);

            ++sent;
            newclock = clock();
            unsigned int int_length = (1/(double)maxrate)*CLOCKS_PER_SEC;
            if ( (newclock - oldclock) < int_length ){
                unsigned int remaining_clocks_to_wait = (int_length - (newclock - oldclock));
                unsigned int mics_to_wait = (double)1000000*remaining_clocks_to_wait / ((double)CLOCKS_PER_SEC);
                usleep(mics_to_wait);
                // assuming CLOCKS_PER_SEC to be 1000000
            }
            oldclock = clock();
        }
        std::cout << std::endl;
    }

    inline void send_hash(){
        char buffer[BUFFERS_LEN];
        MD5_Final( (unsigned char*)buffer + beg_flags_len, &md5_ctx );
        memcpy( buffer, flag_hash, sizeof(flag_hash[0]) );
        send_datagram( buffer, sizeof(flag_hash[0])+4 );
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
        glob_max_rate = maxrate;
        oldclock = clock(); // used for flow control

        send_name(name);
        finalize_sending();
        FILE* file_in = fopen(name, "rb");
        send_length(file_in);
        finalize_sending();

        send_data(file_in, maxrate);
        finalize_sending(true);

        send_hash();
        finalize_sending();

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
    unsigned int maxrate = INT32_MAX;
    if(argc > 3) maxrate = atoi(argv[3]);
    sender.send(argv[2], maxrate);

    return 0;
}
