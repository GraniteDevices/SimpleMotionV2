#ifndef tcp_ethsm_client_buffer_INCLUDED
#define tcp_ethsm_client_buffer_INCLUDED

#define AMOUNT_OF_TCP_BUFFERS 8
#define TCP_BUFFER_SIZE 102410
#define TCP_BUFFER_TYPE char

int findBufferIndexByIdentifier(unsigned int bufferIdentifier);

int createRingBuffer(unsigned int bufferIdentifier);

int removeRingBuffer(unsigned int bufferIdentifier);

void bufferAddByte(unsigned int bufferIdentifier, TCP_BUFFER_TYPE byte);

TCP_BUFFER_TYPE bufferGetByte(unsigned int bufferIdentifier);

void bufferClear(unsigned int bufferIdentifier);

int bufferAmountOfBytes(unsigned int bufferIdentifier);

#endif
