
//Copyright (c) Granite Devices Oy

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "busdevice.h"
#include "user_options.h"
#include "sm485.h"
#include <stdarg.h>


#include "simplemotion_private.h"

SM_STATUS smParseReturnData( smbus handle, uint8_t data );

#define HANDLE_STAT(stat) if(stat!=SM_OK)return (stat);
#define HANDLE_STAT_AND_RET(stat,returndata) { if(returndata==RET_INVALID_CMD||returndata==RET_INVALID_PARAM) return SM_ERR_PARAMETER; if(stat!=SM_OK) return (stat); }

enum RecvState {WaitCmdId,WaitAddr,WaitPayloadSize,WaitPayload,WaitCrcHi,WaitCrcLo};
FILE *smDebugOut=NULL;

//useful macros from extracting/storing multibyte values from/to byte buffer
#define bufput32bit(buf, pos, val) *((uint32_t*)(uint8_t*)((buf)+(pos)))=((uint32_t)(val))
#define bufput16bit(buf, pos, val) *((uint16_t*)(uint8_t*)((buf)+(pos)))=((uint16_t)(val))
#define bufput8bit(buf, pos, val) *((uint8_t*)(uint8_t*)((buf)+(pos)))=((uint8_t)(val))
#define bufget32bit(buf, pos) (*((uint32_t*)(uint8_t*)((buf)+(pos))))
#define bufget16bit(buf, pos) (*((uint16_t*)(uint8_t*)((buf)+(pos))))
#define bufget8bit(buf, pos) (*((uint8_t*)(uint8_t*)((buf)+(pos))))


bool smIsHandleOpen( const smbus handle );

SM_STATUS smReceiveReturnPacket( smbus bushandle );

typedef struct SM_BUS_
{
    smbusdevicehandle bdHandle;
    bool opened;

    enum RecvState recv_state,recv_state_next;

    int16_t recv_payloadsize;
    int16_t recv_storepos;
    uint8_t recv_rsbuf[SM485_RSBUFSIZE];
    uint8_t recv_cmdid;
    uint8_t recv_addr;
    uint16_t recv_crc;
    uint16_t recv_read_crc_hi;
    bool receiveComplete;
    bool transmitBufFull;//set true if user uploads too much commands in one SM transaction. if true, on execute commands, nothing will be sent to bus to prevent unvanted clipped commands and buffer will be cleared
    char busDeviceName[SM_BUSDEVICENAME_LEN];

    int16_t cmd_send_queue_bytes;//for queued device commands
    int16_t cmd_recv_queue_bytes;//recv_queue_bytes counted upwards at every smGetQueued.. and compared to payload size


    SM_STATUS cumulativeSmStatus;
} SM_BUS;


SM_BUS smBus[SM_MAX_BUSES];
uint16_t readTimeoutMs=SM_READ_TIMEOUT;

//init on first smOpenBus call
bool smInitialized=false;

//if debug message has priority this or above will be printed to debug stream
smVerbosityLevel smDebugThreshold=SMDebugTrace;

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
void smSleepMs(int millisecs)
{
    usleep(millisecs*1000);
}

#elif defined(_WIN32) || defined(WIN32)
#include <windows.h>
void smSleepMs(int millisecs)
{
    Sleep(millisecs);
}
#else
#warning Make sure to implement own smSleepMs function for your platform as it is not one of supported ones (unix/win). For more info, see simplemotion_private.h.
#endif


extern const char *smDebugPrefixString;
extern const char *smDebugSuffixString;

#ifdef ENABLE_DEBUG_PRINTS
void smDebug( smbus handle, smVerbosityLevel verbositylevel, char *format, ...)
{
    va_list fmtargs;
    char buffer[1024];

    if(smDebugOut!=NULL && verbositylevel <= smDebugThreshold )
    {
        #ifdef SM_ENABLE_DEBUG_PREFIX_STRING //user app may define this macro if need to write custom prefix, if defined, then define also "const char *smDebugPrefixString="my string";" somewhere in your app.
        fprintf(smDebugOut, smDebugPrefixString);
        #endif

        va_start(fmtargs,format);
        vsnprintf(buffer,sizeof(buffer)-1,format,fmtargs);
        va_end(fmtargs);
        if(handle>=0)
        {
            if(smIsHandleOpen(handle))
            {
                fprintf(smDebugOut,"%s: %s",smBus[handle].busDeviceName, buffer);
            }
            else if(handle==DEBUG_PRINT_RAW)
            {
                fprintf(smDebugOut,"%s", buffer);
            }
            else
            {
                fprintf(smDebugOut,"(bad smbus handle): %s", buffer);
            }
        }
        else
            fprintf(smDebugOut,"SMLib: %s",buffer);//no handle given

        #ifdef SM_ENABLE_DEBUG_SUFFIX_STRING //user app may define this macro if need to write custom suffix, if defined, then define also "const char *smDebugSuffixString="my string";" somewhere in your app.
        fprintf(smDebugOut, smDebugSuffixString);
        #endif
    }
}
#endif

void smResetSM485variables(smbus handle)
{
    smBus[handle].recv_state=WaitCmdId;
    smBus[handle].recv_state_next=WaitCmdId;
    smBus[handle].recv_payloadsize=-1;
    smBus[handle].recv_storepos=0;//number of bytes to expect data in cmd, -1=wait cmd header
    smBus[handle].recv_cmdid=0;// cmdid=0 kun ed komento suoritettu
    smBus[handle].recv_addr=255;
    smBus[handle].recv_crc=SM485_CRCINIT;
    smBus[handle].recv_read_crc_hi=0xffff;//bottom bits will be contains only 1 byte when read
    smBus[handle].receiveComplete=false;
    smBus[handle].transmitBufFull=false;
    smBus[handle].cmd_send_queue_bytes=0;
    smBus[handle].cmd_recv_queue_bytes=0;
}

uint16_t calcCRC16(uint8_t data, uint16_t crc)
{
    unsigned int i; /* will index into CRC lookup */

    i = (crc>>8) ^ data; /* calculate the CRC  */
    crc = (((crc&0xff) ^ table_crc16_hi[i])<<8) | table_crc16_lo[i];

    return crc;
}

uint16_t calcCRC16Buf(const char *buffer, uint16_t buffer_length)
{
    uint8_t crc_hi = 0xFF; /* high CRC byte initialized */
    uint8_t crc_lo = 0xFF; /* low CRC byte initialized */
    unsigned int i; /* will index into CRC lookup */

    /* pass through message buffer */
    while (buffer_length--) {
        i = crc_hi ^ *buffer++; /* calculate the CRC  */
        crc_hi = crc_lo ^ table_crc16_hi[i];
        crc_lo = table_crc16_lo[i];
    }

    return (crc_hi << 8 | crc_lo);
}

