#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <byteswap.h>
#include <arpa/inet.h>

#include <list>
#include <mutex>

#define PORT1                      25
#define PORT2                    5888
#define QUERY_SIZE                128
#define RESPONSE_SIZE             408
#define ATTTEMPTS                  14
#define TIMEOUT          10*1000*1000

uint8_t query[QUERY_SIZE];

struct response
{
    uint32_t unknown;
    uint8_t  version[6];
    char     model[32];
    char     name[32];
    char     serial[34];
    uint32_t unknown2[3];
    char     ssid[64];
    char     password[64];
    uint32_t unknown3[3];
    uint8_t  zipcode[12];
    char     p2pm[16];
    char     p2ps[16];
    char     paw[16];        
    uint32_t unknown4[12];
    char     mac[18];
    char     ip[18];
    uint32_t port;

    friend bool operator< (const response &l, const response &r)
    {
        return l.port < r.port;
    }
};

bool gKeepGoing = true;
std::mutex gResponseMutex;
pthread_t gRecvP1, gRecvP2;
std::list<response> gFound;

struct broadcastParams
{
    int socket;
    uint16_t port;
};

struct sockaddr_in buildServerType(in_port_t port)
{
    struct sockaddr_in server;
    memset(&server, '\0', sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_BROADCAST;
    server.sin_port = htons(port);

    return server;
}

void buildQuery()
{
    memset(query, '\0', sizeof(query));

    time_t now = time(nullptr);
    now += 20637;
    now = bswap_32(now) >> 16;

    query[24] = 0xE2;
    query[25] = 0x07;
    query[26] = 0x0C;
    query[27] = 0x09;
    query[28] = now >> 8;
    query[29] = now & 0x00FF;
}

void quit(int sock)
{
    gKeepGoing = false;
    pthread_join(gRecvP1, nullptr);
    pthread_join(gRecvP1, nullptr);

    printf("Found %zd\n", gFound.size());
    auto iter = gFound.begin();
    while(iter != gFound.end())
    {
        printf("%s\n", iter->name);
        iter++;
    }
    close(sock);
}

void* timeoutThread(void* arg)
{
    int sock = *(int*)arg;

    usleep(TIMEOUT);
    quit(sock);

    exit(-1);

    return nullptr;
}

void* recvBroadcast(void* arg)
{
    broadcastParams* params = (broadcastParams*)arg;

    struct sockaddr_in server = buildServerType(params->port);

    response target;
    socklen_t length;
    uint8_t buffer[RESPONSE_SIZE];

    while(gKeepGoing)
    {
        ssize_t recv = recvfrom(params->socket, buffer, RESPONSE_SIZE, 0, (sockaddr*)&server, &length);
        memcpy(&target, buffer, RESPONSE_SIZE);

        std::lock_guard<std::mutex> lock(gResponseMutex);

        bool found = false;
        auto iter = gFound.begin();
        while(iter != gFound.end())
        {
            if(!strcmp(iter->serial, target.serial))
            {
                found = true;
                break;
            }
            iter++;
        }
        if(!found)
        {
            gFound.push_back(target);
        }
    }

    return nullptr;
}

int main(int argc, char **argv)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) return -1;

    int32_t broadcast = 1;
    int ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    if(ret != 0) return -1;

    struct sockaddr_in server = buildServerType(PORT1);

    broadcastParams params1;
    params1.socket = sock;
    params1.port = PORT1;
    broadcastParams params2 = params1;
    params2.port = PORT2;

    pthread_t watchDog;
    pthread_create(&gRecvP1,  nullptr, recvBroadcast, &params1);
    pthread_create(&gRecvP1,  nullptr, recvBroadcast, &params2);
    pthread_create(&watchDog, nullptr, timeoutThread, &sock);

    buildQuery();

    int32_t count = 0;
    ssize_t sent = sendto(sock, query, QUERY_SIZE, 0, (sockaddr*)&server, sizeof(server));
    while(sent == QUERY_SIZE && count < ATTTEMPTS && gKeepGoing)
    {
        count++;
        server.sin_port = server.sin_port == htons(PORT1) ? htons(PORT2): htons(PORT1);
        usleep(1000*72);
        buildQuery();

        sent = sendto(sock, query, QUERY_SIZE, 0, (sockaddr*)&server, sizeof(server));
    }

    quit(sock);
 
    return 0;
}
