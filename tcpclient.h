#ifndef tcpclient_INCLUDED
#define tcpclient_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//return port handle or -1 if fails
int OpenTCPPort(const char * ip_addr, int port);
int PollTCPPort(int, unsigned char *, int);
int SendTCPByte(int, unsigned char);
int SendTCPBuf(int, unsigned char *, int);
void CloseTCPport(int);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif


