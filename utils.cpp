#include "utils.h"
#include <stdio.h>
#include <string.h>
#ifdef __WIN32

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
namespace Util{
int openListen(const std::string& address,short port,int type,int version)
{
    struct sockaddr_in listenAddr;
    int fd = socket(version,type,0);
    if(fd == -1)
    {
        return fd;
    }
    int opt = 1;
    int ret = setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(ret == -1)
    {
        close(fd);
        return ret;
    }
    listenAddr.sin_family = version;
    listenAddr.sin_port = htons(port);
    if(inet_pton(version,address.c_str(),&listenAddr.sin_addr) <= 0)
    {
        close(fd);
        return -1;
    }
    memset(listenAddr.sin_zero,0,8);
    ret = bind(fd,(const struct sockaddr *)&listenAddr,sizeof(struct sockaddr));
    if(ret < 0)
    {
        close(fd);
        return ret;
    }
    ret = listen(fd,10);
    if(ret < 0)
    {
        close(fd);
        return ret;
    }
    return fd;
}
int openConnect(const std::string& address,short port,int type,int version)
{
    int fd = socket(version,type,0);
    if(fd == -1)
    {
        return -1;
    }
    int opt = 1;
    int ret = setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(ret < 0)
    {
        return -1;
    }
    struct sockaddr_in connectAddr;
    connectAddr.sin_family = version;
    connectAddr.sin_port = htons(port);
    if(0 == inet_pton(version,address.c_str(),&connectAddr.sin_addr))
    {
        // convert char array to nethost failed;
        close(fd);
        return -1;
    }
    memset(connectAddr.sin_zero,0,8);
    ret = connect(fd,(const struct sockaddr*)&connectAddr,sizeof(struct sockaddr_in));
    if(ret == -1)
    {
        // connect error
        close(fd);
        return -1;
    }
    return fd;
}
}
