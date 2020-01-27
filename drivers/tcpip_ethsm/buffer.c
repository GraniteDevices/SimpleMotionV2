#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "buffer.h"

/**********************************************/
/*   RING BUFFER                              */
/**********************************************/


typedef struct RingBuffer
{
    unsigned int bufferIdentifier;
    unsigned int writePointer;
    unsigned int readPointer;
    TCP_BUFFER_TYPE data[TCP_BUFFER_SIZE];
} RingBuffer;

static RingBuffer ringBuffers[AMOUNT_OF_TCP_BUFFERS];

/// Finds the buffer index by the buffer identifier
static int findBufferIndexByIdentifier(unsigned int bufferIdentifier)
{
    int index = -1;
    for (int i = 0; i < AMOUNT_OF_TCP_BUFFERS; ++i)
    {
        if (ringBuffers[i].bufferIdentifier == bufferIdentifier)
        {
            index = i;
            break;
        }
    }
    return index;
}

int createRingBuffer(unsigned int bufferIdentifier)
{
    if (bufferIdentifier == 0) {
        return -1;
    }

    int index = -1;

    if (findBufferIndexByIdentifier(bufferIdentifier) >= 0) {
        return -1;
    }

    for (int i = 0; i < AMOUNT_OF_TCP_BUFFERS; ++i)
    {
        if (ringBuffers[i].bufferIdentifier == 0)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        return -1;
    }

    RingBuffer *buffer = &ringBuffers[index];

    memset(buffer, 0, sizeof(RingBuffer));
    buffer->bufferIdentifier = bufferIdentifier;
    return 1;
}

int removeRingBuffer(unsigned int bufferIdentifier)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        ringBuffers[index].bufferIdentifier = 0;
        return 1;
    }

    return -1;
}

int bufferAddItem(unsigned int bufferIdentifier, TCP_BUFFER_TYPE byte)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer *buf = &ringBuffers[index];
        buf->data[buf->writePointer++] = byte;
        buf->writePointer %= TCP_BUFFER_SIZE;
        return 1;
    }
    return -1;
}

int bufferGetItem(unsigned int bufferIdentifier, TCP_BUFFER_TYPE *item)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer *buf = &ringBuffers[index];
        *item = buf->data[buf->readPointer];
        buf->data[buf->readPointer++] = 0;
        buf->readPointer %= TCP_BUFFER_SIZE;
        return 1;
    }

    return -1;
}

int bufferClear(unsigned int bufferIdentifier)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        RingBuffer *buf = &ringBuffers[index];
        buf->readPointer = 0;
        buf->writePointer = 0;
        memset(buf->data, 0, TCP_BUFFER_SIZE * sizeof(TCP_BUFFER_TYPE));
        return 1;
    }
    return -1;
}

int bufferAmountOfBytes(unsigned int bufferIdentifier)
{
    int index = findBufferIndexByIdentifier(bufferIdentifier);

    if (index >= 0)
    {
        int r = (int)ringBuffers[index].writePointer - (int)ringBuffers[index].readPointer;
        if (r < 0)
        {
            r += TCP_BUFFER_SIZE;
        }
        return r;
    }

    return -1;
}
