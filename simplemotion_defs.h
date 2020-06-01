/*
 * simplemotion_defs.h
 * This file contains addresses for simple motion parameters (SMP's) and if parameter is a bit field
 * or enum (low number of choices), then also bits or choices in the register are defined here.
 *
 *  Created on: 25.2.2012
 *      Author: Tero
 */


/* SMV2 address space areas:
 * 0 NULL, can read or write but has no effect
 * 1-63 SM bus specific parameters
 * 128-167 digital I/O registers for GPIO
 * 168-199 analog I/O reigisters for GPIO
 * 201-8190 application specific parameter addresses. in this case motor drive registers
 * 8191 NOP command. if SM command SMPCMD_SET_PARAM_ADDR with SMP_ADDR_NOP as parameter is sent, no actions are taken in nodes. it's NOP (no operation) command.
 */

/* SMV2 protocol change log:
 * Version 20:
 *    - V20 was introduced with Argon FW 1.0.0 and present at least until 1.4.0
 * Version 25 (is backwards compatible in syntax but not in setpoint behavior, see details below):
 *    - V25 was introduced with IONI
 *    - Setpoint calculation is different:
 *      now there is only one common setpoint and all ABS and INC commands (buffered & instant)
 *      change the same variable. In V20 there was separate setpoint variables for each stream
 *      (instant & buffered) and for INC and ABS separately and finally all setpoints were summed together.
 *      Formula for total setpoint is = SMV2setpoint+OptionalPhysicalSetpoint (step/dir, analog etc)
 *    - In buffered stream, only setpoint commands execute on timed pace. all others execute as fast as possible.
 *      this makes it possible to insert any parameter read/write commands in middle of buffered motion.
 *    - Implemented watchdog functionality in new param SMP_FAULT_BEHAVIOR
 *    - Added param SMP_ADDRESS_OFFSET
 * Version 26:
 *    - Fast SM command added (actually is also present in late V25 too, but as unofficial feature)
 *    - Watchdog timout now resets bitrate to default and aborts buffered motion
 * Version 27:
 *    - Introduced new SMP_BUFFERED_MODE parameter and optional linear interpolation of buffered setpoints. However don't expect it to support all modes, see "capabilities" feature of version 28 for auto-detecting supported interpolation modes
 *    - Introduced SMP_CB1_FORCE_ENABLE flag
 * Version 28:
 *    - Devices implement SMP_DEVICE_CAPABILITIES1 and SMP_DEVICE_CAPABILITIES2 read-only bit fields. By checking the bits of these parameteters, SM application can determine the supported features of the target device.
 *
 */

/* Important when using SMV2 protocol:
 *
 * SMP_SM_VERSION_COMPAT defines the oldest SM bus version in which the current version is
 * fully backwards compatible. If you write app that supports SM bus versions between N and M (i.e. N=20 M=25),
 * then check that connected device complies with the test (pseudo code):
 *
 *
	smuint32 NEWEST_SUPPORTED_SMV2_VERSION=M;
	smuint32 OLDEST_SUPPORTED_SMV2_VERSION=N;
	smuint32 smv2version=getParam(SMP_SM_VERSION);
	smuint32 smv2compatversion=getParam(SMP_SM_COMPAT_VERSION);

	if(smv2version>NEWEST_SUPPORTED_SMV2_VERSION && smv2compatversion>NEWEST_SUPPORTED_SMV2_VERSION)
	{
		error("SimpleMotion protocol version too new, not supported by this software version. Check for upgrades. Connected SMV2 Version: " + smv2version);
		return false;
	}
	if(smv2version<OLDEST_SUPPORTED_SMV2_VERSION && smv2compatversion < OLDEST_SUPPORTED_SMV2_VERSION)
	{
		error("SimpleMotion protocol version too old, not supported by this software version. Try older versions. Connected SMV2 Version: " + smv2version);
		return false;
	}
 *
 * If test fails, then connected device may not be compatible with your app.
 *
 * Its strongly recommended to read these paramers and verify compatibility before any other actions
 * and do same kind of test also with SMP_FIRMWARE_VERSION and SMP_FIRMWARE_BACKWARDS_COMP_VERSION.
 */


#ifndef SIMPLEMOTION_DEFS_H_
#define SIMPLEMOTION_DEFS_H_

/*
 * bit shift macros
 */

#ifndef BV
//bitvise shifts
#define BV(bit) (1<<(bit))
#define BVL(bit) (1L<<(bit))
#endif





//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SimpleMotion V2 constants
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* broadcast address, if packet is sent to SM_BROADCAST_ADDR then all nodes in the network
 * will read it but no-one will send a reply because it would cause packet collision*/
 #define SM_BROADCAST_ADDR 0

/* The following 4 #defines define attributes over parameter address to retrieve different types of values as return data.
 * These can be used to request information about any parameter by reading a parameter normally but making logical OR of
 * parameter address with following #defines. I.e:
 *
 * Reading parameter with address of (SMP_TIMEOUT|SMP_MIN_VALUE_MASK) will give the minimum settable value for param SMP_TIMEOUT.
 */
/* will return actual value of the parameter */
#define SMP_VALUE_MASK  0x0000
/* will return minimum writable value of parameter */
#define SMP_MIN_VALUE_MASK  0x4000
/* will return maximum writable value of parameter */
#define SMP_MAX_VALUE_MASK  0x8000
/* properties mask that can be used to ask whether a parameter is supported in the target device and possibly also figure out it's
 * properties such as it's version or data format details.
 *
 * Note this is not available in all SM devices:
 * SMP_PROPERTIES_MASK is supported when DEVICE_CAPABILITY1_SUPPORTS_SMP_PARAMETER_PROPERTIES_MASK is set in DEVICE_CAPABILITIES1
 */
#define SMP_PROPERTIES_MASK 0xC000
	#define SMP_PROPERTY_PARAM_IS_READABLE BV(0) //if true, parameter exists and is readable (can be used to test whether parameter exists in SM device)
	#define SMP_PROPERTY_PARAM_IS_WRITABLE BV(1) //if true, parameter is writable
	#define SMP_PROPERTY_HAS_EXTRA_FLAGS BV(2) //if 1, then SMP_PARAMETER_PROPERTIES bits 8-15 (SMP_PROPERTY_EXTRA_FLAGS_MASK) will contain parameter specific flags (defined in this header file on each parameter separately, if available). If 0, bits 8-15 will contain 00000000b
	#define SMP_PROPERTY_EXTRA_FLAGS_MASK 0x0000ff00; //mask for parameter specific flags, see SMP_PROPERTY_HAS_EXTRA_FLAGS

/* Mask to filter above attributes, used internally by SM library, not useful for SM application writer. */
#define SMP_ATTRIBUTE_BITS_MASK  0xC000//C=1100
/* Mask to filter above attributes, used internally by SM library, not useful for SM application writer. */
#define SMP_ADDRESS_BITS_MASK  0x1FFF //E=1110

/*
 * SM payload command types and return value types.
 */

//SMP packet header bits (2 bits). These determine content length and type
#define SMPCMD_SETPARAMADDR 2 //Deprecated name, dont use these. Use new names seen below
#define SMPCMD_SET_PARAM_ADDR 2 //Deprecated name, dont use these. Use new names seen below
#define SMPCMD_24B 1 //Deprecated name, dont use these. Use new names seen below
#define SMPCMD_32B 0 //Deprecated name, dont use these. Use new names seen below
#define SMPRET_CMD_STATUS 3 //Deprecated name, dont use these. Use new names seen below
#define SMPRET_OTHER 3 //Deprecated name, dont use these. Use new names seen below
#define SMPRET_16B 2 //Deprecated name, dont use these. Use new names seen below
#define SMPRET_24B 1 //Deprecated name, dont use these. Use new names seen below
#define SMPRET_32B 0 //Deprecated name, dont use these. Use new names seen below

//
//new naming for the above values (more clear meanings):
//

//OUTBOUND SM COMMAND PAYLOAD SUBPACKET TYPES
//each SMCMD_INSTANT_CMD and SMCMD_BUFFERED_CMD payload is filled with subpackets of following types
	//sets param address (one of SMP_ defines) where next written value goes to.
	//consumes 2 bytes from payload buffer.
	#define SM_SET_WRITE_ADDRESS 2
	//writes value to previously defined address. consumes 3 bytes (24 bits) from payload buffer.
	//can hold value size up to 22 bits. bits above 22 are clipped (transmitted as 0s)
	#define SM_WRITE_VALUE_24B 1
	//writes value to previously defined address. consumes 4 bytes (32 bits) from payload buffer.
	//can hold value size up to 30 bits. bits above 30 are clipped (transmitted as 0s)
	#define SM_WRITE_VALUE_32B 0

//INBOUND SM COMMAND PAYLOAD SUBPACKET TYPES
//each outbound subpacket will return one of these. the type and address what is being returned
//is defined by parameters SMP_RETURN_PARAM_ADDR and SMP_RETURN_PARAM_LEN
	//return value contains SM bus status bits (possible faults etc).
	//consumes 1 byte from payload buffer
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

/* subpacket format
 *
 * header:
 * 2 bits packet type which defines also lenght of subpacket
 * data:
 * rest of subpacket contains the transmitted value
 *
 * example:
 * SM_WRITE_VALUE_32B packet looks like this
 *
 * -2 first bits are 00
 * -next 30 bits carry the value
 *
 * SMV2 library takes care of forming subpackets so user only needs to care about using the macros above
 *
 */

/* Following is a list of status (SMP_CMD_STATUS_) values that are the result of every SM subpacket command.
 *
 * To read directly this value on each SM subpacket command, set SMPRET_OTHER as SMP_RETURN_PARAM_LEN. In this
 * case, user can not read value of the variable.
 *
 * As alternative to direct reading like above:
 * All executed SM subpacket commands also modify SMP_CUMULATIVE_STATUS. User can
 * read SMP_CUMULATIVE_STATUS to check whihc status bits the previous commands have set
 * (i.e. to check if all parameters were reawd/written succesfully). Once bit is set in cumulative status, it will stick there
 * until user clears this value by writing 0 to SMP_CUMULATIVE_STATUS. Procedure for using SMP_CUMULATIVE_STATUS:
 *
 * 1. Set SMP_CUMULATIVE_STATUS to 0
 * 2. Write/read some SMP parameters
 * 3. Read SMP_CUMULATIVE_STATUS and check which SMP_CMD_STATUS bits have been set.
 *
 * If all all above read/writes were succesfull, it contains only bit SMP_CMD_STATUS_ACK. If something else is returned, investigate which
 * read/write operation caused the error (i.e. by narrowing the SMP_CUMULATIVE_STATUS monitoring range).
 */
