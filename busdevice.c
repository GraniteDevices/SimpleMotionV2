#include "busdevice.h"

#include "pcserialport.h"
#include "tcpclient.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>

#define BD_NONE 0
#define BD_RS 1
#define BD_FTDI 2
#define BD_TCP 3

//how much bytes available in transmit buffer
#define TANSMIT_BUFFER_LENGTH 128

unsigned long SMBusBaudrate=SM_BAUDRATE; //the next opened port (with smOpenBus) will be opened with the PBS defined here (default 460800 BPS)

typedef struct _SMBusDevice
{
	//common
    smint8 bdType;//bus device type (such as rs232 or ftdi lib or mcu UART etc). 1=rs232 lib
	smbool opened;

    SM_STATUS cumulativeSmStatus;

	//used for rs232 lib only
	int comPort;

    smuint8 txBuffer[TANSMIT_BUFFER_LENGTH];
    smint32 txBufferUsed;//how many bytes in buffer currently

	//used for FTDI lib only
} SMBusDevice;

//init on first open
smbool bdInitialized=smfalse;
SMBusDevice BusDevice[SM_MAX_BUSES];

//init device struct table
void smBDinit()
{
	int i;
	for(i=0;i<SM_MAX_BUSES;i++)
	{
		BusDevice[i].bdType=BD_NONE;
		BusDevice[i].opened=smfalse;
        BusDevice[i].txBufferUsed=0;
	}
	bdInitialized=smtrue;
}

//accepted TCP/IP address format is nnn.nnn.nnn.nnn:pppp where n is IP address numbers and p is port number
static int validateIpAddress(const char *s, const char **pip_end,
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

static int parseIpAddress(const char *s, char *ip, size_t ipsize, short *port)
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

//ie "COM1" "VSD2USB"
//return -1 if fails, otherwise handle number
smbusdevicehandle smBDOpen( const char *devicename )
{
	int handle;

	//true on first call
	if(bdInitialized==smfalse)
		smBDinit();

	//find free handle
	for(handle=0;handle<SM_MAX_BUSES;handle++)
	{
		if(BusDevice[handle].opened==smfalse) break;//choose this
	}

	//all handles in use
	if(handle>=SM_MAX_BUSES) return -1;

        if(strncmp(devicename,"COM",3) == 0 || strncmp(devicename,"/dev/tty",8) == 0 || strncmp(devicename,"/dev/cu.",8) == 0) //use rs232 lib
	{
            BusDevice[handle].comPort=serialPortOpen( devicename, SMBusBaudrate );
                if( BusDevice[handle].comPort == -1 )
		{
			return -1; //failed to open
		}
		BusDevice[handle].bdType=BD_RS;
        BusDevice[handle].txBufferUsed=0;
	}
    else if (validateIpAddress(devicename, NULL, NULL) == 0)
    {
        char ip[128];
        short port = 4001;
        if (parseIpAddress(devicename, ip, sizeof(ip), &port) < 0)
            return -1;
        BusDevice[handle].comPort=OpenTCPPort( ip, port );
        if( BusDevice[handle].comPort == -1 )
        {
            return -1; //failed to open
        }
        BusDevice[handle].bdType=BD_TCP;
        BusDevice[handle].txBufferUsed=0;
    }
    else//no other bus types supproted yet
    {
        return -1;
    }

	//success
    BusDevice[handle].cumulativeSmStatus=0;
	BusDevice[handle].opened=smtrue;
	return handle;
}

smbool smIsBDHandleOpen( const smbusdevicehandle handle )
{
	if(handle<0) return smfalse;
	if(handle>=SM_MAX_BUSES) return smfalse;
	return BusDevice[handle].opened;
}

//return true if ok
smbool smBDClose( const smbusdevicehandle handle )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

	if( BusDevice[handle].bdType==BD_RS )
	{
        serialPortClose( BusDevice[handle].comPort );
		BusDevice[handle].opened=smfalse;
		return smtrue;
	}
    else if( BusDevice[handle].bdType==BD_TCP )
    {
        CloseTCPport( BusDevice[handle].comPort );
        BusDevice[handle].opened=smfalse;
        return smtrue;
    }

	return smfalse;
}




//write one byte to buffer and send later with smBDTransmit()
//returns true on success
smbool smBDWrite(const smbusdevicehandle handle, const smuint8 byte )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

	if( BusDevice[handle].bdType==BD_RS || BusDevice[handle].bdType==BD_TCP )
	{
        if(BusDevice[handle].txBufferUsed<TANSMIT_BUFFER_LENGTH)
        {
            //append to buffer
            BusDevice[handle].txBuffer[BusDevice[handle].txBufferUsed]=byte;
            BusDevice[handle].txBufferUsed++;
            return smtrue;
        }
        else
            return smfalse;
	}

	return smfalse;
}

smbool smBDTransmit(const smbusdevicehandle handle)
{
    //check if handle valid & open
    if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

    if( BusDevice[handle].bdType==BD_RS )
    {
        if(serialPortWriteBuffer(BusDevice[handle].comPort,BusDevice[handle].txBuffer, BusDevice[handle].txBufferUsed)==BusDevice[handle].txBufferUsed)
        {
            BusDevice[handle].txBufferUsed=0;
            return smtrue;
        }
        else
        {
            BusDevice[handle].txBufferUsed=0;
            return smfalse;
        }
    }
    else if( BusDevice[handle].bdType==BD_TCP )
    {
        if(SendTCPBuf(BusDevice[handle].comPort,BusDevice[handle].txBuffer, BusDevice[handle].txBufferUsed)==BusDevice[handle].txBufferUsed)
        {
            BusDevice[handle].txBufferUsed=0;
            return smtrue;
        }
        else
        {
            BusDevice[handle].txBufferUsed=0;
            return smfalse;
        }
    }

    return smfalse;
}

//read one byte from bus. if byte not immediately available, block return up to SM_READ_TIMEOUT millisecs to wait data
//returns true if byte read sucessfully
smbool smBDRead( const smbusdevicehandle handle, smuint8 *byte )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

	if( BusDevice[handle].bdType==BD_RS )
	{
		int n;
        n=serialPortRead(BusDevice[handle].comPort, byte, 1);
		if( n!=1 ) return smfalse;
		else return smtrue;
	}
    else if( BusDevice[handle].bdType==BD_TCP )
    {
        int n;
        n=PollTCPPort(BusDevice[handle].comPort, byte, 1);
        if( n!=1 ) return smfalse;
        else return smtrue;
    }


	return smfalse;
}
