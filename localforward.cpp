#include "cmdline.h"
#include <string>
#include "utils.h"

#ifdef __WIN32

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/select.h> 
#endif

#include <spdlog/spdlog.h>
#include <errno.h>
#include <string.h>
#include <thread>
#include <unordered_map>
#include <signal.h>
#include <mutex>
#include <condition_variable>

std::unordered_map<int,int> gGoList;
std::unordered_map<int,int> gFdMaps;
std::mutex gMutex;
std::mutex gDataMutex;
std::condition_variable gWaiter;
bool gRunning = true;

void listenThreadFunc(int listenfd,std::string serverAddr,short serverPort);
void fordThreadFunc();
void signalHandler(int sig);
void switchLoglevel(std::string level);

int main(int argc,char** argv)
{
    cmdline::parser args;
    args.add<short>("listen",'r',"transmit from port",false,10000);
    args.add<std::string>("destip",'d',"transmit to address",false,"127.0.0.1");
    args.add<short>("destport",'p',"transmit to port",false,10001);
    args.add<std::string>("loglevel",'v',"set loglevel trace/debug/info/error/fatal",false,"info");
    args.parse_check(argc,argv);

    switchLoglevel(args.get<std::string>("loglevel"));

    int listenfd = Util::openListen("0.0.0.0",args.get<short>("listen"),SOCK_STREAM,AF_INET); 
    if(listenfd <=0)
    {
        spdlog::error("error({}):{}",errno,strerror(errno));
        return -1;
    }
    spdlog::info("start forward 0.0.0.0:{} to {}:{}",args.get<short>("listen"),args.get<std::string>("destip"),args.get<short>("destport"));
    std::thread listenThread(listenThreadFunc,listenfd,args.get<std::string>("destip"),args.get<short>("destport"));
    std::thread forwardThread(fordThreadFunc);
    gRunning = true;

    signal(SIGINT,signalHandler);
    signal(SIGTERM,signalHandler);

    while (gRunning)
    {
        std::unique_lock<std::mutex> lock(gMutex);
        gWaiter.wait(lock);
    }

    shutdown(listenfd,SHUT_RDWR);
    close(listenfd);
    listenThread.join();
    forwardThread.join();
    return 0;
}

void listenThreadFunc(int listenfd,std::string serverAddr,short serverPort)
{
    while (gRunning)
    {
        struct sockaddr_in socketClientAddr;
        socklen_t clientLen = sizeof(struct sockaddr);
        int clientFd = accept(listenfd,(struct sockaddr *)&socketClientAddr, &clientLen);
        if(clientFd >= 0)
        {
            char clientaddress[20] = {0};
            spdlog::debug("new request from {}:{}",inet_ntop(AF_INET,(void*)&socketClientAddr.sin_addr,clientaddress,clientLen),ntohs(socketClientAddr.sin_port));
            int serverfd = Util::openConnect(serverAddr,serverPort,SOCK_STREAM,AF_INET);
            if(serverfd > 0)
            {
                std::unique_lock<std::mutex> _l(gDataMutex);
                gFdMaps[serverfd] = clientFd;
            }
            else
            {
                // if open dest addr failed. close request
                close(clientFd);
            }
        }
        else
        {
            spdlog::error("error with({}): {}",errno,strerror(errno));
            gRunning = false;
            gWaiter.notify_one();
            break;
        }
    }
    
}

void fordThreadFunc()
{
    char* buffer = nullptr;
    size_t buffersize = 0;
    struct timeval timeout;
    fd_set sets;
    while(gRunning)
    {
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        {
            std::unique_lock<std::mutex> _l(gDataMutex);
            auto iter = gFdMaps.begin();
            while (iter != gFdMaps.end())
            {
                if (iter->first > 0 && iter->second > 0)
                {
                    FD_SET(iter->first, &sets);
                    FD_SET(iter->second,&sets);
                }
                iter++;
            }
        }

        int ret = select(FD_SETSIZE+1,&sets,NULL,NULL,&timeout);
        if(!gRunning)
        {
            break;
        }
        if(ret > 0)
        {
            std::unique_lock<std::mutex> _l(gDataMutex);
            auto iter = gFdMaps.begin();
            while (iter != gFdMaps.end())
            {
                if (iter->first > 0 && iter->second > 0)
                {
                    int nNeedRead = 0;
                    if(FD_ISSET(iter->first,&sets))
                    {
                        ioctl(iter->first, FIONREAD, &nNeedRead);
                        if (nNeedRead > 0)
                        {
                            if (nNeedRead > buffersize)
                            {
                                buffer = (char *)realloc(buffer, nNeedRead);
                                buffersize = nNeedRead;
                            }
                            int recvLen = recv(iter->first, buffer, nNeedRead, 0);
                            if (recvLen > 0)
                            {
                                int sendLen = send(iter->second, buffer, recvLen, 0);
                                spdlog::trace("transmit {} from destnination addr to listened port", sendLen);
                            }
                            else
                            {
                                close(iter->first);
                                close(iter->second);
                                iter->second = 0;
                            }
                        }
                    }
                    if(iter->second >0 && FD_ISSET(iter->second,&sets))
                    {
                        ioctl(iter->second, FIONREAD, &nNeedRead);
                        if (nNeedRead > 0)
                        {
                            if (nNeedRead > buffersize)
                            {
                                buffer = (char *)realloc(buffer, nNeedRead);
                                buffersize = nNeedRead;
                            }
                            int recvLen = recv(iter->second, buffer, nNeedRead, 0);
                            if (recvLen > 0)
                            {
                                int sendLen = send(iter->first, buffer, recvLen, 0);
                                spdlog::trace("transmit {} from listen port to destination", sendLen);
                            }
                            else
                            {
                                close(iter->first);
                                close(iter->second);
                                iter->second = 0;
                            }
                        }
                    }
                }
                if (iter->first == 0 || iter->second == 0)
                {
                    close(iter->first);
                    close(iter->second);
                    iter = gFdMaps.erase(iter);
                    continue;
                }
                iter++;
            }
        }
    }
    if(buffersize>0 && buffer!=nullptr)
    {
        free(buffer);
        buffer = nullptr;
        buffersize = 0;
    }
    if(!gRunning)
    {
        auto iter = gFdMaps.begin();
        while(iter != gFdMaps.end())
        {
            if(iter->first > 0)
            {
                shutdown(iter->first,SHUT_RDWR);
                close(iter->first);
            }
            if(iter->second > 0)
            {
                shutdown(iter->second,SHUT_RDWR);
                close(iter->second);
            }
            iter++;
        }
    }
}

void signalHandler(int sig)
{
    gRunning = false;
    spdlog::info("interrupt by sig {}",sig);
    std::lock_guard<std::mutex> _l(gMutex);
    gWaiter.notify_all();
}

void switchLoglevel(std::string level)
{
    char loglevel = level.at(0);
    switch (loglevel)
    {
    case 't':
    case 'T':
        spdlog::set_level(spdlog::level::trace);
        break;
    case 'd':
    case 'D':
        spdlog::set_level(spdlog::level::debug);
        break;
    case 'i':
    case 'I':
        spdlog::set_level(spdlog::level::info);
        break;
    case 'e':
    case 'E':
        spdlog::set_level(spdlog::level::err);
        break;
    default:
        spdlog::set_level(spdlog::level::info);
        break;
    }
}
