//Global SimpleMotion functions & definitions
//Copyright (c) Granite Devices Oy

#ifndef SIMPLEMOTION_H
#define SIMPLEMOTION_H

#if defined(BUILD_DLL) && (defined(_WIN64) || defined(_WIN32))
	//dll specs
#define LIB __declspec(dllexport)
#else
#define LIB
#endif

#include <stdio.h>
#include <stdint.h>
#include "simplemotion_defs.h"
#include "simplemotion_types.h"


#ifdef __cplusplus
extern "C"{
#endif


//BusdeviceOpen callback should return this if port open fails (in addition to setting *success to smfalse):
#define SMBUSDEVICE_RETURN_ON_OPEN_FAIL NULL


//max number of simultaneously opened buses. change this and recompiple SMlib if
//necessary (to increase channels or reduce to save memory)
//#define SM_MAX_BUSES 5
///////////////////////////////////////////////////////////////////////////////////////
//FUNCTIONS////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////

/** Open SM RS485 communication bus. Parameters:
    -devicename: formatted string that depend on device type to attempt opening. Supported formats/drivers:
    --Serial port device:
    ---on Windows: COMn where n=port number, i.e. COM2
    ---on Linux: /dev/ttyN where N=port name, i.e. /dev/ttyUSB0 or /dev/ttyS0
    ---on macOS: /dev/tty.cuN where N=port name
    --TCP/IP socket: format is nnn.nnn.nnn.nnn:pppp where n is IP address numbers and p is port number
    --FTDI USB serial port (FTDI D2XX API, availablity depends whether library has been compiled with FTDI support enabled, see SimpleMotionV2.pri):
    ---Opening by device index: FTDIn where n=index (0 or greater)
    ---Opening by device description (programmed in FTDI EEPROM): raw name, i.e. USB-SMV2 or TTL232R (hint: name is displayed in Granity 1.14 or later)
    ---Hint: D2XX driver supports listing available devices. See: smGetNumberOfDetectedBuses() and smGetBusDeviceDetails()
	-return value: handle to be used with all other commands, -1 if fails
	*/
LIB smbus smOpenBus( const char * devicename );

/** Same as smOpenBus but with user supplied port driver callbacks */
LIB smbus smOpenBusWithCallbacks( const char *devicename, BusdeviceOpen busOpenCallback, BusdeviceClose busCloseCallback, BusdeviceReadBuffer busReadCallback, BusdeviceWriteBuffer busWriteCallback );

/** Change baudrate of SM communication port. This does not affect already opened ports but the next smOpenBus will be opened at the new speed. 
	Calling this is optional. By default SM bus and all slave devices operates at 460800 BPS speed.
	Parameters:
	-bps: bus speed in bits per second. for possible choices, see rs232.c (but note that all speeds are not necessarily supported by SM devices)
	Typical usage is:
	- first call smSetParameter(handle,0,SMP_BUS_SPEED,N) to change speed of all connected slaves to N PBS
	- then close port with smCloseBus
	- then call smSetBaudrate(N)
	- then open bus again with smOpenBus

    The above method does not utilize very useful SM watchog feature that allows securely re-connecting in case of lost connection to the target devices.
    For defails, see example at https://granitedevices.com/wiki/Changing_SimpleMotion_baud_rate
	*/
LIB void smSetBaudrate( unsigned long pbs );

/** Set timeout of how long to wait reply packet from bus. Must be set before smOpenBus and cannot be changed afterwards
 * max value 5000ms. Range may depend on underyling OS / drivers. If supplied argument is lower than minimum supported by drivers,
 * then driver minimum is used without notice (return SM_OK).
 *
 * In unix PC serial port minimum is 100ms, on Windows serial port recommended minimum is 30ms and with FTDI driver 10ms. On TCP/IP: TBD.
 *
 *This is the only function that returns SM_STATUS which doesn't accumulate status bits to be read with getCumulativeStatus because it has no bus handle
 */
LIB SM_STATUS smSetTimeout( smuint16 millsecs );

/** Close connection to given bus handle number. This frees communication link therefore makes it available for other apps for opening.
  -return value: a SM_STATUS value, i.e. SM_OK if command succeed
*/
LIB SM_STATUS smCloseBus( const smbus bushandle );


/** Return SM lib version number in hexadecimal format.
Ie V 2.5.1 would be 0x020501 and 1.2.33 0x010233 */
LIB smuint32 smGetVersion();


/** Set stream where debug output is written. By default nothing is written.
smVerbosityLevel:
 * SMDebugOff=no debug prints (default)
 * SMDebugLow=only some excepetion/errors printed
 * SMDebugMid=some common function calls printed
 * SMDebugHigh=more details of function calls/bus communication printed
 * SMDebugTrace=print all raw RX/TX data and parsed read values of RX data
 *
 * NOTE: for debug prints to work, SM library must be compiled with ENABLE_DEBUG_PRINTS defined (i.e. uncomment
 * that definition from simplemotion.h or define it application wide with compiler flag, i.e. -DENABLE_DEBUG_PRINTS).
 * Enabling it may slow down & grow binary significantly especially on MCU systems.
 */
LIB void smSetDebugOutput( smVerbosityLevel level, FILE *stream );

/** This function returns all occurred SM_STATUS bits after smOpenBus or resetCumulativeStatus call*/
LIB SM_STATUS getCumulativeStatus( const smbus handle );
/** Reset cululative status so getCumultiveStatus returns 0 after calling this until one of the other functions are called*/
LIB SM_STATUS resetCumulativeStatus( const smbus handle );


/** SMV2 Device communication functionss */
LIB SM_STATUS smAppendCommandToQueue( smbus handle, smuint8 cmdid, smuint16 param  );
LIB SM_STATUS smExecuteCommandQueue( const smbus bushandle, const smaddr targetaddress );
LIB smuint16  smGetQueuedCommandReturnValue(  const smbus bushandle, smuint16 cmdnumber );

LIB SM_STATUS smUploadCommandQueueToDeviceBuffer( const smbus bushandle, const smaddr targetaddress );
LIB SM_STATUS smBytesReceived( const smbus bushandle, smint32 *bytesinbuffer );

LIB SM_STATUS smAppendSMCommandToQueue( smbus handle, int smpCmdType, smint32 paramvalue  );
LIB SM_STATUS smGetQueuedSMCommandReturnValue(  const smbus bushandle, smint32 *retValue );

LIB SM_STATUS smAppendGetParamCommandToQueue( smbus handle, smint16 paramAddress );
LIB SM_STATUS smGetQueuedGetParamReturnValue(  const smbus bushandle, smint32 *retValue  );
LIB SM_STATUS smAppendSetParamCommandToQueue( smbus handle, smint16 paramAddress, smint32 paramValue );
LIB SM_STATUS smGetQueuedSetParamReturnValue(  const smbus bushandle, smint32 *retValue  );

/** Simple read & write of parameters with internal queueing, so only one call needed.
Use these for non-time critical operations. */
LIB SM_STATUS smRead1Parameter( const smbus handle, const smaddr nodeAddress, const smint16 paramId1, smint32 *paramVal1 );
LIB SM_STATUS smRead2Parameters( const smbus handle, const smaddr nodeAddress, const smint16 paramId1, smint32 *paramVal1,const smint16 paramId2, smint32 *paramVal2 );
LIB SM_STATUS smRead3Parameters( const smbus handle, const smaddr nodeAddress, const smint16 paramId1, smint32 *paramVal1,const smint16 paramId2, smint32 *paramVal2 ,const smint16 paramId3, smint32 *paramVal3 );
LIB SM_STATUS smSetParameter( const smbus handle, const smaddr nodeAddress, const smint16 paramId, smint32 paramVal );


LIB SM_STATUS smGetBufferClock( const smbus handle, const smaddr targetaddr, smuint16 *clock );

/** smFastUpdateCycleWithStructs uses special SimpleMotion command to perform fast turaround communication. May be used with cyclic real time control.
 * smFastUpdateCycleWithStructs has been desniged to have lowest possible response time.
 * Typically the worst case response is 50 microseconds, which makes it to achieve up to 20kHz call rate. This may be useful especially when using external
 * closed loop and controlling motor torque or velocity in real time.
 *
 * Parameters write and read are unions and contain several bit field arrangements.
 * The format mode should be set by setting SMP_FAST_UPDATE_CYCLE_FORMAT value before calling this function.
*/
LIB SM_STATUS smFastUpdateCycleWithStructs( smbus handle, smuint8 nodeAddress, FastUpdateCycleWriteData write, FastUpdateCycleReadData *read);


/** smFastUpdateCycle is similar to smFastUpdateCycleWithStructs with raw integer inputs and outputs instead of structures.
 * This is deprecated, consider using smFastUpdateCycleWithStructs instead.
*/
LIB SM_STATUS smFastUpdateCycle( smbus handle, smuint8 nodeAddress, smuint16 write1, smuint16 write2, smuint16 *read1, smuint16 *read2);

/** Return number of bus devices found. details of each device may be consequently fetched by smGetBusDeviceDetails() */
LIB smint smGetNumberOfDetectedBuses();

/** Fetch information of detected bus nodes at certain index. Example:

    smint num=smGetNumberOfDetectedBuses();
    for(int i=0;i<num;i++)
    {
        SM_BUS_DEVICE_INFO info;
        SM_STATUS s=smGetBusDeviceDetails(i,&info);
        if(s==SM_OK)
        {
            ...do something with info...
        }
        else
        {
            ...report error...
        }
    }
*/
LIB SM_STATUS smGetBusDeviceDetails( smint index, SM_BUS_DEVICE_INFO *info );

#ifdef __cplusplus
}
#endif
#endif // SIMPLEMOTION_H
