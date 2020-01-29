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

static smbool tcpipSetBaudrate(smBusdevicePointer busdevicePointer, smuint32 baudrate);
static void printBuffer(char *buf, unsigned int len);
static void multipleBytesToValue(char *buf, unsigned int len, unsigned int *value);
static void valueToMultipleBytes(char *buf, unsigned int len, unsigned int value);
static void addTCPRequestHeaders(char *buf, char packetType, unsigned int value, unsigned int len);
static char *createNewBufferWithLeadingFreeSpace(unsigned char *buf, unsigned int currentSize, unsigned int sizeToAdd);
static int getETHSMAdapterFeatures(smBusdevicePointer busdevicePointer, unsigned int *features);
static int getETHSMAdapterVersionNumbers(smBusdevicePointer busdevicePointer, char *buf);
static int tcpipFlush(smBusdevicePointer busdevicePointer);
static int tcpipPurge(smBusdevicePointer busdevicePointer);
static smbool tcpipSetETHSMReadTimeout(smBusdevicePointer busdevicePointer, smuint32 timeoutMs);
static void closeTCPConnection(smBusdevicePointer busdevicePointer);
static int openTCPConnection(const char *IP, unsigned short port, smbool *success);
static void closeTCPConnection(smBusdevicePointer busdevicePointer);
static int tcpipReadBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int dataMaxLength);
static int tcpipPortWriteBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int size);
static int tcpipReadBytesFromAdapter(smBusdevicePointer busdevicePointer, unsigned int amountOfBytes);
static int validateAndParseETHSMIPAddress(const char *str, char *IP, unsigned short *port);

/**********************************************/
/*   GLOBALS & CONSTANTS                      */
/**********************************************/

#define TCP_WRITE_REQUEST_HEADER_LENGTH 3
#define TCP_WRITE_RESPONSE_HEADER_LENGTH 2
#define GLOBAL_BUFFER_SIZE 512
#define ETHSM_READ_TIMEOUT_MS 5

static char tempBuffer[GLOBAL_BUFFER_SIZE];

/**********************************************/
/*   SM API FUNCTIONS                         */
/**********************************************/

