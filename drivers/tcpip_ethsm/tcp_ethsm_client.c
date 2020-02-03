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
/*   STATIC FUNCTION PROTOTYPES               */
/**********************************************/

static void printBuffer(char *buf, unsigned int len);
static void multipleBytesToValue(char *buf, unsigned int len, unsigned int *value);
static void valueToMultipleBytes(char *buf, unsigned int len, unsigned int value);
static void addTCPRequestHeaders(char *buf, char packetType, unsigned int value, unsigned int len);
static char *createNewBufferWithLeadingFreeSpace(unsigned char *buf, unsigned int currentSize, unsigned int sizeToAdd);

static int ETHSMGetFeatures(smBusdevicePointer busdevicePointer, unsigned int *features);
static int ETHSMGetVersionNumbers(smBusdevicePointer busdevicePointer, char *buf);
static int ETHSMFlush(smBusdevicePointer busdevicePointer);
static int ETHSMSetBaudrate(smBusdevicePointer busdevicePointer, smuint32 baudrate);
static int ETHSMSetReadTimeout(smBusdevicePointer busdevicePointer, smuint32 timeoutMs);
static int ETHSMPurge(smBusdevicePointer busdevicePointer);
static int ETHSMParseAndValidateIPAddress(const char *str, char *IP, unsigned short *port);
static int ETHSMReadBytes(smBusdevicePointer busdevicePointer, unsigned int amountOfBytes);

static void TCPConnectionClose(smBusdevicePointer busdevicePointer);
static int TCPConnectionOpen(const char *IP, unsigned short port, smbool *success);
static int TCPReadBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int dataMaxLength);
static int TCPWriteBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int size);
static int TCPWaitKnownResponse(smBusdevicePointer busdevicePointer, const char *bytes, unsigned int length);

/**********************************************/
/*   GLOBALS & CONSTANTS                      */
/**********************************************/

#define TCP_WRITE_REQUEST_HEADER_LENGTH 3
#define TCP_WRITE_RESPONSE_HEADER_LENGTH 2
#define GLOBAL_BUFFER_SIZE 512

static char tempBuffer[GLOBAL_BUFFER_SIZE];

/**********************************************/
/*   SM API FUNCTIONS                         */
/**********************************************/

