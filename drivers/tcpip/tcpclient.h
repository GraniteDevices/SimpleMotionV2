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


//accepted TCP/IP address format is nnn.nnn.nnn.nnn:pppp where n is IP address numbers and p is port number
int validateIpAddress(const char *s, const char **pip_end,
                             const char **pport_start);
int parseIpAddress(const char *s, char *ip, size_t ipsize, short *port);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif


