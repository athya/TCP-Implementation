#include "header.h"
#include <string>

std::string headerString(struct Header* head){
    std::string response = std::to_string((int)head->seqNum) + " " + std::to_string((int)head->ackNum);
    if (head->synFlag == FLAG_SET) {
        response += " SYN";
    }
    if (head->finFlag == FLAG_SET) {
        response += " FIN";
    }
    if (head->ackFlag == FLAG_SET) {
        response += " ACK";
    }
    return response;
}

int updateNum(int cur, int update) {
    return (cur + update) % (MAX_SEQ_NUM + 1); //TEST EDGE CASES
}
