#pragma once
#include <string>
namespace Util{
    /**
     * @brief open and listen specific address:port as socket fd
     * 
     * @param address 
     * @param port 
     * @param type SOCK_STREAM/SOCK_DGRAM
     * @param version AF_INET/AF_INET6
     * @return int success: greate than zero, other failed;
     */
    int openListen(const std::string& address,short port,int type,int version);

    /**
     * @brief open and connect specific address:port as socket fd
     * 
     * @param address 
     * @param port 
     * @param type SOCK_STREAM/SOCK_DGRAM
     * @param version AF_INET/AF_INET6
     * @return int success: greate than zero, other failed;
     */
    int openConnect(const std::string& address,short port,int type,int version);
}