uint8_t calcCRC8Buf( uint8_t *buf, int len, int crcinit )
{
    int i;
    uint8_t crc=crcinit;

    for(i=0;i<len;i++)
    {
            crc=table_crc8[crc^buf[i]];
    }

    return crc;
}

SM_STATUS smSetTimeout( uint16_t millsecs )
{
    if(millsecs<=5000 && millsecs>=1)
    {
        readTimeoutMs=millsecs;
        return SM_OK;
    }
    return SM_ERR_PARAMETER;
}

uint32_t smGetVersion()
{
    return SM_VERSION;
}


//init bus struct table
void smBusesInit()
{
    int i;
    for(i=0;i<SM_MAX_BUSES;i++)
    {
        smBus[i].opened=false;;
        smResetSM485variables(i);
    }
    smInitialized=true;
}

bool smIsHandleOpen( const smbus handle )
{
    if(handle<0) return false;
    if(handle>=SM_MAX_BUSES) return false;
    return smBus[handle].opened;
}


/** Open SM RS485 communication bus. Parameters:
-devicename: on Windows COM port as "COMx" or on unix /dev/ttySx or /dev/ttyUSBx where x=port number
-return value: integer handle to be used with all other commands, -1 if fails
*/
smbus smOpenBus( const char * devicename )
{
    int handle;

    //true on first call
    if(!smInitialized)
        smBusesInit();

    //find free handle
    for(handle=0;handle<SM_MAX_BUSES;handle++)
    {
        if(!smBus[handle].opened) break;//choose this
    }
    //all handles in use
    if(handle>=SM_MAX_BUSES) return -1;

    //open bus device
    smBus[handle].bdHandle=smBDOpen(devicename);
    if(smBus[handle].bdHandle==-1) return -1;

    //success
    strncpy( smBus[handle].busDeviceName, devicename, SM_BUSDEVICENAME_LEN );
    smBus[handle].busDeviceName[SM_BUSDEVICENAME_LEN-1]=0;//null terminate string
    smBus[handle].opened=true;
    return handle;
}

/** same as smOpenBus but with user supplied port driver callbacks */
smbus smOpenBusWithCallbacks( const char *devicename, BusdeviceOpen busOpenCallback, BusdeviceClose busCloseCallback, BusdeviceReadBuffer busReadCallback, BusdeviceWriteBuffer busWriteCallback, BusdeviceMiscOperation busMiscOperationCallback )
{
    int handle;

    //true on first call
    if(!smInitialized)
        smBusesInit();

    //find free handle
    for(handle=0;handle<SM_MAX_BUSES;handle++)
    {
        if(!smBus[handle].opened) break;//choose this
    }
    //all handles in use
    if(handle>=SM_MAX_BUSES) return -1;

    //open bus device
    smBus[handle].bdHandle=smBDOpenWithCallbacks(devicename, busOpenCallback, busCloseCallback, busReadCallback, busWriteCallback, busMiscOperationCallback );
    if(smBus[handle].bdHandle==-1) return -1;

    //success
    strncpy( smBus[handle].busDeviceName, devicename, SM_BUSDEVICENAME_LEN );
    smBus[handle].busDeviceName[SM_BUSDEVICENAME_LEN-1]=0;//null terminate string
    smBus[handle].opened=true;
    return handle;
}


/** Change baudrate of SM communication port. This does not affect already opened ports but the next smOpenBus will be opened at the new speed.
    Calling this is optional. By default SM bus and all slave devices operates at 460800 BPS speed.
    Parameters:
    -bps: bus speed in bits per second. for possible choices, see rs232.c (but note that all speeds are not necessarily supported by SM devices)
    Typical usage is:
    - first call smSetParameter(handle,0,SMP_BUS_SPEED,N) to change speed of all connected slaves to N PBS
    - then close port with smCloseBus
    - then call smSetBaudrate(N)
    - then open bus again with smOpenBus

    Note that in upcoming SM device firmware versions, bitrate will be reset to default (460800) if device side SM bus watchdog timer has been enabled, and it timeouts.
    This allows re-establishing connection at defautl speed if connection breaks up and SM bus watchdog timeout gets exceeded. To identify is device supports this,
    read parameter SMP_SM_VERSION. Values above 25 support this feature. Value 25 and below will not reset baudrate.

    Note also that SMP_BUS_SPEED will not be saved in device flash memory - it will reset to default at every reset & power on.
    */
LIB void smSetBaudrate( unsigned long pbs )
{
    SMBusBaudrate=pbs;
}

/** Close connection to given bus handle number. This frees communication link therefore makes it available for other apps for opening.
-return value: a SM_STATUS value, i.e. SM_OK if command succeed
*/
LIB SM_STATUS smCloseBus( const smbus bushandle )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return recordStatus(bushandle,SM_ERR_NODEVICE);

    smBus[bushandle].opened=false;

    if(!smBDClose(smBus[bushandle].bdHandle)) return recordStatus(bushandle,SM_ERR_BUS);

    return SM_OK;
}

/** Clear pending (stray) bytes in bus device reception buffer and reset receiver state. This may be needed i.e. after restarting device to eliminate clitches that appear in serial line.
  -return value: a SM_STATUS value, i.e. SM_OK if command succeed
*/
LIB SM_STATUS smPurge( const smbus bushandle )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return recordStatus(bushandle,SM_ERR_NODEVICE);

    if(smBDMiscOperation( bushandle, MiscOperationPurgeRX ))
        return recordStatus(bushandle,SM_OK);
    else
        return recordStatus(bushandle,SM_ERR_BUS);
}

/** Block until pending TX bytes are phyiscally out. Max blocking time is same that is set with smSetTimeout
  -return value: a SM_STATUS value, i.e. SM_OK if command succeed
*/
LIB SM_STATUS smFlushTX( const smbus bushandle )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return recordStatus(bushandle,SM_ERR_NODEVICE);

    if(smBDMiscOperation( bushandle, MiscOperationFlushTX ))
        return recordStatus(bushandle,SM_OK);
    else
        return recordStatus(bushandle,SM_ERR_BUS);
}


