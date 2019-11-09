//Exported type definitions for SM library
//Copyright (c) Granite Devices Oy

#ifndef SIMPLEMOTION_TYPES_H
#define SIMPLEMOTION_TYPES_H

#include <stdint.h>
#include <stdbool.h>

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
typedef int32_t smint;
typedef bool smbool;

// These are kept only for legacy compatibility; use <stdbool.h> for better versions
#define smtrue 1
#define smfalse 0
typedef int SM_STATUS;
typedef uint8_t smaddr;

// output parameter type of smGetBusDeviceDetails
typedef struct
{
    bool is_simplemotion_device;//true if usable in SM lib
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
 *   If flush operation timeouted, return false (fail), otherwise true (success).
 * MiscOperationPurgeRX = discard all incoming data that is waiting to be read. Return true on success,
 *   false on fail.
 *
 * If operation is unsupported by the callback, return false.
 */
typedef enum _BusDeviceMiscOperationType {MiscOperationFlushTX,MiscOperationPurgeRX} BusDeviceMiscOperationType;

//define communication interface device driver callback types
typedef void* smBusdevicePointer;
typedef smBusdevicePointer (*BusdeviceOpen)(const char *port_device_name, int32_t baudrate_bps, bool *success);
typedef int32_t (*BusdeviceReadBuffer)(smBusdevicePointer busdevicePointer, unsigned char *buf, int32_t size);
typedef int32_t (*BusdeviceWriteBuffer)(smBusdevicePointer busdevicePointer, unsigned char *buf, int32_t size);
typedef bool (*BusdeviceMiscOperation)(smBusdevicePointer busdevicePointer, BusDeviceMiscOperationType operation );
typedef void (*BusdeviceClose)(smBusdevicePointer busdevicePointer);

//must use packed mode for bitfields in structs for smFastUpdateCycleWithStructs
#pragma pack(push,1)

// input parameter type for smFastUpdateCycleWithStructs
typedef union
{
	//raw data
    uint8_t U8[4];
    uint16_t U16[2];
    uint32_t U32;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_DEFAULT
    struct
    {
        int32_t SetPoint:16;
        uint32_t _unused:16;
    } DEFAULT_Write;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1
    struct
    {
        int32_t Setpoint:28;
        uint32_t CB1_Enable:1;
        uint32_t CB1_ClearFaults:1;
        uint32_t CB1_QuickStopSet:1;
        uint32_t CB1_BypassTrajPlanner:1;
    } ALT1_Write;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT2
    struct
    {
        int32_t SetpointMainTorque:15;
        int32_t SetpointEffectTorque:15;
        uint32_t CB1_Enable:1;
        uint32_t CB1_Clearfaults:1;
    } ALT2_Write;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT3
    struct
    {
        int32_t SetpointMainTorque:15;
        int32_t SetpointEffectTorque:15;
        uint32_t CB1_Enable:1;
        uint32_t CB1_Clearfaults:1;
    } ALT3_Write;

	//use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT4
	struct
	{
		int32_t SetpointMainTorque:15;
		int32_t SetpointEffectTorque:15;
		uint32_t CB1_Enable:1;
		uint32_t CB1_Clearfaults:1;
	} ALT4_Write;
} FastUpdateCycleWriteData;

// output parameter type for smFastUpdateCycleWithStructs
typedef union
{
	//raw data
    uint8_t U8[4];
    uint16_t U16[2];
    uint32_t U32;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_DEFAULT
    struct
    {
        int32_t PositionFeedback:16;
        uint32_t StatusRegister:16;
    } DEFAULT_Read;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1 or FAST_UPDATE_CYCLE_FORMAT_ALT2
    struct
    {
        int32_t PositionFeedback:30;
        uint32_t Stat_FaultStop:1;
        uint32_t Stat_ServoReady:1;
    } ALT1_ALT2_Read;

    //use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1 or FAST_UPDATE_CYCLE_FORMAT_ALT3
    struct
    {
        int32_t PositionFeedback:24;
        uint32_t PositionFeedbackSamplingTimestamp:6; //encoder sampling cycle, 400 us periods
        uint32_t Stat_FaultStop:1;
        uint32_t Stat_ServoReady:1;
    } ALT3_Read;

	//use this when SMP_FAST_UPDATE_CYCLE_FORMAT = FAST_UPDATE_CYCLE_FORMAT_ALT1 or FAST_UPDATE_CYCLE_FORMAT_ALT4
	struct
	{
		uint32_t PositionFeedback:24; // scale is full 24 bits per motor revolution. is not hard absolute like it's in ALT3, and will reset to 0 in centering and with offset SMP.
		uint32_t PositionFeedbackSamplingTimestamp:6; //encoder sampling cycle, 400 us periods
		uint32_t Stat_FaultStop:1;
		uint32_t Stat_ServoReady:1;
	} ALT4_Read;
} FastUpdateCycleReadData;

#pragma pack(pop)


#endif // SIMPLEMOTION_TYPES_H
