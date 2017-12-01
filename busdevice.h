
//HW interface to phyiscal bus device
/*
 * idea: strukti, jossa on fpointterit portin avaamiseen, datan siirtoon jne.
 *
 * jokainen busdevicen handle on pointteri. busdevicen funkkarit voi sitten palauttaa siinä vaikka
 * handlen tai oman struktin, joka sisältää mitä tahansa.
 *
 * busOpen täytyy jotenkin purkata inittaamaan fpointterit jos on busname on tunnettua muotoa
 *
 *
 */
#ifndef SM_BUSDEVICE
#define SM_BUSDEVICE

#include "simplemotion.h"
#include "simplemotion_private.h"

typedef smint16 smbusdevicehandle;

#define SM_BAUDRATE 460800

//ie "COM1" "VSD2USB"
//return 0-1 if fails, otherwise handle number
smbusdevicehandle smBDOpen( const char *devicename );

//return true if ok
smbool smBDClose( const smbusdevicehandle handle );

//write one byte to trasmit buffer. send data with smBDTransmit()
//returns smtrue on success. smfalse could mean buffer full error if forgot to call smBDTransmit
smbool smBDWrite( const smbusdevicehandle handle , const smuint8 byte );

//write transmit buffer to physical device
//returns true on success
smbool smBDTransmit(const smbusdevicehandle handle);

//read one byte from bus. if byte not immediately available, block return up to SM_READ_TIMEOUT millisecs to wait data
//returns true if byte read sucessfully
smbool smBDRead( const smbusdevicehandle handle , smuint8 *byte );


//BUS DEVICE INFO FETCH FUNCTIONS:

// Return number of bus devices found. details of each device may be consequently fetched by smBDGetBusDeviceDetails()
smint smBDGetNumberOfDetectedBuses();

//return smtrue if success
smbool smBDGetBusDeviceDetails( smint index, SM_BUS_DEVICE_INFO *info );


#endif