#define SMP_CMD_STATUS_ACK 0 /* subpacket command executed successfully */
#define SMP_CMD_STATUS_NACK 1 /* subpacket command failed, perhaps writing into read-only variable or invalid value */
#define SMP_CMD_STATUS_INVALID_ADDR 2 /* subpacket command has been targetted in invalid SMP parameter address, perhaps parameter is not supported in target device */
#define SMP_CMD_STATUS_INVALID_VALUE 4 /* attempt to set invalid value into parameter */
#define SMP_CMD_STATUS_VALUE_TOO_HIGH 8 /* attempt to too high value into parameter */
#define SMP_CMD_STATUS_VALUE_TOO_LOW 16 /* attempt to too low value into parameter */

//params 0-63 are reserved for SM specific params, application specifics are >64-8090
#define SMP_SM_RESERVED_ADDRESS_SPACE 63

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SimpleMotion common parameter definitions start below. These parameters are mandatory on SimpleMotion devices.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//SMP=Simple motion parameter

#define SMP_NULL 0
#define SMP_NODE_ADDRSS 1
#define SMP_BUS_MODE 2
//possible values:
	#define SMP_BUS_MODE_DFU 0
	#define SMP_BUS_MODE_NORMAL 1
    /* Busy mode: device responds only to Granity's detect nodes message and Granity will display message that device can't be connected in this mode. I.e. EtherFOX in EC mode. */
    #define SMP_BUS_MODE_BUSY 2
    #define _SMP_BUS_MODE_LAST 2

/* SMP_SM_VERSION returns current SM protocol version */
#define SMP_SM_VERSION 3
/* SMP_SM_VERSION_COMPAT defines the oldest SM bus version in which the current version is
 * fully backwards compatible. */
#define SMP_SM_VERSION_COMPAT 4
#define SMP_BUS_SPEED 5
#define SMP_BUFFER_FREE_BYTES 6
#define SMP_BUFFERED_CMD_STATUS 7
	//bit mask
	#define SM_BUFCMD_STAT_IDLE 1
	#define SM_BUFCMD_STAT_RUN 2
	#define SM_BUFCMD_STAT_UNDERRUN 4
	#define SM_BUFCMD_STAT_OVERRUN 8

#define SMP_BUFFERED_CMD_PERIOD 8
#define SMP_RETURN_PARAM_ADDR 9
#define SMP_RETURN_PARAM_LEN 10
/*SMP_TIMOUT defines how long device waits for one packet to transmit before discarding it. unit 0.1 milliseconds*/
#define SMP_TIMEOUT 12
#define SMP_CUMULATIVE_STATUS 13 //error bits are set here if any, (SMP_CMD_STATUS_... bits). clear by writing 0. For practical usage of SMP_CUMULATIVE_STATUS, see definitions of SMP_CMD_STATUS_ bits above.
#define SMP_ADDRESS_OFFSET 14 /*used to set or offset device address along physical method, i.e. DIP SW + offset to allow greater range of addresses than switch allows. */
/* SMP_FAULT_BEHAVIOR defines maximum amount of time between to valid received SM packets to device and other SM
 * fault behavior that affect drive operation.
 *
 * If comm is broken longer than watchdog time, drive will go fault stop state.
 * Can be used for additional safety stop when drives are controlled only onver SM bus.
 *
 * Parameter is bit field:
 * bit 0 (LSB): enable device fault stop on any SM comm error (CRC, invalid value etc)
 * bits 1-7: reserved, always 0
 * bits 8-17: watchdog timeout value. nonzero enables watchdog. scale: 1 count=10ms, so allows 0.01-10.230 s delay.
 * bits 18-32: reserved, always 0
 */
#define SMP_FAULT_BEHAVIOR 15

/* SMP_BUFFERED_MODE defines the behavior in buffered motion commands:
 * bits 0-3 (LSB): setpoint interpolation mode.
 * bits 4-31: reserved for future use.
 *
 * Setpoint interpolation modes:
 * 0: apply nearest setpoint (default). If there are non-setpoint commands between setpoint commands,
 *    these are executed in order with setpoint commands. Non-setpoint commands are executed without time delay to allow setpoint
 *    commands to be executed at constant frequency.
 * 1: linear interpolation between setpoints. If there are non-setpoint commands between setpoint commands,
 *    these are executed one setpoit command in advance compared to a setpoint commands. This is a result from look-ahead buffering.
 *    Non-setpoint commands are executed without time delay to allow setpoint commands to be executed at constant frequency.
 *
 * Note: this is present only in SM protocol version 27 and later.
 */
#define SMP_BUFFERED_MODE 16
	#define BUFFERED_INTERPOLATION_MODE_MASK 0x0F
	#define BUFFERED_INTERPOLATION_MODE_NEAREST 0
	#define BUFFERED_INTERPOLATION_MODE_LINEAR 1

/* Sets the data format of input & output parameters of SM API smFastUpdateCycle function.
 * smFastUpdateCycle has 2x16 bit inputs and 2x16 bit outputs. The value of SMP_FAST_UPDATE_CYCLE_FORMAT
 * sets what the actual data means.
 *
 *
 * format 0 (default):
 *  write1=absolute setpoint value
 *  write2=not used (set 0)
 *  read1=lowest 16 bits of position feedback value
 *  read2=first 16 bits of SMP_STATUS
 *
 * format 1 (ALT1):
 *  description: this type has 28 bits absolute setpoint and 30 bits absolute feedback value + 4 output bits for control + 2 input bits for status
 *  write1=lowest 16 bits absolute setpoint value
 *  write2=bits 0-11: upper 12 bits of absolute setpoint value, bits 12-15: these bits are written as bits 0-3 of SMP_CONTROL_BITS_1. See SMP_CB1_nn for functions. So write2 bit 15=bypass trajplanner 14=set quickstop 13=clear faults 12=enable.
 *  read1=lowest 16 bits of position feedback value
 *  read2=bit nr 16 = STAT_SERVO_READY, bit nr 15=STAT_FAULTSTOP, bits 0-14=upper bits of position feedback value (pos FB bits 17-30)
 *
 * format 2 (ALT2):
 *  This is specific for simucube application mode only on available only in some drive models.
 *  write is bit field of:
 *  -signed 15 bits main torque setpoint
 *  -signed 15 bits effects torque setpoint
 *  -1 bit CB1_ENABLE
 *  -1 bit CB1_CLEARFAULTS
 *  read data is same format as in ALT1
 *
 *  format 3 (ALT3)
 *  This is specific for simucube application mode only on available only in some drive models.
 *
 * Note:
 * Before reading/writing this, check if device supports this by checking capability flag DEVICE_CAPABILITY1_SELECTABLE_FAST_UPDATE_CYCLE_FORMAT.
 *
 * Also before setting check the maximum value this parameter supports because all modes might not be implemented in your firmware
 * version. For more info, see https://granitedevices.com/wiki/SimpleMotion_parameter_valid_value_range
 */
#define SMP_FAST_UPDATE_CYCLE_FORMAT 17
	#define FAST_UPDATE_CYCLE_FORMAT_DEFAULT 0
	#define FAST_UPDATE_CYCLE_FORMAT_ALT1 1
	#define FAST_UPDATE_CYCLE_FORMAT_ALT2 2
	#define FAST_UPDATE_CYCLE_FORMAT_ALT3 3
	#define FAST_UPDATE_CYCLE_FORMAT_ALT4 4

/* Intro: SMP_BINARY_DATA and SMP_INIT_BINARY_DATA parameters allow reading & writing binary data from pre-defined buffers. i.e. text strings or calibration data blob.
 *
 * Reading and writing data is done by sequentially writing or reading SMP_BINARY_DATA after SMP_INIT_BINARY_DATA_MODE has been properly set.
 * I.e. set BINARY_DATA_MODE = BINARY_DATA_MODE_BLOCK_CALIBRATION|BINARY_DATA_MODE_FLAG_ERASE.
 *
 * Read parameter address (SMP_BINARY_DATA|SMP_MAX_VALUE_MASK) to get block size in bytes. If returned size is -1, it means invalid settings in SMP_BINARY_DATA_MODE.
 *
 * The write/read unit is 16 bits.
 *
 * Binary data parameter availability: to check whether SMP_BINARY_DATA is supported on target device, first check that DEVICE_CAPABILITY1_SUPPORTS_SMP_PARAMETER_PROPERTIES_MASK is set and
 * then read parameter with address (SMP_BINARY_DATA|SMP_PROPERTIES_MASK) and check that it returns valid (no SM error) non-zero value.
 *
 * Note: if write or read to this parameter fails, it could mean one of following:
 * - block does not support given operation, some buffers may be write or read only
 * - SMP_BINARY_DATA_MODE is set incorrectly
 * - read/write operation is performed out of bounds (overflow)
 * - offset is not given in valid granularity
 */
#define SMP_BINARY_DATA 18
/* SMP_BINARY_DATA_MODE initializes access mode to the buffer data. This must be written before reading/writing to SMP_BINARY_DATA.
 *
 * Accepted value is bit field, options defined below.
 *
 * Erasing block: depending on block, it may be cleared to zeroes (0x00) or ones (0xff). Erasing may take up to one second of time depending on block. User must not send
 * any new SM commands to the bus during erase time because device performing erase may be offline during that time and cause error in communication if communication attempt
 * is made during erase.
 *
 * Bits starting from LSB:
 * 8 = buffer ID (one of BINARY_DATA_MODE_BLOCK_)
 * 8 = access mode flags (or'ed combination of BINARY_DATA_MODE_FLAG_ defines)
 * 16 = starting offset of read/write with SMP_BINARY_DATA
 */
#define SMP_BINARY_DATA_MODE 19
	//data blocks:
	#define BINARY_DATA_MODE_BLOCK_CALIBRATION 0 //id 0 = motor calibration data block. note that starting offset must be multiple of 2 for this block.
	#define BINARY_DATA_MODE_BLOCK_POWER_HISTOGRAM 1 //id 1 = power consumption histogram data
	//...more blocks to be defined here
	//flags:
	#define BINARY_DATA_MODE_FLAG_ERASE BV(8) //if true, then the defined block will be erased immediately by writing this bit to SMP_BINARY_DATA_MODE. not all blocks support this.

