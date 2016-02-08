#ifndef BUFFEREDMOTION_H
#define BUFFEREDMOTION_H

#ifdef __cplusplus
extern "C"{
#endif

#include "simplemotion.h"

//typedef enum _smBufferedState {BufferedStop=0,BufferedRun=1} smBufferedState;

typedef struct _BufferedMotionAxis {
    smbool initialized;
    smbool readParamInitialized;
    smint32 numberOfDiscardableReturnDataPackets;
    smint32 numberOfPendingReadPackets;//number of read data packets that should be arriving from device (to read rest of pending data, use smBufferedFillAndReceive(numFillPoints=0) until this variable this goes to zero)
    smbus bushandle;
    smaddr deviceAddress;
    smint32 samplerate;
    smint16 readParamAddr;
    smuint8 readParamLength;
    smint32 driveFlagsBeforeInit;
    smint32 driveAccelerationBeforeInit;
    smuint16 driveClock;//clock counter is updated at smBufferedRunAndSyncClocks only for the one axis that is used with that func. clock is running up at 10kHz count rate, meaning that it rolls over every 6.5536 secs
    smint32 bufferLength;//buffer lenght in bytes of the device. note this may be different in different devices types. so call smBufferedGetFree on the device that has the smallest buffer. however as of 2.2016 all GD drives have 2048 bytes buffers.
    smint32 bufferFreeBytes;//number of bytes free in buffer, updated at smBufferedGetFree
    smint32 bufferFill;//percentage of buffer fill, updated at smBufferedGetFree. this should stay above 50% to ensure gapless motion. if gaps occur, check SMV2USB adpater COM port latency setting (set to 1ms) or try lower samplerate.
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
LIB SM_STATUS smBufferedInit( BufferedMotionAxis *newAxis, smbus handle, smaddr deviceAddress, smint32 sampleRate, smint16 readParamAddr, smuint8 readDataLength  );

/** uninitialize axis from buffered motion, recommended to call this before closing bus so drive's adjusted parameters are restored to originals*/
LIB SM_STATUS smBufferedDeinit( BufferedMotionAxis *axis );

/* this also starts buffered motion when it's not running*/
LIB SM_STATUS smBufferedRunAndSyncClocks( BufferedMotionAxis *axis );
LIB SM_STATUS smBufferedGetFree(BufferedMotionAxis *axis, smint32 *numBytesFree );
LIB smint32 smBufferedGetMaxFillSize(BufferedMotionAxis *axis, smint32 numBytesFree );
LIB smint32 smBufferedGetBytesConsumed(BufferedMotionAxis *axis, smint32 numFillPoints );
LIB SM_STATUS smBufferedFillAndReceive( BufferedMotionAxis *axis, smint32 numFillPoints, smint32 *fillPoints, smint32 *numReceivedPoints, smint32 *receivedPoints, smint32 *bytesFilled );
/** This will stop executing buffered motion immediately and discard rest of already filled buffer on a given axis. May cause drive fault state such as tracking error if done at high speed because stop happens without deceleration.
Note: this will not stop motion, but just stop executing the sent buffered commands. The last executed motion point will be still followed by drive. So this is bad function
for quick stopping stopping, for stop to the actual place consider using disable drive instead (prefferably phsyical input disable).
*/
LIB SM_STATUS smBufferedAbort(BufferedMotionAxis *axis);


#ifdef __cplusplus
}
#endif
#endif // BUFFEREDMOTION_H
