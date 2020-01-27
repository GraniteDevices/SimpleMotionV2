#ifndef tcp_ethsm_client_INCLUDED
#define tcp_ethsm_client_INCLUDED

#include "simplemotion_private.h"
#include "buffer.h"

#define SM_PACKET_TYPE_PING 0x00
#define SM_PACKET_TYPE_WRITE 0x01
#define SM_PACKET_TYPE_READ 0x02
#define SM_PACKET_TYPE_FLUSH 0x03 // Wait until writing ends
#define SM_PACKET_TYPE_SET_BAUDRATE 0x04
#define SM_PACKET_TYPE_GET_BAUDRATE 0x05
#define SM_PACKET_TYPE_GET_TIMEOUT 0x06
#define SM_PACKET_TYPE_SET_TIMEOUT 0x07

#define SM_PACKET_TYPE_GET_BUS_STATUS 0x08
#define SM_PACKET_TYPE_GET_DEVICE_FEATURES 0x09
#define SM_PACKET_TYPE_GET_DEVICE_VERSION_NUMBERS 0x0a

#ifdef __cplusplus
extern "C"
{
#endif

//return port handle. sets success=smtrue if ok
smBusdevicePointer tcpipEthSMPortOpen(const char *devicename, smint32 baudrate_bps, smbool *success);
int tcpipEthSMPortRead(smBusdevicePointer busdevicePointer, unsigned char *, int);
int tcpipEthSMPortWrite(smBusdevicePointer busdevicePointer, unsigned char *, int);
smbool tcpipEthSMMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation, smint32 value);
void tcpipEthSMPortClose(smBusdevicePointer busdevicePointer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