/* SMP_BINARY_DATA_MODE_ARGS is a helper macro to generate value for SMP_BINARY_DATA_MODE. Example:
 * smSetParameter( .., .., SMP_BINARY_DATA_MODE, SMP_BINARY_DATA_MODE_ARGS( BINARY_DATA_MODE_BLOCK_CALIBRATION, BINARY_DATA_MODE_FLAG_ERASE, 500 )
*/
#define SMP_BINARY_DATA_MODE_ARGS(block,flags,offset) ((smuint32(block)&0xff) | (smuint32(flags)&0xff00) | ((smuint32(offset)<<16)&0xffff0000))

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SimpleMotion device specific parameter definitions start below. Note: all parameters are not available in all device types/versions. To test which are available,
// try reading them. If SMP_CMD_STATUS_INVALID_ADDR bit is being set into SMP_CUMULATIVE_STATUS on the read attempt, it means parameter doesn't exist in the target device.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//choose input from SMP_DIGITAL_IN_VALUES_1 to be used as home switch.
//note: this might not have effect depending on other parameters (i.e. if output is consumed by some built-in functionality)
#define SMP_HOME_SWITCH_SOURCE_SELECT 100

//choose signal to be forwarded into aux output of drive (on IONI it is GPO5)
/*list of aux sources
 * value  	signal
 * 0		constant 0
 * 1		constant 1 (default)
 */
#define SMP_AUX_OUTPUT_SOURCE_SELECT 101

/* used only for device production testing, not recommended for normal operation, therefore not publicly documented */
#define SMP_GPIO_LOW_LEVEL_ACCESS 102
	#define SMP_GPIO_LOW_LEVEL_ACCESS_SET_PIN_MODE 0
	#define SMP_GPIO_LOW_LEVEL_ACCESS_WRITE_PIN_OUTPUT 1
	#define SMP_GPIO_LOW_LEVEL_ACCESS_READ_PIN_INPUT 2
	#define SMP_GPIO_LOW_LEVEL_ACCESS_READ_ADC_PIN_INPUT 3
/* used only for device production testing, not recommended for normal operation, therefore not publicly documented */
#define SMP_GPIO_LOW_LEVEL_INPUT_VALUE 103

//each DIGITAL variable holds one bit for each physical digital input
/*SMP_DIGITAL_IN_VALUES_1 on GD motor drives:
 * bit	physical input
 * 0	GPI1
 * 1	GPI2
 * 2	GPI3
 * 3 	GPI4
 * 4	GPI5
 * 5	HSIN1
 * 6	HSIN2
 * 7	Analog In 1 as digital value
 * 8	Analog In 2 as digital value
 * 9	ENC A
 * 10	ENC B
 * 11	ENC C
 * 12	ENC D
 * 13	Hall U
 * 14	Hall V
 * 15	Hall W
 * 16	reserved
 * 17	reserved
 * 18	reserved
 * 19	reserved
 * 20	GPI6
 * 21-	reserved
 */
#define SMP_DIGITAL_IN_VALUES_1 128
	#define SMP_DIG_IN1_GPI1 BV(0)
	#define SMP_DIG_IN1_GPI2 BV(1)
	#define SMP_DIG_IN1_GPI3 BV(2)
	#define SMP_DIG_IN1_GPI4 BV(3)
	#define SMP_DIG_IN1_GPI5 BV(4)
	#define SMP_DIG_IN1_GPI6 BV(20)
	#define SMP_DIG_IN1_HSIN1 BV(5)
	#define SMP_DIG_IN1_HSIN2 BV(6)
	#define SMP_DIG_IN1_ANA1 BV(7)
	#define SMP_DIG_IN1_ANA2 BV(8)
	#define SMP_DIG_IN1_ENCA BV(9)
	#define SMP_DIG_IN1_ENCB BV(10)
	#define SMP_DIG_IN1_ENCC BV(11)
	#define SMP_DIG_IN1_ENCD BV(12)
	#define SMP_DIG_IN1_HALLU BV(13)
	#define SMP_DIG_IN1_HALLV BV(14)
	#define SMP_DIG_IN1_HALLW BV(15)

#define SMP_DIGITAL_IN_VALUES_2 129
#define SMP_DIGITAL_IN_VALUES_3 130
#define SMP_DIGITAL_IN_VALUES_4 131

#define SMP_DIGITAL_IN_CHANGED_1 132
#define SMP_DIGITAL_IN_CHANGED_2 133
#define SMP_DIGITAL_IN_CHANGED_3 134
#define SMP_DIGITAL_IN_CHANGED_4 135

#define SMP_DIGITAL_OUT_VALUE_1 136
#define SMP_DIGITAL_OUT_VALUE_2 137
#define SMP_DIGITAL_OUT_VALUE_3 138
#define SMP_DIGITAL_OUT_VALUE_4 139

//set IO pin direction: 0=input, 1=output. May not be supported by HW.
#define SMP_DIGITAL_IO_DIRECTION_1 140
#define SMP_DIGITAL_IO_DIRECTION_2 141
#define SMP_DIGITAL_IO_DIRECTION_3 142
#define SMP_DIGITAL_IO_DIRECTION_4 143

#define SMP_ANALOG_IN_VALUE_1 168
#define SMP_ANALOG_IN_VALUE_2 169
#define SMP_ANALOG_IN_VALUE_3 170
#define SMP_ANALOG_IN_VALUE_4 171
#define SMP_ANALOG_IN_VALUE_5 172
// continue to .. 183

#define SMP_ANALOG_OUT_VALUE_1 184
#define SMP_ANALOG_OUT_VALUE_2 185
#define SMP_ANALOG_OUT_VALUE_3 186
#define SMP_ANALOG_OUT_VALUE_4 187
#define SMP_ANALOG_OUT_VALUE_5 188
// continue to .. 200

/*
 * List of motor control parameters.
 *
 * WARNING: Don't assume anything about these variables! Refer to SimpleMotion V2 documentation
 * for details. If unknown, ask Granite Devices.
 */

 //control setpoint values (pos, vel or torq depending on control mode):
#define SMP_INCREMENTAL_SETPOINT 550
#define SMP_ABSOLUTE_SETPOINT 551
//old names for backwards compatibility:
#define SMP_INCREMENTAL_POS_TARGET SMP_INCREMENTAL_SETPOINT
#define SMP_ABSOLUTE_POS_TARGET SMP_ABSOLUTE_SETPOINT

/*for SMP_FAULTS and SMP_STATUS descriptions, see https://granitedevices.com/wiki/Drive_status_%26_fault_bits_explained */
#define SMP_FAULTS 552
	//bitfield bits:
	#define FLT_FOLLOWERROR	BV(1)
	#define FLT_OVERCURRENT BV(2)
	#define FLT_COMMUNICATION BV(3)
	#define FLT_ENCODER	BV(4)
	#define FLT_OVERTEMP BV(5)
	#define FLT_UNDERVOLTAGE BV(6)
	#define FLT_OVERVOLTAGE BV(7)
	#define FLT_PROGRAM_OR_MEM BV(8)
	#define FLT_HARDWARE BV(9)
	#define FLT_OVERVELOCITY BV(10)
	#define FLT_INIT BV(11)
	#define FLT_MOTION BV(12)
	#define FLT_RANGE BV(13)
	#define FLT_PSTAGE_FORCED_OFF BV(14)
	#define FLT_HOST_COMM_ERROR BV(15)
	#define FLT_CONFIG BV(16)
	//IO side macros
	#define FLT_GC_COMM BV(15)
	#define FLT_QUEUE_FULL FLT_PROGRAM_OR_MEM
	#define FLT_SM485_ERROR FLT_COMMUNICATION
	#define FLT_FIRMWARE FLT_PROGRAM_OR_MEM //non-recoverable program error
	#define FLT_ALLOC FLT_PROGRAM_OR_MEM //memory etc allocation failed

#define SMP_FIRST_FAULT 8115

/*for SMP_FAULTS and SMP_STATUS descriptions, see https://granitedevices.com/wiki/Drive_status_%26_fault_bits_explained */
#define SMP_STATUS 553
	//bitfield bits:
	#define STAT_RESERVED_ BV(0)
	#define STAT_TARGET_REACHED BV(1)//this is 1 when trajectory planner target reached
	#define STAT_FERROR_RECOVERY BV(2)
	#define STAT_RUN BV(3)//run is true only if motor is being actually driven. run=0 clears integrators etc
	#define STAT_ENABLED BV(4)
	#define STAT_FAULTSTOP BV(5)
	#define STAT_FERROR_WARNING BV(6)//follow error warning, recovering or disabled
	#define STAT_STO_ACTIVE BV(7)
	#define STAT_SERVO_READY BV(8)//ready for user command: initialized, running (no fault), not recovering, not homing & no homing aborted, not running sequence
	#define STAT_BRAKING BV(10)
	#define STAT_HOMING BV(11)
	#define STAT_INITIALIZED BV(12)
	#define STAT_VOLTAGES_OK BV(13)
	#define STAT_PERMANENT_STOP BV(15)//outputs disabled until reset
	/*STAT_STANDING_STILL is true in following conditions:
	 * Measured velocity (low pass filtered version of it) has been equal or less than 1% of overspeed fault [FEV], and
	 * in velocity and position control modes additionally motor has zero velocity target.
	 *
	 * Note: not all firmware versions support this. Check DEVICE_CAPABILITY1_SUPPORTS_STAT_STANDING_STILL bit. If it's 0, then STAT_STANDING_STILL always reads 0.
	 */
	#define STAT_STANDING_STILL BV(16)
	#define STAT_QUICK_STOP_ACTIVE BV(17)
	#define STAT_SAFE_TORQUE_MODE_ACTIVE BV(18)
	#define STAT_STANDBY BV(19) //automatic standby in simucube

