/* Device deployment library based on SimpleMotionV2 lib. Features:
 *
 * - install .gdf format firmware to drive
 * - load .drc settings file to drive
 * - read installed firmware binary checksum from device - this may be used to verify that exact correct FW build is installed in the drive
 *
 * TODO:
 * - Support Argon. Currently tested only on IONI/ATOMI series drives. Currently FW upgrade on Argon will fail, but settings load may work.
 * - Add some way of reading binary checksum from .gdf file or settings file. Now user must read it from device and store that number somewhere manually.
 */

#ifndef SMDEPLOYMENTTOOL_H
#define SMDEPLOYMENTTOOL_H

#ifdef WIN32
//dll specs
#ifdef BUILD_DLL
    #define LIB __declspec(dllexport)
#else
//    #define LIB __declspec(dllimport)
#define LIB
#endif
#else
#define LIB
#endif


#include "simplemotion.h"


#ifdef __cplusplus
extern "C"{
#endif


typedef enum
{
    FWComplete=100,
    FWInvalidFile=-1,
    FWConnectionError=-2,
    FWIncompatibleFW=-3,
    FWConnectionLoss=-4,
    FWUnsupportedTargetDevice=-5,
    FWFileNotReadable=-6,
    FWConnectingDFUModeFailed=-7
} FirmwareUploadStatus;

/**
 * @brief smFirmwareUpload Sets drive in firmware upgrade mode if necessary and uploads a new firmware. Call this many until it returns value 100 (complete) or a negative value (error).
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address
 * @param filename .gdf file name
 * @return Enum FirmwareUploadStatus that indicates errors or Complete status. Typecast to integer to get progress value 0-100.
 */
LIB FirmwareUploadStatus smFirmwareUpload(const smbus smhandle, const int smaddress, const char *firmware_filename );

/**
 * @brief smFirmwareUpload Sets drive in firmware upgrade mode if necessary and uploads a new firmware. Call this many until it returns value 100 (complete) or a negative value (error).
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address. Can be device in DFU mode or main operating mode. For Argon, one device in a bus must be started into DFU mode by DIP switches and smaddress must be set to 255.
 * @param fwData pointer to memory address where .gdf file contents are loaded. Note: on some architectures (such as ARM Cortex M) fwData must be aligned to nearest 4 byte boundary to avoid illegal machine instructions.
 * @param fwDataLenght number of bytes in fwData
 * @return Enum FirmwareUploadStatus that indicates errors or Complete status. Typecast to integer to get progress value 0-100.
 */
FirmwareUploadStatus smFirmwareUploadFromBuffer( const smbus smhandle, const int smaddress, smuint8 *fwData, const int fwDataLength );

typedef enum
{
    CFGComplete=100,
    CFGInvalidFile=-1,
    CFGCommunicationError=-2,
    CFGIncompatibleFW=-4,
    CFGUnsupportedTargetDevice=-5,
    CFGUnableToOpenFile=-6

} LoadConfigurationStatus;

//TODO implement: #define CONFIGMODE_REQUIRE_SAME_FW 1 //will return IncompatibleFW if firmware checksum does not match the one in .drc files. if this error is returned, perform smFirmwareUpload and perform smLoadConfiguration again. Requires DRC file version 111 or later (if not met, returns InvalidFile).
#define CONFIGMODE_ALWAYS_RESTART_TARGET 2 //will perform device restart after setup even when it's not required
#define CONFIGMODE_DISABLE_DURING_CONFIG 4 //will set device in disabled state during configuration
#define CONFIGMODE_CLEAR_FAULTS_AFTER_CONFIG 8 //will perform clear faults command after configuration

/**
 * @brief smConfigureParameters Configures all target device parameters from file and performs device restart if necessary. This can take few seconds to complete. This may take 2-5 seconds to call.
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address
 * @param filename .DRC file name
 * @param mode Combined from CONFIGMODE_ define bits (can logic OR mutliple values).
 * @return Enum LoadConfigurationStatus
 *
 * Requires DRC file version 111 or later to use CONFIGMODE_REQUIRE_SAME_FW.
 */
LIB LoadConfigurationStatus smLoadConfiguration( const smbus smhandle, const int smaddress, const char *filename, unsigned int mode, int *skippedCount, int *errorCount );

/**
 * @brief smConfigureParametersFromBuffer Same as smConfigureParameters but reads data from user specified memory address instead of file. Configures all target device parameters from file and performs device restart if necessary. This can take few seconds to complete. This may take 2-5 seconds to call.
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address
 * @param drcData Pointer to to a memory where .drc file is loaded
 * @param drcDataLen Number of bytes available in the drcData buffer
 * @param mode Combined from CONFIGMODE_ define bits (can logic OR mutliple values).
 * @return Enum LoadConfigurationStatus
 *
 * Requires DRC file version 111 or later to use CONFIGMODE_REQUIRE_SAME_FW.
 */
LIB LoadConfigurationStatus smLoadConfigurationFromBuffer(const smbus smhandle, const int smaddress, const smuint8 *drcData, const int drcDataLength, unsigned int mode, int *skippedCount, int *errorCount );


/**
 * @brief smGetDeviceFirmwareUniqueID Reads installed firmware binary checksum that can be used to verify whether a wanted FW version is installed
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address. Can be device in DFU mode or main operating mode. For Argon, one device in a bus must be started into DFU mode by DIP switches and smaddress must be set to 255.
 * @param UID result will be written to this pointer
 * @return smtrue if success, smfalse if failed (if communication otherwise works, then probably UID feature not present in this firmware version)
 */
smbool smGetDeviceFirmwareUniqueID( smbus smhandle, int deviceaddress, smuint32 *UID );



#ifdef __cplusplus
}
#endif
#endif // SMDEPLOYMENTTOOL_H
