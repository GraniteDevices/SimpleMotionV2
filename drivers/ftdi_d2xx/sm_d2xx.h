/*
 * ftdi_d2xx.h
 *
 * Header for FTDI D2XX serial port access library
 *
 *  Created on: 19.8.2017
 *      Author: Tero Kontkanen
 */


#ifndef SM_D2XX_H
#define SM_D2XX_H

#include "simplemotion_private.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "simplemotion.h"

//max number of simultaneously connected FTDI devices
#define MAX_OPEN_PORTS 32

smBusdevicePointer d2xxPortOpen(const char *port_device_name, smint32 baudrate_bps, smbool *success);
smint32 d2xxPortRead(smBusdevicePointer busdevicepointer, unsigned char *buf, smint32 size);
smint32 d2xxPortWrite(smBusdevicePointer busdevicepointer, unsigned char *buf, smint32 size);
smbool d2xxPortMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation);
void d2xxPortClose(smBusdevicePointer busdevicepointer);

//Return number of bus devices found. details of each device may be consequently fetched by smGetBusDeviceDetails()
smint d2xxGetNumberOfDetectedBuses();
smbool d2xxGetBusDeviceDetails( smint index, SM_BUS_DEVICE_INFO *info );

#ifdef __cplusplus
}
#endif

#endif