char *cmdidToStr(uint8_t cmdid )
{
    char *str;
    switch(cmdid)
    {
    case SMCMD_INSTANT_CMD : str="SMCMD_INSTANT_CMD";break;
    case SMCMD_INSTANT_CMD_RET :str="SMCMD_INSTANT_CMD_RET";break;
    case SMCMD_BUFFERED_CMD :str="SMCMD_BUFFERED_CMD";break;
    case SMCMD_BUFFERED_RETURN_DATA :str="SMCMD_BUFFERED_RETURN_DATA";break;
    case SMCMD_BUFFERED_RETURN_DATA_RET :str="SMCMD_BUFFERED_RETURN_DATA_RET";break;
    case SMCMD_BUFFERED_CMD_RET:str="SMCMD_BUFFERED_CMD_RET";break;
#ifdef PROCESS_IMAGE_SUPPORT
    case SMCMD_PROCESS_IMAGE:str="SMCMD_PROCESS_IMAGE";break;
    case SMCMD_PROCESS_IMAGE_RET: str="SMCMD_PROCESS_IMAGE_RET";break;
#endif
    case SMCMD_GET_CLOCK: str="SMCMD_GET_CLOCK";break;
    case SMCMD_GET_CLOCK_RET :str="SMCMD_GET_CLOCK_RET";break;
    case SMCMD_FAST_UPDATE_CYCLE:str="SMCMD_FAST_UPDATE_CYCLE";break;
    case SMCMD_FAST_UPDATE_CYCLE_RET:str="SMCMD_FAST_UPDATE_CYCLE_RET";break;
    default: str="unknown cmdid";break;
    }
    //puts(str);
    return str;
}

//write one byte to tx buffer
//returns true on success
bool smWriteByte( const smbus handle, const uint8_t byte, uint16_t *crc )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    bool success=smBDWrite(smBus[handle].bdHandle,byte);
    if(crc!=NULL)
        *crc = calcCRC16(byte,*crc);

    return success;
}

//write tx buffer to bus
//returns true on success
bool smTransmitBuffer( const smbus handle )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    bool success=smBDTransmit(smBus[handle].bdHandle);
    return success;
}

SM_STATUS smSendSMCMD( smbus handle, uint8_t cmdid, uint8_t addr, uint8_t datalen, uint8_t *cmddata )
{
    int i;
    uint16_t sendcrc;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    sendcrc=SM485_CRCINIT;

    smDebug(handle, SMDebugHigh, "> %s (id=%d, addr=%d, payload=%d)\n",cmdidToStr(cmdid),cmdid,
            addr,
            datalen);


    smDebug(handle,SMDebugHigh,"  Outbound packet raw data: CMDID (%d) ",cmdid);
    if(!smWriteByte(handle,cmdid, &sendcrc)) return recordStatus(handle,SM_ERR_BUS);

    if(cmdid&SMCMD_MASK_N_PARAMS)
    {
        smDebug(DEBUG_PRINT_RAW,SMDebugHigh,"SIZE (%d bytes) ", datalen);
        if(!smWriteByte(handle,datalen, &sendcrc)) return recordStatus(handle,SM_ERR_BUS);
    }

    smDebug(DEBUG_PRINT_RAW,SMDebugHigh,"ADDR (%d) ",addr);
    if(!smWriteByte(handle,addr, &sendcrc)) return recordStatus(handle,SM_ERR_BUS);

    smDebug(DEBUG_PRINT_RAW,SMDebugHigh,"PAYLOAD (");
    for(i=0;i<datalen;i++)
    {
        smDebug(DEBUG_PRINT_RAW,SMDebugHigh,"%02x ",cmddata[i]);
        if(!smWriteByte(handle,cmddata[i], &sendcrc)) return recordStatus(handle,SM_ERR_BUS);
    }
    smDebug(DEBUG_PRINT_RAW,SMDebugHigh,") ");
    smDebug(DEBUG_PRINT_RAW,SMDebugHigh,"CRC (%02x %02x)\n",sendcrc>>8, sendcrc&0xff);
    if(!smWriteByte(handle,sendcrc>>8, NULL)) return recordStatus(handle,SM_ERR_BUS);
    if(!smWriteByte(handle,sendcrc&0xff,NULL)) return recordStatus(handle,SM_ERR_BUS);

    //transmit bytes to bus that were written in buffer by smWriteByte calls
    if(!smTransmitBuffer(handle)) return recordStatus(handle,SM_ERR_BUS);

    return recordStatus(handle,SM_OK);
}

LIB SM_STATUS smFastUpdateCycleWithStructs( smbus handle, uint8_t nodeAddress, FastUpdateCycleWriteData write, FastUpdateCycleReadData *read)
{
    return smFastUpdateCycle( handle, nodeAddress, write.U16[0], write.U16[1], &read->U16[0], &read->U16[1]);
}


SM_STATUS smFastUpdateCycle( smbus handle, uint8_t nodeAddress, uint16_t write1, uint16_t write2, uint16_t *read1, uint16_t *read2)
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    smDebug(handle, SMDebugHigh, "> %s (addr=%d, w1=%d, w2=%d)\n",cmdidToStr(SMCMD_FAST_UPDATE_CYCLE),
            nodeAddress,
            write1,write2);


    //form the tx packet
    uint8_t cmd[8];
    int i;
    cmd[0]=SMCMD_FAST_UPDATE_CYCLE;
    cmd[1]=nodeAddress;
    bufput16bit(cmd,2,write1);
    bufput16bit(cmd,4,write2);
    cmd[6]=calcCRC8Buf(cmd,6,0x52);

    //send
    for(i=0;i<7;i++)
    {
        if(!smWriteByte(handle,cmd[i], NULL))
            return recordStatus(handle,SM_ERR_BUS);
    }
    smTransmitBuffer(handle);//this sends the bytes entered with smWriteByte

    smDebug(handle, SMDebugHigh, "  Reading reply packet\n");
    for(i=0;i<6;i++)
    {
        bool success;
        uint8_t rx;
        success=smBDRead(smBus[handle].bdHandle,&rx);
        cmd[i]=rx;
        if(!success)
        {
            smDebug(handle,SMDebugLow,"Not enough data received on smFastUpdateCycle");
            return recordStatus(handle,SM_ERR_BUS|SM_ERR_LENGTH);//no enough data received
        }
    }

    //parse
    uint8_t localCRC=calcCRC8Buf(cmd,5,0x52);
    if( cmd[5]!=localCRC|| cmd[0]!=SMCMD_FAST_UPDATE_CYCLE_RET )
    {
        smDebug(handle,SMDebugLow,"Corrupt data received on smFastUpdateCycle. RX CRC %02x (expected %02x), RX ID %02x, (expected %02x)\n",cmd[5],localCRC,cmd[0],SMCMD_FAST_UPDATE_CYCLE_RET);
        return recordStatus(handle,SM_ERR_COMMUNICATION);//packet error
    }
    if(read1!=NULL)
        *read1=bufget16bit(cmd,1);
    if(read2!=NULL)
        *read2=bufget16bit(cmd,3);

    //return data read complete
    smDebug(handle,SMDebugHigh, "< %s (id=%d, r1=%d, r2=%d)\n",
            cmdidToStr( cmd[0] ),
            cmd[0],*read1,*read2);

    return recordStatus(handle,SM_OK);
}



SM_STATUS smReceiveErrorHandler( smbus handle, bool flushrx )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;


    //empty pending rx buffer to avoid further parse errors
    if(flushrx)
    {
        bool success;
        do{
            uint8_t rx;
            success=smBDRead(smBus[handle].bdHandle,&rx);
        }while(success);
    }
    smResetSM485variables(handle);
    smBus[handle].receiveComplete=true;
    return recordStatus(handle,SM_ERR_COMMUNICATION);
}


SM_STATUS smAppendSMCommandToQueue( smbus handle, int smpCmdType,int32_t paramvalue  )
{
    int cmdlength;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    switch(smpCmdType)
    {
    case SMPCMD_SETPARAMADDR:
        cmdlength=2;
        break;
    case SMPCMD_24B:
        cmdlength=3;
        break;
    case SMPCMD_32B:
        cmdlength=4;
        break;
    default:
        return recordStatus(handle,SM_ERR_PARAMETER);
        break;
    }

    //check if space if buffer
    if(smBus[handle].cmd_send_queue_bytes>(SM485_MAX_PAYLOAD_BYTES-cmdlength) )
    {
        smBus[handle].transmitBufFull=true; //when set true, smExecute will do nothing but clear transmit buffer. so this prevents any of overflowed commands getting thru
        return recordStatus(handle,SM_ERR_LENGTH); //overflow, too many commands in buffer
    }

    if(smpCmdType==SMPCMD_SETPARAMADDR)
    {
        SMPayloadCommand16 newcmd;
        newcmd.ID=SMPCMD_SETPARAMADDR;
        newcmd.param=paramvalue;
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[1]);
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[0]);
    }
    if(smpCmdType==SMPCMD_24B)
    {
        SMPayloadCommand24 newcmd;
        newcmd.ID=SMPCMD_24B;
        newcmd.param=paramvalue;
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[2]);
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[1]);
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[0]);
    }
    if(smpCmdType==SMPCMD_32B)
    {
        SMPayloadCommand32 newcmd;
        newcmd.ID=SMPCMD_32B;
        newcmd.param=paramvalue;
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[3]);
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[2]);
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[1]);
        bufput8bit( smBus[handle].recv_rsbuf, smBus[handle].cmd_send_queue_bytes++, ((unsigned char*)&newcmd)[0]);
    }


    return recordStatus(handle,SM_OK);
}


SMPayloadCommandRet32 smConvertToPayloadRet32_16(SMPayloadCommandRet16 in)
{
    SMPayloadCommandRet32 out;
    out.ID=in.ID;
    out.retData=(long)in.retData; //negative value, extend ones
    return out;
}

//for library internal use only
SM_STATUS smTransmitReceiveCommandQueue( const smbus bushandle, const smaddr targetaddress, uint8_t cmdid )
{
    SM_STATUS stat;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return recordStatus(bushandle,SM_ERR_NODEVICE);

    if(!smBus[bushandle].transmitBufFull) //dont send/receive commands if queue was overflowed by user error
    {
        stat=smSendSMCMD(bushandle,cmdid,targetaddress, smBus[bushandle].cmd_send_queue_bytes, smBus[bushandle].recv_rsbuf ); //send commands to bus
        if(stat!=SM_OK) return recordStatus(bushandle,stat);
    }

    smBus[bushandle].cmd_send_queue_bytes=0;
    smBus[bushandle].cmd_recv_queue_bytes=0;//counted upwards at every smGetQueued.. and compared to payload size

    if(!smBus[bushandle].transmitBufFull && targetaddress!=0)//dont send/receive commands if queue was overflowed by user error, or if target is broadcast address (0) where no slave will respond and it's ok
    {
        stat=smReceiveReturnPacket(bushandle);//blocking wait & receive return values from bus
        if(stat!=SM_OK) return recordStatus(bushandle,stat); //maybe timeouted
    }
    if(targetaddress==0)
    {
        //make sure we don't return function before all data is really sent as we're not waiting for RX data
        //note: we're note checking return value of it as some driver's dont support this atm and will return false. TODO fix this & drivers.
        smFlushTX(bushandle);
    }

    smBus[bushandle].transmitBufFull=false;//reset overflow status
    return recordStatus(bushandle,SM_OK);
}


SM_STATUS smExecuteCommandQueue( const smbus bushandle, const smaddr targetaddress )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return SM_ERR_NODEVICE;

    return recordStatus(bushandle,smTransmitReceiveCommandQueue(bushandle,targetaddress,SMCMD_INSTANT_CMD));
}

SM_STATUS smUploadCommandQueueToDeviceBuffer( const smbus bushandle, const smaddr targetaddress )
{
    return recordStatus(bushandle,smTransmitReceiveCommandQueue(bushandle,targetaddress,SMCMD_BUFFERED_CMD));
}

//return number of how many bytes waiting to be read with smGetQueuedSMCommandReturnValue
SM_STATUS smBytesReceived( const smbus bushandle, int32_t *bytesinbuffer )
{
    if(!smIsHandleOpen(bushandle)) return recordStatus(bushandle,SM_ERR_NODEVICE);

    int32_t bytes=smBus[bushandle].recv_payloadsize - smBus[bushandle].cmd_recv_queue_bytes;//how many bytes waiting to be read with smGetQueuedSMCommandReturnValue
    *bytesinbuffer=bytes;

    return recordStatus(bushandle,SM_OK);
}

