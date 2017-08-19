/*
 * ftdi_d2xx.c
 *
 * Header for FTDI D2XX serial port access library
 *
 *  Created on: 19.8.2017
 *      Author: Tero Kontkanen
 */

#include "drivers/ftdi_d2xx/sm_d2xx.h"
#include "simplemotion_private.h" //needed for timeout variable
#include "drivers/ftdi_d2xx/ftd2xx.h"
#include <string.h>
#include <simplemotion.h>

smbool handles_initialized=smfalse;
FT_HANDLE handles[MAX_OPEN_PORTS];//FT_HANDLE type is just a pointer


static int stringToNumber( const char *str, smbool *ok )
{
    int len=strlen(str);
    int i, number=0, decade=1;

    if(len<1)
    {
        *ok=smfalse;
        return 0;
    }

    for(i=len-1;i>=0;i--)
    {
        if(str[i]<'0' || str[i]>'9')//non-numeric char->fail
        {
            *ok=smfalse;
            return 0;
        }
        number+=decade*(str[i]-'0');
        decade*=10;
    }
    *ok=smtrue;
    return number;
}

smint32 d2xxPortOpen(const char *port_device_name, smint32 baudrate_bps)
{
    //init array of handles if not done yet
    if(handles_initialized==smfalse)
    {
        int i;
        for(i=0;i<MAX_OPEN_PORTS;i++)
            handles[i]=NULL;
        handles_initialized=smtrue;
    }

    //parse name string
    int ftdiIndex;
    smbool ok;

    if (strncmp(port_device_name,"FTDI",4) != 0)//must start with FTDI. Full name is FTDIn where n=index starting from 0.
    {
        smDebug( -1, Low, "FTDI port error: malformed port name '%s'. Must be 'FTDIx' where x is index starting from 0.\n",port_device_name);
        return -1;
    }
    ftdiIndex=stringToNumber(port_device_name+4,&ok);
    if (ok==smfalse)
    {
        smDebug( -1, Low, "FTDI port error: malformed port number '%s'. Must be 'FTDIx' where x is index starting from 0.\n",port_device_name);
        return -1;
    }

    //open port
    FT_HANDLE h;
    FT_STATUS s=FT_Open(ftdiIndex,&h);

    if(s==FT_OK)
    {
        //all good, find free handle
        int i;
        for(i=0;i<MAX_OPEN_PORTS;i++)
        {
            if(handles[i]==NULL)
            {
                //init port settings
                s=FT_SetBaudRate(h,baudrate_bps);
                if(s!=FT_OK)
                {
                    smDebug( -1, Low, "FTDI port error: failed to set baud rate\n");
                    goto error;
                }

                if(FT_SetLatencyTimer(h,1)!=FT_OK)//API doc says 2ms is minimum but 1 seem to work too
                {
                    smDebug( -1, Low, "FTDI port error: failed to set latency\n");
                    goto error;
                }

                if(FT_SetTimeouts(h,readTimeoutMs,readTimeoutMs)!=FT_OK)
                {
                    smDebug( -1, Low, "FTDI port error: failed to set timeout\n");
                    goto error;
                }

                smDebug( -1, Mid, "FTDI port opened\n");
                handles[i]=h;
                return i;
            }
        }
        smDebug( -1, Low, "FTDI port error: all handles taken, too many ports open\n");
        goto error;
    }
    else
        smDebug( -1, Low, "FTDI port error: FT_Open failed\n");

    error:
    FT_Close(h);
    return -1;
}


smint32 d2xxPortRead(smint32 serialport_handle, unsigned char *buf, smint32 size)
{
    FT_STATUS s;
    DWORD BytesReceived;

    s=FT_Read(handles[serialport_handle],buf,size,&BytesReceived);
    if(s!=FT_OK)
    {
        //failed
        smDebug( -1, Low, "FTDI port error: failed to receive data from port\n");
    }

    return BytesReceived;
}


smint32 d2xxPortWriteByte(smint32 serialport_handle, unsigned char byte)
{
    unsigned char buf[1];
    DWORD BytesWritten;
    buf[0]=byte;
    FT_STATUS s=FT_Write(handles[serialport_handle], buf, 1, &BytesWritten);

    if(s!=FT_OK)
    {
        //failed
        smDebug( -1, Low, "FTDI port error: failed to write data to port\n");
    }

    return BytesWritten;
}


smint32 d2xxPortWriteBuffer(smint32 serialport_handle, unsigned char *buf, smint32 size)
{
    DWORD BytesWritten;
    FT_STATUS s=FT_Write(handles[serialport_handle], buf, size, &BytesWritten);

    if(s!=FT_OK)
    {
        //failed
        smDebug( -1, Low, "FTDI port error: failed to write data to port\n");
    }

    return BytesWritten;
}


void d2xxPortClose(smint32 serialport_handle)
{
    if(FT_Close(handles[serialport_handle])!=FT_OK)
    {
        //failed
        smDebug( -1, Low, "FTDI port error: failed to close port\n");
    }
    else
        handles[serialport_handle]=NULL;
}



//BUS DEVICE INFO FETCH FUNCTIONS:

//Return number of bus devices found. details of each device may be consequently fetched by smGetBusDeviceDetails()
smint d2xxGetNumberOfDetectedBuses()
{
    FT_STATUS ftStatus;
    DWORD numDevs;
    // Get the number of devices currently connected
    ftStatus = FT_ListDevices(&numDevs,NULL,FT_LIST_NUMBER_ONLY);
    if (ftStatus == FT_OK)
    {
        return numDevs;
    }
    else
    {
        return 0;
    }

    return 0;
}

smbool d2xxGetBusDeviceDetails( smint index, SM_BUS_DEVICE_INFO *info )
{
    DWORD devIndex = index;
    char description[64]; // more than enough room
    FT_STATUS ftStatus = FT_ListDevices((PVOID)devIndex, description, FT_LIST_BY_INDEX|FT_OPEN_BY_DESCRIPTION);
    if (ftStatus == FT_OK)
    {
        smbool compatible=smfalse;
        if(strncmp(description,"TTL232R",7)==0
                || strncmp(description,"SMV2USB",7)==0
                || strncmp(description,"USB-SMV2",8)==0
                || strncmp(description,"FT230X",6)==0
                || strncmp(description,"IONICUBE",8)==0
                || strncmp(description,"ATOMI",5)==0)
            compatible=smtrue;

        info->is_simplemotion_device=compatible;

        if(compatible==smtrue)
        {
            sprintf(info->description,"SimpleMotion USB (%s)",description);
            sprintf(info->device_name,"FTDI%d",index);
        }
        else//some unknown device with FTDI chip
        {
            sprintf(info->description,"Unknown FTDI device (%s)",description);
            sprintf(info->device_name,"FTDI%d",index);
        }
        return smtrue;
    }
    else
    {
        return smfalse;
    }

    return smfalse;
}

