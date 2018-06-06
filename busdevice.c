#include "busdevice.h"
#include "user_options.h"

#include "drivers/serial/pcserialport.h"
#include "drivers/tcpip/tcpclient.h"
#include "drivers/ftdi_d2xx/sm_d2xx.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>


//how much bytes available in transmit buffer
#define TANSMIT_BUFFER_LENGTH 128

unsigned long SMBusBaudrate=SM_BAUDRATE; //the next opened port (with smOpenBus) will be opened with the PBS defined here (default 460800 BPS)

typedef struct _SMBusDevice
{
	//common
    smbool opened;

    SM_STATUS cumulativeSmStatus;

    //pointer used by bus device drivers
    smBusdevicePointer busDevicePointer;

    smuint8 txBuffer[TANSMIT_BUFFER_LENGTH];
    smint32 txBufferUsed;//how many bytes in buffer currently

    BusdeviceOpen busOpenCallback;
    BusdeviceReadBuffer busReadCallback;
    BusdeviceWriteBuffer busWriteCallback;
    BusdeviceClose busCloseCallback;
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
		BusDevice[i].opened=smfalse;
        BusDevice[i].txBufferUsed=0;
	}
	bdInitialized=smtrue;
}


//ie "COM1" "VSD2USB"
//return -1 if fails, otherwise handle number
smbusdevicehandle smBDOpen( const char *devicename )
{
#ifdef ENABLE_BUILT_IN_DRIVERS
    smbusdevicehandle h;

    //try opening with all drivers:
    h=smBDOpenWithCallbacks( devicename, serialPortOpen, serialPortClose, serialPortRead, serialPortWrite );
    if(h>=0) return h;//was success
    h=smBDOpenWithCallbacks( devicename, tcpipPortOpen, tcpipPortClose, tcpipPortRead, tcpipPortWrite );
    if(h>=0) return h;//was success
#ifdef FTDI_D2XX_SUPPORT
    h=smBDOpenWithCallbacks( devicename, d2xxPortOpen, d2xxPortClose, d2xxPortRead, d2xxPortWrite );
    if(h>=0) return h;//was success
#endif
#else
    smDebug( -1, High, "smBDOpen ENABLE_BUILT_IN_DRIVERS is not defined during SM library compile time. smOpenBus not supported in this case, see README.md.");
#endif

    //none succeeded
    //smDebug( -1, Low, "smBDOpen device name argument syntax didn't match any supported driver port name");
    return -1;
}



smbusdevicehandle smBDOpenWithCallbacks(const char *devicename, BusdeviceOpen busOpenCallback, BusdeviceClose busCloseCallback , BusdeviceReadBuffer busReadCallback, BusdeviceWriteBuffer busWriteCallback)
{
    int handle;
    smbool success;

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

    //setup callbacks
    BusDevice[handle].busOpenCallback=busOpenCallback;
    BusDevice[handle].busWriteCallback=busWriteCallback;
    BusDevice[handle].busReadCallback=busReadCallback;
    BusDevice[handle].busCloseCallback=busCloseCallback;

    //try opening
    BusDevice[handle].busDevicePointer=BusDevice[handle].busOpenCallback( devicename, SMBusBaudrate, &success );
    if( success==smfalse )
    {
        return -1; //failed to open
    }

    //success
    BusDevice[handle].txBufferUsed=0;
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

    BusDevice[handle].busCloseCallback(BusDevice[handle].busDevicePointer );
    BusDevice[handle].opened=smfalse;
    return smtrue;
}




//write one byte to buffer and send later with smBDTransmit()
//returns true on success
smbool smBDWrite(const smbusdevicehandle handle, const smuint8 byte )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

    if(BusDevice[handle].txBufferUsed<TANSMIT_BUFFER_LENGTH)
    {
        //append to buffer
        BusDevice[handle].txBuffer[BusDevice[handle].txBufferUsed]=byte;
        BusDevice[handle].txBufferUsed++;
        smDebug(handle, SMDebugTrace, "  Sending byte %02x\n",byte);
        return smtrue;
    }

    smDebug(handle, SMDebugMid, "  Sending byte %02x failed, TX buffer overflown\n",byte);
    return smfalse;
}

smbool smBDTransmit(const smbusdevicehandle handle)
{
    //check if handle valid & open
    if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

    if(BusDevice[handle].busWriteCallback(BusDevice[handle].busDevicePointer,BusDevice[handle].txBuffer, BusDevice[handle].txBufferUsed)==BusDevice[handle].txBufferUsed)
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

//read one byte from bus. if byte not immediately available, block return up to SM_READ_TIMEOUT millisecs to wait data
//returns true if byte read sucessfully
smbool smBDRead( const smbusdevicehandle handle, smuint8 *byte )
{
	//check if handle valid & open
	if( smIsBDHandleOpen(handle)==smfalse ) return smfalse;

    int n;
    n=BusDevice[handle].busReadCallback(BusDevice[handle].busDevicePointer, byte, 1);
    if( n!=1 )
    {
        smDebug(handle, SMDebugMid, "  Reading a byte from bus failed\n");
        return smfalse;
    }
    else
    {
        smDebug(handle, SMDebugTrace, "  Got byte %02x \n",*byte);
        return smtrue;
    }
}

//BUS DEVICE INFO FETCH FUNCTIONS:

//Return number of bus devices found. details of each device may be consequently fetched by smGetBusDeviceDetails()
smint smBDGetNumberOfDetectedBuses()
{
    //only supports FTDI D2XX at the moment
#ifdef FTDI_D2XX_SUPPORT
    return d2xxGetNumberOfDetectedBuses();
#endif
    return 0;
}

smbool smBDGetBusDeviceDetails( smint index, SM_BUS_DEVICE_INFO *info )
{
    //only supports FTDI D2XX at the moment
#ifdef FTDI_D2XX_SUPPORT
    return d2xxGetBusDeviceDetails(index,info);
#endif
    return smfalse;

}
