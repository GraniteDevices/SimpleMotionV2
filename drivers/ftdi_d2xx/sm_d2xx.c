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
#include "drivers/ftdi_d2xx/third_party/ftd2xx.h"
#include <string.h>
#include <simplemotion.h>


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

smBusdevicePointer d2xxPortOpen(const char *port_device_name, smint32 baudrate_bps, smbool *success)
{
    *success=smfalse;

    //parse name string
    int ftdiIndex=0;
    smbool ftdiIndexParseOk=smfalse;

    if (strncmp(port_device_name,"FTDI",4) == 0)//must start with FTDI. Full name is FTDIn where n=index starting from 0.
    {
        ftdiIndex=stringToNumber(port_device_name+4,&ftdiIndexParseOk);
    }

    //open port
    FT_HANDLE h;
    FT_STATUS s;

    if(ftdiIndexParseOk==smtrue)//if name suggests that we should try opening by index
        s=FT_Open(ftdiIndex,&h);
    else//try to open by device name instead
        s=FT_OpenEx((char*)port_device_name,FT_OPEN_BY_DESCRIPTION,&h);

    if(s==FT_OK)
    {
        if(FT_ResetDevice(h)!=FT_OK)
        {
            smDebug( -1, Low, "FTDI port error: failed to reset USB chip\n");
            goto error;
        }

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

        if(FT_SetFlowControl(h,FT_FLOW_NONE,0,0)!=FT_OK)
        {
            smDebug( -1, Low, "FTDI port error: failed to set flow control\n");
            goto error;
        }

        if(FT_SetDataCharacteristics(h,FT_BITS_8,FT_STOP_BITS_1,FT_PARITY_NONE)!=FT_OK)
        {
            smDebug( -1, Low, "FTDI port error: failed to set data characteristics\n");
            goto error;
        }

        if(FT_SetTimeouts(h,readTimeoutMs,readTimeoutMs)!=FT_OK)
        {
            smDebug( -1, Low, "FTDI port error: failed to set timeout\n");
            goto error;
        }

        if(FT_Purge(h,FT_PURGE_RX|FT_PURGE_TX)!=FT_OK)
        {
            smDebug( -1, Low, "FTDI port error: failed to set purge\n");
            goto error;
        }

        smDebug( -1, Mid, "FTDI port opened\n");
        *success=smtrue;
        return (smBusdevicePointer)h;

        smDebug( -1, Low, "FTDI port error: all handles taken, too many ports open\n");
        goto error;
    }
    else
    {
        if(ftdiIndexParseOk==smtrue)
        {
            smDebug( -1, Low, "FTDI port error: FT_Open with name %s (index %d) failed\n", port_device_name, ftdiIndex);
        }
        else
        {
            smDebug( -1, Low, "FTDI port error: FT_OpenEx with device description %s failed.\n", port_device_name);
            smDebug( -1, Low, "FTDI port error additional info: FTDI D2XX port open attempt was made by device description because smOpenBus() argument format was not FTDIn where index number (0 or greater), or any other supported port name format.\n");
        }
    }

    error:
    FT_Close(h);
    return SMBUSDEVICE_RETURN_ON_OPEN_FAIL;
}


smint32 d2xxPortRead(smBusdevicePointer busdevicepointer, unsigned char *buf, smint32 size)
{
    FT_HANDLE hanlde=(FT_HANDLE)busdevicepointer;
    FT_STATUS s;
    DWORD BytesReceived;

    s=FT_Read(hanlde,buf,size,&BytesReceived);
    if(s!=FT_OK)
    {
        //failed
        smDebug( -1, Low, "FTDI port error: failed to receive data from port\n");
    }

    return BytesReceived;
}



smint32 d2xxPortWriteBuffer(smBusdevicePointer busdevicepointer, unsigned char *buf, smint32 size)
{
    FT_HANDLE handle=(FT_HANDLE)busdevicepointer;
    DWORD BytesWritten;
    FT_STATUS s=FT_Write(handle, buf, size, &BytesWritten);

    if(s!=FT_OK)
    {
        //failed
        smDebug( -1, Low, "FTDI port error: failed to write data to port\n");
    }

    return BytesWritten;
}


void d2xxPortClose(smBusdevicePointer busdevicepointer)
{
    FT_HANDLE handle=(FT_HANDLE)busdevicepointer;

    if(FT_Close(handle)!=FT_OK)
    {
        //failed
        smDebug( -1, Low, "FTDI port error: failed to close port\n");
    }
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

