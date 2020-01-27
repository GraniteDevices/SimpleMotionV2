#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "buffer.h"

/**********************************************/
/*   RING BUFFER                              */
/**********************************************/

typedef struct RingBuffer {
    unsigned int bufferIdentifier;
    unsigned int writePointer;
    unsigned int readPointer;
    TCP_BUFFER_TYPE data[TCP_BUFFER_SIZE];
} RingBuffer;

static RingBuffer* ringBuffers[AMOUNT_OF_TCP_BUFFERS];

int findBufferIndexByIdentifier(unsigned int bufferIdentifier)
{
    int index = -1;
    for (int i = 0; i < AMOUNT_OF_TCP_BUFFERS; ++i)
    {
        if (ringBuffers[i] != NULL && ringBuffers[i]->bufferIdentifier == bufferIdentifier)
        {
            index = i;
            break;
        }
    }
    return index;
}

int createRingBuffer(unsigned int bufferIdentifier)
{
    int index = -1;

    for (int i = 0; i < AMOUNT_OF_TCP_BUFFERS; ++i)
    {
        if (ringBuffers[i] == NULL)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        return -1;
    }

    RingBuffer *buffer = (RingBuffer*) malloc(sizeof(RingBuffer));

    if (!buffer)
    {
        printf("ALLOCATE ERROR\r\n");
        return -1;
    }

    memset(buffer,0,sizeof(RingBuffer));
    ringBuffers[index] = buffer;
    buffer->bufferIdentifier = bufferIdentifier;
    return index;
}

int removeRingBuffer(unsigned int bufferIdentifier)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        free(ringBuffers[index]);
        ringBuffers[index] = NULL;
    }

    return index;
}

void bufferAddByte(unsigned int bufferIdentifier, TCP_BUFFER_TYPE byte)
{ // TODO: virheen palautus
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];
        buf->data[buf->writePointer++] = byte;
        buf->writePointer %= TCP_BUFFER_SIZE;
    }
}

TCP_BUFFER_TYPE bufferGetByte(unsigned int bufferIdentifier)
{ // TODO: virheen palautus

    TCP_BUFFER_TYPE r = 0;
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];
        r = buf->data[buf->readPointer];
        buf->data[buf->readPointer++] = 0;
        buf->readPointer %= TCP_BUFFER_SIZE;
    }

    return r;
}

void bufferClear(unsigned int bufferIdentifier)
{
    printf ("FUNCTION %s \r\n", __PRETTY_FUNCTION__);
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];
        buf->readPointer = 0;
        buf->writePointer = 0;
        memset(buf->data, 0, TCP_BUFFER_SIZE * sizeof(TCP_BUFFER_TYPE));
    }
}

int bufferAmountOfBytes(unsigned int bufferIdentifier)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer* buf = ringBuffers[index];

        int r = (int)buf->writePointer - (int)buf->readPointer;
        if (r < 0)
        {
            r += TCP_BUFFER_SIZE;
        }
        return r;
    } else {
        return -1;
    }
}
