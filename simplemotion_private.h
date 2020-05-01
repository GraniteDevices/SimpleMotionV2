//Internal functions & definitions, not for library user
//Copyright (c) Granite Devices Oy


#ifndef SIMPLEMOTION_PRIVATE_H
#define SIMPLEMOTION_PRIVATE_H

#include "simplemotion.h"
#include "busdevice.h"
#include "user_options.h"
#include <stdio.h>

#define SM_VERSION 0x020700

//bus device types
#define BUSDEV_NONE 0
#define BUSDEV_RS 1 /* rs232 like com port support */
#define BUSDEV_FTDI 2 /*not implemented yet: direct FTDI lib support*/

#define SM_BUSDEVICENAME_LEN 64
extern unsigned long SMBusBaudrate; //the next opened port (with smOpenBus) will be opened with the PBS defined here (default 460800 BPS)

//default timeout in ms
//Argon drive's worst case response time should be ~20ms with max length packets
#define SM_READ_TIMEOUT 500


extern const uint8_t table_crc16_hi[];
extern const uint8_t table_crc16_lo[];
extern const uint8_t table_crc8[];
extern FILE *smDebugOut; //such as stderr or file handle. if NULL, debug info disbled
extern uint16_t readTimeoutMs;

#define DEBUG_PRINT_RAW 0x524157
//smDebug: prints debug info to smDebugOut stream. If no handle available, set it to -1, or if wish to print as raw text, set handle to DEBUG_PRINT_RAW.
//set verbositylevel according to frequency of prints made.
//I.e SMDebugLow=low frequency, so it gets displayed when global verbosity level is set to at least Low or set it to Trace which gets filtered
//out if global verbisity level is set less than SMDebugTrace
#ifdef ENABLE_DEBUG_PRINTS
void smDebug( smbus handle, smVerbosityLevel verbositylevel, char *format, ...);
#else
#define smDebug(...) {}
#endif
//accumulates status to internal variable by ORing the bits. returns same value that is fed as paramter
SM_STATUS recordStatus( const smbus handle, const SM_STATUS stat );

SM_STATUS smRawCmd( const char *axisname, uint8_t cmd, uint16_t val, uint32_t *retdata );

/*Workaround to have packed structs that compile on GCC and MSVC*/
#ifdef __GNUC__
#define PACKED __attribute__ ((__packed__))
#else/*Assuming MSVC*/
#define PACKED
#pragma pack(push,1)
#endif

typedef struct {
        /* ID=0 param size 30 bits (cmd total 4 bytes)
         * ID=1 param size 22 bits (cmd total 3 bytes)
         * ID=2 set parameter store address, param: 14 bits=register address  (cmd total 2 bytes)
         * ID=3 reserved
         */
        long param :30; //LSB 30 bits
        unsigned long ID:2; //MSB 2 bits. when serailzied to bytestream byte4 must be transmitted first to contain ID
} PACKED SMPayloadCommand32;

typedef struct {
        /* ID=0 param size 30 bits (cmd total 4 bytes)
         * ID=1 param size 22 bits (cmd total 3 bytes)
         * ID=2 set parameter store address, param: 14 bits=register address  (cmd total 2 bytes)
         * ID=3 reserved
         */
        long param :14; //LSB 30 bits
        unsigned long ID:2; //MSB 2 bits. when serailzied to bytestream byte4 must be transmitted first to contain ID
} PACKED SMPayloadCommand16;

typedef struct {
        /* ID=0 param size 30 bits (cmd total 4 bytes)
         * ID=1 param size 22 bits (cmd total 3 bytes)
         * ID=2 set parameter store address, param: 14 bits=register address  (cmd total 2 bytes)
         * ID=3 reserved
         */
        long param :22; //MSB 30 bits
        unsigned long ID:2; //MSB 2 bits. when serailzied to bytestream byte4 must be transmitted first to contain ID
} PACKED SMPayloadCommand24;

//SM payload command return data structure
typedef struct {
        /* ID=0 ret data 30 bits (tot 4 bytes)
         * ID=1 ret data 22 bits (tot 3 bytes)
         * ID=2 ret data 16 bits (tot 2 bytes), i.e. ACK/NACK.
         * ID=3 reserved
         */
        long retData: 30; //LSB 30 bits
        unsigned long ID:2; //MSB 2 bits. when serailzied to bytestream byte4 must be transmitted first to contain ID
} PACKED SMPayloadCommandRet32;

//SM payload command return data structure
typedef struct {
        /* ID=0 ret data 30 bits (tot 4 bytes)
         * ID=1 ret data 22 bits (tot 3 bytes)
         * ID=2 ret data 16 bits (tot 2 bytes), i.e. ACK/NACK.
         * ID=3 reserved
         */
        long retData: 22; //LSB 30 bits
        unsigned long ID:2; //MSB 2 bits. when serailzied to bytestream byte4 must be transmitted first to contain ID
} PACKED SMPayloadCommandRet24;

//SM payload command return data structure
typedef struct {
        /* ID=0 ret data 30 bits (tot 4 bytes)
         * ID=1 ret data 22 bits (tot 3 bytes)
         * ID=2 ret data 16 bits (tot 2 bytes), i.e. ACK/NACK.
         * ID=3 reserved
         */
        long retData: 14; //LSB 30 bits
        unsigned long ID:2; //MSB 2 bits. when serailzied to bytestream byte4 must be transmitted first to contain ID
} PACKED SMPayloadCommandRet16;

//SM payload command return data structure
typedef struct {
        /* ID=0 ret data 30 bits (tot 4 bytes)
         * ID=1 ret data 22 bits (tot 3 bytes)
         * ID=2 ret data 16 bits (tot 2 bytes), i.e. ACK/NACK.
         * ID=3 reserved
         */
        long retData: 6; //LSB 30 bits
        unsigned long ID:2; //MSB 2 bits. when serailzied to bytestream byte4 must be transmitted first to contain ID
} PACKED SMPayloadCommandRet8;

typedef union
{
    uint8_t U8[4];
    uint16_t U16[2];
    uint32_t U32;
} UnionOf4Bytes;

/*Workaround to have packed structs that compile on GCC and MSVC*/
#ifdef __GNUC__
#else/*Assuming MSVC*/
#pragma pack(pop)
#undef PACKED
#endif


/** Clear pending (stray) bytes in bus device reception buffer and reset receiver state. This may be needed i.e. after restarting device to eliminate clitches that appear in serial line.
  -return value: a SM_STATUS value, i.e. SM_OK if command succeed
*/
LIB SM_STATUS smPurge( const smbus bushandle );

/** Block until pending TX bytes are phyiscally out. Max blocking time is same that is set with smSetTimeout
  -return value: a SM_STATUS value, i.e. SM_OK if command succeed
*/
LIB SM_STATUS smFlushTX( const smbus bushandle );

/* OS independent sleep function for SM internal use
 *
 * SM lib has implementation for unix/win systems (incl linux & mac). For other systems, please add your own smSleepMs implementation in your application.
 * i.e. write function in your main.c or any other .c file that get's compiled and linked:
 *
 * void smSleepMs(int millisecs)
 * {
 *    // do your delay here
 * }
 */
void smSleepMs(int millisecs);


#endif // SIMPLEMOTION_PRIVATE_H
