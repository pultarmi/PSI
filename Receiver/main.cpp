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
#include <fcntl.h>
#include <unistd.h>
#include <deque>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdexcept>
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
const int flag_end[] = {-9, -10};
const int flag_auth[] = {-11, -12, -13, -14};

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
}
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
}

class Auth_key{
public:
    int n, ex;
    Auth_key(){}
    Auth_key(int n, int ex){
        this->n = n;
        this->ex = ex;
    }
};

class Receiver {
private:
    struct sockaddr_in local, from;
    socklen_t fromlen;
    int sockfd;
    char buffer[BUFFERS_LEN];
    ssize_t recv_len;
    MD5_CTX md5_ctx;
    unsigned char beg_flags_len = 4;
    static const unsigned short CRC_LEN_bits = 16;
    static const unsigned short CRC_LEN_bytes = CRC_LEN_bits / 8;
    std::deque<int> got;
//    Auth_key key_receiver_public;
    Auth_key key_receiver_private;
    Auth_key key_sender_public;

    inline short strip_CRC(ssize_t &recv_len){
        short crc;
        recv_len -= CRC_LEN_bytes;
        memcpy((void*)&crc, buffer+recv_len, CRC_LEN_bytes);
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
            if(!check_CRC(crc, recv_len) || memcmp(buffer, flag_name, beg_flags_len) != 0) {
                std::cout << "wrong CRC" << std::endl;
                sendto(sockfd, flag_name+1, sizeof(flag_name[1]), 0, (sockaddr *) &from, sizeof(from));
                continue;
            }
            std::cout << "OK, len: " << recv_len << std::endl;
            recv_len -= beg_flags_len;
            file_name = new char[recv_len + 1];
            file_name[recv_len] = 0;
            memcpy(file_name, buffer+beg_flags_len, recv_len);

            std::cout << "sending rate = " << recv_max_rate << std::endl;
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
            if(!check_CRC(crc, recv_len) || memcmp(buffer, flag_length, beg_flags_len) != 0) {
                sendto(sockfd, flag_length+1, sizeof(flag_length[1]), 0, (sockaddr *) &from, sizeof(from));
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
    void receive_data(unsigned char *mapped_output, int length){
        int pos = 0;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            short crc = strip_CRC(recv_len);
            if(!check_CRC(crc, recv_len)) {
                sendto(sockfd, flag_data+1, sizeof(flag_data[1]), 0, (sockaddr *) &from, sizeof(from));
                int flag;
                memcpy((void*)&flag, buffer, 4);
                std::cout << "wrong CRC, has flag: " << flag << std::endl;
                continue;
            }
            sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));

            memcpy((void*)&pos, buffer, beg_flags_len);
            if(pos == flag_end[0]) {
                std::cout << "end of data received" << std::endl;
                return;
            }

            if(pos < 0)
                continue;

            bool found=false;
            for(unsigned int aa=0; aa < got.size(); aa++){
                if(got.at(aa) == pos)
                    found = true;
            }
            if(found)
                continue;

            got.push_back(pos);
            if(got.size() > 100)
                got.pop_front();

            std::cout << "received data " << pos << std::endl;
            memcpy(mapped_output+pos, buffer+beg_flags_len, recv_len - beg_flags_len);
        }
    }
    char *receive_hash(unsigned char *mapped_output, int out_size){
        char *hash;
        while(true) {
            recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);
            short crc = strip_CRC(recv_len);
            if( !check_CRC(crc, recv_len) || memcmp(buffer, flag_hash, beg_flags_len) != 0 ) {
                sendto( sockfd, flag_hash+1, sizeof(flag_hash[1]), 0, (sockaddr *) &from, sizeof(from) );
                continue;
            }
            sendto(sockfd, buffer, beg_flags_len, 0, (sockaddr *) &from, sizeof(from));

            if( memcmp(buffer, flag_hash, sizeof(flag_hash[0])) != 0 )
                continue;

            recv_len -= beg_flags_len;
            hash = new char[recv_len + 1];
            hash[recv_len] = 0;
            memcpy(hash, buffer+beg_flags_len, recv_len);
            break;
        }

        std::cout << "validating hash..." << std::endl;

        for(int i=0; i < out_size; i++)
            MD5_Update(&md5_ctx, mapped_output + i, 1);

        MD5_Final((unsigned char*)buffer, &md5_ctx);
        if(memcmp(hash, buffer, 4) == 0)
            std::cout << "Hash is OK" << std::endl;
        else std::cout << "Hash does not match" << std::endl;
        return hash;
    }

    inline bool authorize(const unsigned int w_number){
        char buffer[BUFFERS_LEN];

        recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr *) &from, &fromlen);

        unsigned int recv_number;
        memcpy((void*)&recv_number, buffer+4, sizeof(recv_number));
        unsigned int in_number = recv_number;
        for(int i=1; i < key_sender_public.ex; i++){
            in_number *= recv_number;
            in_number %= key_sender_public.n;
        }
        std::cout << "Got number " << in_number << std::endl;

        unsigned int out_number = w_number;
        for(int i=1; i < key_receiver_private.ex; i++){
            out_number *= w_number;
            out_number %= key_receiver_private.n;
        }
        memcpy(buffer, (void*)&flag_auth, beg_flags_len);
        memcpy(buffer+sizeof(out_number), (void*)&out_number, sizeof(out_number));
        sendto(sockfd, buffer, beg_flags_len + sizeof(out_number), 0, (sockaddr *) &from, sizeof(from));

        return in_number == w_number;
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

    void set_key_receiver(int n, int d){
//        key_receiver_public.n = n;
//        key_receiver_public.ex = e;

        key_receiver_private.n = n;
        key_receiver_private.ex = d;
    }

    void set_key_sender(int n, int e){
        key_sender_public.n = n;
        key_sender_public.ex = e;
    }

    void receive(unsigned int w_number, unsigned int recv_max_rate){
        if(!authorize(w_number))
            throw std::runtime_error("Authorization failed");

        char *file_name = receive_name(recv_max_rate);
        std::cout << "name " << file_name << std::endl;

        int length = receive_length();
        std::cout << "length: " << length << std::endl;

        int fd_out = open(file_name, O_RDWR|O_CREAT, 0644);
        lseek (fd_out, length-1, SEEK_SET);
        if(write (fd_out, "1", 1) == -1) throw("sth really bad");
        lseek (fd_out, 0, SEEK_SET);
        unsigned char *mapped_output = (unsigned char*)mmap(NULL, length, PROT_WRITE, MAP_SHARED, fd_out, 0);
        receive_data(mapped_output, length);

        char *hash = receive_hash(mapped_output, length);

        munmap(mapped_output, length);
    }
};

int main(int argc, char** argv) {
    Receiver receiver;

    receiver.set_key_receiver(/*n*/323, /*d*/79); // n=323,e=31,d=79
    receiver.set_key_sender(/*n*/3233, /*e*/17); // n=3233,e=17,d=413

    if(argc < 2){
        throw std::invalid_argument("Please provide welcome number");
    }
    unsigned int w_number = atoi(argv[1]);

    if(argc > 2)
        receiver.receive(w_number, atoi(argv[2]));
    else receiver.receive(w_number, INT32_MAX);
    return 0;
}