smBusdevicePointer ETHSMPortOpen(const char *devicename, smint32 baudrate_bps, smbool *success)
{
    // Parse and check IP address and port
    char IP[16] = {0};
    unsigned short port = 80;
    {
        if (!ETHSMParseAndValidateIPAddress(devicename, IP, &port))
        {
            smDebug(-1, SMDebugLow, "TCP/IP: device name '%s' does not appear to be IP address, skipping TCP/IP open attempt (note: this is normal if opening a non-TCP/IP port)\n", devicename);
            return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
        }
    }

    // Open TCP connection
    int sockfd = TCPConnectionOpen(IP, port, success);

    if (!success)
    {
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Create ring buffer for the connection
    if (!createRingBuffer((unsigned int)sockfd))
    {
        ETHSMPortClose((smBusdevicePointer)sockfd);
        printf("ERROR! Could not create ring buffer\r\n");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }
    // Set ETHSM baudrate
    if (!ETHSMSetBaudrate((smBusdevicePointer)sockfd, (smuint32)baudrate_bps))
    {
        ETHSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Read ETHSM feature flags
    unsigned int features = 0;
    if (!ETHSMGetFeatures((smBusdevicePointer)sockfd, &features))
    {
        ETHSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }
    printf("ETHSM Adapter Software Feature Flags: %d \r\n", features);

    // Read ETHSM software version number
    char version[10] = {0};
    if (!ETHSMGetVersionNumbers((smBusdevicePointer)sockfd, version))
    {
        ETHSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }
    printf("ETHSM Adapter software version: %s \r\n", version);
    printf("Return bus device pointer: %d \r\n", sockfd);
    *success = smtrue;
    return (smBusdevicePointer)sockfd;
}

void ETHSMPortClose(smBusdevicePointer busdevicePointer)
{
    TCPConnectionClose(busdevicePointer);
    removeRingBuffer((unsigned int)busdevicePointer);
}

int ETHSMPortWrite(smBusdevicePointer busdevicePointer, unsigned char *buf, int size)
{
    if (size <= 0)
    {
        return 0;
    }

    // Send request
    {
        int response = 0;

        char *requestBuffer = createNewBufferWithLeadingFreeSpace(buf, (unsigned int)size, TCP_WRITE_REQUEST_HEADER_LENGTH);
        unsigned int requestBufferSize = (unsigned int)size + TCP_WRITE_REQUEST_HEADER_LENGTH;

        if (!requestBuffer)
        {
            return 0;
        }

        addTCPRequestHeaders(requestBuffer, SM_PACKET_TYPE_WRITE, (unsigned int)size, 2 /*TAIKALUKU*/);

        response = TCPWriteBytes(busdevicePointer, requestBuffer, requestBufferSize);

        free(requestBuffer);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    // Read response
    {
        int response = 0;
        char responseBuffer[TCP_WRITE_RESPONSE_HEADER_LENGTH] = {0};

        response = TCPReadBytes(busdevicePointer, responseBuffer, TCP_WRITE_RESPONSE_HEADER_LENGTH);

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
            return size;
        }
    }

    return 0;
}

int ETHSMPortRead(smBusdevicePointer busdevicePointer, unsigned char *buf, int size)
{
    int missingBytes = size - bufferAmountOfBytes((unsigned int)busdevicePointer);

    if (missingBytes > 0)
    {
        ETHSMReadBytes(busdevicePointer, (unsigned int)missingBytes);
    }

    unsigned int cnt = 0;

    for (cnt = 0; cnt < (unsigned int)size; ++cnt)
    {
        if (bufferAmountOfBytes((unsigned int)busdevicePointer))
        {
            bufferGetItem((unsigned int)busdevicePointer, (char *)&buf[cnt]);
        }
        else
        {
            break;
        }
    }

    return (int)cnt;
}

smbool ETHSMMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation, smint32 value)
{
    switch (operation)
    {

    case MiscOperationCheckIfBaudrateIsOK:
    {

        if (value <= 800000 && value >= 50000) {
            return smtrue;
        } else {
            return smfalse;
        }
    }

    case MiscOperationPurgeRX:
    {
        if (ETHSMPurge(busdevicePointer)) {
            return smtrue;
        }else {
            return smfalse;
        }
    }
    case MiscOperationFlushTX:
        if (ETHSMFlush(busdevicePointer)) {
            return smtrue;
        }else {
            return smfalse;
        }
    default:
        smDebug(-1, SMDebugLow, "TCP/IP: given MiscOperataion not implemented\n");
        return smfalse;
    }
}

/**********************************************/
/*   MISCELLANEOUS FUNCTIONS                  */
/**********************************************/

static void printBuffer(char *buf, unsigned int len)
{
    printf("printBuffer(%d bytes)(", len);
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
    for (unsigned int i = 0; i < len; ++i)
    {
        buf[i] = ((char)(value >> (8 * i))) & (char)0xFF;
    }
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
        return NULL;
    }

    memcpy(buffer + sizeToAdd, buf, (unsigned int)currentSize);
    return buffer;
}

