
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define CRCTEST

using namespace std;

// CRC16 (ibm/modbus?) using polynomial 1 1000 0000 0000 0101 (18005h)
// acts weirdly, probably a new nonstandard algorithm
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

#ifdef CRCTEST
int main(int argc, char** argv){
    const char kebab[6] = "kebab";
    unsigned short crc = crc16ibm("kebab",5);
    printf("CRC for \"kebab\" (%2x%2x%2x%2x%2x):\nhex %hx.\n", kebab[0],kebab[1],kebab[2],kebab[3],kebab[4], crc);
    puts("Should be 0xA216.");

    crc = crc16ibm("*",1);
    printf("CRC for \"*\" (2A):\nhex %hx.\n", crc);
    puts("Should be 0x9F3E.");

    crc = crc16ibm("My hovercraft is full of eels.",30);
    printf("CRC for \"My hovercraft is full of eels.\":\noct %ho, dec %hd, hex %hx.\n",crc, crc, crc);
    puts("Should be 0x9FEB.");

    crc = crc16ibm("supercalifragilisticexpialidocious",34);
    printf("CRC for \"supercalifragilisticexpialidocious\":\noct %ho, dec %hd, hex %hx.\n",crc, crc, crc);
    puts("Should be 0x751F.");
    return 0;
}
#endif

