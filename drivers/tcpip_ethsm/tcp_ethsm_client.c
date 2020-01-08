#include "simplemotion_private.h"
#include "tcp_ethsm_client.h"
#include "user_options.h"
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


/**********************************************/
/*   GLOBALS & CONSTANTS                      */
/**********************************************/


#define TCP_WRITE_REQUEST_HEADER_LENGTH 3
#define TCP_WRITE_RESPONSE_HEADER_LENGTH 2
#define GLOBAL_BUFFER_SIZE 512

static char tempBuffer[GLOBAL_BUFFER_SIZE];


/**********************************************/
/*   RING BUFFER                              */
/**********************************************/

#define AMOUNT_OF_BUFFERS 8
#define BUFFER_SIZE 512
#define BUFFER_TYPE char

typedef struct RingBuffer {
    unsigned int bufferIdentifier;
    unsigned int writePointer;
    unsigned int readPointer;
    BUFFER_TYPE data[BUFFER_SIZE];
} RingBuffer;

static RingBuffer* ringBuffers[AMOUNT_OF_BUFFERS];

static int findBufferIndexByIdentifier(unsigned int bufferIdentifier)
{
    int index = -1;
    for (int i = 0; i < AMOUNT_OF_BUFFERS; ++i)
    {
        if (ringBuffers[i] != NULL && ringBuffers[i]->bufferIdentifier == bufferIdentifier)
        {
            index = i;
            break;
        }
    }
    return index;
}

static int createRingBuffer(unsigned int bufferIdentifier)
{
    int index = -1;

    for (int i = 0; i < AMOUNT_OF_BUFFERS; ++i)
    {
        if (ringBuffers[i] == NULL)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        return -1;
    }

    RingBuffer *buffer = (RingBuffer*) malloc(sizeof(RingBuffer));

    if (!buffer)
    {
        printf("ALLOCATE ERROR\r\n");
        return -1;
    }

    memset(buffer,0,sizeof(RingBuffer));
    ringBuffers[index] = buffer;
    buffer->bufferIdentifier = bufferIdentifier;
    return index;
}

static int removeRingBuffer(unsigned int bufferIdentifier)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        free(ringBuffers[index]);
        ringBuffers[index] = NULL;
    }

    return index;
}

static void bufferAddByte(unsigned int bufferIdentifier, BUFFER_TYPE byte)
{ // TODO: virheen palautus
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];
        buf->data[buf->writePointer++] = byte;
        buf->writePointer %= BUFFER_SIZE;
    }
}

static BUFFER_TYPE bufferGetByte(unsigned int bufferIdentifier)
{ // TODO: virheen palautus

    BUFFER_TYPE r = 0;
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];
        r = buf->data[buf->readPointer];
        buf->data[buf->readPointer++] = 0;
        buf->readPointer %= BUFFER_SIZE;
    }

    return r;
}

static void bufferClear(unsigned int bufferIdentifier)
{
    printf ("FUNCTION %s \r\n", __PRETTY_FUNCTION__);
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];
        buf->readPointer = 0;
        buf->writePointer = 0;
        memset(buf->data, 0, BUFFER_SIZE * sizeof(BUFFER_TYPE));
    }
}

static int bufferAmountOfBytes(unsigned int bufferIdentifier)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];

        int r = (int)buf->writePointer - (int)buf->readPointer;
        if (r < 0)
        {
            r += BUFFER_SIZE;
        }
        return r;
    } else {
        return -1;
    }
}


/**********************************************/
/*   MISCELLANEOUS                            */
/**********************************************/

static void printBuffer(char* buf, unsigned int len)
{
    printf("PB(");
    for (unsigned int i = 0; i < len; ++i)
    {
        printf("%d ", buf[i]);
    }
    printf(") ");
}

static void multipleBytesToValue(char* buf, unsigned int len, unsigned int* value)
{
    *value = 0;
    for (unsigned int i = 0; i < len; ++i)
    {
        *value += (unsigned int) (buf[i] << (8*i));
    }
}

static void valueToMultipleBytes(char* buf, unsigned int len, unsigned int* value)
{
    for (unsigned int i = 0; i < len; ++i)
    {
        buf[i] = ( (char) *value >> (8 * i) ) & (char) 0xFF;
    }
}

static void addTCPRequestHeaders(char* buf, char packetType, unsigned int value, unsigned int len)
{
    buf[0] = packetType;
    valueToMultipleBytes(&buf[1], len, &value);
}

static char* createNewBufferWithLeadingFreeSpace(unsigned char* buf, unsigned int currentSize, unsigned int sizeToAdd)
{
    unsigned int newBufferSize = currentSize + sizeToAdd;

    char *buffer = (char*) malloc(newBufferSize);

    if (!buffer)
    {
        printf("ALLOCATE ERROR\r\n");
        return NULL;
    }

    memcpy(buffer+sizeToAdd, buf, (unsigned int) currentSize);
    return buffer;
}


/**********************************************/
/*   TCP BUS HANDLING FUNCTIONS               */
/**********************************************/

