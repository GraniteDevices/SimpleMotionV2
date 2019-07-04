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
#include "user_options.h"
#include "simplemotion_private.h" //needed for timeout variable

#if defined(__unix__) || defined(__APPLE__)

#include <termios.h>
#include <limits.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#if defined(__linux__)
//needed for setting low latency
#include <linux/serial.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/serial/ioss.h>
#include <IOKit/IOBSD.h>
#endif

smBusdevicePointer serialPortOpen(const char * port_device_name, smint32 baudrate_bps, smbool *success)
{
    int port_handle;
    int err;
    int baudrateEnumValue;
    struct termios new_port_settings;
    int customBaudRate = 0;
    *success=smfalse;

    //check if devicename is correct format
    if( strncmp(port_device_name,"/dev/tty",8) != 0 && strncmp(port_device_name,"/dev/cu.",8) != 0)
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;

    port_handle = open(port_device_name, O_RDWR | O_NOCTTY | O_NONBLOCK);

    if(port_handle==-1)
    {
        smDebug(-1, SMDebugLow, "Serial port error: port open failed");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }


    // open() follows POSIX semantics: multiple open() calls to the same file will succeed
    // unless the TIOCEXCL ioctl is issued (except for root)
    if (ioctl(port_handle, TIOCEXCL) == -1) {
        smDebug(-1, SMDebugLow, "Serial port error: error setting TIOCEXCL");
        close(port_handle);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    // as the port is now open, clear O_NONBLOCK flag for subsequent I/O calls
    if (fcntl(port_handle, F_SETFL, 0) == -1) {
        smDebug(-1, SMDebugLow, "Serial port error: error clearing O_NONBLOCK");
        close(port_handle);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    switch(baudrate_bps)
    {
#if defined(B9600)
        case    9600 : baudrateEnumValue = B9600; break;
#endif
#if defined(B19200)
        case   19200 : baudrateEnumValue = B19200; break;
#endif
#if defined(B38400)
        case   38400 : baudrateEnumValue = B38400; break;
#endif
#if defined(B57600)
        case   57600 : baudrateEnumValue = B57600; break;
#endif
#if defined(B115200)
        case  115200 : baudrateEnumValue = B115200; break;
#endif
#if defined(B230400)
        case  230400 : baudrateEnumValue = B230400; break;
#endif
#if defined(B460800)
        case  460800 : baudrateEnumValue = B460800; break;
#endif
#if defined(B500000)
        case  500000 : baudrateEnumValue = B500000; break;
#endif
#if defined(B576000)
        case  576000 : baudrateEnumValue = B576000; break;
#endif
#if defined(B921600)
        case  921600 : baudrateEnumValue = B921600; break;
#endif
#if defined(B1000000)
        case 1000000 : baudrateEnumValue = B1000000; break;
#endif
#if defined(B1152000)
        case 1115200 : baudrateEnumValue = B1152000; break;
#endif
#if defined(B1500000)
        case 1500000 : baudrateEnumValue = B1500000; break;
#endif
#if defined(B2000000)
        case 2000000 : baudrateEnumValue = B2000000; break;
#endif
#if defined(B2500000)
        case 2500000 : baudrateEnumValue = B2500000; break;
#endif
#if defined(B3000000)
        case 3000000 : baudrateEnumValue = B3000000; break;
#endif
#if defined(B3500000)
        case 3500000 : baudrateEnumValue = B3500000; break;
#endif
#if defined(B4000000)
        case 4000000 : baudrateEnumValue = B4000000; break;
#endif
        default:
            customBaudRate = 1;
            baudrateEnumValue=B9600;//must set something initially, changed later
        break;
    }

    memset(&new_port_settings, 0, sizeof(new_port_settings)); //reset struct
    cfmakeraw(&new_port_settings);//reset struct
    new_port_settings.c_cflag = CS8 | CLOCAL | CREAD;
    new_port_settings.c_iflag = IGNPAR;
    new_port_settings.c_oflag = 0;
    new_port_settings.c_lflag = 0;
    new_port_settings.c_cc[VMIN] = 0;      /* non blocking mode */
    new_port_settings.c_cc[VTIME] = readTimeoutMs/100;     /* timeout 100 ms steps */
    if(new_port_settings.c_cc[VTIME]<1)//don't allow value 0ms
        new_port_settings.c_cc[VTIME]=1;
#if defined(_BSD_SOURCE)
    cfsetspeed(&new_port_settings, baudrateEnumValue);
#else
    cfsetispeed(&new_port_settings, baudrateEnumValue);
    cfsetospeed(&new_port_settings, baudrateEnumValue);
#endif

    // Activate settings
    err = tcsetattr(port_handle, TCSANOW, &new_port_settings);
    if(err==-1)
    {
        close(port_handle);
        smDebug(-1, SMDebugLow, "Serial port error: failed to set port parameters");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    if(customBaudRate)
    {
        #if defined(IOSSIOSPEED)
        speed_t bps = baudrate_bps;
        if (ioctl(port_handle, IOSSIOSPEED, &bps) == -1)
        {
            smDebug(-1, SMDebugLow, "Serial port error: unsupported baudrate\n");
            close(port_handle);
            return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
        }
        #else
        smDebug(-1, SMDebugLow, "Serial port error: unsupported baudrate\n");
        close(port_handle);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
        #endif
    }

    // set receive latency to 1 ms
    #if defined(IOSSDATALAT)
    unsigned long microsecs = 1000UL;
    if (ioctl(port_handle, IOSSDATALAT, &microsecs) == -1) {
        smDebug(-1, SMDebugLow, "Serial port error: error setting read latency");
        close(port_handle);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }
    #endif
    
    #if defined(TIOCGSERIAL) && defined(ASYNC_LOW_LATENCY)
    struct serial_struct serial; 
    if(ioctl(port_handle, TIOCGSERIAL, &serial)!=-1)
    {
        serial.flags |= ASYNC_LOW_LATENCY;
        if(ioctl(port_handle, TIOCSSERIAL, &serial) == -1 )
        {
            smDebug(-1, SMDebugLow, "Serial port warning: unable to set low latency mode, maybe try running with root permissions.");
        }
    }
    else
        smDebug(-1, SMDebugLow, "Serial port warning: unable to read TIOCGSERIAL for low latency mode, maybe try running with root permissions.");
    #endif

    //flush any stray bytes from device receive buffer that may reside in it
    //note: according to following page, delay before this may be necessary http://stackoverflow.com/questions/13013387/clearing-the-serial-ports-buffer
    usleep(100000);
    tcflush(port_handle,TCIOFLUSH);

    *success=smtrue;
    return (smBusdevicePointer)port_handle;
}


smint32 serialPortRead(smBusdevicePointer busdevicePointer, smuint8 *buf, smint32 size)
{
    int serialport_handle=(int)busdevicePointer;
    smint32 n;
    if(size>4096)  size = 4096;
    n = read((int)serialport_handle, buf, size);
    return n;
}



smint32 serialPortWrite(smBusdevicePointer busdevicePointer, unsigned char *buf, smint32 size)
{
    int serialport_handle=(int)busdevicePointer;
    return(write((int)serialport_handle, buf, size));
}

smbool serialPortMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation)
{
    int serialport_handle=(int)busdevicePointer;
    switch(operation)
    {
    case MiscOperationPurgeRX:
        //flush any stray bytes from device receive buffer that may reside in it
        //note: according to following page, delay before this may be necessary http://stackoverflow.com/questions/13013387/clearing-the-serial-ports-buffer
        usleep(20000);
        tcflush(serialport_handle,TCIFLUSH);
        return smtrue;
        break;
    case MiscOperationFlushTX://TODO implement
        usleep(20000);
        //waits until all output written to the object referred to by fd has been transmitted.
        tcdrain(serialport_handle);
        return smtrue;
        break;
    default:
        smDebug( -1, SMDebugLow, "Serial port error: given MiscOperataion not implemented\n");
        return smfalse;
        break;
    }
}

void serialPortClose(smBusdevicePointer busdevicePointer)
{
    int serialport_handle=(int)busdevicePointer;
    close((int)serialport_handle);
}

#else   //windows: for API, see https://msdn.microsoft.com/en-us/library/ff802693.aspx

#include <windows.h>
#include <string.h>

smBusdevicePointer serialPortOpen(const char *port_device_name, smint32 baudrate_bps, smbool *success)
{
    char port_def_string[64], port_name[32];
    HANDLE port_handle;
    *success=smfalse;

    //check port name
    if(strncmp(port_device_name,"COM",3) != 0 )
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;

    sprintf(port_def_string,"baud=%d data=8 parity=N stop=1", (int)baudrate_bps);
    sprintf(port_name,"\\\\.\\%s",port_device_name);

    port_handle = CreateFileA(port_name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);

    if(port_handle==INVALID_HANDLE_VALUE)
    {
        smDebug( -1, SMDebugLow, "Serial port error: Unable to create serial port handle");
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    //fill DCB settings struct
    DCB dcb;
    FillMemory(&dcb, sizeof(dcb), 0);
    dcb.DCBlength = sizeof(dcb);

    if(!BuildCommDCBA(port_def_string, &dcb))
    {
        smDebug( -1, SMDebugLow, "Serial port error: Unable to build DCB settings\n");
        CloseHandle(port_handle);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
    }

    if(!SetCommState(port_handle, &dcb))
    {
        smDebug( -1, SMDebugLow, "Serial port error: Unable to set port settings\n");
        CloseHandle(port_handle);
        return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
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
        smDebug( -1, SMDebugLow, "Serial port error: Failed to set port timeout settings\n");
        CloseHandle(port_handle);
        return(SMBUSDEVICE_RETURN_ON_OPEN_FAIL);
    }

    //flush any stray bytes from device receive buffer that may reside in it
    PurgeComm((HANDLE)port_handle,PURGE_RXABORT|PURGE_RXCLEAR|PURGE_TXABORT|PURGE_TXCLEAR);

    *success=smtrue;
    return( (smBusdevicePointer)port_handle);
}


smint32 serialPortRead(smBusdevicePointer busdevicePointer, unsigned char *buf, smint32 size)
{
    HANDLE serialport_handle=(HANDLE)busdevicePointer;
    smint32 n;
    if(size>4096)
        size = 4096;
    ReadFile((HANDLE)serialport_handle, buf, size, (LPDWORD)((void *)&n), NULL);
    return n;
}


smint32 serialPortWrite(smBusdevicePointer busdevicePointer, unsigned char *buf, smint32 size)
{
    HANDLE serialport_handle=(HANDLE)busdevicePointer;
    smint32 n;
    if(WriteFile((HANDLE)serialport_handle, buf, size, (LPDWORD)((void *)&n), NULL))
        return n;
    return -1;
}

void serialPortClose(smBusdevicePointer busdevicePointer)
{
    HANDLE serialport_handle=(HANDLE)busdevicePointer;
    CloseHandle((HANDLE)serialport_handle);
}

smbool serialPortMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation)
{
    HANDLE serialport_handle=(HANDLE)busdevicePointer;

    switch(operation)
    {
    case MiscOperationPurgeRX:
        //flush any stray bytes from device receive buffer that may reside in it
        if(PurgeComm((HANDLE)serialport_handle, PURGE_RXABORT|PURGE_RXCLEAR) )
            return smtrue;
        else
            return smfalse;
        break;
    case MiscOperationFlushTX:
        //flush any stray bytes from device transmit buffer that may reside in it
        if(PurgeComm((HANDLE)serialport_handle, PURGE_TXABORT|PURGE_TXCLEAR) )
            return smtrue;
        else
            return smfalse;
        break;
        break;
    default:
        smDebug( -1, SMDebugLow, "Serial port error: given MiscOperataion not implemented\n");
        return smfalse;
        break;
    }
}

#endif//windows
