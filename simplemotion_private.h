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


extern const smuint8 table_crc16_hi[];
extern const smuint8 table_crc16_lo[];
extern const smuint8 table_crc8[];
extern FILE *smDebugOut; //such as stderr or file handle. if NULL, debug info disbled
extern smuint16 readTimeoutMs;

//smDebug: prints debug info to smDebugOut stream. If no handle available, set it to -1.
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

SM_STATUS smRawCmd( const char *axisname, smuint8 cmd, smuint16 val, smuint32 *retdata );

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

/*Workaround to have packed structs that compile on GCC and MSVC*/
#ifdef __GNUC__
#else/*Assuming MSVC*/
#pragma pack(pop)
#undef PACKED
#endif


#endif // SIMPLEMOTION_PRIVATE_H
