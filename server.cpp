#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <string>
#include <iostream>
#include "header.h"
#include <fstream>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int listenfd;
    socklen_t len;
    struct sockaddr_in servaddr, cliaddr;
    bzero(&servaddr, sizeof(servaddr));
    
    int port = atoi(argv[1]);
    int count = 1;
    
    while(1) {
        // create socket
        listenfd = socket(AF_INET, SOCK_DGRAM, 0);
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        servaddr.sin_port = htons(port);
        servaddr.sin_family = AF_INET;
        
        int oldfl;
        oldfl = fcntl(listenfd, F_GETFL);
        fcntl(listenfd, F_SETFL, oldfl & ~O_NONBLOCK);
        
        // bind server address to socket descriptor
        bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
        
        /* HANDSHAKE */
        struct Message recvMsg;
        int curSeqNum;
        int curAckNum;
        int msgSize;
        
        int receivedSyn = 0;
        srand(time(NULL));
        curSeqNum = rand() % (MAX_SEQ_NUM + 1);
        while(1){
            len = sizeof(cliaddr);
            memset(&recvMsg, 0, sizeof(recvMsg));
            // receive message
            msgSize = recvfrom(listenfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr*)&cliaddr,&len);
            if (recvMsg.head.synFlag == FLAG_SET) { // on SYN
                std::cout << "RECV " << headerString(&(recvMsg.head)) << std::endl;
                receivedSyn++;
                // send SYN-ACK message
                curAckNum = updateNum(recvMsg.head.seqNum,1);
                
                struct Header synAckMsg;
                memset(&synAckMsg, 0, sizeof(synAckMsg));
                synAckMsg.ackFlag = FLAG_SET;
                synAckMsg.synFlag = FLAG_SET;
                synAckMsg.ackNum = curAckNum;
                synAckMsg.seqNum = curSeqNum;
                
                sendto(listenfd, (void *)&synAckMsg, sizeof(synAckMsg), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
                if (receivedSyn > 1){
                    std::string printString = headerString(&synAckMsg);
                    printString = printString.substr(0, printString.find("ACK")) + "DUP-ACK";
                    std::cout << "SEND " << printString << std::endl;
                } else {
                    std::cout << "SEND " << headerString(&synAckMsg) << std::endl;
                }
            } else if (recvMsg.head.ackFlag == FLAG_SET && receivedSyn) { // receive ACK (final part of handshake)
                std::cout << "RECV " << headerString(&(recvMsg.head)) << std::endl;
                curSeqNum = updateNum(curSeqNum,1); // should be the same as the ack num received
                break;
            }
        }
        
        /* RESPOND TO CLIENT ACK */
        
        std::ofstream MyFile;
        MyFile.open(std::to_string(count) + ".file", std::fstream::out | std::fstream::trunc | std::fstream::binary);
        MyFile.write(recvMsg.body, msgSize - HEADER_SIZE);
        
        struct Header sendHdr;
        
        // On receipt of packet, send ACK
        memset(&sendHdr, 0, sizeof(sendHdr));
        sendHdr.ackFlag = FLAG_SET;
        sendHdr.seqNum = curSeqNum;
        sendHdr.ackNum = updateNum(recvMsg.head.seqNum,(msgSize - HEADER_SIZE));
        sendto(listenfd, (void *)&sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
        std::cout << "SEND " << headerString(&sendHdr) << std::endl;
        
        int expectedSeqNum = sendHdr.ackNum;
        
        while(1){
            // Receive new packet
            memset(&recvMsg, 0, sizeof(recvMsg));
            msgSize = recvfrom(listenfd, &recvMsg, sizeof(recvMsg), 0, (struct sockaddr*)&cliaddr,&len);
            std::cout << "RECV " << headerString(&(recvMsg.head)) << std::endl;
            
            // if FIN, then begin termination process
            if (recvMsg.head.finFlag == FLAG_SET) {
                curAckNum = updateNum(recvMsg.head.seqNum, 1);

                // SEND ACK for FIN
                sendHdr.ackNum = curAckNum;
                sendHdr.seqNum = curSeqNum; // redundant
                sendHdr.ackFlag = FLAG_SET;
                sendHdr.synFlag = NOT_SET;
                sendHdr.finFlag = NOT_SET;
                sendto(listenfd, &sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
                std::cout << "SEND " << headerString(&sendHdr) << std::endl;
                break;
            } else { // otherwise send back ACK
                // if in-order segment
                if (recvMsg.head.seqNum == expectedSeqNum) {
                    MyFile.write(recvMsg.body, msgSize - HEADER_SIZE);
                    
                    // On receipt of packet, send ACK
                    memset(&sendHdr, 0, sizeof(sendHdr));
                    sendHdr.ackFlag = FLAG_SET;
                    sendHdr.seqNum = curSeqNum; // shouldn't change
                    sendHdr.ackNum = updateNum(recvMsg.head.seqNum,(msgSize - HEADER_SIZE));
                    sendto(listenfd, (void *)&sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
                    std::cout << "SEND " << headerString(&sendHdr) << std::endl;
                    expectedSeqNum = sendHdr.ackNum;
                } else {
                    // Out of order packet
                    memset(&sendHdr, 0, sizeof(sendHdr));
                    sendHdr.ackFlag = FLAG_SET;
                    sendHdr.seqNum = curSeqNum;
                    sendHdr.ackNum = expectedSeqNum;
                    sendto(listenfd, (void *)&sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
                    std::string printString = headerString(&sendHdr);
                    printString = printString.substr(0, printString.find("ACK")) + "DUP-ACK";
                    std::cout << "SEND " << printString << std::endl;
                }
            }
        }
        
        // SEND FIN
        sendHdr.seqNum = curSeqNum;
        sendHdr.ackNum = 0;
        sendHdr.ackFlag = NOT_SET;
        sendHdr.synFlag = NOT_SET;
        sendHdr.finFlag = FLAG_SET;
        sendto(listenfd, &sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
        std::cout << "SEND " << headerString(&sendHdr) << std::endl;
        std::chrono::high_resolution_clock::time_point twoSecTimer = std::chrono::high_resolution_clock::now();
        
        fcntl(listenfd, F_SETFL, O_NONBLOCK);
        /* CLOSE CONNECTION */
        while (1) {
            std::chrono::high_resolution_clock::time_point curTime = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(curTime - twoSecTimer);
            if (time_span.count() >= 2){
                break;
            } else if (time_span.count() >= 0.5) {
                std::cout << "TIMEOUT " << headerString(&sendHdr) << std::endl;
                sendHdr.seqNum = curSeqNum; // doesn't change, but just for ref
                sendHdr.ackNum = 0;
                sendHdr.ackFlag = NOT_SET;
                sendHdr.synFlag = NOT_SET;
                sendHdr.finFlag = FLAG_SET;
                sendto(listenfd, &sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
                std::cout << "RESEND " << headerString(&sendHdr) << std::endl;
                twoSecTimer = std::chrono::high_resolution_clock::now();
            }
            // if get another FIN, resend ack
            if (recvfrom(listenfd, &(recvMsg.head), sizeof(recvMsg.head), 0, (struct sockaddr*)&cliaddr,&len) > 0) {
                std::cout << "RECV " << headerString(&recvMsg.head) << std::endl;
                if (recvMsg.head.finFlag == FLAG_SET) {
                    curAckNum = updateNum(recvMsg.head.seqNum, 1);
                    // SEND ACK for FIN
                    sendHdr.ackNum = curAckNum;
                    sendHdr.seqNum = curSeqNum; 
                    sendHdr.ackFlag = FLAG_SET;
                    sendHdr.synFlag = NOT_SET;
                    sendHdr.finFlag = NOT_SET;
                    sendto(listenfd, &sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr));
                    std::cout << "RESEND " << headerString(&sendHdr) << std::endl;
                } else if (recvMsg.head.ackFlag == FLAG_SET) {
                    break;
                }
            }
        }
        MyFile.close();
        count++;
        close(listenfd);
    }
}
