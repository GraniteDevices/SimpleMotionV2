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

#ifdef __cplusplus
extern "C" {
#endif

#include "simplemotion.h"

//max number of simultaneously connected FTDI devices
#define MAX_OPEN_PORTS 32

//return port handle or -1 if fails
smint32 d2xxPortOpen(const char *port_device_name, smint32 baudrate_bps);
smint32 d2xxPortRead(smint32 serialport_handle, unsigned char *buf, smint32 size);
smint32 d2xxPortWriteByte(smint32 serialport_handle, unsigned char byte);
smint32 d2xxPortWriteBuffer(smint32 serialport_handle, unsigned char *buf, smint32 size);
void d2xxPortClose(smint32 serialport_handle);


#ifdef __cplusplus
}
#endif

#endif