smBusdevicePointer tcpipEthSMPortOpen(const char *devicename, smint32 baudrate_bps, smbool *success)
{
    // Parse and check IP address and port
    char IP[16] = {0};
    unsigned short port = 80;
    {
        if (!validateAndParseETHSMIPAddress(devicename, IP, &port))
        {
            smDebug(-1, SMDebugLow, "TCP/IP: device name '%s' does not appear to be IP address, skipping TCP/IP open attempt (note: this is normal if opening a non-TCP/IP port)\n", devicename);
            return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
        }
    }

    // Open TCP connection
    int sockfd = openTCPConnection(IP, port, success);

    if (!success)
    {
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Create ring buffer for the connection
    if (!createRingBuffer((unsigned int)sockfd))
    {
        tcpipEthSMPortClose((smBusdevicePointer)sockfd);
        printf("ERROR! Could not create ring buffer\r\n");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Set ETHSM baudrate
    if (!tcpipSetBaudrate((smBusdevicePointer)sockfd, (smuint32)baudrate_bps))
    {
        tcpipEthSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Read ETHSM feature flags
    unsigned int features = 0;
    if (!getETHSMAdapterFeatures((smBusdevicePointer)sockfd, &features))
    {
        tcpipEthSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Read ETHSM software version number
    char version[10] = {0};
    if (!getETHSMAdapterVersionNumbers((smBusdevicePointer)sockfd, version))
    {
        tcpipEthSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Flush
    if (!tcpipFlush((smBusdevicePointer)sockfd))
    {
        tcpipEthSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // Purge
    if (!tcpipPurge((smBusdevicePointer)sockfd))
    {
        tcpipEthSMPortClose((smBusdevicePointer)sockfd);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    printf("Return bus device pointer: %d \r\n", sockfd);
    *success = smtrue;
    return (smBusdevicePointer)sockfd;
}

void tcpipEthSMPortClose(smBusdevicePointer busdevicePointer)
{
    closeTCPConnection(busdevicePointer);
    removeRingBuffer((unsigned int)busdevicePointer);
}

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

smbool tcpipEthSMMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation, smint32 value)
{
    switch (operation)
    {

    case MiscOperationCheckIfBaudrateIsOK:
    {

        // static smuint32 ETHSM_baudrates[] = {7500000, 6000000, 5000000, 4000000, 3750000, 3000000, 2500000, 2400000, 2000000, 1875000, 1500000, 1250000, 1200000, 1000000, 937500, 800000, 750000, 625000, 600000, 500000, 480000, 468750, 400000, 375000, 312500, 300000, 250000, 240000, 234375, 200000, 187500, 160000, 156250, 150000, 125000, 120000, 100000, 96000, 93750, 80000, 78125, 75000, 62500, 60000, 50000, 48000, 46875, 40000, 37500, 32000, 31250, 30000, 25000, 24000, 20000, 19200, 18750, 16000, 15625, 15000, 12500, 12000, 10000, 9600};
        // IONI_baudrates[] = {9.00000   4.50000   3.00000   2.25000   1.80000   1.50000   1.28571   1.12500   1.00000   0.90000   0.81818   0.75000   0.69231   0.64286   0.60000   0.56250};
        value++;
        return smtrue;

        /*

STM32F207 Fractional baudrate generator can generate following baudrates with 8-bit oversampling:
60 MHz / (8 * USARTDIV),
where USARTDIV can be anything between 1 and 4096 with 0.125 steps


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
*/
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
    case MiscOperationFlushTX:
        //FlushTX should be taken care with disabled Nagle algoritmh
        return smtrue;
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

// A suitable address is in form ETHSM:<IP>:<PORT>
// The shortest possible address: ETHSM:1.1.1.1:1 (15 characters)
// The longest possible address: ETHSM:255.255.255.255:65535 (27 characters)
// Returns 1 if a suitable address and port have been found. Otherwise returns 0.
static int validateAndParseETHSMIPAddress(const char *str, char *IP, unsigned short *port)
{

    printf("validateAndParseETHSMIPAddress %s \r\n", str);

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

static int openTCPConnection(const char *IP, unsigned short port, smbool *success)
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

static void closeTCPConnection(smBusdevicePointer busdevicePointer)
{
    int sockfd = (int)busdevicePointer; //compiler warning expected here on 64 bit compilation but it's ok
    close((SOCKET)sockfd);
#if defined(_WIN32)
    WSACleanup();
#endif
}

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

/**********************************************/
/*   TCP TASKS                                */
/**********************************************/

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

static smbool tcpipSetETHSMReadTimeout(smBusdevicePointer busdevicePointer, smuint32 timeoutMs)
{
    printf("tcpipSetETHSMReadTimeout %d \r\n", timeoutMs);

    /* SEND WRITE REQUEST */

    {
        int response = 0;

        const unsigned int requestLength = 3;

        char requestBuffer[3] = {0};
        requestBuffer[0] = SM_PACKET_TYPE_SET_TIMEOUT;

        valueToMultipleBytes(&requestBuffer[1], 2, timeoutMs);

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

        if (response == SM_PACKET_TYPE_SET_TIMEOUT && responseByte == 1)
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

        const int responseLength = 10;

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
static int getETHSMAdapterFeatures(smBusdevicePointer busdevicePointer, unsigned int *features)
{
    printf("getETHSMAdapterFeatures \r\n");

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

        const int responseLength = 4;
        char responseData[4] = {0};

        // Odotetaan vastaus pyyntöön
        response = tcpipReadBytes(busdevicePointer, responseData, responseLength);

        printf(" WR:%d\r\n ", response);

        if (response == responseLength)
        {
            multipleBytesToValue(responseData, responseLength, features);
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

static int tcpipPurge(smBusdevicePointer busdevicePointer)
{

    printf("tcpipPurge\r\n");

    tcpipSetETHSMReadTimeout(busdevicePointer, 0);

    unsigned char buffer[100];
    int bufferSize = sizeof(buffer) / sizeof(buffer[0]);
    int receivedData = bufferSize;

    while (receivedData == bufferSize)
    {
        printf("Purge %d \r\n", receivedData);
        receivedData = tcpipEthSMPortRead(busdevicePointer, buffer, (int)bufferSize);
    }

    printf("tcpipPurge DONE\r\n");

    tcpipSetETHSMReadTimeout(busdevicePointer, ETHSM_READ_TIMEOUT_MS);
    return 1;
}

static int tcpipFlush(smBusdevicePointer busdevicePointer)
{
    printf("tcpipFlush \r\n");

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
