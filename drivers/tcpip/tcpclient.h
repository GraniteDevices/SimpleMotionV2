#ifndef tcpclient_INCLUDED
#define tcpclient_INCLUDED

#include "simplemotion_private.h"

#ifdef __cplusplus
extern "C" {
#endif

//return port handle. sets success=smtrue if ok
smBusdevicePointer TCPIPPortOpen(const char * devicename, smint32 baudrate_bps, smbool *success);
int TCPIPPortRead(smBusdevicePointer busdevicePointer, unsigned char *, int);
int TCPIPPortWrite(smBusdevicePointer busdevicePointer, unsigned char *, int);
void TCPIPPortClose(smBusdevicePointer busdevicePointer);


//accepted TCP/IP address format is nnn.nnn.nnn.nnn:pppp where n is IP address numbers and p is port number
int validateIpAddress(const char *s, const char **pip_end,
                             const char **pport_start);
int parseIpAddress(const char *s, char *ip, size_t ipsize, unsigned short *port);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif


