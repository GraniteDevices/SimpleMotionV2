#include "simplemotion_private.h"
#include "tcp_ethsm_client.h"
#include "user_options.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "buffer.h"

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

/**********************************************/
/*   GLOBALS & CONSTANTS                      */
/**********************************************/

#define TCP_WRITE_REQUEST_HEADER_LENGTH 3
#define TCP_WRITE_RESPONSE_HEADER_LENGTH 2
#define GLOBAL_BUFFER_SIZE 512

static char tempBuffer[GLOBAL_BUFFER_SIZE];

/**********************************************/
/*   STATIC FUNCTION PROTOTYPES               */
/**********************************************/

static smbool tcpipSetBaudrate(smBusdevicePointer busdevicePointer, smuint32 baudrate);
static void printBuffer(char *buf, unsigned int len);
static void multipleBytesToValue(char *buf, unsigned int len, unsigned int *value);
static void valueToMultipleBytes(char *buf, unsigned int len, unsigned int value);
static void addTCPRequestHeaders(char *buf, char packetType, unsigned int value, unsigned int len);
static char *createNewBufferWithLeadingFreeSpace(unsigned char *buf, unsigned int currentSize, unsigned int sizeToAdd);
smBusdevicePointer tcpipEthSMPortOpen(const char *devicename, smint32 baudrate_bps, smbool *success);
static int getETHSMAdapterFeatures(smBusdevicePointer busdevicePointer);
static int getETHSMAdapterVersionNumbers(smBusdevicePointer busdevicePointer, char *buf);
static int tcpipFlush(smBusdevicePointer busdevicePointer);
static void tcpipPurge(smBusdevicePointer busdevicePointer);

/**********************************************/
/*   MISCELLANEOUS                            */
/**********************************************/

static void printBuffer(char *buf, unsigned int len)
{
    printf("buffer(");
    for (unsigned int i = 0; i < len; ++i)
    {
        printf("%d ", buf[i]);
    }
    printf(") ");
}

static void multipleBytesToValue(char *buf, unsigned int len, unsigned int *value)
{
    *value = 0;
    for (unsigned int i = 0; i < len; ++i)
    {
        *value += (unsigned int)(buf[i] << (8 * i));
    }
}

static void valueToMultipleBytes(char *buf, unsigned int len, unsigned int value)
{
    //printf("valueToMultipleBytes %d %d -> ", len, value);
    for (unsigned int i = 0; i < len; ++i)
    {
        buf[i] = ((char)(value >> (8 * i))) & (char)0xFF;
        //printf("%u ", buf[i]);
    }
    //printf("\r\n");
}

static void addTCPRequestHeaders(char *buf, char packetType, unsigned int value, unsigned int len)
{
    buf[0] = packetType;
    valueToMultipleBytes(&buf[1], len, value);
}

static char *createNewBufferWithLeadingFreeSpace(unsigned char *buf, unsigned int currentSize, unsigned int sizeToAdd)
{
    unsigned int newBufferSize = currentSize + sizeToAdd;

    char *buffer = (char *)malloc(newBufferSize);

    if (!buffer)
    {
        printf("ALLOCATE ERROR\r\n");
        return NULL;
    }

    memcpy(buffer + sizeToAdd, buf, (unsigned int)currentSize);
    return buffer;
}

//accepted TCP/IP address format is ETHSM:nnn.nnn.nnn.nnn:pppp where n is IP address numbers and p is port number
//params: s=whole ip address with port number with from user, pip_end=output pointer pointint to end of ip address part of s, pport_stasrt pointing to start of port number in s
static int validateEthSMIpAddress(const char *s, const char **pip_end,
                                  const char **pport_start)
{
    int octets = 0;
    int ch = 0, prev = 0;
    int len = 0;
    const char *ip_end = NULL;
    const char *port_start = NULL;

    const char *start = "ETHSM:";

    if (strncmp(s, start, strlen(start)) != 0)
    {
        return -1;
    }

    s += strlen(start);

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

//params: s=whole ip:port string, ip=output for ip number only, port=output for port number integer
static int parseEthSMIpAddress(const char *s, char *ip, unsigned short *port)
{
    const char *ip_end, *port_start;

    //ip_end and port_start are pointers to memory area of s, not offsets or indexes to s
    if (validateEthSMIpAddress(s, &ip_end, &port_start) == -1)
        return -1;

    int IPAddrLen = ip_end - s;

    printf("ip len: %d \r\n", IPAddrLen);

    const char *start = "ETHSM:";
    IPAddrLen -= strlen(start);
    s += strlen(start);

    if (IPAddrLen < 7 || IPAddrLen > 15) //check length before writing to *ip
        return -1;

    memcpy(ip, s, IPAddrLen);
    ip[IPAddrLen] = '\0';

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

/**********************************************/
/*   TCP BUS HANDLING FUNCTIONS               */
/**********************************************/

smBusdevicePointer tcpipEthSMPortOpen(const char *devicename, smint32 baudrate_bps, smbool *success)
{
    printf("tcpipEthSMPortOpen %s \r\n", devicename);

    int sockfd;
    struct sockaddr_in server;
    struct timeval tv;
    fd_set myset;
    int res, valopt;
    socklen_t lon;
    unsigned long arg;

    *success = smfalse;

    printf("Validate IP address: \r\n");
    if (validateEthSMIpAddress(devicename, NULL, NULL) != 0)
    {
        smDebug(-1, SMDebugLow, "TCP/IP: device name '%s' does not appear to be IP address, skipping TCP/IP open attempt (note: this is normal if opening a non-TCP/IP port)\n", devicename);
        printf("Not valid IP \r\n");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    printf("Valid IP! \r\n");
    char ip_addr[16];
    unsigned short port = 4001;

    printf("Parse IP address: \r\n");
    if (parseEthSMIpAddress(devicename, ip_addr, &port) < 0)
    {
        smDebug(-1, SMDebugLow, "TCP/IP: IP address parse failed\n");
        printf("Failed! \r\n");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    printf("Success! Connect to IP %s, port %d \r\n", ip_addr, port);

    smDebug(-1, SMDebugLow, "TCP/IP: Attempting to connect to %s:%d\n", ip_addr, port);

#if defined(_WIN32)
    initwsa();
#endif

    //Create socket
    sockfd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1)
    {
        printf("Failed to create socket\r\n");
        smDebug(-1, SMDebugLow, "TCP/IP: Socket open failed (sys error: %s)\n", strerror(errno));
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Set OFF NAGLE algorithm to disable stack buffering of small packets
    int one = 1;
    setsockopt((SOCKET)sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&one, sizeof(one));

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
    ioctlsocket((SOCKET)sockfd, (long)FIONBIO, &arg);
#endif

    res = connect((SOCKET)sockfd, (struct sockaddr *)&server, sizeof(server));

    if (res < 0) //connection not established (at least yet)
    {
        if (errno == EINPROGRESS) //check if it may be due to non-blocking mode (delayed connect)
        {
            tv.tv_sec = 1; //max wait time // TODO, magic number
            tv.tv_usec = 0;
            FD_ZERO(&myset);
            FD_SET((unsigned int)sockfd, &myset);
            if (select(sockfd + 1, NULL, &myset, NULL, &tv) > 0)
            {
                lon = sizeof(int);
                getsockopt((SOCKET)sockfd, SOL_SOCKET, SO_ERROR, (void *)(&valopt), &lon);
                if (valopt) //if valopt!=0, then there was an error. if it's 0, then connection established successfully (will return here from smtrue eventually)
                {
                    printf("Err1\r\n");
                    smDebug(-1, SMDebugLow, "TCP/IP: Setting socket properties failed (sys error: %s)\n", strerror(errno));
                    return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
                }
            }
            else
            {
                printf("Err2 %s\r\n", strerror(errno));
                smDebug(-1, SMDebugLow, "TCP/IP: Setting socket properties failed (sys error: %s)\n", strerror(errno));
                return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
            }
        }
        else
        {
            printf("Err3\r\n");
            smDebug(-1, SMDebugLow, "TCP/IP: Connecting socket failed (sys error: %s)\n", strerror(errno));
            return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
        }
    }

    // Set to blocking mode again
#if !defined(_WIN32)
    arg = fcntl(sockfd, F_GETFL, NULL);
    arg &= (~O_NONBLOCK);
    fcntl(sockfd, F_SETFL, arg);
#else
    arg = 0;
    ioctlsocket((SOCKET)sockfd, (long)FIONBIO, &arg);
#endif

    if (!createRingBuffer((unsigned int)sockfd)) {
        tcpipEthSMPortClose((smBusdevicePointer)sockfd);
        printf("ERROR! Could not create ring buffer\r\n");
        return 0;
    }

    tcpipSetBaudrate((smBusdevicePointer)sockfd, (smuint32)baudrate_bps);

    int features = getETHSMAdapterFeatures((smBusdevicePointer)sockfd);
    printf("ETHSM Features: %d \r\n", features);

    char version[10] = {0};
    getETHSMAdapterVersionNumbers((smBusdevicePointer)sockfd, version);
    printf("ETHSM version: %s \r\n", version);

    unsigned int flush = tcpipFlush((smBusdevicePointer)sockfd);
    printf("Flush successful: %d \r\n", flush);

    tcpipPurge((smBusdevicePointer)sockfd);

    printf("Return bus device pointer: %d \r\n", sockfd);

    *success = smtrue;

    return (smBusdevicePointer)sockfd; //compiler warning expected here on 64 bit compilation but it's ok
}

void tcpipEthSMPortClose(smBusdevicePointer busdevicePointer)
{
    int sockfd = (int)busdevicePointer; //compiler warning expected here on 64 bit compilation but it's ok
    close((SOCKET)sockfd);
#if defined(_WIN32)
    WSACleanup();
#endif
    removeRingBuffer((unsigned int)sockfd);
}

/**********************************************/
/*   TCP TASKS                                */
/**********************************************/

static int tcpipPortWriteBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int size)
{
    printBuffer(buf, size);
    int sent = write((SOCKET)busdevicePointer, buf, (int)size);
    return sent;
}

// Try to read given amount of bytes from TCP
static int tcpipReadBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int dataMaxLength)
{

    SOCKET sockfd = (SOCKET)busdevicePointer;
    fd_set input;
    FD_ZERO(&input);
    FD_SET((unsigned int)sockfd, &input);
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0; // TODO, where to get this?

    int n = select((int)sockfd + 1, &input, NULL, NULL, &timeout); //n=-1, select failed. 0=no data within timeout ready, >0 data available. Note: sockfd+1 is correct usage.

    if (n < 1) //n=-1 error, n=0 timeout occurred
    {
        return (n);
    }

    if (!FD_ISSET(sockfd, &input))
    {
        return (n); //no data available
    }

    n = read(sockfd, buf, (int)dataMaxLength);
    return n;
}

static int tcpipReadBytesFromAdapter(smBusdevicePointer busdevicePointer, unsigned int amountOfBytes)
{
    printf("tcpipReadBytesFromAdapter %d ", amountOfBytes);

    unsigned int supposedPacketLength = 0;

    /* SEND READ REQUEST */

    {
        int response = 0;
        char requestBuffer[3] = {0};
        addTCPRequestHeaders(requestBuffer, SM_PACKET_TYPE_READ, amountOfBytes, 2 /*TAIKALUKU*/);
        response = tcpipPortWriteBytes(busdevicePointer, requestBuffer, 3);
        printf(" RW:%d ", response);
        if (response == SOCKET_ERROR)
        {
            printf("TCPIPREADBYTESFROMADAPTER ERROR 0 \r\n");
            return SOCKET_ERROR;
        }
    }

    /* READ RESPONSE HEADER */

    {
        char responseHeaderBuffer[3] = {0};
        int response = tcpipReadBytes(busdevicePointer, responseHeaderBuffer, 3);
        printf(" RR:%d ", response);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPREADBYTESFROMADAPTER ERROR 1 \r\n \r\n");
            return SOCKET_ERROR;
        }

        if (response != 3)
        {
            printf(" TCPIPREADBYTESFROMADAPTER ERROR 2 \r\n");
            return 0;
        }

        char packetType = responseHeaderBuffer[0];

        if (packetType != SM_PACKET_TYPE_READ)
        {
            printf(" TCPIPREADBYTESFROMADAPTER ERROR 3 \r\n");
            return 0;
        }

        multipleBytesToValue(&responseHeaderBuffer[1], 2, &supposedPacketLength);
    }

    /* READ RESPONSE BYTES */

    {
        int response = tcpipReadBytes(busdevicePointer, tempBuffer, supposedPacketLength);
        unsigned int dataLength = (unsigned int)response;

        printf(" B:%d ", response);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPREADBYTESFROMADAPTER ERROR 4 \r\n");
            return SOCKET_ERROR;
        }

        if (response != (int)supposedPacketLength)
        {
            printf(" TCPIPREADBYTESFROMADAPTER ERROR 5 \r\n");
            return 0;
        }

        printBuffer(tempBuffer, dataLength);

        for (unsigned int i = 0; i < dataLength; ++i)
        {
            bufferAddItem((unsigned int)busdevicePointer, tempBuffer[i]);
        }
    }

    printf("\r\n");

    return 0;
}

static smbool tcpipSetBaudrate(smBusdevicePointer busdevicePointer, smuint32 baudrate)
{
    printf("tcpipSetbaudrate %d \r\n", baudrate);

    /* SEND WRITE REQUEST */

    {
        int response = 0;

        const unsigned int requestLength = 4;

        char requestBuffer[4] = {0};
        requestBuffer[0] = SM_PACKET_TYPE_SET_BAUDRATE;

        valueToMultipleBytes(&requestBuffer[1], 3, baudrate);

        response = tcpipPortWriteBytes(busdevicePointer, requestBuffer, requestLength);

        printf(" WW:%d ", response);

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    /* READ RESPONSE */

    {
        int response = 0;
        char responseByte = 0;

        // Odotetaan vastaus pyyntöön
        response = tcpipReadBytes(busdevicePointer, &responseByte, TCP_WRITE_RESPONSE_HEADER_LENGTH);

        printf(" WR:%d ", response);

        if (response == 1 && responseByte == 1)
        {
            return 1;
        }

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 2 \r\n");
            return SOCKET_ERROR;
        }

        if (response != 1)
        {
            printf(" TCPIPPORTWRITE ERROR 3 \r\n");
            return 0;
        }
    }

    printf("\r\n");

    return 1;
}

static int getETHSMAdapterVersionNumbers(smBusdevicePointer busdevicePointer, char *buf)
{
    printf("getETHSMAdapterFeatures \r\n");

    unsigned int featureFlags = 0;

    /* SEND WRITE REQUEST */

    {
        int response = 0;

        char request = SM_PACKET_TYPE_GET_DEVICE_VERSION_NUMBERS;

        response = tcpipPortWriteBytes(busdevicePointer, &request, 1);

        printf(" WW:%d ", response);

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    /* READ RESPONSE */

    {
        int response = 0;

        const unsigned int responseLength = 10;

        // Odotetaan vastaus pyyntöön
        response = tcpipReadBytes(busdevicePointer, buf, responseLength);

        printf(" WR:%d\r\n ", response);

        if (response == responseLength)
        {
            return (int)1;
        }

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 2 \r\n");
            return SOCKET_ERROR;
        }

        printf(" TCPIPPORTWRITE ERROR 3 \r\n");
        return SOCKET_ERROR;
    }
}

// Returns -1 or feature flags
static int getETHSMAdapterFeatures(smBusdevicePointer busdevicePointer)
{
    printf("getETHSMAdapterFeatures \r\n");

    unsigned int featureFlags = 0;

    /* SEND WRITE REQUEST */

    {
        int response = 0;

        char request = SM_PACKET_TYPE_GET_DEVICE_FEATURES;

        response = tcpipPortWriteBytes(busdevicePointer, &request, 1);

        printf(" WW:%d ", response);

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    /* READ RESPONSE */

    {
        int response = 0;

        const unsigned int responseLength = 4;
        char responseData[4] = {0};

        // Odotetaan vastaus pyyntöön
        response = tcpipReadBytes(busdevicePointer, responseData, responseLength);

        printf(" WR:%d\r\n ", response);

        if (response == responseLength)
        {
            multipleBytesToValue(responseData, responseLength, &featureFlags);
            return (int)featureFlags;
        }

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 2 \r\n");
            return SOCKET_ERROR;
        }

        printf(" TCPIPPORTWRITE ERROR 3 \r\n");
        return SOCKET_ERROR;
    }
}

static void tcpipPurge(smBusdevicePointer busdevicePointer)
{
    printf("tcpipPurge\r\n");
    unsigned char buffer[100];
    unsigned int bufferSize = sizeof(buffer) / sizeof(buffer[0]);
    int receivedData = bufferSize;

    while (receivedData == bufferSize)
    {
        printf("Purge %d \r\n", receivedData);
        receivedData = tcpipEthSMPortRead(busdevicePointer, buffer, bufferSize);
    }

    printf("tcpipPurge DONE\r\n");
}

static int tcpipFlush(smBusdevicePointer busdevicePointer)
{
    printf("tcpipFlush \r\n");

    unsigned int featureFlags = 0;

    /* SEND WRITE REQUEST */

    {
        int response = 0;

        char request = SM_PACKET_TYPE_FLUSH;

        response = tcpipPortWriteBytes(busdevicePointer, &request, 1);

        printf(" WW:%d ", response);

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    /* READ RESPONSE */

    {
        int response = 0;

        char responseByte = 0;

        // Odotetaan vastaus pyyntöön
        response = tcpipReadBytes(busdevicePointer, &responseByte, 1);

        printf(" WR:%d\r\n ", response);

        if (response == 1 && responseByte == 3)
        {
            return 1;
        }

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 2 \r\n");
            return SOCKET_ERROR;
        }

        printf(" TCPIPPORTWRITE ERROR 3 \r\n");
        return SOCKET_ERROR;
    }
}

/**********************************************/
/*   SM API FUNCTIONS                         */
/**********************************************/

/**
 * @brief tcpipPortWrite
 * @param busdevicePointer
 * @param buf
 * @param size
 * @return amount of bytes written or SOCKET_ERROR if there was one
 */
int tcpipEthSMPortWrite(smBusdevicePointer busdevicePointer, unsigned char *buf, int size)
{

    printf("tcpipPortWrite %d ", size);

    if (size <= 0)
    {
        return 0;
    }

    /* SEND WRITE REQUEST */

    {
        int response = 0;

        // Tehdään isompi puskuri headereita varten
        char *requestBuffer = createNewBufferWithLeadingFreeSpace(buf, (unsigned int)size, TCP_WRITE_REQUEST_HEADER_LENGTH); // TODO, VAPAUTA TÄMÄ
        unsigned int requestBufferSize = (unsigned int)size + TCP_WRITE_REQUEST_HEADER_LENGTH;

        if (!requestBuffer)
        {
            return 0;
        }

        // Lisätään headerit
        addTCPRequestHeaders(requestBuffer, SM_PACKET_TYPE_WRITE, (unsigned int)size, 2 /*TAIKALUKU*/);

        // Lähetetään data
        response = tcpipPortWriteBytes(busdevicePointer, requestBuffer, requestBufferSize);

        printf(" WW:%d ", response);

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    /* READ RESPONSE */

    {
        int response = 0;
        char responseBuffer[TCP_WRITE_RESPONSE_HEADER_LENGTH] = {0};

        // Odotetaan vastaus pyyntöön
        response = tcpipReadBytes(busdevicePointer, responseBuffer, TCP_WRITE_RESPONSE_HEADER_LENGTH);

        printf(" WR:%d ", response);

        // Jos virhe, palautetaan virhe // TODO: Suljetaan yhteys?
        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 2 \r\n");
            return SOCKET_ERROR;
        }

        if (response != TCP_WRITE_RESPONSE_HEADER_LENGTH)
        {
            printf(" TCPIPPORTWRITE ERROR 3 \r\n");
            return 0;
        }

        if (responseBuffer[0] != SM_PACKET_TYPE_WRITE)
        {
            printf(" TCPIPPORTWRITE ERROR 4 \r\n");
            return 0;
        }

        if (responseBuffer[1] == 1)
        {
            printf("\r\n");
            return size;
        }
    }

    printf("\r\n");

    return 0;
}

int tcpipEthSMPortRead(smBusdevicePointer busdevicePointer, unsigned char *buf, int size)
{
    int missingBytes = size - bufferAmountOfBytes((unsigned int)busdevicePointer);

    if (missingBytes > 0)
    {
        tcpipReadBytesFromAdapter(busdevicePointer, (unsigned int)missingBytes);
    }

    int cnt = 0;

    for (cnt = 0; cnt < size; ++cnt)
    {
        if (bufferAmountOfBytes((unsigned int)busdevicePointer))
        {
            bufferGetItem((unsigned int)busdevicePointer, (char*)&buf[cnt]);
        }
        else
        {
            break;
        }
    }

    return cnt;
}

smbool tcpipEthSMMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation, smint32 value)
{
    switch (operation)
    {

    case MiscOperationCheckIfBaudrateIsOK:
    {

        static smuint32 ETHSM_baudrates[] = {7500000, 6000000, 5000000, 4000000, 3750000, 3000000, 2500000, 2400000, 2000000, 1875000, 1500000, 1250000, 1200000, 1000000, 937500, 800000, 750000, 625000, 600000, 500000, 480000, 468750, 400000, 375000, 312500, 300000, 250000, 240000, 234375, 200000, 187500, 160000, 156250, 150000, 125000, 120000, 100000, 96000, 93750, 80000, 78125, 75000, 62500, 60000, 50000, 48000, 46875, 40000, 37500, 32000, 31250, 30000, 25000, 24000, 20000, 19200, 18750, 16000, 15625, 15000, 12500, 12000, 10000, 9600};
        // IONI_baudrates[] = {9.00000   4.50000   3.00000   2.25000   1.80000   1.50000   1.28571   1.12500   1.00000   0.90000   0.81818   0.75000   0.69231   0.64286   0.60000   0.56250};
        return smtrue;

        /*

STM32F207 Fractional baudrate generator can generate following baudrates with 8-bit oversampling:
60 MHz / (8 * USARTDIV),
where USARTDIV can be anything between 1 and 4096 with 0.125 steps

*/

        static const smuint32 ETHSM_BAUDRATES[3] = {
            9600,
            115200,
            460800};

        unsigned int baudrates = sizeof(ETHSM_BAUDRATES) / sizeof(ETHSM_BAUDRATES[0]);
        for (unsigned int i = 0; i < baudrates; ++i)
        {
            printf("Check BR %d \r\n", ETHSM_BAUDRATES[i]);
            if ((smuint32)value == ETHSM_BAUDRATES[i])
            {
                printf("Baudrate found!\r\n");
                return 1;
            }
        }

        return 0;
    }

    case MiscOperationPurgeRX:
    {
        int n;
        do //loop read as long as there is data coming in
        {
            char discardbuf[256];
            int sockfd = (int)busdevicePointer; //compiler warning expected here on 64 bit compilation but it's ok
            fd_set input;
            FD_ZERO(&input);
            FD_SET((unsigned int)sockfd, &input);
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;

            n = select(sockfd + 1, &input, NULL, NULL, &timeout); //Check whether socket is readable. Note: sockfd+1 is correct usage.

            if (n < 0) //  n=-1 select error
            {
                return smfalse; //select failed
            }
            if (n == 0) //  n=0 no data within timeout
            {
                return smtrue;
            }
            if (!FD_ISSET(sockfd, &input))
            {
                return smtrue; //no data available, redundant check (see above)?
            }
            //data is available, read it
            n = read((SOCKET)sockfd, (char *)discardbuf, 256); //TODO: should we read 1 byte at a time to avoid blocking here?
        } while (n > 0);
        return smtrue;
    }
    break;
    case MiscOperationFlushTX:
        //FlushTX should be taken care with disabled Nagle algoritmh
        return smtrue;
        break;
    default:
        smDebug(-1, SMDebugLow, "TCP/IP: given MiscOperataion not implemented\n");
        return smfalse;
        break;
    }
}
