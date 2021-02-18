/*
 * SimpleMotion V2 subpacket frame format constants
 */
#ifndef SM485_H
#define SM485_H

#define SM485_MAX_PAYLOAD_BYTES 120

//00xb
#define SMCMD_MASK_0_PARAMS 0
//01xb, 2 bytes as payload
#define SMCMD_MASK_2_PARAMS 2
//10xb
#define SMCMD_MASK_N_PARAMS 4
//11xb
#define SMCMD_MASK_RESERVED 6
//xx1b
#define SMCMD_MASK_RETURN 1
//110b
#define SMCMD_MASK_PARAMS_BITS 6


//cmdnumber must be 0-31
#define SMCMD_ID(cmdnumber, flags) (((cmdnumber)<<3)|flags)


//returned subpacket if error occurred. payload is two bytes where second byte is one of SMERR_ values (below)
#define SMCMD_ERROR_RET SMCMD_ID(3,SMCMD_MASK_2_PARAMS|SMCMD_MASK_RETURN)
#define SMERR_CRC 1
#define SMERR_INVALID_CMD 2
#define SMERR_BUF_OVERFLOW 4
#define SMERR_INVALID_PARAMETER 8
#define SMERR_PAYLOAD_SIZE 16

//format 5, u8 bytesfollowing,u8 toaddr,cmddata,u16 crc
//return, 6, u8 bytesfollowing,u8 fromaddr, returndata,u16 crc
#define SMCMD_INSTANT_CMD SMCMD_ID(4,SMCMD_MASK_N_PARAMS)
#define SMCMD_INSTANT_CMD_RET SMCMD_ID(4,SMCMD_MASK_N_PARAMS|SMCMD_MASK_RETURN)

//format  ID, u8 bytesfollowing,u8 toaddr,cmddata,u16 crc
//return, ID, u8 bytesfollowing,u8 fromaddr,returndata,u16 crc
#define SMCMD_BUFFERED_CMD SMCMD_ID(6,SMCMD_MASK_N_PARAMS)
#define SMCMD_BUFFERED_CMD_RET SMCMD_ID(6,SMCMD_MASK_N_PARAMS|SMCMD_MASK_RETURN)

//read return data from buffered cmds. repeat this command before SMCMD_BUFFERED_CMD until N!=120 from download
#define SMCMD_BUFFERED_RETURN_DATA SMCMD_ID(7,SMCMD_MASK_0_PARAMS)
#define SMCMD_BUFFERED_RETURN_DATA_RET SMCMD_ID(7,SMCMD_MASK_N_PARAMS|SMCMD_MASK_RETURN)

//cmd 20,u8 toaddr, u8 crc
//ret 21, u8=2, u8 fromaddr, u16 clock,u16 crc, muut clientit lukee t?n
//oldret 21, u8 fromaddr, u16 clock,u16 crc, muut clientit lukee t?n
#define SMCMD_GET_CLOCK SMCMD_ID(10,SMCMD_MASK_0_PARAMS)
#define SMCMD_GET_CLOCK_RET SMCMD_ID(10,SMCMD_MASK_2_PARAMS|SMCMD_MASK_RETURN)

// SMCMD_FAST_UPDATE_CYCLE is a high priority command for fast realtime cyclic operation. content and lenght of
// payload data may be application specific and may be controlled by other variables
// cmd u8 ID, u8 toaddr, u32 data u16 crc
// ret u8 ID, u8 fromaddr, u32 data, u16 crc
#define SMCMD_FAST_UPDATE_CYCLE SMCMD_ID(2,SMCMD_MASK_0_PARAMS)
#define SMCMD_FAST_UPDATE_CYCLE_RET SMCMD_ID(2,SMCMD_MASK_0_PARAMS|SMCMD_MASK_RETURN)

//payload data is sent to the bus unmodified, no other return data
#define SMCMD_ECHO SMCMD_ID(12,SMCMD_MASK_N_PARAMS)


#endif
