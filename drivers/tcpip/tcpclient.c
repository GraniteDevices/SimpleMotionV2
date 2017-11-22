#include "simplemotion_private.h"
#include "tcpclient.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

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

    // Set OFF NAGLE algorithm to disable stack buffering of small packets
    int one = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&one, sizeof(one));

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
    timeout.tv_sec = readTimeoutMs/1000;
    timeout.tv_usec = (readTimeoutMs%1000) * 1000;

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


//accepted TCP/IP address format is nnn.nnn.nnn.nnn:pppp where n is IP address numbers and p is port number
int validateIpAddress(const char *s, const char **pip_end,
                             const char **pport_start)
{
    int octets = 0;
    int ch = 0, prev = 0;
    int len = 0;
    const char *ip_end = NULL;
    const char *port_start = NULL;

    while (*s)
    {
        ch = *s;

        if (isdigit(ch))
        {
            ++len;
            // Octet len must be 1-3 digits
            if (len > 3)
            {
                return -1;
            }
        }
        else if (ch == '.' && isdigit(prev))
        {
            ++octets;
            len = 0;
            // No more than 4 octets please
            if (octets > 4)
            {
                return -1;
            }
        }
        else if (ch == ':' && isdigit(prev))
        {
            ++octets;
            // We want exactly 4 octets at this point
            if (octets != 4)
            {
                return -1;
            }
            ip_end = s;
            ++s;
            port_start = s;
            while (isdigit((ch = *s)))
                ++s;
            // After port we want the end of the string
            if (ch != '\0')
                return -1;
            // This will skip over the ++s below
            continue;
        }
        else
        {
            return -1;
        }

        prev = ch;
        ++s;
    }

    // We reached the end of the string and did not encounter the port
    if (*s == '\0' && ip_end == NULL)
    {
        ++octets;
        ip_end = s;
    }

    // Check that there are exactly 4 octets
    if (octets != 4)
        return -1;

    if (pip_end)
        *pip_end = ip_end;

    if (pport_start)
        *pport_start = port_start;

    return 0;
}

int parseIpAddress(const char *s, char *ip, size_t ipsize, short *port)
{
    const char *ip_end, *port_start;

    //ip_end and port_start are pointers to memory area of s, not offsets or indexes to s
    if (validateIpAddress(s, &ip_end, &port_start) == -1)
        return -1;

    // If ip=NULL, we just report that the parsing was ok
    if (!ip)
        return 0;

    if (ipsize < (size_t)(ip_end - s + 1))
        return -1;

    memcpy(ip, s, ip_end - s);
    ip[ip_end - s] = '\0';

    if (port_start)
    {
        *port = 0;
        while (*port_start)
        {
            *port = *port * 10 + (*port_start - '0');
            ++port_start;
        }
    }

    return 0;
}
