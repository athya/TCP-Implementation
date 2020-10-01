#include <string>

#define FLAG_SET 65535
#define NOT_SET 0
#define BODY_SIZE 512
#define HEADER_SIZE 12
#define MAX_SEQ_NUM 25600
#define WINDOW_SIZE 5120

struct Header {
    __uint16_t seqNum;
    __uint16_t ackNum;
    __uint16_t ackFlag;
    __uint16_t synFlag;
    __uint16_t finFlag;
    __uint8_t padding;
};

struct Message {
    struct Header head;
    char body[BODY_SIZE];
};

std::string headerString(struct Header* head);

int updateNum(int cur, int update);