SM_STATUS smGetQueuedSMCommandReturnValue(  const smbus bushandle, int32_t *retValue )
{
    uint8_t rxbyte, rettype;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return SM_ERR_NODEVICE;


    //if get called so many times that receive queue buffer is already empty, return error
    if(smBus[bushandle].cmd_recv_queue_bytes>=smBus[bushandle].recv_payloadsize)
    {

        smDebug(bushandle,SMDebugTrace, "Packet receive error, return data coudn't be parsed\n");

        //return 0
        if(retValue!=NULL) *retValue=0;//check every time if retValue is set NULL by caller -> don't store anything to it if its NULL

        return recordStatus(bushandle,SM_ERR_LENGTH);//not a single byte left
    }

    //get first byte to deterime packet length
    rxbyte=bufget8bit(smBus[bushandle].recv_rsbuf, smBus[bushandle].cmd_recv_queue_bytes++);
    rettype=rxbyte>>6; //extract ret packet header 2 bits

    //read rest of data based on packet header:

    if(rettype == SMPRET_16B)
    {
        //extract return packet and convert to 32 bit and return
        SMPayloadCommandRet16 read;
        uint8_t *readBuf=(uint8_t*)&read;
        readBuf[1]=rxbyte;
        readBuf[0]=bufget8bit(smBus[bushandle].recv_rsbuf, smBus[bushandle].cmd_recv_queue_bytes++);
        smDebug(bushandle,SMDebugTrace,"  RET16B: %d\n",read.retData);

        if(retValue!=NULL) *retValue=read.retData;
        return recordStatus(bushandle,SM_OK);
    }
    if(rettype == SMPRET_24B)
    {
        //extract return packet and convert to 32 bit and return
        SMPayloadCommandRet24 read;
        uint8_t *readBuf=(uint8_t*)&read;
        readBuf[2]=rxbyte;
        readBuf[1]=bufget8bit(smBus[bushandle].recv_rsbuf, smBus[bushandle].cmd_recv_queue_bytes++);
        readBuf[0]=bufget8bit(smBus[bushandle].recv_rsbuf, smBus[bushandle].cmd_recv_queue_bytes++);
        smDebug(bushandle,SMDebugTrace,"  RET24B: %d\n",read.retData);

        if(retValue!=NULL) *retValue=read.retData;
        return recordStatus(bushandle,SM_OK);
    }
    if(rettype == SMPRET_32B)
    {
        //extract return packet and convert to 32 bit and return
        SMPayloadCommandRet32 read;
        uint8_t *readBuf=(uint8_t*)&read;
        readBuf[3]=rxbyte;
        readBuf[2]=bufget8bit(smBus[bushandle].recv_rsbuf, smBus[bushandle].cmd_recv_queue_bytes++);
        readBuf[1]=bufget8bit(smBus[bushandle].recv_rsbuf, smBus[bushandle].cmd_recv_queue_bytes++);
        readBuf[0]=bufget8bit(smBus[bushandle].recv_rsbuf, smBus[bushandle].cmd_recv_queue_bytes++);
        smDebug(bushandle,SMDebugTrace,"  RET32B: %d\n",read.retData);

        if(retValue!=NULL) *retValue=read.retData;
        return recordStatus(bushandle,SM_OK);
    }
    if(rettype == SMPRET_OTHER) //8bit
    {
        //extract return packet and convert to 32 bit and return
        SMPayloadCommandRet8 read;
        uint8_t *readBuf=(uint8_t*)&read;
        readBuf[0]=rxbyte;
        smDebug(bushandle,SMDebugTrace,"  RET_OTHER: %d\n",read.retData);

        if(retValue!=NULL) *retValue=read.retData;
        return recordStatus(bushandle,SM_OK);
    }

    return recordStatus(bushandle,SM_ERR_PARAMETER); //something went wrong, rettype not known
}


SM_STATUS smReceiveReturnPacket( smbus bushandle )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return SM_ERR_NODEVICE;

    smDebug(bushandle, SMDebugHigh, "  Reading reply packet\n");
    do
    {
        uint8_t ret;
        SM_STATUS stat;

        bool succ=smBDRead(smBus[bushandle].bdHandle,&ret);

        if(!succ)
        {
            smReceiveErrorHandler(bushandle,false);
            return recordStatus(bushandle,SM_ERR_COMMUNICATION);
        }

        stat=smParseReturnData( bushandle, ret );
        if(stat!=SM_OK) return recordStatus(bushandle,stat);
    } while(!smBus[bushandle].receiveComplete); //loop until complete packaget has been read

    //return data read complete
    smDebug(bushandle,SMDebugHigh, "< %s (id=%d, addr=%d, payload=%d)\n",
            cmdidToStr( smBus[bushandle].recv_cmdid ),
            smBus[bushandle].recv_cmdid,
            smBus[bushandle].recv_addr,
            smBus[bushandle].recv_payloadsize);

    return recordStatus(bushandle,SM_OK);
}


/** Set stream where debug output is written. By default nothing is written. */
LIB void smSetDebugOutput( smVerbosityLevel level, FILE *stream )
{
    smDebugThreshold=level;
    smDebugOut=stream;
}


//short returndata16=0, payload=0;
//can be called at any frequency
SM_STATUS smParseReturnData( smbus handle, uint8_t data )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    //buffered variable allows placing if's in any order (because recv_state may changes in this function)
    smBus[handle].recv_state=smBus[handle].recv_state_next;
    smBus[handle].receiveComplete=false;//overwritten to true later if complete

    if(smBus[handle].recv_state==WaitPayload)
    {
        smBus[handle].recv_crc=calcCRC16(data,smBus[handle].recv_crc);

        //normal handling for all payload data
        if(smBus[handle].recv_storepos<SM485_MAX_PAYLOAD_BYTES)
            smBus[handle].recv_rsbuf[smBus[handle].recv_storepos++]=data;
        else//rx payload buffer overflow
        {
            return recordStatus(handle,(smReceiveErrorHandler(handle,true)));
        }

        //all received
        if(smBus[handle].recv_payloadsize<=smBus[handle].recv_storepos)
            smBus[handle].recv_state_next=WaitCrcHi;

        return recordStatus(handle,SM_OK);
    }


    if(smBus[handle].recv_state==WaitCmdId)
    {
        smBus[handle].recv_crc=calcCRC16(data,smBus[handle].recv_crc);
        smBus[handle].recv_cmdid=data;
        switch(data&SMCMD_MASK_PARAMS_BITS)//commands with fixed payload size
        {
        case SMCMD_MASK_2_PARAMS: smBus[handle].recv_payloadsize=2; smBus[handle].recv_state_next=WaitAddr; break;
        case SMCMD_MASK_0_PARAMS: smBus[handle].recv_payloadsize=0; smBus[handle].recv_state_next=WaitAddr; break;
        case SMCMD_MASK_N_PARAMS: smBus[handle].recv_payloadsize=-1; smBus[handle].recv_state_next=WaitPayloadSize;break;//-1 = N databytes
        default:
            return recordStatus(handle,(smReceiveErrorHandler(handle, true)));
            break; //error, unsupported command id
        }

        return recordStatus(handle,SM_OK);
    }

    //no data payload size known yet
    if(smBus[handle].recv_state==WaitPayloadSize)
    {
        smBus[handle].recv_crc=calcCRC16(data,smBus[handle].recv_crc);
        smBus[handle].recv_payloadsize=data;
        smBus[handle].recv_state_next=WaitAddr;
        return recordStatus(handle,SM_OK);
    }

    if(smBus[handle].recv_state==WaitAddr)
    {
        smBus[handle].recv_crc=calcCRC16(data,smBus[handle].recv_crc);
        smBus[handle].recv_addr=data;//can be receiver or sender addr depending on cmd
        if(smBus[handle].recv_payloadsize>smBus[handle].recv_storepos)
            smBus[handle].recv_state_next=WaitPayload;
        else
            smBus[handle].recv_state_next=WaitCrcHi;
        return recordStatus(handle,SM_OK);
    }

    if(smBus[handle].recv_state==WaitCrcHi)
    {
        smBus[handle].recv_read_crc_hi=data;//crc_msb
        smBus[handle].recv_state_next=WaitCrcLo;
        return recordStatus(handle,SM_OK);
    }

    //get crc_lsb, check crc and execute
    if(smBus[handle].recv_state==WaitCrcLo)
    {
        if(((smBus[handle].recv_read_crc_hi<<8)|data)!=smBus[handle].recv_crc)
        {
            //CRC error
            return recordStatus(handle,(smReceiveErrorHandler(handle,true)));
        }
        else
        {
            //CRC ok
            //if(smBus[handle].recv_addr==config.deviceAddress || smBus[handle].recv_cmdid==SMCMD_GET_CLOCK_RET || smBus[handle].recv_cmdid==SMCMD_PROCESS_IMAGE ) executeSMcmd();
            smBus[handle].receiveComplete=true;
        }

        //smResetSM485variables(handle);
        smBus[handle].recv_storepos=0;
        smBus[handle].recv_crc=SM485_CRCINIT;
        smBus[handle].recv_state_next=WaitCmdId;
        return recordStatus(handle,SM_OK);
    }

    return recordStatus(handle,SM_OK);
}


