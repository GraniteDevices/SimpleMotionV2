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

#ifdef __cplusplus
extern "C" {
#endif

#include "simplemotion.h"

//return port handle or -1 if fails
smint32 serialPortOpen(const char *port_device_name, smint32 baudrate_bps);
smint32 serialPortRead(smint32 serialport_handle, unsigned char *buf, smint32 size);
smint32 serialPortWriteByte(smint32 serialport_handle, unsigned char byte);
smint32 serialPortWriteBuffer(smint32 serialport_handle, unsigned char *buf, smint32 size);
void serialPortClose(smint32 serialport_number);


#ifdef __cplusplus
}
#endif

#endif