smBusdevicePointer tcpipEthSMPortOpen(const char * devicename, smint32 baudrate_bps, smbool *success)
{
    printf("tcpipEthSMPortOpen %s \r\n", devicename);

    int sockfd;
    struct sockaddr_in server;
    struct timeval tv;
    fd_set myset;
    int res, valopt;
    socklen_t lon;
    unsigned long arg;

    *success=smfalse;

    printf("Validate IP address: \r\n");
    if (validateEthSMIpAddress(devicename, NULL, NULL) != 0)
    {
        smDebug(-1,SMDebugLow,"TCP/IP: device name '%s' does not appear to be IP address, skipping TCP/IP open attempt (note: this is normal if opening a non-TCP/IP port)\n",devicename);
        printf("Not valid IP \r\n");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    printf("Valid IP! \r\n");
    char ip_addr[16];
    unsigned short port = 4001;

    printf("Parse IP address: \r\n");
    if (parseEthSMIpAddress(devicename, ip_addr, &port) < 0)
    {
        smDebug(-1,SMDebugLow,"TCP/IP: IP address parse failed\n");
        printf("Failed! \r\n");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    printf("Success!\r\n");

    if(baudrate_bps!=SM_BAUDRATE)
    {
        smDebug(-1,SMDebugLow,"TCP/IP: Non-default baudrate not supported by TCP/IP protocol\n"); // TODO
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    smDebug(-1,SMDebugLow,"TCP/IP: Attempting to connect to %s:%d\n",ip_addr,port);

#if defined(_WIN32)
    initwsa();
#endif

    //Create socket
    sockfd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1)
    {
        smDebug(-1,SMDebugLow,"TCP/IP: Socket open failed (sys error: %s)\n",strerror(errno));
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
            tv.tv_sec = 1;//max wait time // TODO, magic number
            tv.tv_usec = 0;
            FD_ZERO(&myset);
            FD_SET((unsigned int)sockfd, &myset);
            if (select(sockfd+1, NULL, &myset, NULL, &tv) > 0)
            {
                lon = sizeof(int);
                getsockopt((SOCKET)sockfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon);
                if (valopt) //if valopt!=0, then there was an error. if it's 0, then connection established successfully (will return here from smtrue eventually)
                {
                    smDebug(-1,SMDebugLow,"TCP/IP: Setting socket properties failed (sys error: %s)\n",strerror(errno));
                    return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
                }
            }
            else
            {
                smDebug(-1,SMDebugLow,"TCP/IP: Setting socket properties failed (sys error: %s)\n",strerror(errno));
                return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
            }
        }
        else
        {
            smDebug(-1,SMDebugLow,"TCP/IP: Connecting socket failed (sys error: %s)\n",strerror(errno));
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

    *success=smtrue;

    createRingBuffer((unsigned int)sockfd); // TODO: Virheenkäsittely

    return (smBusdevicePointer)sockfd;//compiler warning expected here on 64 bit compilation but it's ok
}

void tcpipEthSMPortClose(smBusdevicePointer busdevicePointer)
{
    int sockfd=(int)busdevicePointer;//compiler warning expected here on 64 bit compilation but it's ok
    close((SOCKET)sockfd);
#if defined(_WIN32)
    WSACleanup();
#endif
    removeRingBuffer((unsigned int)sockfd);
}

int tcpipConnectionStatus(smBusdevicePointer busdevicePointer)
{
    (void) busdevicePointer;
    // TODO, What if connection is closed by other device?
    return 1;
}




/**********************************************/
/*   TCP TASKS                                */
/**********************************************/

static int tcpipPortWriteBytes(smBusdevicePointer busdevicePointer, char *buf, unsigned int size)
{
    printBuffer(buf, size);
    int sent = write((SOCKET) busdevicePointer, buf, (int)size);
    return sent;
}

// Try to read given amount of bytes from TCP
static int tcpipReadBytes(smBusdevicePointer busdevicePointer, char* buf, unsigned int dataMaxLength)
{

    SOCKET sockfd=(SOCKET)busdevicePointer;
    fd_set input;
    FD_ZERO(&input);
    FD_SET((unsigned int)sockfd, &input);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // TODO, where to get this?

    int n = select((int)sockfd + 1, &input, NULL, NULL, &timeout);//n=-1, select failed. 0=no data within timeout ready, >0 data available. Note: sockfd+1 is correct usage.

    if (n < 1) //n=-1 error, n=0 timeout occurred
    {
        return(n);
    }

    if(!FD_ISSET(sockfd, &input))
    {
        return(n);//no data available
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
        char requestBuffer[3] = { 0 };
        addTCPRequestHeaders(requestBuffer, SM_PACKET_TYPE_READ, amountOfBytes, 2/*TAIKALUKU*/);
        response = tcpipPortWriteBytes(busdevicePointer, requestBuffer, 3);
        printf(" W:%d ", response);
        if (response == SOCKET_ERROR)
        {
            printf("TCPIPREADBYTESFROMADAPTER ERROR 0 \r\n");
            return SOCKET_ERROR;
        }
    }


    /* READ RESPONSE HEADER */

    {
        char responseHeaderBuffer[3] = { 0 };
        int response = tcpipReadBytes(busdevicePointer, responseHeaderBuffer, 3);
        printf(" R:%d ", response);
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
        unsigned int dataLength = (unsigned int) response;

        printf(" B:%d ", response);

        if (response == SOCKET_ERROR)
        {
            printf(" TCPIPREADBYTESFROMADAPTER ERROR 4 \r\n");
            return SOCKET_ERROR;
        }

        if (response != (int) supposedPacketLength)
        {
            printf(" TCPIPREADBYTESFROMADAPTER ERROR 5 \r\n");
            return 0;
        }

        printBuffer(tempBuffer, dataLength);

        for (unsigned int i = 0; i < dataLength; ++i)
        {
            bufferAddByte((unsigned int)busdevicePointer, tempBuffer[i]); // TODO: buffer full?
        }
    }

    printf("\r\n");

    return 0;
}

static void tcpipPurge()
{
}

static void tcpipFlush()
{

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
int tcpipEthSMPortWrite(smBusdevicePointer busdevicePointer, unsigned char * buf, int size)
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
        char* requestBuffer = createNewBufferWithLeadingFreeSpace(buf, (unsigned int)size, TCP_WRITE_REQUEST_HEADER_LENGTH); // TODO, VAPAUTA TÄMÄ
        unsigned int requestBufferSize = (unsigned int) size + TCP_WRITE_REQUEST_HEADER_LENGTH;

        if (!requestBuffer)
        {
            return 0;
        }

        // Lisätään headerit
        addTCPRequestHeaders(requestBuffer, SM_PACKET_TYPE_WRITE, (unsigned int)size, 2/*TAIKALUKU*/);

        // Lähetetään data
        response = tcpipPortWriteBytes(busdevicePointer, requestBuffer, requestBufferSize);

        printf(" W:%d ", response);

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
        char responseBuffer[TCP_WRITE_RESPONSE_HEADER_LENGTH] = { 0 };

        // Odotetaan vastaus pyyntöön
        response = tcpipReadBytes(busdevicePointer, responseBuffer, TCP_WRITE_RESPONSE_HEADER_LENGTH);

        printf(" R:%d ", response);

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

int tcpipEthSMPortRead(smBusdevicePointer busdevicePointer, unsigned char* buf, int size)
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
            buf[cnt] = (unsigned char)bufferGetByte((unsigned int)busdevicePointer);
        } else {
            break;
        }
    }

    return cnt;
}


smbool tcpipEthSMMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation)
{
    switch(operation)
    {
    case MiscOperationPurgeRX:
    {
        int n;
        do //loop read as long as there is data coming in
        {
            char discardbuf[256];
            int sockfd=(int)busdevicePointer;//compiler warning expected here on 64 bit compilation but it's ok
            fd_set input;
            FD_ZERO(&input);
            FD_SET((unsigned int)sockfd, &input);
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;

            n = select(sockfd + 1, &input, NULL, NULL, &timeout);//Check whether socket is readable. Note: sockfd+1 is correct usage.

            if (n < 0)//  n=-1 select error
            {
                return smfalse;//select failed
            }
            if (n == 0)//  n=0 no data within timeout
            {
                return smtrue;
            }
            if(!FD_ISSET(sockfd, &input))
            {
                return smtrue; //no data available, redundant check (see above)?
            }
            //data is available, read it
            n = read((SOCKET)sockfd, (char*)discardbuf, 256);//TODO: should we read 1 byte at a time to avoid blocking here?
        } while( n>0 );
        return smtrue;
    }
        break;
    case MiscOperationFlushTX:
        //FlushTX should be taken care with disabled Nagle algoritmh
        return smtrue;
        break;
    default:
        smDebug( -1, SMDebugLow, "TCP/IP: given MiscOperataion not implemented\n");
        return smfalse;
        break;
    }
}


//accepted TCP/IP address format is ETHSM:nnn.nnn.nnn.nnn:pppp where n is IP address numbers and p is port number
//params: s=whole ip address with port number with from user, pip_end=output pointer pointint to end of ip address part of s, pport_stasrt pointing to start of port number in s
int validateEthSMIpAddress(const char *s, const char **pip_end,
                             const char **pport_start)
{
    int octets = 0;
    int ch = 0, prev = 0;
    int len = 0;
    const char *ip_end = NULL;
    const char *port_start = NULL;

    const char* start = "ETHSM:";

    if (strncmp(s, start, strlen(start)) != 0) {
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
int parseEthSMIpAddress(const char *s, char *ip, unsigned short *port)
{
    const char *ip_end, *port_start;

    //ip_end and port_start are pointers to memory area of s, not offsets or indexes to s
    if (validateEthSMIpAddress(s, &ip_end, &port_start) == -1)
        return -1;

    int IPAddrLen=ip_end - s;

    printf("ip len: %d \r\n", IPAddrLen);

    const char* start = "ETHSM:";
    IPAddrLen -= strlen(start);
    s += strlen(start);

    if(IPAddrLen<7 || IPAddrLen>15 )//check length before writing to *ip
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