//reply consumes 16 bytes in payload buf, so max calls per cycle is 7
SM_STATUS smAppendGetParamCommandToQueue( smbus handle, int16_t paramAddress )
{
    SM_STATUS stat=SM_NONE;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    //possible errors will set bits to stat
    stat|=smAppendSMCommandToQueue( handle, SMPCMD_SETPARAMADDR, SMP_RETURN_PARAM_LEN ); //2b
    stat|=smAppendSMCommandToQueue( handle, SMPCMD_24B, SMPRET_32B );//3b
    stat|=smAppendSMCommandToQueue( handle, SMPCMD_SETPARAMADDR, SMP_RETURN_PARAM_ADDR );//2b
    stat|=smAppendSMCommandToQueue( handle, SMPCMD_24B, paramAddress );//3b
    //=10 bytes

    return recordStatus(handle,stat);
}

SM_STATUS smGetQueuedGetParamReturnValue(  const smbus bushandle, int32_t *retValue )
{
    int32_t retVal=0;
    SM_STATUS stat=SM_NONE;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return SM_ERR_NODEVICE;

    //must get all inserted commands from buffer
    stat|=smGetQueuedSMCommandReturnValue( bushandle, &retVal );//4x4b
    stat|=smGetQueuedSMCommandReturnValue( bushandle, &retVal );
    stat|=smGetQueuedSMCommandReturnValue( bushandle, &retVal );
    stat|=smGetQueuedSMCommandReturnValue( bushandle, &retVal );  //the real return value is here
    if(retValue!=NULL) *retValue=retVal;
    return recordStatus(bushandle,stat);
}

//consumes 6 bytes in payload buf, so max calls per cycle is 20
SM_STATUS smAppendSetParamCommandToQueue( smbus handle, int16_t paramAddress, int32_t paramValue )
{
    SM_STATUS stat=SM_NONE;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    stat|=smAppendSMCommandToQueue( handle, SMPCMD_SETPARAMADDR, paramAddress );//2b
    stat|=smAppendSMCommandToQueue( handle, SMPCMD_32B, paramValue );//4b
    return recordStatus(handle,stat);
}


SM_STATUS smGetQueuedSetParamReturnValue(  const smbus bushandle, int32_t *retValue )
{
    int32_t retVal=0;
    SM_STATUS stat=SM_NONE;

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(bushandle)) return SM_ERR_NODEVICE;

    //must get all inserted commands from buffer
    stat|=smGetQueuedSMCommandReturnValue( bushandle, &retVal );
    stat|=smGetQueuedSMCommandReturnValue( bushandle, &retVal );  //the real return value is here
    if(retValue!=NULL) *retValue=retVal;

    return recordStatus(bushandle,stat);
}


SM_STATUS smGetBufferClock( const smbus handle, const smaddr targetaddr, uint16_t *clock )
{
    SM_STATUS stat;

    smDebug(handle,SMDebugMid,"smGetBufferClock: target SM address %d.\n",(int)targetaddr);

    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return recordStatus(handle,SM_ERR_NODEVICE);

    stat=smSendSMCMD(handle, SMCMD_GET_CLOCK ,targetaddr, 0, NULL ); //send get clock commands to bus
    if(stat!=SM_OK) return recordStatus(handle,stat);

    stat=smReceiveReturnPacket(handle);//blocking wait & receive return values from bus
    if(stat!=SM_OK) return recordStatus(handle,stat); //maybe timeouted

    if(clock!=NULL)
	memcpy(clock,smBus[handle].recv_rsbuf,sizeof(uint16_t));

    smBus[handle].recv_storepos=0;

    return recordStatus(handle,SM_OK);
}

/** Simple read & write of parameters with internal queueing, so only one call needed.
Use these for non-time critical operations. */
SM_STATUS smRead1Parameter( const smbus handle, const smaddr nodeAddress, const int16_t paramId1, int32_t *paramVal1 )
{
    SM_STATUS smStat=0;

    smDebug(handle,SMDebugMid,"smRead1Parameter: reading parameter address %hu from SM address %d.\n",(unsigned short)paramId1,(int)nodeAddress);

    smStat|=smAppendGetParamCommandToQueue(handle,paramId1);
    smStat|=smExecuteCommandQueue(handle,nodeAddress);
    smStat|=smGetQueuedGetParamReturnValue(handle,paramVal1);

    smDebug(handle,SMDebugMid,"  ^^ got value %d\n",(int)*paramVal1);

    if(smStat!=SM_OK)
        smDebug(handle,SMDebugLow,"smRead1Parameter failed (SM_STATUS=%d)",(int)smStat);

    return recordStatus(handle,smStat);
}

SM_STATUS smRead2Parameters( const smbus handle, const smaddr nodeAddress, const int16_t paramId1, int32_t *paramVal1,const int16_t paramId2, int32_t *paramVal2 )
{
    SM_STATUS smStat=0;

    smDebug(handle,SMDebugMid,"smRead2Parameters: reading parameter addresses %hu and %hu from SM address %d.\n",(unsigned short)paramId1,(unsigned short)paramId2,(int)nodeAddress);

    smStat|=smAppendGetParamCommandToQueue(handle,paramId1);
    smStat|=smAppendGetParamCommandToQueue(handle,paramId2);
    smStat|=smExecuteCommandQueue(handle,nodeAddress);
    smStat|=smGetQueuedGetParamReturnValue(handle,paramVal1);
    smStat|=smGetQueuedGetParamReturnValue(handle,paramVal2);

    smDebug(handle,SMDebugMid,"  ^^ got values %d and %d\n",(int)*paramVal1,(int)*paramVal2);

    if(smStat!=SM_OK)
        smDebug(handle,SMDebugLow,"smRead2Parameters failed (SM_STATUS=%d).",(int)smStat);

    return recordStatus(handle,smStat);
}

SM_STATUS smRead3Parameters( const smbus handle, const smaddr nodeAddress, const int16_t paramId1, int32_t *paramVal1,const int16_t paramId2, int32_t *paramVal2 ,const int16_t paramId3, int32_t *paramVal3 )
{
    SM_STATUS smStat=0;

    smDebug(handle,SMDebugMid,"smRead3Parameters: reading parameter addresses %hu, %hu and %hu from SM address %d.\n",(unsigned short)paramId1,(unsigned short)paramId2,(unsigned short)paramId3,(int)nodeAddress);

    smStat|=smAppendGetParamCommandToQueue(handle,paramId1);
    smStat|=smAppendGetParamCommandToQueue(handle,paramId2);
    smStat|=smAppendGetParamCommandToQueue(handle,paramId3);
    smStat|=smExecuteCommandQueue(handle,nodeAddress);
    smStat|=smGetQueuedGetParamReturnValue(handle,paramVal1);
    smStat|=smGetQueuedGetParamReturnValue(handle,paramVal2);
    smStat|=smGetQueuedGetParamReturnValue(handle,paramVal3);

    smDebug(handle,SMDebugMid,"  ^^ got values %d, %d and %d\n",(int)*paramVal1,(int)*paramVal2,(int)*paramVal3);

    if(smStat!=SM_OK)
        smDebug(handle,SMDebugLow,"smRead3Parameters failed (SM_STATUS=%d). ",(int)smStat);

    return recordStatus(handle,smStat);
}

SM_STATUS smSetParameter( const smbus handle, const smaddr nodeAddress, const int16_t paramId, int32_t paramVal )
{
    int32_t nul;
    SM_STATUS smStat=0;

    smDebug(handle,SMDebugMid,"smSetParameter: writing parameter [%hu]=%d into SM address %d.\n",(unsigned short)paramId,(int)paramVal,(int)nodeAddress);

    smStat|=smAppendSetParamCommandToQueue( handle, paramId, paramVal );
    smStat|=smExecuteCommandQueue(handle,nodeAddress);
    if(nodeAddress!=0)//don't attempt to read if target address was broadcast address where no slave device will respond
        smStat|=smGetQueuedSetParamReturnValue(  handle, &nul );

    if(smStat!=SM_OK)
        smDebug(handle,SMDebugLow,"smSetParameter failed (SM_STATUS=%d).",(int)smStat);

    return recordStatus(handle,smStat);
}


//accumulates status to internal variable by ORing the bits. returns same value that is fed as paramter
SM_STATUS recordStatus( const smbus handle, const SM_STATUS stat )
{
    //check if bus handle is valid & opened
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    if(smBus[handle].cumulativeSmStatus!=stat && stat!=SM_OK)//if status changed and new status is not SM_OK
        smDebug(handle,SMDebugLow,"Previous SM call failed and changed the SM_STATUS value obtainable with getCumulativeStatus(). Status before failure was %d, and new error flag valued %d has been now set.\n",(int)smBus[handle].cumulativeSmStatus,(int)stat);

    smBus[handle].cumulativeSmStatus|=stat;

    return stat;
}


/** This function returns all occurred SM_STATUS bits after smOpenBus or resetCumulativeStatus call*/
SM_STATUS getCumulativeStatus( const smbus handle )
{
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    return smBus[handle].cumulativeSmStatus;
}

/** Reset cululative status so getCumultiveStatus returns 0 after calling this until one of the other functions are called*/
SM_STATUS resetCumulativeStatus( const smbus handle )
{
    if(!smIsHandleOpen(handle)) return SM_ERR_NODEVICE;

    smDebug(handle,SMDebugMid,"resetCumulativeStatus called.\n");

    smBus[handle].cumulativeSmStatus=0;

    return SM_OK;
}


/** Return number of bus devices found. details of each device may be consequently fetched by smGetBusDeviceDetails() */
int smGetNumberOfDetectedBuses()
{
    return smBDGetNumberOfDetectedBuses();
}

