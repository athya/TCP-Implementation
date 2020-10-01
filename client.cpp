#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <iostream>
#include <fstream>
#include "header.h"
#include <chrono>
#include <fcntl.h>
#include <sys/time.h>

struct PacketMsg {
    struct Message msg;
    int size;
};

int main(int argc, char *argv[])
{
    int sockfd, n;
    struct sockaddr_in servaddr;
    
    // parse command line
    std::string hostname = argv[1];
    if (hostname.compare("localhost") == 0) {
        hostname = "127.0.0.1";
    }
    int port = atoi(argv[2]);
    std::string filename = argv[3];
    
    // open file
    std::ifstream fs;
    fs.open (filename);
    if (!fs) {
        perror("Unable to open file");
        exit(1);
    }
    
    // clear servaddr
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_addr.s_addr = inet_addr(hostname.c_str());
    servaddr.sin_port = htons(port);
    servaddr.sin_family = AF_INET;
    
    // create socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    // connect to server
    if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        printf("\n Error : Connect Failed \n");
        exit(0);
    }
    
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    
    /* HANDSHAKE */
    
    // SYN, SYN-ACK
    int notAcked = 1;
    
    // initial SYN message
    struct Header synMessage;
    memset(&synMessage, 0, sizeof(synMessage));
    synMessage.synFlag = FLAG_SET;
    srand(time(NULL));
    int curSeqNum = rand() % (MAX_SEQ_NUM + 1);
    synMessage.seqNum = curSeqNum;
    sendto(sockfd, (void *) &synMessage, sizeof(synMessage), 0, (struct sockaddr*)NULL, sizeof(servaddr));
    std::cout << "SEND " << headerString(&synMessage) << std::endl;
    std::chrono::high_resolution_clock::time_point initTime = std::chrono::high_resolution_clock::now();
    int curAckNum;
    
    // if time, rewrite this logic
    while(notAcked){
        // waiting for SYN-ACK message
        int receivedAck;
        struct Header synAckMsg;
        memset(&synAckMsg, 0, sizeof(synAckMsg));
        if(recvfrom(sockfd, &synAckMsg, sizeof(synAckMsg), 0, (struct sockaddr*)NULL, NULL) > 0){
            // check if SYN ACK
            notAcked = 0;
            std::cout << "RECV " << headerString(&synAckMsg) << std::endl;
            curAckNum = synAckMsg.seqNum;
            break;
        }

        // Send initial SYN message
        std::chrono::high_resolution_clock::time_point checkSynTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> synSpan  = std::chrono::duration_cast<std::chrono::duration<double>>(checkSynTime - initTime);
        //std::cerr << synSpan.count() << std::endl;
        if (synSpan.count() >= 0.5){
            std::cout << "TIMEOUT " << synMessage.seqNum << std::endl;
            sendto(sockfd, (void *) &synMessage, sizeof(synMessage), 0, (struct sockaddr*)NULL, sizeof(servaddr));
            std::cout << "RESEND " << headerString(&synMessage) << std::endl;
            initTime = std::chrono::high_resolution_clock::now();
        }
    }
    curSeqNum = updateNum(curSeqNum, 1);
    // figure out when we increment stuff
    
    // send ACK message
    struct Header sendHdr;
    memset(&sendHdr, 0, sizeof(sendHdr));
    int absoluteBase = curSeqNum; // first ever seq num with data
    int sendBase = curSeqNum; // base of sending window
    int nextSeqNum; // seqNum of packet to be sent
    
    sendHdr.seqNum = curSeqNum;
    sendHdr.ackNum = updateNum(curAckNum,1);
    sendHdr.ackFlag = FLAG_SET; // set for first ACK back to server
    
    struct Message sendMsg;
    memset(&sendMsg, 0, sizeof(sendMsg));
    sendMsg.head = sendHdr;
    fs.read(sendMsg.body, BODY_SIZE);
    int bodySize = fs.gcount();
    nextSeqNum = updateNum(curSeqNum, bodySize);
    
    sendto(sockfd, (void *) &sendMsg, bodySize + HEADER_SIZE, 0, (struct sockaddr*)NULL, sizeof(servaddr));
    std::cout << "SEND " << headerString(&(sendMsg.head)) << std::endl;
    std::chrono::high_resolution_clock::time_point packetTimer = std::chrono::high_resolution_clock::now();
    
    /* PIPELINE FILE CONTENTS */
    
    struct PacketMsg packetWindow[100];
    packetWindow[0].msg = sendMsg;
    packetWindow[0].size = bodySize;
    int begin = 0;
    int end = 1;
    int lastAck;
    
    struct Header recvHdr;
    while(nextSeqNum != sendBase){
        std::chrono::high_resolution_clock::time_point pipelineTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> oldestSpan  = std::chrono::duration_cast<std::chrono::duration<double>>(pipelineTime - packetTimer);
        if (oldestSpan.count() >= 0.5) { // oldest timeout
            // resend packets
            std::cout << "TIMEOUT " << sendBase << std::endl;
            int tempSeqNum = sendBase;
            packetTimer = std::chrono::high_resolution_clock::now();
            //std::cerr << begin << std::endl;
            //std::cerr << end << std::endl;
            for(int i = begin; i != end; i = (i + 1) % 100){
                sendto(sockfd, (void *) &packetWindow[i].msg, packetWindow[i].size + HEADER_SIZE, 0, (struct sockaddr*)NULL, sizeof(servaddr));
                std::cout << "RESEND " << headerString(&(packetWindow[i].msg.head)) << std::endl;
            }
        }
        int gap;
        if (nextSeqNum < sendBase) { // since modular
            gap = (MAX_SEQ_NUM - sendBase) + nextSeqNum;
        } else {
            gap = nextSeqNum - sendBase;
        }
        if (gap < WINDOW_SIZE && !fs.eof()) {
            
            memset(&sendMsg, 0, sizeof(sendMsg));
            curSeqNum = nextSeqNum; // should be same as updateNum(recvHdr.ackNum,1)
            sendHdr.seqNum = curSeqNum;
            sendHdr.ackNum = 0;
            sendHdr.ackFlag = NOT_SET;
            sendMsg.head = sendHdr;
            fs.read(sendMsg.body, BODY_SIZE);
            bodySize = fs.gcount();
            
            packetWindow[end].msg = sendMsg;
            packetWindow[end].size = bodySize;
            end = (end + 1) % 100;
            
            if (bodySize > 0) {
                sendto(sockfd, (void *) &sendMsg, bodySize + HEADER_SIZE, 0, (struct sockaddr*)NULL, sizeof(servaddr));
                std::cout << "SEND " << headerString(&(sendMsg.head)) << std::endl;
            }
            nextSeqNum = updateNum(curSeqNum, bodySize); ///????
        } else if (recvfrom(sockfd, &recvHdr, sizeof(recvHdr), 0, (struct sockaddr*)NULL, NULL) > 0) {
            std::cout << "RECV " << headerString(&recvHdr) << std::endl;
            if (recvHdr.ackNum != lastAck){
                // only reset timer if sendBase changed
                packetTimer = std::chrono::high_resolution_clock::now();
                begin = (begin + 1) % 100;
//                std::cerr << "UPDATEACK  WINDOW" << std::endl;
//                std::cerr << begin << std::endl;
//                std::cerr << end << std::endl;
            }
            sendBase = recvHdr.ackNum;
            lastAck = recvHdr.ackNum;
        }
    }
    
    /* CLOSE CONNECTION */
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    
    sendHdr.seqNum = recvHdr.ackNum;
    sendHdr.ackNum = 0;
    sendHdr.ackFlag = NOT_SET;
    sendHdr.finFlag = FLAG_SET;
    
    // SEND FIN
    
    int timesFinWasSent = 0;
    std::chrono::high_resolution_clock::time_point sendFinTime;
    int serverFinNum = 0;
    int timesFinWasAcked = 0;
    
    // Send init fin
    sendto(sockfd, (void *) &sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)NULL, sizeof(servaddr));
    std::cout << "SEND " << headerString(&sendHdr) << std::endl;
    sendFinTime = std::chrono::high_resolution_clock::now();
    timesFinWasSent++;
    std::chrono::high_resolution_clock::time_point twoSecTimer;
    struct Header finHdr = sendHdr;
    while (1) {
        std::chrono::high_resolution_clock::time_point curTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(curTime - sendFinTime);
        // if need to resend FIN on timeout
        if (serverFinNum > 0){
            std::chrono::duration<double> end_time_span = std::chrono::duration_cast<std::chrono::duration<double>>(curTime - twoSecTimer);
            if (end_time_span.count() >= 2) {
                break; //done!
            }
        }
        if (time_span.count() >= 0.5 && timesFinWasAcked == 0){
            std::cout << "TIMEOUT " << finHdr.seqNum << std::endl;
            sendto(sockfd, (void *) &finHdr, sizeof(finHdr), 0, (struct sockaddr*)NULL, sizeof(servaddr));
            sendFinTime = std::chrono::high_resolution_clock::now(); //reset timer
            std::cout << "RESEND " << headerString(&finHdr) << std::endl;
        } else if (recvfrom(sockfd, &recvHdr, sizeof(recvHdr), 0, (struct sockaddr*)NULL, NULL) > 0){
            if (recvHdr.ackFlag == FLAG_SET) {
                std::cout << "RECV " << headerString(&recvHdr) << std::endl;
                curSeqNum = recvHdr.ackNum;
                timesFinWasAcked++; // account for dups in msg?? see sample output
            } else if (recvHdr.finFlag == FLAG_SET) {
                serverFinNum++;
                if (serverFinNum == 1) {
                    twoSecTimer = std::chrono::high_resolution_clock::now();
                }
                std::cout << "RECV " << headerString(&recvHdr) << std::endl;
                sendHdr.seqNum = curSeqNum; //updated earlier upon ACK
                sendHdr.ackNum = updateNum(recvHdr.seqNum, 1);
                sendHdr.ackFlag = FLAG_SET;
                sendHdr.finFlag = NOT_SET;
                sendto(sockfd, (void *) &sendHdr, sizeof(sendHdr), 0, (struct sockaddr*)NULL, sizeof(servaddr));
                if (serverFinNum == 1) {
                    std::cout << "SEND " << headerString(&sendHdr) << std::endl;
                } else {
                    std::string printString = headerString(&sendHdr);
                    printString = printString.substr(0, printString.find("ACK")) + "DUP-ACK";
                    std::cout << "SEND " << printString << std::endl;
                }
            }
        }
    }
    
    // close the descriptor
    close(sockfd);
    fs.close();
}