#define SMP_SYSTEM_CONTROL 554 //writing 1 initiates settings save to flash, writing 2=device restart, 4=abort buffered motion
	//possible values listed
	//save active settings  to flash:
	#define SMP_SYSTEM_CONTROL_SAVECFG 1
	//restart drive:
	#define SMP_SYSTEM_CONTROL_RESTART 2
	//abort buffered commands (discard rest of buffer)
	#define SMP_SYSTEM_CONTROL_ABORTBUFFERED 4
	//next 3 for factory use only:
    #define SMP_SYSTEM_SAMPLE_TEST_VARIABLES 8 //production testing function
    #define SMP_SYSTEM_START_PRODUCTION_TEST 16 //production testing function
    #define SMP_SYSTEM_STOP_PRODUCTION_TEST 32 //production testing function
	//restart device into DFU mode
	#define SMP_SYSTEM_CONTROL_RESTART_TO_DFU_MODE 64
	//read miscellaneous device specific flag bits
	#define SMP_SYSTEM_CONTROL_GET_FLAGS 128
	/*start measure motor RL function.
	 * poll debug param 5 for state
	 * -100 fail
	 * 700 busy
	 * 800 success -new RL values inserted in drive params
	 *
	 * SMP_STATUS must obey:
	 * requiredStats=STAT_INITIALIZED|STAT_RUN|STAT_ENABLED|STAT_VOLTAGES_OK;
       deniedStats=STAT_FERROR_RECOVERY|STAT_PERMANENT_STOP|STAT_STO_ACTIVE|STAT_FAULTSTOP|STAT_HOMING;
	 */
	#define SMP_SYSTEM_CONTROL_MEASURE_MOTOR_RL 256
	//resets position mode FB and setpoint values to 0, and also resets homing status. useful after using in vel or torq mode.
	#define SMP_SYSTEM_CONTROL_RESET_FB_AND_SETPOINT 512
	//writes various FW version specific values into debug parameters
	#define SMP_SYSTEM_CONTROL_GET_SPECIAL_DATA 1024
	//stores encoder index position in SMP_DEBUGPARAM_1. while busy (index not found) SMP_DEBUGPARAM_2 will be 100, after found it is 200.
	#define SMP_SYSTEM_CONTROL_CAPTURE_INDEX_POSITION 2048
	//start a procedure to automatically configure hall sensors direction & offset, or other absolute sensor capable of commutation
	#define SMP_SYSTEM_CONTROL_START_COMMUTATION_SENSOR_AUTOSETUP 4096
	//load settings that are saved in device flash memory. useful when changing parameters on the fly and want to restore originals, or when app is started and drive may have unknown parameter modifications active.
	#define SMP_SYSTEM_CONTROL_RESTORE_SAVED_CONFIG 8192
	//triggers parameter activation for some buffered/deladed parameters such as SMP_TORQUE_BIQUAD_FILTERnnn parameters that do not immediately take effect to avoid glitches
	#define SMP_SYSTEM_CONTROL_TRIGGER_PENDING_PARAMETER_ACTIVATION 16384
	//write SM bus SM_CRCINIT constant modifier. special purposes only, don't use if unsure because
	//it is one time programmable variable (permanently irreversible operation, can't be ever reset to default by provided methods)
	#define SMP_SYSTEM_CONTROL_MODIFY_CRCINIT 262144
	#define SMP_SYSTEM_CONTROL_EXIT_STANDBY 32768 //exit simucube standby mode
	//following three commands execute FW version specific functions (i.e. debugging or customized FW functions)
	#define SMP_SYSTEM_CONTROL_TRIGGER_FW_SPECIFIC_FUNC1 10000000
	#define SMP_SYSTEM_CONTROL_TRIGGER_FW_SPECIFIC_FUNC2 10000001
	#define SMP_SYSTEM_CONTROL_TRIGGER_FW_SPECIFIC_FUNC3 10000002

	//follow error tolerance for position control:
#define SMP_POS_FERROR_TRIP 555
//velocity follow error trip point. units: velocity command (counts per PIDcycle * divider)
#define SMP_VEL_FERROR_TRIP 556
//recovery velocity from disabled/fault state. same scale as other velocity limits
#define SMP_POS_ERROR_RECOVERY_SPEED 557

//motor type:
#define SMP_MOTOR_MODE 558
	//factory state, no choice:
	#define MOTOR_NONE 0
    //DC servo:
    #define MOTOR_DC 1
	//2 phase AC motor
    #define MOTOR_AC_VECTOR_2PHA 2  /*2 phase ac or bldc */
	//3 phase AC motor
    #define MOTOR_AC_VECTOR 3 /*3 phase ac or bldc */
	//2 phase stepper
	#define MOTOR_STEPPER_2PHA 4 /*2 phase stepper */
	//linear 3 phase ac
	#define MOTOR_LINEAR_3PH 5
	//for drive internal use only:
	#define _MOTOR_LAST 7




//position follow error trip point
//velocity follow error trip point. units: velocity command (counts per PIDcycle * divider)

//#define CFG_INPUT_FILTER_LEN 30
#define SMP_CONTROL_MODE 559
	//control mode choices:
	#define CM_TORQUE 3
	#define CM_VELOCITY 2
	#define CM_POSITION 1
	#define CM_NONE 0
#define SMP_INPUT_MULTIPLIER 560
#define SMP_INPUT_DIVIDER 561
#define SMP_INPUT_REFERENCE_MODE 562
	//choices:
	#define SMP_INPUT_REFERENCE_MODE_SERIALONLY 0
	#define SMP_INPUT_REFERENCE_MODE_PULSETRAIN 1
	#define SMP_INPUT_REFERENCE_MODE_QUADRATURE 2
	#define SMP_INPUT_REFERENCE_MODE_PWM 3
	#define SMP_INPUT_REFERENCE_MODE_ANALOG 4
	#define SMP_INPUT_REFERENCE_MODE_RESERVED1 5
	#define SMP_INPUT_REFERENCE_MODE_RESERVED2 6
	#define SMP_INPUT_REFERENCE_MODE_LAST_ 6

//setpoint source offset when absolute setpoint is used (pwm/analog)
#define SMP_ABS_IN_OFFSET 563
//scaler, not used after VSD-E
#define SMP_ABS_IN_SCALER 564

//FB1 device resolution, pulses per rev
#define SMP_ENCODER_PPR 565 //note this is not used if DEVICE_CAPABILITY1_COMMUTATION_CONFIG_V2 is set, see SMP_FBD1_COUNTS_PER_POLEPAIR instead


/*
 * AC motor pole configuration
 *
 * function depends on motor type:
 * -dc motor - no effect
 * -AC/bldc motor, number of polepairs
 * -linear motor (FLAG_IS_LINEAR_MOTOR is set), then this defines motor pole pair pitch in micrometers
 *  note: linear motor mode supported only if DEVICE_CAPABILITY1_COMMUTATION_CONFIG_V2 is set
 */
#define SMP_MOTOR_POLEPAIRS 566 /*old name kept for compatibility*/


//flag bits & general
#define SMP_DRIVE_FLAGS 567
	//bitfield bits:
    #define FLAG_DISABLED_AT_STARTUP BV(0)
    #define FLAG_NO_DCBUS_FAULT BV(1)/*obsolete?*/
    #define FLAG_ENABLE_DIR_INPUT_ON_ABS_SETPOINT BV(2) /*if 1, then use direction input signal for analog and PWM setpoint mode*/
    #define FLAG_INVERT_ENCODER BV(3)
    #define FLAG_INVERT_MOTOR_DIRECTION BV(4) /*invert positive direction*/
    #define FLAG_ENABLE_REQUIRES_PULSING BV(5) /*enable signal must be fed 50hz-1khz toggling signal to validate enable status*/
    #define FLAG_USE_PULSE_IN_ACCEL_LIMIT BV(7)/*obsolete*/
    #define FLAG_2PHASE_AC_MOTOR BV(9)/*obsolete*/
    #define FLAG_ALLOW_VOLTAGE_CLIPPING BV(10)
    #define FLAG_USE_INPUT_LP_FILTER BV(11)
    #define FLAG_USE_PID_CONTROLLER BV(12)//PIV is the default if bit is 0/*obsolete*/
    #define FLAG_INVERTED_HALLS BV(13) /*becoming obsolete, no effect on device where param SMP_COMMUTATION_SENSOR_CONFIG is present */
    #define FLAG_USE_HALLS BV(14) /*becoming obsolete, no effect on device where param SMP_COMMUTATION_SENSOR_CONFIG is present */
    #define FLAG_MECH_BRAKE_DURING_PHASING BV(15)
	#define FLAG_LIMIT_SWITCHES_NORMALLY_OPEN_TYPE BV(16)
	#define FLAG_ENABLE_MOTOR_SOUND_NOTIFICATIONS BV(17)
#define SMP_MOTION_FAULT_THRESHOLD 568
#define SMP_HV_VOLTAGE_HI_LIMIT 569
#define SMP_HV_VOLTAGE_LOW_LIMIT 570
#define SMP_MAX_OUTPUT_POWER 571 //in watts
#define SMP_MOTOR_MAX_SPEED 572

//in Ioni only
#define SMP_ELECTRICAL_MODE 573
    #define EL_MODE_STANDARD 0
    #define EL_MODE_IONICUBE 1
	#define EL_MODE_SIMUCUBE 2
    #define EL_MODE_IONIZER 3

/*for BiSS/SSI encoder
 * bits defined as (from LSB):
 * bits 0-7: single turn bits, accepted value range 4-32
 * bits 8-15: multi turn bits, accepted value range 0-16
 * ^^ sum of MT and ST must be max 32
 * bits 16-19: serial encoder mode (see below)
 * rest: reserved for future use (always 0)
 *
 * Serial encoder read modes (bits 16-19):
 * 0 BiSS C
 * 1 BiSS B
 * 2 SSI
 * 3 SSI gray
 * 4 AMS SSI
 */
#define SMP_SERIAL_ENC_BITS 574
#define SMP_SERIAL_ENC2_BITS 578 // supported only if DEVICE_CAPABILITY1_HAS_SECOND_SERIAL_ENCODER_PORT is set

/*
 * if HOMING_RESET_POS_AND_SETPOINT_TO_ABSOLUTE_FBD_READING is 1, then SMP_SERIAL_ENC_OFFSET will be added to the absolute feedback reading before resetting fb value and setpoint to it.
 */
#define SMP_SERIAL_ENC_OFFSET 575



//primary feedback loop 200-299
#define SMP_VEL_I 200
#define SMP_POS_P 201
#define SMP_VEL_P 202
#define SMP_VEL_D 203
#define SMP_VEL_FF 220
#define SMP_ACC_FF 221
#define SMP_POS_FF 222

//anti dither limits
#define SMP_ANTIDITHER_MODE 230

