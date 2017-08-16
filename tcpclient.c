#include "simplemotion_private.h"
#include "tcpclient.h"
#include <stdio.h>

#if defined(_WIN32)
#if defined(CM_NONE)
#undef CM_NONE
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define read(SOCKET, BUF, LEN) recv((SOCKET), (BUF), (LEN), 0)
#define write(SOCKET, BUF, LEN) send((SOCKET), (BUF), (LEN), 0)
#ifdef errno
#undef errno
#endif
#define errno (WSAGetLastError())
#ifdef EINPROGRESS
#undef EINPROGRESS
#endif
#define EINPROGRESS WSAEWOULDBLOCK
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#if defined(_WIN32)
static int initwsa()
{
    WORD req;
    WSADATA data;
    req = MAKEWORD(2, 2);
    int err = WSAStartup(req, &data);
    if (err != 0)
    {
        printf("WSAStartup failed\n");
        return 0;
    }
    return 1;
}
#endif

int OpenTCPPort(const char * ip_addr, int port)
{
    int sockfd;
    struct sockaddr_in server;
    struct timeval tv;
    fd_set myset;
    int res, valopt;
    socklen_t lon;
    unsigned long arg;

#if defined(_WIN32)
    initwsa();
#endif

    //Create socket
    sockfd = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP);
    if (sockfd == -1)
    {
        return -1;
    }

    // Set OFF NAGLE algorithm to reduce latency with small packets
    /*
    int one = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&one, sizeof(one));
    */

    server.sin_addr.s_addr = inet_addr(ip_addr);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    // Set non-blocking when trying to establish the connection
#if !defined(_WIN32)
    arg = fcntl(sockfd, F_GETFL, NULL);
    arg |= O_NONBLOCK;
    fcntl(sockfd, F_SETFL, arg);
#else
    arg = 1;
    ioctlsocket(sockfd, FIONBIO, &arg);
#endif

    res = connect(sockfd, (struct sockaddr *)&server, sizeof(server));

    if (res < 0)
    {
        if (errno == EINPROGRESS)
        {
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            FD_ZERO(&myset);
            FD_SET((unsigned int)sockfd, &myset);
            if (select(sockfd+1, NULL, &myset, NULL, &tv) > 0)
            {
                lon = sizeof(int);
                getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon);
                if (valopt)
                {
                    return -1;
                }
            }
            else
            {
               return -1;
            }
        }
        else
        {
            return -1;
        }
    }

    // Set to blocking mode again
#if !defined(_WIN32)
    arg = fcntl(sockfd, F_GETFL, NULL);
    arg &= (~O_NONBLOCK);
    fcntl(sockfd, F_SETFL, arg);
#else
    arg = 0;
    ioctlsocket(sockfd, FIONBIO, &arg);
#endif

    return sockfd;
}

// Read bytes from socket
int PollTCPPort(int sockfd, unsigned char *buf, int size)
{
    int n;
    fd_set input;
    FD_ZERO(&input);
    FD_SET((unsigned int)sockfd, &input);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = readTimeoutMs * 1000;

    n = select(sockfd + 1, &input, NULL, NULL, &timeout);

    // Error or timeout
    if (n < 1)
    {
        return(-1);
    }
    if(!FD_ISSET(sockfd, &input))
    {
        return(-1);
    }

    n = read(sockfd, (char*)buf, size);
    return(n);
}

int SendTCPByte(int sockfd, unsigned char byte)
{
    int n;
    n = write(sockfd, (char*)&byte, 1);
    if(n<0)
        return(1);
    return(0);
}


int SendTCPBuf(int sockfd, unsigned char *buf, int size)
{
    int sent = write(sockfd, (char*)buf, size);
    if (sent != size)
    {
        return sent;
    }
    return sent;
}


void CloseTCPport(int sockfd)
{
    close(sockfd);
#if defined(_WIN32)
    WSACleanup();
#endif
}
