#ifndef tcp_ethsm_client_buffer_INCLUDED
#define tcp_ethsm_client_buffer_INCLUDED

#define AMOUNT_OF_TCP_BUFFERS 8
#define TCP_BUFFER_SIZE 1024
#define TCP_BUFFER_TYPE char

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief createRingBuffer creates a ring buffer.
 * @details Buffer size and type and maximum amount of buffers are defined in this file before.
 * @param bufferIdentifier The buffer ID number, can be anything > 0. It is needed for selecting correct buffer when using other buffer functions.
 * @returns 1 if a buffer was successfully created.
 * @returns -1 if the maximum amount of buffers has been reached or if there already is a buffer with the same bufferIdentifier
 */
int createRingBuffer(unsigned int bufferIdentifier);

/**
 * @brief Clears a buffer identified by bufferIdentifier
 * @param bufferIdentifier The number used for creating buffer.
 * @returns 1 if a buffer with given bufferIdentifier was found and cleared.
 * @returns -1 if buffer with given bufferIdentifier could not be found.
 */
int removeRingBuffer(unsigned int bufferIdentifier);

/**
 * @brief Adds a byte to the buffer identified by given bufferIdentifier.
 * @param bufferIdentifier The number used for creating buffer.
 * @param byte The byte to be saved to the buffer.
 * @returns 1 if a buffer with given bufferIdentifier was found and the given byte was saved to it
 * @returns -1 if buffer with given bufferIdentifier could not be found
 */
int bufferAddItem(unsigned int bufferIdentifier, TCP_BUFFER_TYPE byte);

/**
 * @brief Get the next byte from the buffer identified by given bufferIdentifier.
 * @param bufferIdentifier The number used for creating buffer.
 * @param item The item read from the buffer will be copied to this pointer
 * @returns 1 if a buffer with given bufferIdentifier was found and an item was read from it
 * @returns -1 if buffer with given bufferIdentifier could not be found
 */
int bufferGetItem(unsigned int bufferIdentifier, TCP_BUFFER_TYPE *item);

/**
 * @brief Clears the buffer identified by given bufferIdentifier.
 * @param bufferIdentifier The number used for creating buffer.
 * @returns 1 if a buffer with given bufferIdentifier was found and cleared
 * @returns -1 if buffer with given bufferIdentifier could not be found
 */
int bufferClear(unsigned int bufferIdentifier);

/**
 * @brief Returns amount of unread items in the buffer identified by given bufferIdentifier.
 * @param bufferIdentifier The number used for creating buffer.
 * @return Amount of unread items or -1 if buffer with given bufferIdentifier could not be found.
 */
int bufferAmountOfBytes(unsigned int bufferIdentifier);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