//torque modifiers & effects.
/*SMP_TORQUE_NOTCH_FILTER contains 3 values in different bit positions:
 * 0-7 (lowest byte), attenuation in 0.1dB steps and value V=1-255 means attenuation of (-V-255)/10 dB gain.  if V set 0, use notch filter instead of peaking with "infinite" attenuation. value 255 disables notch filter.
 * 8-15, Q factor in 0.1 steps. Minimum is 0.1, below that filter is disabled.
 * 16-29, center frequency in 0.1Hz steps, and 1.1Hz is minimum, so range is 1.1-409.5 Hz. value below or equal 1Hz disables the filter.
 *
 *Example, peaking filter with gain -11.5dB, center freq 20Hz and Q=3.5 value is: 0x00648c23 = dec 6589475
 *
 *Notch filter works in all control modes and is applied to torque controller setpoint
 */
#define SMP_TORQUE_NOTCH_FILTER 240
//define damping effect gain in torque control mode, torque added to setpoint equals -speed*gain with
#define SMP_TORQUE_EFFECT_DAMPING 241
//define friction effect gain in torque control mode
#define SMP_TORQUE_EFFECT_FRICTION 242
//define inertia effect gain in torque control mode, torque added to setpoint equals -acceleration*gain
#define SMP_TORQUE_EFFECT_INERTIA 243
//special smoothing filter. 0=disabled, other choices application dependent. this value is not saved in flash at the time of release, set it in run-time.
#define SMP_SETPOINT_FILTER_MODE 244
//static torque reduction effect, scale 0..10000 = 0..100% reduction
#define SMP_TORQUE_EFFECT_STATIC_TORQUE_REDUCTION 245
//static torque reduction speed, 1000=default, smaller=slower, higher=faster
#define SMP_TORQUE_EFFECT_STATIC_TORQUE_REDUCTION_SPEED 246
//torque nonlinearity. gamma function: normalized_torque_input^(1000/SMP_TORQUE_EFFECT_GAMMA), scale 1000=1.0 gamma.
#define SMP_TORQUE_EFFECT_GAMMA 247
//separate damping value for center region of motion range. same scale as SMP_TORQUE_EFFECT_DAMPING. see also SMP_TORQUE_EFFECTS_CENTER_POSITION.
#define SMP_TORQUE_EFFECT_CENTER_DAMPING 248
//angle span where SMP_TORQUE_EFFECT_CENTER_DAMPING is applied. outside of span SMP_TORQUE_EFFECT_DAMPING is applied. value in degrees from - to + end. resulting damping will change smoothly by cosine function within the span.
#define SMP_TORQUE_EFFECT_CENTER_DAMPING_ANGLE_SPAN 249
//slew rate limit, value in Nm/s. requires that motor torque constant SMP_MOTOR_TORQUE_OR_FORCE_CONSTANT has been set. value 0 disables the limiter (default).
#define SMP_TORQUE_SLEW_RATE_LIMIT 250
/* Center offset of wheel in in scale of unsigned 24 bits.
 * Writing -1 here will automatically set this value so that current position becomes center.
 * Value of this will affect the value output in fast update command ALT4 format and various simucube specific functions (like center damping and wheel safety rotation limit). */
#define SMP_SIMUCUBE_WHEEL_CENTER_OFFSET 251

//various Simucube option flags
#define SMP_SIMCUBE_OPTIONS 252
	// two lowest bits of flags define hands off the wheel detection sensitivity (automatically activates temporary safe mode). do not binary OR multiple HANDS_OFF_SENSITIVITY_ values, pick just one.
	#define SMP_SIMCUBE_OPTIONS_HANDS_OFF_SENSITIVITY_HIGH 3 // default
	#define SMP_SIMCUBE_OPTIONS_HANDS_OFF_SENSITIVITY_MEDIUM 2
	#define SMP_SIMCUBE_OPTIONS_HANDS_OFF_SENSITIVITY_LOW 1
	#define SMP_SIMCUBE_OPTIONS_HANDS_OFF_SENSITIVITY_OFF 0
	#define MASK_SMP_SIMCUBE_OPTIONS_HANDS_OFF_SENSITIVITY 3 // binary and SMP_SIMCUBE_OPTION_FLAGS and MASK_SMP_SIMCUBE_OPTION_FLAGS_HANDS_OFF_SENSITIVITY to identify which HANDS_OFF_SENSITIVITY setting is set.
	#define SMP_SIMCUBE_OPTIONS_ENABLE_TORQUE_SATURATION_INDICATION_SOUND BV(2)
	#define SMP_SIMCUBE_OPTIONS_REDUCE_RESONANCE BV(3)
	#define SMP_SIMCUBE_OPTIONS_TRACKING_CENTER_DAMPING_POSITION BV(4) // if 1, then fixed center angle offset SMP_TORQUE_EFFECTS_CENTER_POSITION has is superseded by tracking center position
//friction effect stiffness variable for SC2U. Value 100=normal, 50=low, 200=high.
#define SMP_TORQUE_EFFECT_FRICTION_STIFFNESS 253

/* Torque setpoint biquad filters that run at full torque controller update frequency.
 * - Scale of values is 10 000 000=1.0.
         	b0=1;
        	b1=b2=a1=a2=0;
 * - Set values B0=10000000 and B1=B2=A1=A2=0 disable/bypass the filter (FW default)
 * - These are buffered variables and become effective only on boot and after SMP_SYSTEM_CONTROL_TRIGGER_PENDING_PARAMETER_ACTIVATION
 * - Each change of filter will reset filter state (may cause a bump)
 * - If DEVICE_CAPABILITY2_TORQUE_BIQUAD_FILTERS_V1, then filters run at 20kHz sample rate and 32 bits floating point precision.
 *
 * Note: design filters carefully. Make sure that filter is stable at single precision floating point precision and that it does not have greater than unity DC gain.
 */
#define SMP_TORQUE_BIQUAD_FILTER1_B0 260
#define SMP_TORQUE_BIQUAD_FILTER1_B1 261
#define SMP_TORQUE_BIQUAD_FILTER1_B2 262
#define SMP_TORQUE_BIQUAD_FILTER1_A1 263
#define SMP_TORQUE_BIQUAD_FILTER1_A2 264

#define SMP_TORQUE_BIQUAD_FILTER2_B0 265
#define SMP_TORQUE_BIQUAD_FILTER2_B1 266
#define SMP_TORQUE_BIQUAD_FILTER2_B2 267
#define SMP_TORQUE_BIQUAD_FILTER2_A1 268
#define SMP_TORQUE_BIQUAD_FILTER2_A2 269

//secondary feedback loop 300-399
//NOT IMPLEMENTED YET

//current loop related 400-499
//motor inductance and resistance, in milliohms and millihenries
#define SMP_MOTOR_RES 405
#define SMP_MOTOR_IND 406

/* SMP_MOTOR_VOLTAGE_CONSTANT specifies motor Back EMF voltage per Hz (on ac/bldc/stepper motors) and on DC motor voltage per raw velocity feedback unit.
 * Scale:
 * - on AC/BLDC/stepper motors value is mV/Hz (peak of sine voltage, phase to neutral).
 *   (to get good estimate of this value, test how many revs/second (=S) motor turns at given DC supply voltage (U) and calculate:
 *   SMP_MOTOR_VOLTAGE_CONSTANT = U/2*PWMModulationDepth*1.15*MotorPoleCount/2*S*1000 = 287.5*PWMModulationDepth*U*MotorPoleCount/S.
 *   Typical values for PWMModulationDepth is 0.95 (actual model specfic value obtainable form GD Wiki drive specs).
 * - on brush DC motors value is uV/raw_velocity_fb_unit (phase to phase)
 *
 * The value is optional and value of 0 means that the constant is unspecified.
 *
 * Note: not supported in all devices
 */
#define SMP_MOTOR_VOLTAGE_CONSTANT 407

//continuous current limit mA
#define SMP_TORQUELIMIT_CONT 410
//peak current limit mA
#define SMP_TORQUELIMIT_PEAK 411
//homing current limit mA
#define SMP_TORQUELIMIT_HOMING 415
//next 2 set fault sensitivity
#define SMP_TORQUEFAULT_MARGIN 420
#define SMP_TORQUEFAULT_OC_TOLERANCE 421
/* SMP_MOTOR_TORQUE_OR_FORCE_CONSTANT specifies motor torque (rotary motor) or force (linear motor) constant.
 * Scale:
 * - on rotary motors value is 10000*Nm/A (peak of sine)
 * - on linear motors value is 10000*N/A  (peak of sine)
 * Amps are in peak of sine or DC
 *
 * The value is optional and value of 0 means that the constant is unspecified.
 *
 * Note: before using, check if this parameter is supported in target device from SMP_CAPABILITES1
 */
#define SMP_MOTOR_TORQUE_OR_FORCE_CONSTANT 422

/* next four parameters allow compensation of motor detent torque and torque ripple (cogging torque).
 * xxx_TORQUE_FUNCTION and xxx_TORQUE_AMPLITUDE sets function, value range is 0-25 and used like:
 *
 * if((function%1)==0) xxx_torque_modifier=sin(electrical_angle*(function/2))*amplitude;
 * else xxx_torque_modifier=cos(electrical_angle*(function/2))*amplitude;
 *
 * i.e. functions go like:
 * 0=no effect (sin(0a))
 * 1=constant (cos(0a))
 * 2=sin(1a)
 * 3=cos(1a)
 * 4=sin(2a)
 * 5=cos(a2)
 * 6=sin(3a)
 * 7=cos(3a)
 *
 *
 * Detent torque compensation value (static_torque modifier) is added to the original torque setpoint.
 * Ripple torque compensation value (dynamic_torque modifier) will modulate the original torque setpoint.
 * Overall formula how these affect to troque output is:
 *
 * torqueSetpoint=torqueSetpoint*(dynamic_torque_modifier+1) +static_torque_modifier;
 */
#define SMP_MOTOR_STATIC_TORQUE_FUNCTION 424
#define SMP_MOTOR_STATIC_TORQUE_AMPLITUDE 425
#define SMP_MOTOR_DYNAMIC_TORQUE_FUNCTION 426
#define SMP_MOTOR_DYNAMIC_TORQUE_AMPLITUDE 427

//motor thermal time constant in seconds:
#define SMP_MOTOR_THERMAL_TIMECONSTANT 430
//how fast to ramp up voltage during phase search:
#define SMP_PHASESEARCH_VOLTAGE_SLOPE 480
//by default this is calculated from other motor params:
#define SMP_PHASESEARCH_CURRENT 481
/* Commutation angle congiuration, i.e. for hall sensors or absolute encoder. can be automatically set with SMP_SYSTEM_CONTROL_START_COMMUTATION_SENSOR_AUTOSET.
 * Format:
 * bits 0-15 LSB: commutation sensor initialization offset 0-65535 represents commutation angle offset 0-360 electical degrees
 * bit 16: invert sensor count direction
 * bit 17: enable commutation sensor
 *
 * Note: this is for devices where params 484-486
 */
