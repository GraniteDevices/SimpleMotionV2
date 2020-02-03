#ifndef tcp_ethsm_client_INCLUDED
#define tcp_ethsm_client_INCLUDED

#include "simplemotion_private.h"
#include "buffer.h"

#define SM_PACKET_TYPE_PING 0x00
#define SM_PACKET_TYPE_WRITE 0x01
#define SM_PACKET_TYPE_READ 0x02
#define SM_PACKET_TYPE_FLUSH 0x03
#define SM_PACKET_TYPE_SET_BAUDRATE 0x04
#define SM_PACKET_TYPE_GET_BAUDRATE 0x05
#define SM_PACKET_TYPE_GET_TIMEOUT 0x06
#define SM_PACKET_TYPE_SET_TIMEOUT 0x07

#define SM_PACKET_TYPE_GET_BUS_STATUS 0x08
#define SM_PACKET_TYPE_GET_DEVICE_FEATURES 0x09
#define SM_PACKET_TYPE_GET_DEVICE_VERSION_NUMBERS 0x0a

// How many milliseconds the ETHSM adapter waits answers from SM bus
#define ETHSM_READ_TIMEOUT_MS 10

// How many milliseconds this driver waits answers from the ETHSM adapter
#define LOCAL_TCP_READ_TIMEOUT_MS 2000 // TODO

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Opens TCP connection to an ETHSM adapter
 * @param devicename Successful operation requires devicename in form
 * ETHSM:<IP>:<PORT>, e.g. ETHSM:192.168.8.100:80
 *
 * @param baudrate_bps This baudrate will be set to the ETHSM adapter
 * @param success This parameter will be set smtrue after successful operation. Otherwise smfalse.
 * @return Opened port handle. This handle is needed when calling other ETHSM functions.
 */
smBusdevicePointer ETHSMPortOpen(const char *devicename, smint32 baudrate_bps, smbool *success);

/**
 * @brief Tries to read given amount of bytes from the SM bus connected to an ETHSM adapter
 * @param busdevicePointer selects the connection where to read these bytes
 * @param buf Buffer where to read the bytes from ETHSM adapter
 * @param size How many bytes this driver tries to read
 * @return amount of bytes read to the given buffer
 */
int ETHSMPortRead(smBusdevicePointer busdevicePointer, unsigned char *buf, int size);

/**
 * @brief Tries to write given bytes to the SM bus connected to an ETHSM adapter
 * @param busdevicePointer selects the connection where to write these bytes
 * @param buf Data to the SM bus
 * @param size Amount of data to be written
 * @return Amount of bytes written to the SM bus
 */
int ETHSMPortWrite(smBusdevicePointer busdevicePointer, unsigned char *buf, int size);

/**
 * @brief Executes given operation
 * @param busdevicePointer selects the connection where to execute the operation
 * @param operation
 * @param value Some operation commands have also a numerical value with them
 * @returns smtrue if the operation was executed successfully
 * @returns smfalse if the operation has failed
 */
smbool ETHSMMiscOperation(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation, smint32 value);

/**
 * @brief Closes the connection selected by busdevicePointer
 * @param busdevicePointer determines the connection to close
 */
void ETHSMPortClose(smBusdevicePointer busdevicePointer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
