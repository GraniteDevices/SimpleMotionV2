#ifndef BUFFEREDMOTION_H
#define BUFFEREDMOTION_H

#ifdef __cplusplus
extern "C"{
#endif

#include "simplemotion.h"

//typedef enum _smBufferedState {BufferedStop=0,BufferedRun=1} smBufferedState;

typedef struct _BufferedMotionAxis {
    bool initialized;
    bool readParamInitialized;
    int32_t numberOfDiscardableReturnDataPackets;
    int32_t numberOfPendingReadPackets;//number of read data packets that should be arriving from device (to read rest of pending data, use smBufferedFillAndReceive(numFillPoints=0) until this variable this goes to zero)
    smbus bushandle;
    smaddr deviceAddress;
    int32_t samplerate;
    int16_t readParamAddr;
    uint8_t readParamLength;
    int32_t driveFlagsBeforeInit;
    bool driveFlagsModifiedAtInit;//true if deInit should restore driveFlagsBeforeInit
    int32_t driveAccelerationBeforeInit;
    uint16_t driveClock;//clock counter is updated at smBufferedRunAndSyncClocks only for the one axis that is used with that func. clock is running up at 10kHz count rate, meaning that it rolls over every 6.5536 secs
    int32_t bufferLength;//buffer lenght in bytes of the device. note this may be different in different devices types. so call smBufferedGetFree on the device that has the smallest buffer. however as of 2.2016 all GD drives have 2048 bytes buffers.
    int32_t bufferFreeBytes;//number of bytes free in buffer, updated at smBufferedGetFree
    int32_t bufferFill;//percentage of buffer fill, updated at smBufferedGetFree. this should stay above 50% to ensure gapless motion. if gaps occur, check SMV2USB adpater COM port latency setting (set to 1ms) or try lower samplerate.
    int32_t smProtocolVersion;//version of SM protocol of the target device. some internal functionality of API may use this info.
    int32_t deviceCapabilityFlags1;//value of SMP_DEVICE_CAPABILITIES1 if target device has SM protocol version 28 or later (if SM version<28, then value is 0)
    int32_t deviceCapabilityFlags2;//value of SMP_DEVICE_CAPABILITIES2 if target device has SM protocol version 28 or later (if SM version<28, then value is 0)
} BufferedMotionAxis;

/** initialize buffered motion for one axis with address and samplerate (Hz) */
/* returnDataLenght must be one of following:
#define SM_RETURN_STATUS 3
//return value contains a read value from address defined by SMP_RETURN_PARAM_ADDR
//consumes 2 byte from payload buffer. can contain value up to 14 bits. value greater than 14 bits is clipped (padded with 0s)
#define SM_RETURN_VALUE_16B 2
//return value contains a read value from address defined by SMP_RETURN_PARAM_ADDR
//consumes 3 byte from payload buffer. can contain value up to 14 bits. value greater than 22 bits is clipped (padded with 0s)
#define SM_RETURN_VALUE_24B 1
//return value contains a read value from address defined by SMP_RETURN_PARAM_ADDR
//consumes 4 byte from payload buffer. can contain value up to 30 bits. value greater than 14 bits is clipped (padded with 0s)
#define SM_RETURN_VALUE_32B 0

Note return data per one FillAndReceive must not exceed 120 bytes. So max allowed numFillPoints will depend on returnDataLength.
numFillPoints must be equal or below 30 for 32B, 40 for 24B and 60 for 16B.
*/
LIB SM_STATUS smBufferedInit( BufferedMotionAxis *newAxis, smbus handle, smaddr deviceAddress, int32_t sampleRate, int16_t readParamAddr, uint8_t readDataLength  );

/** uninitialize axis from buffered motion, recommended to call this before closing bus so drive's adjusted parameters are restored to originals*/
LIB SM_STATUS smBufferedDeinit( BufferedMotionAxis *axis );

/* this also starts buffered motion when it's not running*/
LIB SM_STATUS smBufferedRunAndSyncClocks( BufferedMotionAxis *axis );
LIB SM_STATUS smBufferedGetFree(BufferedMotionAxis *axis, int32_t *numBytesFree );
LIB int32_t smBufferedGetMaxFillSize(BufferedMotionAxis *axis, int32_t numBytesFree );
LIB int32_t smBufferedGetBytesConsumed(BufferedMotionAxis *axis, int32_t numFillPoints );
LIB SM_STATUS smBufferedFillAndReceive( BufferedMotionAxis *axis, int32_t numFillPoints, int32_t *fillPoints, int32_t *numReceivedPoints, int32_t *receivedPoints, int32_t *bytesFilled );
/** This will stop executing buffered motion immediately and discard rest of already filled buffer on a given axis. May cause drive fault state such as tracking error if done at high speed because stop happens without deceleration.
Note: this will not stop motion, but just stop executing the sent buffered commands. The last executed motion point will be still followed by drive. So this is bad function
for quick stopping stopping, for stop to the actual place consider using disable drive instead (prefferably phsyical input disable).
*/
LIB SM_STATUS smBufferedAbort(BufferedMotionAxis *axis);


#ifdef __cplusplus
}
#endif
#endif // BUFFEREDMOTION_H