#define SMP_COMMUTATION_SENSOR_CONFIG 482
	#define SMP_COMMUTATION_SENSOR_CONFIG_ANGLE_MASK 0xFFFF
	#define SMP_COMMUTATION_SENSOR_CONFIG_INVERT_MASK 0x10000
	#define SMP_COMMUTATION_SENSOR_CONFIG_ENABLE_MASK 0x20000

/* Following parameter is part of feedback sensor interface version 2.
 * To test whether drive supports this, test if the DEVICE_CAPABILITY1_COMMUTATION_CONFIG_V2 is set.
 * If this interface v2 is supported, then target device does not support parameters: SMP_COMMUTATION_SENSOR_CONFIG as
 * new interface has equivalents. */
/* Number of FBD1 sensor position counts per motor pole count (set by SMP_MOTOR_POLEPAIRS).*/
#define SMP_COMMUTATION_COUNTS_PER_POLE_PAIR_COUNT 483

/* Following parameter is part of feedback sensor interface version 2.
 * To test whether drive supports this, test if the DEVICE_CAPABILITY1_COMMUTATION_CONFIG_V2 is set.
 * If this interface v2 is supported, then target device does not support parameters: SMP_COMMUTATION_SENSOR_CONFIG & FLAG_INVERT_ENCODER as
 * new interface has equivalents. */
/* Commutation sensor initialization source */
#define SMP_COMMUTATION_INIT_SOURCE 484
		#define COMMUTATION_INIT_SOURCE_PHASING 0
		#define COMMUTATION_INIT_SOURCE_HALL 1
		#define COMMUTATION_INIT_SOURCE_HALL_INVERTED_DIRECTION 2
		#define COMMUTATION_INIT_SOURCE_FB1_ABSOLUTE 3
		#define _COMMUTATION_INIT_SOURCE_LAST 3

/* Following parameter is part of feedback sensor interface version 2.
 * To test whether drive supports this, test if the DEVICE_CAPABILITY1_COMMUTATION_CONFIG_V2 is set.
 * If this interface v2 is supported, then target device does not support parameters: SMP_COMMUTATION_SENSOR_CONFIG as
 * new interface has equivalents. */
/* Commutation sensor init offset for 0..360 electrical degrees equals value 0-65535 */
#define SMP_COMMUTATION_INIT_OFFSET 485

//low pass filter selector, value 0=100Hz, 9=3300Hz, 10=4700Hz, 11=unlimited (see Granity for all options):
#define SMP_TORQUE_LPF_BANDWIDTH 490

//motor rev to rotary/linear unit scale. like 5mm/rev or 0.1rev/rev. 30bit parameter & scale 100000=1.0
#define SMP_AXIS_SCALE 491
//output unit 0=mm 1=um 2=inch 3=revolutions 4=degrees
#define SMP_AXIS_UNITS 492

#define SMP_FB1_DEVICE_SELECTION 493
	#define SMP_FBD_NONE 0
	#define SMP_FBD_INCR1 1
	#define SMP_FBD_INCR2 2
	#define SMP_FBD_RESOLVER 3
	#define SMP_FBD_HALLS 4
	#define SMP_FBD_SERIALDATA 5 /*configured with SMP_SERIAL_ENC_BITS */
	#define SMP_FBD_SINCOS16X 6
	#define SMP_FBD_SINCOS64X 7
	#define SMP_FBD_SINCOS256X 8
	#define SMP_FBD_RESERVED 9
	#define SMP_FBD_SERIALDATA_PORT2 10 /* configured with SMP_SERIAL_ENC2_BITS */

/* FB2 is secondary/aux feedback device (optional) to form a dual loop feedback system. has same allowed values than SMP_FB1_DEVICE_SELECTION. */
#define SMP_FB2_DEVICE_SELECTION 494

/* WIP, not yet implemented.
 *
 * Ratio of how many counts FB1 changes in relation to one FB2 count change.
 * Formula = 100000*FB2Counts/FB1Counts
 *
 * Note: value can be also negative when counting direction of FB2 is inverted compared to FB1
 *
 * Parameter requires that DEVICE_CAPABILITY2_SUPPORT_FB2_AUX_ENCODER is 1.
 */
#define SMP_FB1_TO_FB2_COUPLING_RATIO 500

//in 1/2500 seconds.
#define SMP_GOAL_FAULT_FILTER_TIME 495
//in speed scale
#define SMP_OVERSPEED_FAULT_LIMIT 496
//0=min 6=max
#define SMP_OVERCURRENT_FAULT_SENSITIVITY 497
//when hit limt switch, do: 0=nothing, 1=disable torque, 2=fault stop, 3=dynamic braking beyond limits
#define SMP_LIMIT_SW_FUNCTION 498
		//choices:
        #define SMP_LIMIT_SW_DISABLED 0
        #define SMP_LIMIT_SW_NOTORQUE 1
        #define SMP_LIMIT_SW_FAULTSTOP 2
		#define SMP_LIMIT_SW_SERVOSTOP 3 /*this is obsolete definition, kept for backwards compatibility, use new SMP_LIMIT_SW_DYNAMIC_BRAKING instead */
        #define SMP_LIMIT_SW_DYNAMIC_BRAKING 3
        #define _SMP_LIMIT_SW_LAST 3

/*parameter to configure activation of SMP_LIMIT_SW_FUNCTION.
 * by default (0) limit switch function will perform when a physical limit switch OR soft travel limit is exceeded.
 * optionally with value (1) limit switch function can be made to activate only on physical limit switch.
 * this is useful if user want's to have soft limits and hardware limit switches as backup.
 */
#define SMP_LIMIT_SW_FUNCTION_SOURCE 499
		//choices
		#define SMP_LIMIT_SW_FUNCTION_SOURCE_SOFT_AND_HARD_LIMITS 0
		#define SMP_LIMIT_SW_FUNCTION_SOURCE_HARD_LIMITS_ONLY 1

#define SMP_CONTROL_BITS1 2533
	//bitfiled values:
	//CB1 & CB2 enables must be 1 to have drive enabled
	//Control bits 1 are controlled by host software
	#define SMP_CB1_ENABLE BV(0)//software enable
	#define SMP_CB1_CLEARFAULTS BV(1)//Note: not all drive firmware versions support this. to check whether it's supported, verify that bit DEVICE_CAPABILITY1_CONTROL_BITS1_VERSION2 is set in SMP_DEVICE_CAPABILITIES1 parameter
	/*QUICKSTOP:
	 * SMP_CB1_QUICKSTOP_SET drive will attempt to stop motor as soon as possible.
	 *  - in velocity and position control modes, drive uses SMP_TRAJ_PLANNER_STOP_DECEL deceration
	 *    ramp to reduce velocity setpoint to zero.
	 *  - in torque mode, drive will set torq setpoint to zero and activate motor dynamic braking.
	 *  - after quick stop has completed, setpoint will be automatically modified so that no motion
	 *    should occur on QUICKSTOP_RELEASE event (except if user has ongoing buffered motion commands that
	 *    may constantly may alter setpoint)
	 *
	 * SMP_CB1_QUICKSTOP_RELEASE will restore normal running state after above SET.
	 *  - in position and velocity modes, drive speed will ramp-up at current user settable acceleration limit or at SMP_TRAJ_PLANNER_STOP_DECEL whichever is smaller.
	 *  - if SMP_CB1_QUICKSTOP_SET is 1 simultaneously, it overrides the release CB.
	 *
 	 * Notes:
	 * - not all drive firmware versions support this. to check whether it's supported,
	 *    check DEVICE_CAPABILITY1_CONTROL_BITSB1_VERSION2.
	 * - one may check whether motor has been stopped by polling STAT_STANDING_STILL
	 * - quickstop will abort homing if it was active
	 */
	#define SMP_CB1_QUICKSTOP_SET BV(2)//see above comment
	#define SMP_CB1_QUICKSTOP_RELEASE BV(6)//see above comment
	#define SMP_CB1_BYPASS_TRAJPLANNER BV(3)//when 1, drive will not obey acceration limit (equals setting acceleration limit to max, useful when external trajectory planner is used). Note: not all drive firmware versions support this. to check whether it's supported, verify that bit DEVICE_CAPABILITY1_CONTROL_BITS1_VERSION2 is set in SMP_DEVICE_CAPABILITIES1 parameter
	#define SMP_CB1_START_HOMING BV(4)//write 1 here to start homing //not implemented at the moment
	#define SMP_CB1_FORCE_ENABLE BV(5)//writing & holding value 1 here will override lack of phyiscal enable signal (SMP_CB2_ENABLE). User can force device go in enabled state when both SMP_CB1_ENABLE and aSMP_CB1_FORCE_ENABLE are set.
	#define SMP_STATIC_CBS1 (SMP_CB1_ENABLE|SMP_CB1_BYPASS_TRAJPLANNER|SMP_CB1_FORCE_ENABLE)//list of controbits that are static by nature (not edge triggered functions)

#define SMP_CONTROL_BITS2 2534
	//bitfiled values:
	//both enables must be 1 to have drive enabled
	//Control bits 2 are controlled by physical inputs, dont try to write on these thu SMV2. reading is ok
	#define SMP_CB2_ENABLE BV(0)//hadrware enable pin
	#define SMP_CB2_ENA_POS_FEED BV(1)
	#define SMP_CB2_ENA_NEG_FEED BV(2)
	#define SMP_CB2_HOMESW_ON BV(3)
    #define SMP_CB2_CLEARFAULTS BV(4)
    #define SMP_CB2_START_HOMING BV(5)