/** Fetch information of detected bus nodes at certain index. Example:

    int num=smGetNumberOfDetectedBuses();
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
LIB SM_STATUS smGetBusDeviceDetails( int index, SM_BUS_DEVICE_INFO *info )
{
    bool ok=smBDGetBusDeviceDetails(index,info);

    if(ok)
        return SM_OK;
    else
        return SM_ERR_NODEVICE;
}

#ifdef ENABLE_DEBUG_PRINTS

// early exiting snprintf wrapper which will also:
//  - increment the size variable on success
//  - early exit on failure
#define SAFE_SNPRINTF(target, size, remaining, ...) \
	{ \
		int _written = snprintf((target), (remaining), __VA_ARGS__); \
		if (_written < 0) { \
			return _written; \
		} \
		else if ((size_t)_written >= remaining) { \
			return size + _written; \
		} \
		size += _written; \
	}

// conditional string appending if source & name != 0.
#define APPEND_IF(source, name, str, target, size, remaining, checked, prefix_len) \
	{ \
		if (((source) & (name)) != 0) { \
			SAFE_SNPRINTF(target, size, remaining, "%s ", &str[prefix_len]); \
		} \
		checked |= (name); \
	}

// removes the single trailing whitespace from the buffer by writing a null on it.
#define TRIM_TRAILING_WS(str, len) \
	{ \
		if (len > 1 && str[len - 1] == ' ') { \
			str[len - 1] = '\0'; \
			len--; \
		} \
	}

#else

#define SAFE_SNPRINTF(...)
#define APPEND_IF(...)
#define TRIM_TRAILING_WS(...)

#endif

// size has been defined as const to help with the name confusion w.r.t macros.
LIB int smDescribeSmStatus(char* str, const size_t size, SM_STATUS status)
{
	size_t len = 0; // mutated by SAFE_SNPRINTF (nested or direct)
	str[0] = '\0'; // this is probably something snprintf does not always do
	SM_STATUS checked = 0;

// "local" as in binds the APPEND_IF to local variables declared above
#define LOCAL_APPEND(name) APPEND_IF(status, name, #name, &str[len], len, size - len, checked, 3)

	if (status == SM_NONE) {
		// special case: zero
		SAFE_SNPRINTF(str, len, size - len, "NONE");
	} else {
		LOCAL_APPEND(SM_OK);
		LOCAL_APPEND(SM_ERR_NODEVICE);
		LOCAL_APPEND(SM_ERR_BUS);
		LOCAL_APPEND(SM_ERR_COMMUNICATION);
		LOCAL_APPEND(SM_ERR_PARAMETER);
		LOCAL_APPEND(SM_ERR_LENGTH);
	}

#undef LOCAL_APPEND

	SM_STATUS extras = status & ~checked;
	if (extras != 0) {
		SAFE_SNPRINTF(&str[len], len, size - len, "EXTRA(%d)", extras);
	}

	TRIM_TRAILING_WS(str, len);

	return len;
}

LIB int smDescribeFault(char* str, const size_t size, int32_t faults)
{
	size_t len = 0;
	str[0] = '\0';
	int32_t checked = 0;

#define LOCAL_APPEND(name) APPEND_IF(faults, name, #name, &str[len], len, size - len, checked, 4)

	LOCAL_APPEND(FLT_FOLLOWERROR);
	LOCAL_APPEND(FLT_OVERCURRENT);
	LOCAL_APPEND(FLT_COMMUNICATION)
	LOCAL_APPEND(FLT_ENCODER)
	LOCAL_APPEND(FLT_OVERTEMP)
	LOCAL_APPEND(FLT_UNDERVOLTAGE)
	LOCAL_APPEND(FLT_OVERVOLTAGE)
	LOCAL_APPEND(FLT_PROGRAM_OR_MEM)
	LOCAL_APPEND(FLT_HARDWARE)
	LOCAL_APPEND(FLT_OVERVELOCITY)
	LOCAL_APPEND(FLT_INIT)
	LOCAL_APPEND(FLT_MOTION)
	LOCAL_APPEND(FLT_RANGE)
	LOCAL_APPEND(FLT_PSTAGE_FORCED_OFF)
	LOCAL_APPEND(FLT_HOST_COMM_ERROR)
	LOCAL_APPEND(FLT_CONFIG)
	LOCAL_APPEND(FLT_GC_COMM)

#undef LOCAL_APPEND

	const int32_t extras = faults & ~checked;
	if (extras != 0) {
		SAFE_SNPRINTF(&str[len], len, size - len, "EXTRA(%" PRId32 ")", extras);
	}

	TRIM_TRAILING_WS(str, len);

	return len;
}

LIB int smDescribeStatus(char* str, const size_t size, int32_t status)
{
	size_t len = 0;
	str[0] = '\0';
	int32_t checked = 0;

#define LOCAL_APPEND(name) APPEND_IF(status, name, #name, &str[len], len, size - len, checked, 5)

	LOCAL_APPEND(STAT_RESERVED_)
	LOCAL_APPEND(STAT_TARGET_REACHED)
	LOCAL_APPEND(STAT_FERROR_RECOVERY)
	LOCAL_APPEND(STAT_RUN)
	LOCAL_APPEND(STAT_ENABLED)
	LOCAL_APPEND(STAT_FAULTSTOP)
	LOCAL_APPEND(STAT_FERROR_WARNING)
	LOCAL_APPEND(STAT_STO_ACTIVE)
	LOCAL_APPEND(STAT_SERVO_READY)
	LOCAL_APPEND(STAT_BRAKING)
	LOCAL_APPEND(STAT_HOMING)
	LOCAL_APPEND(STAT_INITIALIZED)
	LOCAL_APPEND(STAT_VOLTAGES_OK)
	LOCAL_APPEND(STAT_PERMANENT_STOP)
	LOCAL_APPEND(STAT_STANDING_STILL)
	LOCAL_APPEND(STAT_QUICK_STOP_ACTIVE)
	LOCAL_APPEND(STAT_SAFE_TORQUE_MODE_ACTIVE)

#undef LOCAL_APPEND

	const int32_t extras = status & ~checked;
	if (extras != 0) {
		SAFE_SNPRINTF(&str[len], len, size - len, "EXTRA(%" PRId32 ")", extras);
	}

	TRIM_TRAILING_WS(str, len);

	return len;
}

#undef APPEND_IF
#undef SAFE_SNPRINTF
#undef TRIM_TRAILING_WS


LIB SM_STATUS smCheckDeviceCapabilities(const smbus handle, const int nodeAddress,
                                         const int32_t capabilitiesParameterNr,
                                         const int32_t requiredCapabilityFlags,
                                         bool *resultHasAllCapabilities )
{
    SM_STATUS smStat=0;
    int32_t SMProtocolVersion;
    *resultHasAllCapabilities=false;//set true later

    smStat|=smRead1Parameter(handle,nodeAddress,SMP_SM_VERSION,&SMProtocolVersion);
    if(smStat!=SM_OK) return smStat; //error in above call

    if(SMProtocolVersion>=28) //v28+ supports capabilities flags
    {
        //all devices with v28+ has two frist capabilities flags
        if(capabilitiesParameterNr==SMP_DEVICE_CAPABILITIES1 || capabilitiesParameterNr==SMP_DEVICE_CAPABILITIES2)
        {
            int32_t capabilities;
            smStat|=smRead1Parameter(handle,nodeAddress,capabilitiesParameterNr,&capabilities);
            if(smStat!=SM_OK) return smStat; //error in above call

            if( (capabilities&requiredCapabilityFlags)==requiredCapabilityFlags )//if all required capabilities are supported
                *resultHasAllCapabilities=true;//set result

            return SM_OK;
        }
        else //for rest (future capabilities parameters), test if capabilitiesParameterNr is readable
        {
            int32_t capabilities1;
            smStat|=smRead1Parameter(handle,nodeAddress,SMP_DEVICE_CAPABILITIES1,&capabilities1);
            if(smStat!=SM_OK) return smStat; //error in above call

            //check if device supports testing whether paramter is available
            if(capabilities1&DEVICE_CAPABILITY1_SUPPORTS_SMP_PARAMETER_PROPERTIES_MASK)
            {
                int32_t paramProperties;
                //test if parameter is readable
                smStat|=smRead1Parameter(handle,nodeAddress,capabilitiesParameterNr|SMP_PROPERTIES_MASK,&paramProperties);
                if(smStat!=SM_OK) return smStat; //error in above call

                if(paramProperties&SMP_PROPERTY_PARAM_IS_READABLE) //requested capabilitiesParameterNr is available
                {
                    int32_t capabilities;
                    smStat|=smRead1Parameter(handle,nodeAddress,capabilitiesParameterNr,&capabilities);
                    if(smStat!=SM_OK) return smStat; //error in above call

                    if( (capabilities&requiredCapabilityFlags)==requiredCapabilityFlags )//if all required capabilities are supported
                        *resultHasAllCapabilities=true;//set result

                    return SM_OK;
                }
                else
                    return SM_OK; //requested capabilities parameter is not available, therefore feature requested is not supported in target
            }
            else
                return SM_OK;//capability parameter not available therefore requested capability not available
        }
    }
    else
        return SM_OK;//capability parameter not available therefore requested capability not available
}

