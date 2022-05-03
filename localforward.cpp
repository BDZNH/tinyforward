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
std::unordered_map<int,int> gbackList;
std::mutex gMutex;
std::mutex gDataMutex;
std::condition_variable gWaiter;

bool gRunning = true;

void listenThreadFunc(int fd,std::string serverAddr,short serverPort);
void transmitListenToDest();
void transmitDestToListen();
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
    std::thread listenThread(listenThreadFunc,listenfd,args.get<std::string>("destip"),args.get<short>("destport"));
    std::thread transmitThread(transmitListenToDest);
    std::thread transmitThread2(transmitDestToListen);
    gRunning = true;
    while (gRunning)
    {
        std::unique_lock<std::mutex> lock(gMutex);
        gWaiter.wait(lock);
    }
    close(listenfd);
    listenThread.join();
    transmitThread.join();
    transmitThread2.join();
    return 0;
}

void listenThreadFunc(int fd,std::string serverAddr,short serverPort)
{
    while (gRunning)
    {
        struct sockaddr_in socketClientAddr;
        socklen_t clientLen = sizeof(struct sockaddr);
        int clientFd = accept(fd,(struct sockaddr *)&socketClientAddr, &clientLen);
        if(clientFd > 0)
        {
            char clientaddress[20] = {0};
            spdlog::debug("new request from {}:{}",inet_ntop(AF_INET,(void*)&socketClientAddr.sin_addr,clientaddress,clientLen),ntohs(socketClientAddr.sin_port));
            int serverfd = Util::openConnect(serverAddr,serverPort,SOCK_STREAM,AF_INET);
            if(serverfd > 0)
            {
                std::unique_lock<std::mutex> _l(gDataMutex);
                gGoList[clientFd] = serverfd;
                gbackList[serverfd] = clientFd;
            }
        }
    }
    
}
void transmitListenToDest()
{
    char* buffer = nullptr;
    size_t buffersize = 0;
    struct timeval timeout;
    fd_set sets;
    while(gRunning)
    {
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        {
            std::unique_lock<std::mutex> _l(gDataMutex);
            auto iter = gGoList.begin();
            while (iter != gGoList.end())
            {
                if (iter->first > 0 && iter->second > 0)
                {
                    FD_SET(iter->first, &sets);
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
            auto iter = gGoList.begin();
            while (iter != gGoList.end())
            {
                if (FD_ISSET(iter->first,&sets) && iter->first > 0 && iter->second > 0)
                {
                    int nNeedRead = 0;
                    ioctl(iter->first,FIONREAD,&nNeedRead);
                    if(nNeedRead>0)
                    {
                        if(nNeedRead > buffersize)
                        {
                            buffer = (char*)realloc(buffer,nNeedRead);
                            buffersize = nNeedRead;
                        }
                        int recvLen = recv(iter->first,buffer,nNeedRead,0);
                        if(recvLen > 0)
                        {
                            int sendLen = send(iter->second,buffer,recvLen,0);
                            spdlog::trace("transmit {} from listened port to destnination addr",sendLen);
                        }
                        else
                        {
                            close(iter->first);
                            close(iter->second);
                            iter->second = 0;
                        }
                    }
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
}

void transmitDestToListen()
{
    char* buffer = nullptr;
    size_t buffersize = 0;
    struct timeval timeout;
    fd_set sets;
    while(gRunning)
    {
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        {
            std::unique_lock<std::mutex> _l(gDataMutex);
            auto iter = gbackList.begin();
            while (iter != gbackList.end())
            {
                if (iter->first > 0 && iter->second > 0)
                {
                    FD_SET(iter->first, &sets);
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
            auto iter = gbackList.begin();
            while (iter != gbackList.end())
            {
                if (FD_ISSET(iter->first,&sets) && iter->first > 0 && iter->second > 0)
                {
                    int nNeedRead = 0;
                    ioctl(iter->first,FIONREAD,&nNeedRead);
                    if(nNeedRead>0)
                    {
                        if(nNeedRead > buffersize)
                        {
                            buffer = (char*)realloc(buffer,nNeedRead);
                            buffersize = nNeedRead;
                        }
                        int recvLen = recv(iter->first,buffer,nNeedRead,0);
                        if(recvLen > 0)
                        {
                            int sendLen = send(iter->second,buffer,recvLen,0);
                            spdlog::trace("transmit {} from destnination addr to listened port",sendLen);
                        }
                        else
                        {
                            close(iter->first);
                            close(iter->second);
                            iter->second = 0;
                        }
                    }
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
}

void signalHandler(int sig)
{
    spdlog::info("interrupt by sig {}",sig);
    std::lock_guard<std::mutex> _l(gMutex);
    gWaiter.notify_one();
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