// A suitable address is in form ETHSM:<IP>:<PORT>
// The shortest possible address: ETHSM:1.1.1.1:1 (15 characters)
// The longest possible address: ETHSM:255.255.255.255:65535 (27 characters)
// Returns 1 if a suitable address and port have been found. Otherwise returns 0.
static int ETHSMParseAndValidateIPAddress(const char *str, char *IP, unsigned short *port)
{
    const char *ADDR_START = "ETHSM:";
    const unsigned int ADDR_START_LEN = strlen(ADDR_START);

    const unsigned int addrMinLen = 15;
    const unsigned int addrMaxLen = 27;
    const unsigned int addrLen = strlen(str);

    // Check str length
    if (addrLen < addrMinLen || addrLen > addrMaxLen)
    {
        return 0;
    }

    // Check if str starts with correct characters
    if (strncmp(str, ADDR_START, ADDR_START_LEN) != 0)
    {
        return 0;
    }

    // Find the end of starting word
    const char *addrStart = str + ADDR_START_LEN;

    // Find the colon separating IP address and port
    const char *colon = strchr(addrStart, ':');

    // Parse port
    {
        unsigned int p = (unsigned int)atoi(colon + 1);
        if (p > 65535 || p == 0)
        {
            return 0;
        }
        *port = (unsigned short)p;
        printf("Found port: %d \r\n", *port);
    }

    // Check IP address
    {
        unsigned int digits;
        const char *s = addrStart;

        printf("Parse IP starting from %s \r\n", addrStart);

        // Go through four numeric parts
        for (int k = 0; k < 4; ++k)
        {
            digits = 0;

            // Check that every numeric part contains 1-3 numbers
            for (unsigned int i = 0; i < 4; ++i)
            {
                if (!isdigit(*s))
                {
                    digits = i;
                    break;
                }
                s++;
            }

            if (digits == 0)
            {
                return 0;
            }

            if (k < 3 && *s != '.')
            {
                return 0;
            }

            if (strtoul(s - digits, NULL, 10) > 255)
            {
                return 0;
            }

            s++;
        }
    }

    strncpy(IP, addrStart, (unsigned int)(colon - addrStart));

    return 1;
}

/**********************************************/
/*   FUNCTIONS DIRECTLY HANDLING TCP BUS      */
/**********************************************/

// Open TCP connection.
static int TCPConnectionOpen(const char *IP, unsigned short port, smbool *success)
{

#if defined(_WIN32)
    initwsa();
#endif

    printf("Connect to IP %s, port %d \r\n", IP, port);
    smDebug(-1, SMDebugLow, "TCP/IP: Attempting to connect to %s:%d\n", IP, port);

    *success = smfalse;

    // Open TCP socket
    int sockfd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1)
    {
        smDebug(-1, SMDebugLow, "TCP/IP: Socket open failed (sys error: %s)\n", strerror(errno));
        return 0;
    }

    // Set OFF NAGLE algorithm to disable stack buffering of small packets
    {
        int one = 1;
        setsockopt((SOCKET)sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&one, sizeof(one));
    }

    // Set non-blocking when trying to establish the connection
    {
        unsigned long arg;
#if !defined(_WIN32)
        arg = fcntl(sockfd, F_GETFL, NULL);
        arg |= O_NONBLOCK;
        fcntl(sockfd, F_SETFL, arg);
#else
        arg = 1;
        ioctlsocket((SOCKET)sockfd, (long)FIONBIO, &arg);
#endif
    }

    // Connect to ETHSM device
    {
        struct sockaddr_in server;
        server.sin_addr.s_addr = inet_addr(IP);
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        int res = connect((SOCKET)sockfd, (struct sockaddr *)&server, sizeof(server));

        if (res < 0) //connection not established (at least yet)
        {
            if (errno == EINPROGRESS) //check if it may be due to non-blocking mode (delayed connect)
            {
                struct timeval tv;
                tv.tv_sec = 5; //max wait time
                tv.tv_usec = 0;

                fd_set myset;
                FD_ZERO(&myset);
                FD_SET((unsigned int)sockfd, &myset);
                if (select(sockfd + 1, NULL, &myset, NULL, &tv) > 0)
                {
                    socklen_t lon = sizeof(int);
                    int valopt;
                    getsockopt((SOCKET)sockfd, SOL_SOCKET, SO_ERROR, (void *)(&valopt), &lon);
                    if (valopt) //if valopt!=0, then there was an error. if it's 0, then connection established successfully (will return here from smtrue eventually)
                    {
                        printf("Err1\r\n");
                        smDebug(-1, SMDebugLow, "TCP/IP: Setting socket properties failed (sys error: %s)\n", strerror(errno));
                        return 0;
                    }
                }
                else
                {
                    printf("Err2 %s\r\n", strerror(errno));
                    smDebug(-1, SMDebugLow, "TCP/IP: Setting socket properties failed (sys error: %s)\n", strerror(errno));
                    return 0;
                }
            }
            else
            {
                printf("Err3\r\n");
                smDebug(-1, SMDebugLow, "TCP/IP: Connecting socket failed (sys error: %s)\n", strerror(errno));
                return 0;
            }
        }
    }
    // Set to blocking mode again
    {
        unsigned long arg = 0;
#if !defined(_WIN32)
        arg = fcntl(sockfd, F_GETFL, NULL);
        arg &= (~O_NONBLOCK);
        fcntl(sockfd, F_SETFL, arg);
#else
        arg = 0;
        ioctlsocket((SOCKET)sockfd, (long)FIONBIO, &arg);
#endif
    }
    printf("Return bus device pointer: %d \r\n", sockfd);

    *success = smtrue;

    return sockfd; //compiler warning expected here on 64 bit compilation but it's ok
}