//trajectory planner
#define SMP_TRAJ_PLANNER_ACCEL 800
#define SMP_TRAJ_PLANNER_STOP_DECEL 801
#define SMP_TRAJ_PLANNER_VEL 802
//accel limit for homing & ferror recovery
//if set to 0, use CFG_TRAJ_PLANNER_ACCEL instead (for backwards compatibility)
#define SMP_TRAJ_PLANNER_HOMING_ACCEL 803
#define SMP_TRAJ_PLANNER_HOMING_VEL 804
#define SMP_TRAJ_PLANNER_HOMING_BITS 806
	//homing config bit field. illegal to set Not to use INDEX or HOME
	#define HOMING_POS_INDEX_POLARITY BV(0)
	#define HOMING_POS_INDEX_SEARCH_DIRECTION BV(1)
	#define HOMING_POS_HOME_SWITCH_POLARITY BV(2)
	#define HOMING_USE_HOME_SWITCH BV(3)
	#define HOMING_USE_INDEX_PULSE BV(4)
	#define HOMING_HOME_AT_POWER_ON BV(5)
	#define HOMING_FULL_SPEED_OFFSET_MOVE BV(6)
	#define HOMING_ENABLED BV(7) /*if 0, homing cant be started */
	#define HOMING_RESET_POS_AND_SETPOINT_TO_ABSOLUTE_FBD_READING BV(8) /*if 1, init feedback & setpoint values froma absolute positions sensor + SMP_ABS_FBD_OFFSET. If enabled, this step is performed after index search. */
	#define _HOMING_CFG_MAX_VALUE 0x01ff

//defines from which direction & distance home switch will be approached for second time (eliminate switch hysteresis)
#define SMP_TRAJ_PLANNER_HOMING_SECOND_APPROACH_DISTANCE 807

//starting and stopping homing:
#define SMP_HOMING_CONTROL 7532


//hard stop triggered by follow error in encoder counts
#define SMP_TRAJ_PLANNER_HOMING_HARD_STOP_THRESHOLD 808
//32bit position offset in encoder counts, value must not overflow on divider*offset
#define SMP_TRAJ_PLANNER_HOMING_OFFSET 823
//signed 32bit absolute position limits, active after homing. disabled when both=0
#define SMP_ABSPOSITION_HI_LIMIT 835
#define SMP_ABSPOSITION_LO_LIMIT 836

//readouts
#define SMP_ACTUAL_BUS_VOLTAGE 900
#define SMP_ACTUAL_TORQUE 901
#define SMP_ACTUAL_VELOCITY_FB 902
#define SMP_ACTUAL_POSITION_FB 903
#define SMP_SCOPE_CHANNEL_VALUE 904
#define SMP_SCOPE_CHANNEL_SELECT 905
#define SMP_ACTUAL_POSITION_FB_NEVER_RESETTING 906 /*this is same than SMP_ACTUAL_POSITION_FB but does not reset to 0 on homing or init (it is always the original counter value at power-on)*/
#define SMP_RAW_QUADRATURE_ENCODER_POSITION_FB 907 /*This is always readable main quadrature encoder counter value, this is updated in all [FBD] settings, even when [FBD]=None. Useful for open loop stepper monitoring. */

#define SMP_MECH_BRAKE_RELEASE_DELAY 910
#define SMP_MECH_BRAKE_ENGAGE_DELAY 911
#define SMP_DYNAMIC_BRAKING_SPEED 912
#define SMP_BRAKE_STOP_ENGAGE_DELAY SMP_MECH_BRAKE_ENGAGE_DELAY /* SMP_BRAKE_STOP_ENGAGE_DELAY is old name, kept for compatibility*/

/*Special torque mode specific settings, currently IONI only. This paramterer is a bitfield
 * defined as: bits 0-7: option bits, bits 8-29: rotation angle limit (in feedback counts).
 *
 * Guide:
 * 0 = disabled, standard drive operation
 * nonzero = calculate value by: round(allowed_rotation_in_counts/256)*256 + modebits
 *
 * where modebits is a bitfield defined as:
 *    bit 0: input scaling. if true, analog input 0-5V sets torque scale to 10-100%. Note: cant use analog input for torque setpoint now.
 *    bits 1-7: reserved for future use.
 *
 * example 1: we want rotation limit of 8000 encoder counts (for each direction) and no scaling input, we
 * use value 7936.
 *
 * example 2: we want rotation limit of 8000 encoder counts (for each direction) and torque
 * command scaling input, we use value 7937

 * example 3: no rotation limit but just saling is on: use value 1
 *
 * Tip: use Granity Ctrl+P (Shortcut) dialog to set value:
 * http://granitedevices.com/wiki/Reading_and_writing_an_arbitrary_parameter_with_Granity
 */
#define SMP_TORQUE_MODE_ATTRIBUTES 920

//////////////////////////////////////////////////////////////////////////////////RUNTIME PARAMS 5000-9999

#define SMP_CAPTURE_SOURCE 5020
	//bitfield values (shift these with BV())
	#define CAPTURE_TORQUE_TARGET 1
	#define CAPTURE_TORQUE_ACTUAL 2
	#define CAPTURE_VELOCITY_TARGET 3
	#define CAPTURE_VELOCITY_ACTUAL 4
	#define CAPTURE_POSITION_TARGET 5
	#define CAPTURE_POSITION_ACTUAL 6
	#define CAPTURE_FOLLOW_ERROR 7
	#define CAPTURE_OUTPUT_VOLTAGE 8
	#define CAPTURE_BUS_VOLTAGE 9
	#define CAPTURE_STATUSBITS 10
	#define CAPTURE_FAULTBITS 11

	//rest are availalbe in debug/development firmware only:
	#define CAPTURE_PWM1 16
	#define CAPTURE_PWM2 17
	#define CAPTURE_PWM3 18
	#define CAPTURE_DEBUG1 19
	#define CAPTURE_DEBUG2 20
	#define CAPTURE_CURRENT1 21
	#define CAPTURE_CURRENT2 22
	#define CAPTURE_ACTUAL_FLUX 23
	#define CAPTURE_OUTPUT_FLUX 24
	#define CAPTURE_TARGET_FLUX 26
	#define CAPTURE_DEBUG3 27
	#define CAPTURE_DEBUG4 28

#define SMP_CAPTURE_TRIGGER 5011
	//choices:
	#define TRIG_NONE 0
	#define TRIG_INSTANT 1
	#define TRIG_FAULT 2
	#define TRIG_TARGETCHANGE 3
	#define TRIG_TARGETCHANGE_POS 4
	#define TRIG_EXTERNAL_INPUT 5
	#define TRIG_STATUSBITS_CHANGE 6
	#define TRIG_DEBUG1 7
	#define TRIG_DEBUG2 8

#define SMP_CAPTURE_SAMPLERATE 5012
//rdonly
#define SMP_CAPTURE_BUF_LENGHT 5013
//SMP_CAPTURE_BEFORE_TRIGGER_PERCENTS sets how much samples will be preserved before trigger event. Value 0 is traditional, +n starts capture n percents before trigger (relative to whole capture length), -n after trigger. Value range -1000000%..+100%.
#define SMP_CAPTURE_BEFORE_TRIGGER_PERCENTS 5014
#define SMP_CAPTURE_STATE 5015
	//note this parameter works differently in write and read directions. observer READ and WRITE tags below:
	#define CAPTURE_STATE_READ_IDLE 0 //scope doing nothing, ready for new capture
	#define CAPTURE_STATE_WRITE_START 1 //write 1 to start capturing with present settings
	#define CAPTURE_STATE_READ_WAITING_TRIGGER 1 //when reads 1, scope is waiting for trigger to occur
	#define CAPTURE_STATE_READ_BUSY 2 //scope capturing in progress
	#define CAPTURE_STATE_WRITE_START_WITH_SAMPLE_AVERAGING 3 //start capturing with sampling with averaging of samples  (useful with low sample rates, check if this feature is supported by capability bit DEVICE_CAPABILITY2_SUPPORT_SCOPE_SAMPLE_AVERAGING)
//this is looped 0-n to make samples 0-n readable from SMP_CAPTURE_BUFFER_GET_VALUE
#define SMP_CAPTURE_BUFFER_GET_ADDR 5333
#define SMP_CAPTURE_BUFFER_GET_VALUE 5334

//#define RUNTIME_FEATURES1 6000
#define SMP_SERIAL_NR 6002
#define SMP_UID_NR 6003

//read only bit field that is can be used to identify device capabilities
//the list below is subject to extend
//SMP_DEVICE_CAPABILITIES1 and SMP_DEVICE_CAPABILITIES2 are implemented on devices where SM protocol version is 28 or greater
#define SMP_DEVICE_CAPABILITIES1 6006
	#define DEVICE_CAPABILITY1_PMDC BV(0)
	#define DEVICE_CAPABILITY1_PMAC BV(1)
	#define DEVICE_CAPABILITY1_STEPPER BV(2)
	#define DEVICE_CAPABILITY1_TORQUE BV(3) /* true if device supports torque control  */
	#define DEVICE_CAPABILITY1_POSITIONING BV(4) /* true if device supports position control  */
	#define DEVICE_CAPABILITY1_VELOCITY BV(5) /* true if device supports velocity control  */
	#define DEVICE_CAPABILITY1_TRAJ_PLANNER BV(6) /* true if device has trajectory planner feature  */
	#define DEVICE_CAPABILITY1_HALLS BV(7) /* true if device supports hall sensor input */
	#define DEVICE_CAPABILITY1_INDEXER BV(8) /* true if device supports indexed (preprogrammed stored motion) feature */
	#define DEVICE_CAPABILITY1_HOMING BV(9) /* true if device supports homing function */
	#define DEVICE_CAPABILITY1_REF_PULSETRAIN BV(10) /* true if device has pulse & dir setpoint input */
	#define DEVICE_CAPABILITY1_REF_PWM BV(11) /* true if device has pwm setpoint input */
	#define DEVICE_CAPABILITY1_REF_ANALOG BV(12) /* true if device has analog setpoint input */
	#define DEVICE_CAPABILITY1_REF_QUADRATURE BV(13)
	#define DEVICE_CAPABILITY1_FB_QUADRATURE BV(14)
	#define DEVICE_CAPABILITY1_FB_SSI BV(15)
	#define DEVICE_CAPABILITY1_FB_BISS BV(16)
	#define DEVICE_CAPABILITY1_FB_SINCOS BV(17)
	#define DEVICE_CAPABILITY1_GEARING BV(18)
	#define DEVICE_CAPABILITY1_AUTOSETUP_COMMUTATION_SENSOR BV(19)
	#define DEVICE_CAPABILITY1_BUFFERED_MOTION_LINEAR_INTERPOLATION BV(20)
	#define DEVICE_CAPABILITY1_MOTOR_DRIVE BV(21) /*1 if device has motor drive capabilities, this flag is implemented on devices with SM protocol version 28 or greater */
	#define DEVICE_CAPABILITY1_FAULT_INFO_VALUES BV(22) /*1 if device supports parameters 8112 and 8113 */
	#define DEVICE_CAPABILITY1_SELECTABLE_FAST_UPDATE_CYCLE_FORMAT BV(23) /*1 if device supports parameter SMP_FAST_UPDATE_CYCLE_FORMAT */
	#define DEVICE_CAPABILITY1_CONTROL_BITS1_VERSION2 BV(24) /*drive implements CB1_QUICKSTOP_SET, CB1_QUICKSTOP_RELEASE, CB1_CLEARFAULTS, CB1_BYPASS_TRAJPLANNER bits in SMP_CONTROL_BITS1 */
	#define DEVICE_CAPABILITY1_SUPPORTS_STAT_STANDING_STILL BV(25) /*drive implements STAT_STANDING_STILL status bit */
	#define DEVICE_CAPABILITY1_COMMUTATION_CONFIG_V2 BV(26) /* new commutation sensor config  */
	#define DEVICE_CAPABILITY1_HAS_SECOND_SERIAL_ENCODER_PORT BV(27) /*true if device has two serial encoder inputs */
	#define DEVICE_CAPABILITY1_SUPPORTS_SMP_PARAMETER_PROPERTIES_MASK BV(28) /*true if support for SMP_PARAMETER_PROPERTIES_MASK */
	#define DEVICE_CAPABILITY1_HAS_TORQUE_OR_FORCE_CONSTANT_PARAMETER BV(29) /*true if SMP_TORQUE_OR_FORCE_CONSTANT parameter is supported */

