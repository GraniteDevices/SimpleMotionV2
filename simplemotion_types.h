//Exported type definitions for SM library
//Copyright (c) Granite Devices Oy

#ifndef SIMPLEMOTION_TYPES_H
#define SIMPLEMOTION_TYPES_H

#include <stdint.h>

//possible return values (SM_STATUS type)
#define SM_NONE 0
#define SM_OK 1
#define SM_ERR_NODEVICE 2
#define SM_ERR_BUS 4
#define SM_ERR_COMMUNICATION 8
#define SM_ERR_PARAMETER 16
#define SM_ERR_LENGTH 32

//declare SM lib integer types
typedef long smbus;
typedef uint32_t smuint32;
typedef uint16_t smuint16;
typedef uint8_t smuint8;
typedef int32_t smint32;
typedef int16_t smint16;
typedef int8_t smint8;
typedef int8_t smbool;
typedef smint32 smint;
#define smtrue 1
#define smfalse 0
typedef int SM_STATUS;
typedef smuint8 smaddr;

// output parameter type of smGetBusDeviceDetails
typedef struct
{
    smbool is_simplemotion_device;//smtrue if usable in SM lib
    char device_name[64];//name that should be fed to smOpenBus
    char description[128];//such as "SimpleMotion USB"
} SM_BUS_DEVICE_INFO;

/* Parametr type for smSetDebugOutput
 * SMDebugOff=no debug prints (default)
 * SMDebugLow=only some excepetion/errors printed
 * SMDebugMid=some common function calls printed
 * SMDebugHigh=more details of function calls/bus communication printed
 * SMDebugTrace=print all raw RX/TX data and parsed read values of RX data
 */
typedef enum _smVerbosityLevel {SMDebugOff,SMDebugLow,SMDebugMid,SMDebugHigh,SMDebugTrace} smVerbosityLevel;

/* Operations for BusdeviceMiscOperation callback.
 *
 * MiscOperationFlushTX = blocking call to make sure that all data has been physically transmitter
 *   before returing the function. Max blocking duration is the value set with smSetTimeout.
 *   If flush operation timeouted, return smfalse (fail), otherwise smtrue (success).
 * MiscOperationPurgeRX = discard all incoming data that is waiting to be read. Return smtrue on success,
 *   smfalse on fail.
 *
 * If operation is unsupported by the callback, return smfalse.
 */
typedef enum _BusDeviceMiscOperationType {MiscOperationFlushTX,MiscOperationPurgeRX} BusDeviceMiscOperationType;

//define communication interface device driver callback types
typedef void* smBusdevicePointer;
typedef smBusdevicePointer (*BusdeviceOpen)(const char *port_device_name, smint32 baudrate_bps, smbool *success);
typedef smint32 (*BusdeviceReadBuffer)(smBusdevicePointer busdevicePointer, unsigned char *buf, smint32 size);
typedef smint32 (*BusdeviceWriteBuffer)(smBusdevicePointer busdevicePointer, unsigned char *buf, smint32 size);
typedef smbool (*BusdeviceMiscOperation)(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation );
typedef void (*BusdeviceClose)(smBusdevicePointer busdevicePointer);

//must use packed mode for bitfields in structs for smFastUpdateCycleWithStructs
#pragma pack(push,1)

// input parameter type for smFastUpdateCycleWithStructs
typedef union
{
	//raw data
    smuint8 U8[4];
    smuint16 U16[2];
    smuint32 U32;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_DEFAULT
    struct
    {
        smint32 SetPoint:16;
        smuint32 _unused:16;
    } DEFAULT_Write;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1
    struct
    {
        smint32 Setpoint:28;
        smuint32 CB1_Enable:1;
        smuint32 CB1_ClearFaults:1;
        smuint32 CB1_QuickStopSet:1;
        smuint32 CB1_BypassTrajPlanner:1;
    } ALT1_Write;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT2
    struct
    {
        smint32 SetpointMainTorque:15;
        smint32 SetpointEffectTorque:15;
        smuint32 CB1_Enable:1;
        smuint32 CB1_Clearfaults:1;
    } ALT2_Write;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT3
    struct
    {
        smint32 SetpointMainTorque:15;
        smint32 SetpointEffectTorque:15;
        smuint32 CB1_Enable:1;
        smuint32 CB1_Clearfaults:1;
    } ALT3_Write;

	//use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT4
	struct
	{
		smint32 SetpointMainTorque:15;
		smint32 SetpointEffectTorque:15;
		smuint32 CB1_Enable:1;
		smuint32 CB1_Clearfaults:1;
	} ALT4_Write;
} FastUpdateCycleWriteData;

// output parameter type for smFastUpdateCycleWithStructs
typedef union
{
	//raw data
    smuint8 U8[4];
    smuint16 U16[2];
    smuint32 U32;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_DEFAULT
    struct
    {
        smint32 PositionFeedback:16;
        smuint32 StatusRegister:16;
    } DEFAULT_Read;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1 or FAST_UPDATE_CYCLE_FORMAT_ALT2
    struct
    {
        smint32 PositionFeedback:30;
        smuint32 Stat_FaultStop:1;
        smuint32 Stat_ServoReady:1;
    } ALT1_ALT2_Read;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1 or FAST_UPDATE_CYCLE_FORMAT_ALT3
    struct
    {
        smint32 PositionFeedback:24;
        smuint32 PositionFeedbackSamplingTimestamp:6; //encoder sampling cycle, 400 us periods
        smuint32 Stat_FaultStop:1;
        smuint32 Stat_ServoReady:1;
    } ALT3_Read;

	//use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1 or FAST_UPDATE_CYCLE_FORMAT_ALT4
	struct
	{
		smuint32 PositionFeedback:24; // scale is full 24 bits per motor revolution. is not hard absolute like it's in ALT3, and will reset to 0 in centering and with offset SMP.
		smuint32 PositionFeedbackSamplingTimestamp:6; //encoder sampling cycle, 400 us periods
		smuint32 Stat_FaultStop:1;
		smuint32 Stat_ServoReady:1;
	} ALT4_Read;
} FastUpdateCycleReadData;

#pragma pack(pop)


#endif // SIMPLEMOTION_TYPES_H