// Close a TCP connection
static void TCPConnectionClose(smBusdevicePointer busdevicePointer)
{
    int sockfd = (int)busdevicePointer; //compiler warning expected here on 64 bit compilation but it's ok
    close((SOCKET)sockfd);
#if defined(_WIN32)
    WSACleanup();
#endif
}

// Close given bytes to the TCP connection
static int TCPWriteBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int size)
{
    int sent = write((SOCKET)busdevicePointer, buf, (int)size);
    return sent;
}

// Try to read given amount of bytes from TCP
static int TCPReadBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int dataMaxLength)
{

    SOCKET sockfd = (SOCKET)busdevicePointer;
    fd_set input;
    FD_ZERO(&input);
    FD_SET((unsigned int)sockfd, &input);
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    //n=-1, select failed. 0=no data within timeout ready, >0 data available. Note: sockfd+1 is correct usage.
    int n = select((int)sockfd + 1, &input, NULL, NULL, &timeout);

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

/**********************************************/
/*   TCP TASKS                                */
/**********************************************/

// Wait a known TCP response. Returns 1 if supposed response was received. 0 if another response. SOCKET_ERROR if SOCKET_ERROR.
static int TCPWaitKnownResponse(smBusdevicePointer busdevicePointer, const char *bytes, unsigned int length)
{
    char *receivedBytes = (char*)calloc(length, sizeof(bytes[0]));

    int response = TCPReadBytes(busdevicePointer, receivedBytes, length);

    if (response == SOCKET_ERROR) {
        return SOCKET_ERROR;
    }

    if (response != (int)length) {
        return 0;
    }

    if (memcmp(bytes, receivedBytes, length) != 0) {
        return 0;
    }

    free(receivedBytes);

    return 1;
}

// Reads bytes from the ETHSM adapter
static int ETHSMReadBytes(smBusdevicePointer busdevicePointer, unsigned int amountOfBytes)
{
    unsigned int supposedPacketLength = 0;

    // Send read request
    {
        int response = 0;
        char requestBuffer[3] = {0};
        addTCPRequestHeaders(requestBuffer, SM_PACKET_TYPE_READ, amountOfBytes, 2 /*TAIKALUKU*/);
        response = TCPWriteBytes(busdevicePointer, requestBuffer, 3);
        if (response == SOCKET_ERROR)
        {
            printf("ETHSMReadBytes ERROR 0 \r\n");
            return SOCKET_ERROR;
        }
    }

    // Read response header
    {
        char responseHeaderBuffer[3] = {0};
        int response = TCPReadBytes(busdevicePointer, responseHeaderBuffer, 3);

        if (response == SOCKET_ERROR)
        {
            printf(" ETHSMReadBytes ERROR 1 \r\n \r\n");
            return SOCKET_ERROR;
        }

        if (response != 3)
        {
            printf(" ETHSMReadBytes ERROR 2 \r\n");
            return 0;
        }

        char packetType = responseHeaderBuffer[0];

        if (packetType != SM_PACKET_TYPE_READ)
        {
            printf(" ETHSMReadBytes ERROR 3 \r\n");
            return 0;
        }

        multipleBytesToValue(&responseHeaderBuffer[1], 2, &supposedPacketLength);
    }

    // Read response bytes
    {
        int response = TCPReadBytes(busdevicePointer, tempBuffer, supposedPacketLength);
        unsigned int dataLength = (unsigned int)response;

        if (response == SOCKET_ERROR)
        {
            printf(" ETHSMReadBytes ERROR 4 \r\n");
            return SOCKET_ERROR;
        }

        if (response != (int)supposedPacketLength)
        {
            printf(" ETHSMReadBytes ERROR 5 \r\n");
            return 0;
        }

        for (unsigned int i = 0; i < dataLength; ++i)
        {
            bufferAddItem((unsigned int)busdevicePointer, tempBuffer[i]);
        }
    }

    return 0;
}

static int ETHSMSetReadTimeout(smBusdevicePointer busdevicePointer, smuint32 timeoutMs)
{
    // Send request
    {
        int response = 0;

        const unsigned int requestLength = 3;

        char requestBuffer[3] = {0};
        requestBuffer[0] = SM_PACKET_TYPE_SET_TIMEOUT;

        valueToMultipleBytes(&requestBuffer[1], 2, timeoutMs);

        response = TCPWriteBytes(busdevicePointer, requestBuffer, requestLength);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    // Read response
    {
        char expectedByte = SM_PACKET_TYPE_SET_TIMEOUT;
        int response = TCPWaitKnownResponse(busdevicePointer, &expectedByte, 1);
        return response;
    }
}

static int ETHSMSetBaudrate(smBusdevicePointer busdevicePointer, smuint32 baudrate)
{
    // Send request
    {
        int response = 0;

        const unsigned int requestLength = 4;

        char requestBuffer[4] = {0};
        requestBuffer[0] = SM_PACKET_TYPE_SET_BAUDRATE;

        valueToMultipleBytes(&requestBuffer[1], 3, baudrate);

        response = TCPWriteBytes(busdevicePointer, requestBuffer, requestLength);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    // Read response
    {
        char expectedByte = SM_PACKET_TYPE_SET_BAUDRATE;
        int response = TCPWaitKnownResponse(busdevicePointer, &expectedByte, 1);
        return response;
    }
}

static int ETHSMGetVersionNumbers(smBusdevicePointer busdevicePointer, char *buf)
{
    // Send request
    {
        int response = 0;

        char request = SM_PACKET_TYPE_GET_DEVICE_VERSION_NUMBERS;

        response = TCPWriteBytes(busdevicePointer, &request, 1);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    // Read response
    {
        int response = 0;

        const int responseLength = 10;

        response = TCPReadBytes(busdevicePointer, buf, responseLength);

        if (response == responseLength)
        {
            return 1;
        }

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
static int ETHSMGetFeatures(smBusdevicePointer busdevicePointer, unsigned int *features)
{
    // Send request
    {
        int response = 0;

        char request = SM_PACKET_TYPE_GET_DEVICE_FEATURES;

        response = TCPWriteBytes(busdevicePointer, &request, 1);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    // Read response
    {
        int response = 0;

        const int responseLength = 4;
        char responseData[4] = {0};

        response = TCPReadBytes(busdevicePointer, responseData, responseLength);

        if (response == responseLength)
        {
            multipleBytesToValue(responseData, responseLength, features);
            return 1;
        }

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 2 \r\n");
            return SOCKET_ERROR;
        }

        printf(" TCPIPPORTWRITE ERROR 3 \r\n");
        return SOCKET_ERROR;
    }
}

static int ETHSMPurge(smBusdevicePointer busdevicePointer)
{
    ETHSMSetReadTimeout(busdevicePointer, 0);

    unsigned char buffer[100];
    int bufferSize = sizeof(buffer) / sizeof(buffer[0]);
    int receivedData = bufferSize;

    while (receivedData == bufferSize)
    {
        receivedData = ETHSMPortRead(busdevicePointer, buffer, (int)bufferSize);
    }

    ETHSMSetReadTimeout(busdevicePointer, ETHSM_READ_TIMEOUT_MS);
    return 1;
}

static int ETHSMFlush(smBusdevicePointer busdevicePointer)
{
    // Send request
    {
        int response = 0;

        char request = SM_PACKET_TYPE_FLUSH;

        response = TCPWriteBytes(busdevicePointer, &request, 1);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPPORTWRITE ERROR 1 \r\n");
            return SOCKET_ERROR;
        }
    }

    // Read response
    {
        char expectedByte = SM_PACKET_TYPE_SET_TIMEOUT;
        int response = TCPWaitKnownResponse(busdevicePointer, &expectedByte, 1);

        if (response) {
            return smtrue;
        } else {
            return smfalse;
        }
    }
}
