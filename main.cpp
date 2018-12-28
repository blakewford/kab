#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <byteswap.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <mutex>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

#include "parser.h"

#define DISCOVER_PORT1                 25
#define DISCOVER_PORT2               5888
#define COMMAND_PORT                   80

#define DISCOVER_WEMO                1900

#define QUERY_SIZE                    128
#define RESPONSE_SIZE                 408
#define ATTTEMPTS                      14
#define TIMEOUT              10*1000*1000
#define WEMO_TIMEOUT        120*1000*1000

#define MODIFY_SWITCH              327702
#define SSDP             "239.255.255.250"

uint8_t query[QUERY_SIZE];

#pragma pack(push, 1)
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
#pragma pack(pop)

#pragma pack(push, 1)
struct command
{
    uint32_t cmd;
    uint32_t id;
    uint16_t type;
    uint8_t  version[6];
    char     model[32];
    char     name[32];
    char     serial[32];
    uint32_t status;
    uint32_t counter;
    uint32_t unknown;
    uint32_t id2;
    uint8_t  op;
    uint8_t  value;
};
#pragma pack(pop)

bool gKeepGoing = true;
std::mutex gResponseMutex;
pthread_t gRecvP1, gRecvP2;
std::vector<response> gFound;

struct broadcastParams
{
    int socket;
    uint16_t port;
};

struct sockaddr_in buildServerType(in_addr_t address, in_port_t port)
{
    struct sockaddr_in server;
    memset(&server, '\0', sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = address;
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

void initializeCommand(command& c)
{
    memset(&c, '\0', sizeof(command));

    c.cmd     = MODIFY_SWITCH;
    c.counter = 0x55555555;
    c.type    = 2;
    c.op      = 2;
}

void toggle(size_t ndx, bool on)
{
    response r = gFound.at(ndx);

    srand(time(nullptr));
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in target = buildServerType(inet_addr(r.ip), COMMAND_PORT);

    command toggle;
    initializeCommand(toggle);

    toggle.id = rand();
    memcpy(toggle.model, r.model, 32);
    toggle.value = on;

    ssize_t sent = sendto(sock, &toggle, sizeof(command), 0, (sockaddr*)&target, sizeof(target));
    close(sock);
}

void printTargets(bool cache)
{
    printf("Found (%zd)\n", gFound.size());

    int32_t count = 0;
    auto iter = gFound.begin();
    while(iter != gFound.end())
    {
        printf("[%d] %s\n", count, iter->name);
        if(cache)
        {
            FILE* json = fopen("cache.json", "a");
            fprintf(json, "{\"name\":\"%s\",\"parameters\":{\"ip\":\"%s\",\"model\":\"%s\"}}\n", iter->name, iter->ip, iter->model);
            fclose(json);
        }
        count++;
        iter++;
    }
}

void quit(int sock)
{
    gKeepGoing = false;
    pthread_join(gRecvP1, nullptr);
    pthread_join(gRecvP1, nullptr);

    printTargets(true);

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

    struct sockaddr_in server = buildServerType(INADDR_BROADCAST, params->port);

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

void discover()
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) return;

    int32_t broadcast = 1;
    int ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    if(ret != 0) return;

    struct sockaddr_in server = buildServerType(INADDR_BROADCAST, DISCOVER_PORT1);

    broadcastParams params1;
    params1.socket = sock;
    params1.port = DISCOVER_PORT1;
    broadcastParams params2 = params1;
    params2.port = DISCOVER_PORT2;

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
        server.sin_port = server.sin_port == htons(DISCOVER_PORT1) ? htons(DISCOVER_PORT2): htons(DISCOVER_PORT1);
        usleep(1000*72);
        buildQuery();

        sent = sendto(sock, query, QUERY_SIZE, 0, (sockaddr*)&server, sizeof(server));
    }

    quit(sock);
}

void discoverWemo()
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) return;

    int32_t broadcast = 1;
    int ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    if(ret != 0) return;

    struct sockaddr_in server = buildServerType(inet_addr(SSDP), DISCOVER_WEMO);
    server.sin_port = htons(DISCOVER_WEMO);

    broadcastParams params1;
    params1.socket = sock;
    params1.port = DISCOVER_WEMO;

    pthread_t watchDog;
    pthread_create(&gRecvP1,  nullptr, recvBroadcast, &params1);
    pthread_create(&watchDog, nullptr, timeoutThread, &sock);

    const char* packet =
"M-SEARCH * HTTP/1.1\r\n"\
"HOST: 239.255.255.250:1900\r\n"\
"MAN: \"ssdp:discover\"\r\n"\
"MX: 10\r\n"\
//"ST: ssdp:all\r\n";
"ST: urn:Belkin:device:controllee:1\r\n\r\n";

    ssize_t sent = sendto(sock, packet, strlen(packet), 0, (sockaddr*)&server, sizeof(server));
    usleep(1000*1000*1);

    quit(sock);
}

void wemoResponse()
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) return;

    struct sockaddr_in server = buildServerType(INADDR_ANY, DISCOVER_WEMO);
    if(bind(sock, (struct sockaddr*)&server, sizeof(server)) < 0) return;

    struct ip_mreq group;
    group.imr_multiaddr.s_addr = inet_addr(SSDP);
    int ret = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&group, sizeof(group));
    if(ret != 0) return;

    pthread_t watchDog;
    pthread_create(&watchDog, nullptr, timeoutThread, &sock);

    const char* packet =
"HTTP/1.1 200 OK\r\n"\
"CACHE-CONTROL: max-age=86400\r\n"\
"DATE: %s, %02d %s %d %02d:%02d:%02d %s\r\n"\
"EXT:\r\n"\
"LOCATION: http://%s:49153/setup.xml\r\n"\
"OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n"\
"01-NLS: 11111111-2222-3333-4444-555555555555\r\n"\
"SERVER: Unspecified, UPnP/1.0, Unspecified\r\n"\
"X-User-Agent: redsonic\r\n"\
"ST: urn:Belkin:device:controllee:1\r\n"\
"USN: uuid:Socket-1_0-2GTbor5wqtq79E::urn:Belkin:device:controllee:1\r\n\r\n";

    struct timeval now;
    gettimeofday(&now, nullptr);
    struct tm* gmt = gmtime(&now.tv_sec);

    const char* DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    socklen_t length;
    char buffer[RESPONSE_SIZE];
    while(gKeepGoing)
    {
        ssize_t recv = recvfrom(sock, buffer, RESPONSE_SIZE, 0, (sockaddr*)&server, &length);
        std::string search(buffer);
//        if(search.find("Belkin") != std::string::npos)
        {
            inet_ntop(AF_INET, &server.sin_addr, buffer, INET_ADDRSTRLEN);
            printf("%s %d\n", buffer, server.sin_port);

            int reply = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sendto(reply, packet, strlen(packet), 0, (sockaddr*)&server, sizeof(server));
            close(reply);
        }
    }
}

void addCachedTarget(set_target& t)
{
    response r;
    memset(&r, '\0', RESPONSE_SIZE);

    memcpy(r.name, t.get_name(), strlen(t.get_name()));
    memcpy(r.ip, t.get_ip(), strlen(t.get_ip()));
    memcpy(r.model, t.get_model(), strlen(t.get_model()));
    gFound.push_back(r);
}

void loop()
{
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if(fd < 0) return;

    size_t written = write(fd, "24", 2);
    close(fd);

    fd = open("/sys/class/gpio/gpio24/direction", O_WRONLY);
    if(fd < 0) return;

    if(write(fd, "in", 2) < 0) return;
    close(fd);

    fd = open("/sys/class/gpio/gpio24/value", O_RDONLY);
    if(fd < 0) return;

    char valueString[3];
    while(read(fd, valueString, 3) > 0)
    {
        printf("%d\n", atoi(valueString));
        usleep(1000*1000);
    }

    close(fd);
}

int main(int argc, char **argv)
{
    FILE* cache = fopen("cache.json", "r");
    if(cache != nullptr)
    {
        fclose(cache);
        std::ifstream sample("cache.json");

        set_target t;
        std::string line;
        std::getline(sample, line);
        std::istringstream iss(line);
        parse(line.c_str(), &t);
        addCachedTarget(t);
        while(std::getline(sample, line))
        {
            std::istringstream iss(line);
            parse(line.c_str(), &t);
            addCachedTarget(t);
        }

        int32_t selection = -1;
        printTargets(false);
        printf("[%zu] \u001b[31mCancel\u001b[0m\n", gFound.size());
        printf("\nChoose device:\n");
        std::cin >> selection;

        if(selection >=0 && selection < gFound.size())
        {
            size_t ndx = selection;
            printf("\nSwitch \u001b[34m%s\u001b[0m:\n", gFound.at(ndx).name);
            printf("[0] OFF\n");
            printf("[1] ON\n");
            printf("[2] Listen\n");
            printf("[3] \u001b[31mCancel\u001b[0m\n");
            std::cin >> selection;
            if(selection >=0 && selection < 3)
            {
                switch(selection)
                {
                    case 0:
                    case 1:
                        toggle(ndx, selection ? true: false);
                        break;
                    case 2:
                        loop();
                        break;
                    default:
                        break;
                }
            }
        }
    }
    else
    {
        printf("Starting discovery...\n");
        discover();
    }
 
    return 0;
}
