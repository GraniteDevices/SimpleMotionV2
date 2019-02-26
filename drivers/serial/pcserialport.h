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


/*  Todo:
  -Restore port settings at CloseComport
  */

#ifndef PCSERAILPORT_H
#define PCSERAILPORT_H

#include "simplemotion_private.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "simplemotion.h"

smBusdevicePointer serialPortOpen(const char *port_device_name, smint32 baudrate_bps, smbool *success);
smint32 serialPortRead(smBusdevicePointer busdevicePointer, unsigned char *buf, smint32 size);
smint32 serialPortWrite(smBusdevicePointer busdevicePointer, unsigned char *buf, smint32 size);
void serialPortClose(smBusdevicePointer busdevicePointer);
smbool serialPortMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation);


#ifdef __cplusplus
}
#endif

#endif


