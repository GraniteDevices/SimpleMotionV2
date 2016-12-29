/*
 * pcserialport.h
 *
 * Header for PC serial port access library (win/linux)
 *
 *  Created on: 28.12.2016
 *      Author: Tero
 *
 *  Inspired by RS232 library by Teunis van Beelen
 */



#include "pcserialport.h"
#include "simplemotion_private.h" //needed for timeout variable

#ifdef __linux__

#include <termios.h>
#include <limits.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

smint32 serialPortOpen(const char * port_device_name, smint32 baudrate_bps)
{
    int port_handle;
    int err;
    int baudrateEnumValue;
    struct termios new_port_settings;

    switch(baudrate_bps)
	{
        case    9600 : baudrateEnumValue = B9600; break;
        case   19200 : baudrateEnumValue = B19200; break;
        case   38400 : baudrateEnumValue = B38400; break;
        case   57600 : baudrateEnumValue = B57600; break;
        case  115200 : baudrateEnumValue = B115200; break;
        case  230400 : baudrateEnumValue = B230400; break;
        case  460800 : baudrateEnumValue = B460800; break;
        case  500000 : baudrateEnumValue = B500000; break;
        case  576000 : baudrateEnumValue = B576000; break;
        case  921600 : baudrateEnumValue = B921600; break;
        case 1000000 : baudrateEnumValue = B1000000; break;
        case 1115200 : baudrateEnumValue = B1152000; break;
        case 1500000 : baudrateEnumValue = B1500000; break;
        case 2000000 : baudrateEnumValue = B2000000; break;
        case 2500000 : baudrateEnumValue = B2500000; break;
        case 3000000 : baudrateEnumValue = B3000000; break;
        case 3500000 : baudrateEnumValue = B3500000; break;
        case 4000000 : baudrateEnumValue = B4000000; break;
        default      : smDebug(-1,Low,"Serial port error: unsupported baudrate\n");
		return(1);
		break;
	}

        port_handle = open(port_device_name, O_RDWR | O_NOCTTY );
        if(port_handle==-1)
	{
        smDebug(-1, Low, "Serial port error: port open failed");
           return(port_handle);
	}

    memset(&new_port_settings, 0, sizeof(new_port_settings)); //reset struct

    new_port_settings.c_cflag = baudrateEnumValue | CS8 | CLOCAL | CREAD;
	new_port_settings.c_iflag = IGNPAR;
	new_port_settings.c_oflag = 0;
	new_port_settings.c_lflag = 0;
    new_port_settings.c_cc[VMIN] = 0;      /* non blocking mode */
    new_port_settings.c_cc[VTIME] = readTimeoutMs/100;     /* timeout 100 ms steps */
        err = tcsetattr(port_handle, TCSANOW, &new_port_settings);
    if(err==-1)
	{
                close(port_handle);
                smDebug(-1, Low, "Serial port error: failed to set port parameters");
                return -1;
	}
    return port_handle;
}


smint32 serialPortRead(smint32 serialport_handle, smuint8 *buf, smint32 size)
{
    smint32 n;
    if(size>4096)  size = 4096;
    n = read(serialport_handle, buf, size);
    return n;
}


smint32 serialPortWrite(smint32 serialport_handle, unsigned char byte)
{
    smint32 n;
    n = write(serialport_handle, &byte, 1);
    if(n<0)
        return 1;
    return 0;
}


smint32 serialPortWriteBuffer(smint32 serialport_handle, unsigned char *buf, smint32 size)
{
        return(write(serialport_handle, buf, size));
}


void serialPortClose(smint32 serialport_handle)
{
        close(serialport_handle);
}

#else   //windows: for API, see https://msdn.microsoft.com/en-us/library/ff802693.aspx

#include <windows.h>
#include <string.h>

smint32 serialPortOpen(const char *port_device_name, smint32 baudrate_bps)
{
    char port_def_string[64], port_name[32];
    HANDLE port_handle;

    sprintf(port_def_string,"baud=%d data=8 parity=N stop=1", (int)baudrate_bps);
    sprintf(port_name,"\\\\.\\%s",port_device_name);

    port_handle = CreateFileA(port_name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);

    if(port_handle==INVALID_HANDLE_VALUE)
    {
        smDebug( -1, Low, "Serial port error: Unable to create serial port handle");
        return -1;
    }

    //fill DCB settings struct
    DCB dcb;
    FillMemory(&dcb, sizeof(dcb), 0);
    dcb.DCBlength = sizeof(dcb);

    if(!BuildCommDCBA(port_def_string, &dcb))
    {
        smDebug( -1, Low, "Serial port error: Unable to build DCB settings\n");
        CloseHandle(port_handle);
        return -1;
    }

    if(!SetCommState(port_handle, &dcb))
    {
        smDebug( -1, Low, "Serial port error: Unable to set port settings\n");
        CloseHandle(port_handle);
        return -1;
    }

    //set timeout
    COMMTIMEOUTS port_timeouts;
    port_timeouts.ReadTotalTimeoutConstant    = readTimeoutMs;
    port_timeouts.ReadIntervalTimeout         = 0;
    port_timeouts.ReadTotalTimeoutMultiplier  = 0;
    port_timeouts.WriteTotalTimeoutMultiplier = 50;
    port_timeouts.WriteTotalTimeoutConstant   = 50;

    if(!SetCommTimeouts(port_handle, &port_timeouts))
    {
        smDebug( -1, Low, "Serial port error: Failed to set port timeout settings\n");
        CloseHandle(port_handle);
        return(-1);
    }

    return( (smint32)port_handle);
}


smint32 serialPortRead(smint32 serialport_handle, unsigned char *buf, smint32 size)
{
    smint32 n;
    if(size>4096)
        size = 4096;
    ReadFile((HANDLE)serialport_handle, buf, size, (LPDWORD)((void *)&n), NULL);
    return n;
}


smint32 serialPortWriteByte(smint32 serialport_handle, unsigned char byte)
{
    smint32 n;
    WriteFile((HANDLE)serialport_handle, &byte, 1, (LPDWORD)((void *)&n), NULL);
    if(n<0)
        return 1;
    return 0;
}


smint32 serialPortWriteBuffer(smint32 serialport_handle, unsigned char *buf, smint32 size)
{
    smint32 n;
    if(WriteFile((HANDLE)serialport_handle, buf, size, (LPDWORD)((void *)&n), NULL))
        return n;
    return -1;
}


void serialPortClose(smint32 serialport_number)
{
    CloseHandle((HANDLE)serialport_number);
}


#endif//windows