//read only bit field that is can be used to identify device capabilities
//the list below is subject to extend
//SMP_DEVICE_CAPABILITIES1 and SMP_DEVICE_CAPABILITIES2 are implemented on devices where SM protocol version is 28 or greater
#define SMP_DEVICE_CAPABILITIES2 6007
	#define DEVICE_CAPABILITY2_RESTORE_SAVED_CONFIG BV(0)
	#define DEVICE_CAPABILITY2_MEASURE_RL BV(1)
	#define DEVICE_CAPABILITY2_TORQUE_RIPPLE_COMPENSATION BV(2)
	#define DEVICE_CAPABILITY2_NOTCH_FILTER BV(3)
	#define DEVICE_CAPABILITY2_TORQUE_EFFECTS BV(4)
	#define DEVICE_CAPABILITY2_SENSORLESS_COMMUTATION BV(5)
	#define DEVICE_CAPABILITY2_ANALOG_OUTPUT BV(6)
	#define DEVICE_CAPABILITY2_SCOPE_TRIGGER_DELAY BV(7) /*also means that params SMP_CAPTURE_BEFORE_TRIGGER_PERCENTS and SMP_CAPTURE_STATE exist */
	#define DEVICE_CAPABILITY2_SCOPE_EXTERNAL_TRIGGER BV(8)
	#define DEVICE_CAPABILITY2_SOUND_NOTIFICATIONS_FROM_MOTOR BV(9)
	#define DEVICE_CAPABILITY2_ASSIGN_HOME_AND_AUX_IO BV(10)
	#define DEVICE_CAPABILITY2_HOMING_SECOND_APPROACH BV(11)
	#define DEVICE_CAPABILITY2_UTILIZE_ABSOLUTE_FBD BV(12) /*supports homing by absolute feedback device info & can use absolute fbd as commutation sensor */
	#define DEVICE_CAPABILITY2_SUPPORT_DIGITAL_HALL_SENSOR_FBD BV(13)
    #define DEVICE_CAPABILITY2_SUPPORT_FORCE_CONTROL BV(14)
	#define DEVICE_CAPABILITY2_LOW_LEVEL_GPIO BV(15)
	#define DEVICE_CAPABILITY2_HAS_SMP_LIMIT_SW_FUNCTION_SOURCE BV(16) /*true if device supports parameter SMP_LIMIT_SW_FUNCTION_SOURCE*/
	#define DEVICE_CAPABILITY2_SUPPORT_FB2_AUX_ENCODER BV(17) /* true if secondary feedback device supported */
	#define DEVICE_CAPABILITY2_RETURN_SMP_STATUS_ON_FAILED_SUBPACKETS BV(18) /* if this is set, each SM subpacket that fails (status not SMP_CMD_STATUS_ACK), will return SMPRET_CMD_STATUS subpacket with the non-SMP_CMD_STATUS_ACK status. otherwise, user configured SM subpacket will be always returned */
	#define DEVICE_CAPABILITY2_SUPPORT_SCOPE_STATUSBITS_CHANGE_AND_DEBUG12_TRIGGERS BV(19) /* if this is set, scope supports TRIG_STATUSBITS_CHANGE and TRIG_DEBUG1 and TRIG_DEBUG2 */
	#define DEVICE_CAPABILITY2_TORQUE_BIQUAD_FILTERS_V1 BV(20) /* if this is set, then params 260-269 are supported */
	#define DEVICE_CAPABILITY2_SUPPORT_TRIGGER_PENDING_PARAMETER_ACTIVATION BV(21) /* if true, SMP_SYSTEM_CONTROL_TRIGGER_PENDING_PARAMETER_ACTIVATION is available */
	#define DEVICE_CAPABILITY2_HAS_STANDBY BV(22) // true if device enters in STAT_STANDBY automatically after idling
	#define DEVICE_CAPABILITY2_SUPPORT_SCOPE_SAMPLE_AVERAGING BV(23)

#define SMP_FIRMWARE_VERSION 6010
#define SMP_FIRMWARE_BACKWARDS_COMP_VERSION 6011
#define SMP_GC_FIRMWARE_VERSION 6014
#define SMP_GC_FIRMWARE_BACKWARDS_COMP_VERSION 6015
//added in ioni:
#define SMP_FIRMWARE_BUILD_REVISION 6016
#define SMP_DEVICE_TYPE 6020
#define SMP_PID_FREQUENCY 6055

/*plays sound effect on motor (if motor active). value=sound nr to be played*/
#define SMP_PLAY_SOUND_EFFECT 6070
/* safety function to activate high torque mode (in some drive models only).
 * use by reading this SMP first, then writing a correctly calculated response.
 * challenge changes every time a value is written.
 * if wrong answer or 0 is written, hi torq mode deactivates.
 */
#define SMP_ACTIVATE_HITORQ_MODE_CHALLENGE 6071

//affects only in MC_VECTOR & MC_BLDC mode
//used in drive development only, do not use if unsure
#define SMP_OVERRIDE_COMMUTATION_ANGLE 8000
#define SMP_TORQUE_CMD_OVERRIDE 8001
#define SMP_OVERRIDE_COMMUTATION_FREQ 8003
#define SMP_OVERRIDE_REGENRES_DUTY 8004
#define SMP_DEVICE_TEMPERATURE 8007//reported in 0.01 celsius steps
#define SMP_CURRENT_LIMITED_TO_MA 8008//actual current limit (based on user settings, device temperature, voltage etc)
#define SMP_CURRENT_LIMIT_REASON 8009 //last reason why current was clampled
	#define CURR_LIMIT_REASON_NONE 0
	#define CURR_LIMIT_REASON_VOLTAGE_SATURATION 1
	#define CURR_LIMIT_REASON_SETTINGS 2
	#define CURR_LIMIT_REASON_I2T 3
	#define CURR_LIMIT_REASON_DRIVE_TEMPERATURE 4
	#define CURR_LIMIT_REASON_POWER_CAP 5
	#define CURR_LIMIT_REASON_SAFETY 6

/*IO side CPU sends encoder counter at index every time index is encountered. homing uses this info */
#define SMP_INDEX_PULSE_LOCATION 8005


//GC side debug params
#define SMP_DEBUGPARAM1 8100
#define SMP_DEBUGPARAM2 8101
#define SMP_DEBUGPARAM3 8102
//for STM32 side override
#define SMP_DEBUGPARAM4 8103
#define SMP_DEBUGPARAM5 8104
#define SMP_DEBUGPARAM6 8105
#define SMP_FAULT_LOCATION1  8110 //this is the error code of main CPU of the device (i.e. main GranteCore CPU).
#define SMP_FAULT_LOCATION2  8111 //this is the error code for second CPU (i.e. Argon IO side CPU). always 0 on devices where there is no second CPU.
//Following two parameters 8112 and 8113 can be printed in formatted error messages string based on SMP_FAULT_LOCATION1 like
//"Undervoltage fault, measured %1 VDC was below the limit of %2 VDC" where %1
//and %2 are substituted by SMP_FAULT_LOCATION1_INFO_VALUE1 and SMP_FAULT_LOCATION1_INFO_VALUE2.
//Before using them, check that device has DEVICE_CAPABILITY1_FAULT_INFO_VALUES set.
#define SMP_FAULT_LOCATION1_INFO_VALUE1 8112 //additional info for fault of SMP_FAULT_LOCATION1.
#define SMP_FAULT_LOCATION1_INFO_VALUE2 8113 //additional info for fault of SMP_FAULT_LOCATION1
#define SMP_OVERRIDE_PARAM_ADDR_1 8120
#define SMP_OVERRIDE_PARAM_ADDR_2 8121
#define SMP_OVERRIDE_PARAM_ADDR_3 8122
#define SMP_OVERRIDE_PARAM_VAL_1 8125
#define SMP_OVERRIDE_PARAM_VAL_2 8126
#define SMP_OVERRIDE_PARAM_VAL_3 8127


/*DFU Bootloader parameters begin:*/
//SMP_FAULTS bitfield bits:
#define FLT_INVALID_FW BV(5)
#define FLT_FLASHING_FAIL BV(6)
#define FLT_FLASHING_COMMSIDE_FAIL BV(7)
//SMP_STATUS bitfield bits:
#define STAT_GC_FLASHED_ONCE BV(1)//to prevent flashing it twice in same GCBL session which causes error

//write a value here to execute a bootloader function
#define SMP_BOOTLOADER_FUNCTION 191
//upload 16bits of data to buffer. max buffer length 4096 bytes before it must be written with "write block" function
//total uploaded amount must be multiple of 4 bytes before issuing write function
#define SMP_BOOTLOADER_UPLOAD 192
//bootloaded status
#define SMP_BOOTLOADER_STAT 193
        #define BOOTLOADER_STAT_FLASH_VERIFIED_OK BV(0)
#define SMP_BOOTLOADER_WRITE_OTP 194


/*DFU Bootloader parameters end*/



#endif /* SIMPLEMOTION_DEFS_H_ */
