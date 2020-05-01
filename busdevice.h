
//HW interface to phyiscal bus device
#ifndef SM_BUSDEVICE
#define SM_BUSDEVICE

#include "simplemotion.h"
#include "simplemotion_private.h"

typedef int16_t smbusdevicehandle;

#define SM_BAUDRATE 460800

//ie "COM1" "VSD2USB"
//return 0-1 if fails, otherwise handle number
smbusdevicehandle smBDOpen( const char *devicename );

smbusdevicehandle smBDOpenWithCallbacks(const char *devicename, BusdeviceOpen busOpenCallback, BusdeviceClose busCloseCallback, BusdeviceReadBuffer busReadCallback, BusdeviceWriteBuffer busWriteCallback , BusdeviceMiscOperation busMiscOperationCallback);

//return true if ok
bool smBDClose( const smbusdevicehandle handle );

//write one byte to trasmit buffer. send data with smBDTransmit()
//returns true on success. false could mean buffer full error if forgot to call smBDTransmit
bool smBDWrite( const smbusdevicehandle handle , const uint8_t byte );

//write transmit buffer to physical device
//returns true on success
bool smBDTransmit(const smbusdevicehandle handle);

//read one byte from bus. if byte not immediately available, block return up to SM_READ_TIMEOUT millisecs to wait data
//returns true if byte read sucessfully
bool smBDRead( const smbusdevicehandle handle , uint8_t *byte );

//see info at definition of BusDeviceMiscOperationType
//returns true if sucessfully
bool smBDMiscOperation( const smbusdevicehandle handle, BusDeviceMiscOperationType operation );

//BUS DEVICE INFO FETCH FUNCTIONS:

// Return number of bus devices found. details of each device may be consequently fetched by smBDGetBusDeviceDetails()
int smBDGetNumberOfDetectedBuses();

//return true if success
bool smBDGetBusDeviceDetails( int index, SM_BUS_DEVICE_INFO *info );


#endif
