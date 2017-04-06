#include "devicedeployment.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <simplemotion_private.h>
#include <math.h>

#ifdef __unix__
#include <unistd.h>
void sleep_ms(int millisecs)
{
    msleep(millisecs);
}
#else
#include <windows.h>
void sleep_ms(int millisecs)
{
    Sleep(millisecs);
}
#endif

int globalErrorDetailCode=0;

int smGetDeploymentToolErrroDetail()
{
    return globalErrorDetailCode;
}

//return -1 if EOF
unsigned int readFileLine( FILE *f, int charlimit, char *output, smbool *eof)
{
    int len=0;
    char c;
    do
    {
        c=fgetc(f);

        if(feof(f))
            *eof=smtrue;
        else
            *eof=smfalse;

        //eol or eof
        if( *eof==smtrue || c=='\n' || c=='\r' || len>=charlimit-1 )
        {
            output[len]=0;//terminate str
            return len;
        }

        output[len]=c;
        len++;
    } while(1);
    return len;
}

typedef struct
{
    int address;
    double value;
    smbool readOnly;
    double scale;
    double offset;
} Parameter;

smbool parseParameter(FILE *f, int idx, Parameter *param )
{
    const int maxLineLen=100;
    char line[maxLineLen];
    char scanline[maxLineLen];
    //search correct row
    fseek(f,0,SEEK_SET);
    smbool gotaddr=smfalse,gotvalue=smfalse, gotreadonly=smfalse, gotscale=smfalse,gotoffset=smfalse;
    unsigned int readbytes;
    smbool eof;

    do//loop trhu all lines of file
    {
        readbytes=readFileLine(f,maxLineLen,line,&eof);//read line

        //try read address
        sprintf(scanline,"%d\\addr=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(sscanf(line+strlen(scanline),"%d",&param->address)==1)//parse number after the start of line
                gotaddr=smtrue;//number parse success

        //try read value
        sprintf(scanline,"%d\\value=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(sscanf(line+strlen(scanline),"%lf",&param->value)==1)//parse number after the start of line
                gotvalue=smtrue;//number parse success

        //try read offset
        sprintf(scanline,"%d\\offset=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(sscanf(line+strlen(scanline),"%lf",&param->offset)==1)//parse number after the start of line
                gotoffset=smtrue;//number parse success

        //try read scale
        sprintf(scanline,"%d\\scaling=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(sscanf(line+strlen(scanline),"%lf",&param->scale)==1)//parse number after the start of line
                gotscale=smtrue;//number parse success

        //try read readonly status
        sprintf(scanline,"%d\\readonly=true",idx);//check if readonly=true
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes >= strlen(scanline))//line match
        {
            param->readOnly=smtrue;
            gotreadonly=smtrue;
        }
        sprintf(scanline,"%d\\readonly=false",idx);//check if readonly=false
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes >= strlen(scanline))//line match
        {
            param->readOnly=smfalse;
            gotreadonly=smtrue;
        }
    }
    while( (gotvalue==smfalse || gotaddr==smfalse || gotreadonly==smfalse || gotscale==smfalse || gotoffset==smfalse) && eof==smfalse );

    if(gotvalue==smtrue&&gotaddr==smtrue&&gotoffset==smtrue&&gotscale==smtrue&&gotreadonly==smtrue)
    {
        return smtrue;
    }

    return smfalse;//not found
}

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
LoadConfigurationStatus smLoadConfiguration(const smbus smhandle, const int smaddress, const char *filename, unsigned int mode , int *skippedCount, int *errorCount)
{
    //test connection
    smint32 devicetype;
    SM_STATUS stat;
    FILE *f;
    int ignoredCount=0;
    int setErrors=0;
    smint32 CB1Value;
    int changed=0;

    *skippedCount=-1;
    *errorCount=-1;

    //test connection
    stat=smRead1Parameter(smhandle,smaddress,SMP_DEVICE_TYPE,&devicetype);
    if(stat!=SM_OK)
        return CFGCommunicationError;

    //smSetParameter( smhandle, smaddress, SMP_RETURN_PARAM_LEN, SMPRET_CMD_STATUS );//get command status as feedback from each executed SM command

    if(mode&CONFIGMODE_DISABLE_DURING_CONFIG)
    {
        smRead1Parameter( smhandle, smaddress, SMP_CONTROL_BITS1, &CB1Value );
        smSetParameter( smhandle, smaddress, SMP_CONTROL_BITS1, 0);//disable drive
    }

    if(getCumulativeStatus( smhandle )!=SM_OK )
        return CFGCommunicationError;

    f=fopen(filename,"rb");
    if(f==NULL)
        return CFGUnableToOpenFile;

    smDebug(smhandle,Low,"Setting parameters\n");

    int i=1;
    smbool readOk;
    do
    {
        Parameter param;
        readOk=parseParameter(f,i,&param);

        if(readOk==smtrue && param.readOnly==smfalse)
        {
            smint32 currentValue;

            int configFileValue=round(param.value*param.scale-param.offset);

            //set parameter to device
            if(smRead1Parameter( smhandle, smaddress, param.address, &currentValue )==SM_OK)
            {
                if(currentValue!=configFileValue  ) //set only if different
                {
                    resetCumulativeStatus( smhandle );
                    smint32 dummy;
                    smint32 cmdSetAddressStatus;
                    smint32 cmdSetValueStatus;

                    //use low level SM commands so we can get execution status of each subpacet:
                    smAppendSMCommandToQueue( smhandle, SM_SET_WRITE_ADDRESS, SMP_RETURN_PARAM_LEN );
                    smAppendSMCommandToQueue( smhandle, SM_WRITE_VALUE_24B, SMPRET_CMD_STATUS );
                    smAppendSMCommandToQueue( smhandle, SM_SET_WRITE_ADDRESS, param.address );
                    smAppendSMCommandToQueue( smhandle, SM_WRITE_VALUE_32B, configFileValue );
                    smExecuteCommandQueue( smhandle, smaddress );
                    smGetQueuedSMCommandReturnValue( smhandle, &dummy );
                    smGetQueuedSMCommandReturnValue( smhandle, &dummy );
                    smGetQueuedSMCommandReturnValue( smhandle, &cmdSetAddressStatus );
                    smGetQueuedSMCommandReturnValue( smhandle, &cmdSetValueStatus );

                    //check if above code succeed
                    if( getCumulativeStatus(smhandle)!=SM_OK || cmdSetAddressStatus!=SMP_CMD_STATUS_ACK || cmdSetValueStatus!=SMP_CMD_STATUS_ACK )
                    {
                        SM_STATUS stat=getCumulativeStatus(smhandle);
                        setErrors++;
                        smDebug(smhandle,Low,"Failed to write parameter value %d to address %d (status: %d %d %d)\n",configFileValue,param.address,(int)stat,cmdSetAddressStatus,cmdSetValueStatus);
                    }

                    changed++;
                }
            }
            else//device doesn't have such parameter. perhaps wrong model or fw version.
            {
                ignoredCount++;
                smDebug(smhandle,Low,"Ignoring parameter parameter value %d to address %d\n",configFileValue,param.address);
            }
        }

        i++;
    } while(readOk==smtrue);

    *skippedCount=ignoredCount;
    *errorCount=setErrors;

    resetCumulativeStatus( smhandle );

    //save to flash if some value was changed
    if(changed>0)
        smSetParameter( smhandle, smaddress, SMP_SYSTEM_CONTROL, SMP_SYSTEM_CONTROL_SAVECFG );

    if(mode&CONFIGMODE_CLEAR_FAULTS_AFTER_CONFIG )
    {
        smDebug(smhandle,Low,"Restting faults\n");
        smSetParameter( smhandle, smaddress, SMP_FAULTS, 0 );//reset faults
    }

    //re-enable drive
    if(mode&CONFIGMODE_DISABLE_DURING_CONFIG)
    {
        smDebug(smhandle,Low,"Restoring CONTROL_BITS1 to value 0x%x\n",CB1Value);
        smSetParameter( smhandle, smaddress, SMP_CONTROL_BITS1, CB1Value );//restore controbits1 (enable if it was enabled before)
    }

    smint32 statusbits;
    smRead1Parameter( smhandle, smaddress, SMP_STATUS, &statusbits );

    //restart drive if necessary or if forced
    if( (statusbits&STAT_PERMANENT_STOP) || (mode&CONFIGMODE_ALWAYS_RESTART_TARGET) )
    {
        smDebug(smhandle,Low,"Restarting device\n");
        smSetParameter( smhandle, smaddress, SMP_SYSTEM_CONTROL, SMP_SYSTEM_CONTROL_RESTART );
        sleep_ms(2000);//wait power-on
    }

    if(getCumulativeStatus(smhandle)!=SM_OK)
        return CFGCommunicationError;

    return CFGComplete;
}

/**
 * @brief smGetDeviceFirmwareUniqueID Reads installed firmware binary checksum that can be used to verify whether a wanted FW version is installed
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address. Can be device in DFU mode or main operating mode. For Argon, one device in a bus must be started into DFU mode by DIP switches and smaddress must be set to 255.
 * @param UID result will be written to this pointer
 * @return smtrue if success, smfalse if failed (if communication otherwise works, then probably UID feature not present in this firmware version)
 */
smbool smGetDeviceFirmwareUniqueID( smbus smhandle, int deviceaddress, smuint32 *UID )//FIXME gives questionable value if device is in DFU mode. Check FW side.
{
    smint32 fwBinaryChecksum;
    resetCumulativeStatus(smhandle);
    smSetParameter( smhandle, deviceaddress, SMP_SYSTEM_CONTROL, SMP_SYSTEM_CONTROL_GET_SPECIAL_DATA);
    smRead1Parameter( smhandle, deviceaddress, SMP_DEBUGPARAM1, &fwBinaryChecksum );
    *UID=(smuint32) fwBinaryChecksum;

    if(getCumulativeStatus(smhandle)==SM_OK)
        return smtrue;
    else
        return smfalse;
}

FirmwareUploadStatus verifyFirmwareData(smuint8 *data, smuint32 numbytes, int connectedDeviceTypeId,
                                        smuint32 *primaryMCUDataOffset, smuint32 *primaryMCUDataLenth,
                                        smuint32 *secondaryMCUDataOffset,smuint32 *secondaryMCUDataLength )
{
    //see http://granitedevices.com/wiki/Argon_firmware_file_format

    smuint32 filetype;
    filetype=((smuint32*)data)[0];
    if(filetype!=0x57464447) //check header string "GDFW"
        return FWInvalidFile;

    smuint16 filever, deviceid;
    smuint32 primaryMCUSize, secondaryMCUSize;

    filever=((smuint16*)data)[2];
    deviceid=((smuint16*)data)[3];
    primaryMCUSize=((smuint32*)data)[2];
    secondaryMCUSize=((smuint32*)data)[3];
    if(secondaryMCUSize==0xffffffff)
        secondaryMCUSize=0;//it is not present

    if(filever!=300)
        return FWIncompatibleFW;

    if(deviceid/1000!=connectedDeviceTypeId/1000)//compare only device and model family. AABBCC so AAB is compared value, ARGON=004 IONI=011
        return FWIncompatibleFW;

    //get checksum and check it
    smuint32 cksum,cksumcalc=0;
    smuint32 i;
    smuint32 cksumOffset=4+2+2+4+4+primaryMCUSize+secondaryMCUSize;
    if(cksumOffset>numbytes-4)
        return FWInvalidFile;
    cksum=((smuint32*)((smuint32)data+cksumOffset))[0];

    for(i=0;i< numbytes-4;i++)
    {
        cksumcalc+=data[i];
    }

    if(cksum!=cksumcalc)
        return FWIncompatibleFW;

    //let caller know where the firmware data is located in buffer
    *primaryMCUDataOffset=4+2+2+4+4;
    *primaryMCUDataLenth=primaryMCUSize;
    *secondaryMCUDataOffset=*primaryMCUDataOffset+*primaryMCUDataLenth;
    *secondaryMCUDataLength=secondaryMCUSize;

    return FWComplete;
}

smbool loadBinaryFile( const char *filename, smuint8 **data, int *numbytes )
{
    FILE *f;
    f=fopen(filename,"rb");
    if(f==NULL)
        return smfalse;

    *numbytes=0;

    //get length
    fseek(f,0,SEEK_END);
    int length=ftell(f);
    fseek(f,0,SEEK_SET);

    //allocate buffer
    *data=malloc(length);
    if(*data==NULL)
        return smfalse;

    //read
    *numbytes=fread(*data,1,length,f);
    if(*numbytes!=length)//failed to read it all
    {
        free(*data);
        *numbytes=0;
        return smfalse;
    }

    return smtrue;//successl
}



//flashing STM32 (host side mcu)
smbool flashFirmwarePrimaryMCU( smbus smhandle, int deviceaddress, const smuint8 *data, smint32 size, int *progress )
{
    smint32 ret;
    static smint32 deviceType, fwVersion;
    static int uploadIndex;
    int c;
    const int BL_CHUNK_LEN=32;
    static enum {Init,Upload,Finish} state=Init;

    if(state==Init)
    {
        resetCumulativeStatus( smhandle );
        smRead2Parameters( smhandle, deviceaddress, SMP_FIRMWARE_VERSION, &fwVersion, SMP_DEVICE_TYPE,&deviceType );

        if(getCumulativeStatus(smhandle)!=SM_OK)
        {
            state=Init;
            return smfalse;
        }

/*      kommentoitu pois koska ei haluta erasoida parskuja koska parametri SMO ei saisi nollautua mielellään

        if(deviceType!=4000)//argon does not support BL function 11
            smSetParameter(smhandle,deviceaddress,SMP_BOOTLOADER_FUNCTION,11);//BL func on ioni 11 = do mass erase on STM32, also confifuration
        else//does not reset on ioni and drives that support preserving settings. but resets on argon
*/
            smSetParameter(smhandle,deviceaddress,SMP_BOOTLOADER_FUNCTION,1);//BL func 1 = do mass erase on STM32. On Non-Argon devices it doesn't reset confifuration

        sleep_ms(2000);//wait some time. 500ms too little 800ms barely enough

        //flash
        smSetParameter(smhandle,deviceaddress,SMP_RETURN_PARAM_LEN, SMPRET_CMD_STATUS);

        if(getCumulativeStatus(smhandle)!=SM_OK)
        {
            state=Init;
            return smfalse;
        }

        state=Upload;
        uploadIndex=0;
        *progress=5;
    }
    else if(state==Upload)
    {
        size/=2;//bytes to 16 bit words

        //upload data in 32=BL_CHUNK_LEN word chunks
        for(;uploadIndex<size;)
        {
            smAppendSMCommandToQueue( smhandle, SMPCMD_SETPARAMADDR, SMP_BOOTLOADER_UPLOAD );
            for(c=0;c<BL_CHUNK_LEN;c++)
            {
                smuint16 upword;
                //pad end of file with constant to make full chunk
                if(uploadIndex>=size)
                    upword=0xeeee;
                else
                    upword=((smuint16*)data)[uploadIndex];
                smAppendSMCommandToQueue( smhandle, SMPCMD_24B, upword );
                uploadIndex++;
            }

            smAppendSMCommandToQueue( smhandle, SMPCMD_SETPARAMADDR, SMP_BOOTLOADER_FUNCTION );
            smAppendSMCommandToQueue( smhandle, SMPCMD_24B, 2);//BL func 2 = do write on STM32
            smExecuteCommandQueue( smhandle, deviceaddress );

            //read return packets
            for(c=0;c<BL_CHUNK_LEN+3;c++)
            {
                smGetQueuedSMCommandReturnValue( smhandle,&ret );

                if(getCumulativeStatus(smhandle)!=SM_OK)
                {
                    state=Init;
                    return smfalse;
                }
            }

            *progress=5+90*uploadIndex/size;//gives value 5-95
            if(*progress>=94)//95 will indicate that progress is complete. dont let it indicate that yet.
                *progress=94;

            if(uploadIndex%256==0)
            {
                //printf("upload %d\n",uploadIndex);
                return smtrue;//in progress. return often to make upload non-blocking
            }
        }
        if(uploadIndex>=size)//finished
        {
            state=Finish;
        }
    }
    else if(state==Finish)
    {
        //verify STM32 flash if supported by BL version
        if(fwVersion>=1210)
        {
            smSetParameter(smhandle,deviceaddress,SMP_BOOTLOADER_FUNCTION,3);//BL func 3 = verify STM32 FW integrity
            smint32 faults;
            smRead1Parameter(smhandle,deviceaddress,SMP_FAULTS,&faults);

            if(getCumulativeStatus(smhandle)!=SM_OK)
            {
                state=Init;
                *progress=0;
                return smfalse;
            }

            if(faults&FLT_FLASHING_COMMSIDE_FAIL)
            {
                //printf("verify failed\n");
                *progress=0;
                state=Init;
                return smfalse;
            }
            else
            {
                //printf("verify success\n");
            }
        }

        *progress=95;//my job is complete
        state=Init;
    }

    return smtrue;
}


typedef enum { StatIdle=0, StatEnterDFU, StatFindDFUDevice, StatLoadFile, StatUpload, StatLaunch } UploadState;//state machine status

//free buffer and return given status code
FirmwareUploadStatus abortFWUpload( FirmwareUploadStatus stat, smuint8 *fwData, UploadState *state, int errorDetailCode )
{
    globalErrorDetailCode=errorDetailCode;
    *state=StatIdle;
    free(fwData);
    return stat;
}

/**
 * @brief smFirmwareUpload Sets drive in firmware upgrade mode if necessary and uploads a new firmware. Call this many until it returns value 100 (complete) or a negative value (error).
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address. Can be device in DFU mode or main operating mode. For Argon, one device in a bus must be started into DFU mode by DIP switches and smaddress must be set to 255.
 * @param filename .gdf file name
 * @return Enum FirmwareUploadStatus that indicates errors or Complete status. Typecast to integer to get progress value 0-100.
 */
FirmwareUploadStatus smFirmwareUpload( const smbus smhandle, const int smaddress, const char *firmware_filename )
{
    static smuint8 *fwData=NULL;
    static int fwDataLength;
    static smuint32 primaryMCUDataOffset, primaryMCUDataLenth;
    static smuint32 secondaryMCUDataOffset,secondaryMCUDataLength;
    static UploadState state=StatIdle;//state machine status
    static smint32 deviceType=0;
    static int DFUAddress;
    static int progress=0;

    SM_STATUS stat;

    //state machine
    if(state==StatIdle)
    {

        //check if device is in DFU mode already
        smint32 busMode;
        stat=smRead2Parameters(smhandle,smaddress,SMP_BUS_MODE,&busMode, SMP_DEVICE_TYPE, &deviceType);
        if(stat==SM_OK && busMode==SMP_BUS_MODE_DFU)
        {
            state=StatLoadFile;
        }
        else if(stat==SM_OK && busMode!=0)//device not in bus mode
        {
            if(deviceType==4000)//argon does not support restarting in DFU mode by software
            {
                return abortFWUpload(FWConnectionError,fwData,&state,200);
            }

            //restart device into DFU mode
            state=StatEnterDFU;

            stat=smSetParameter(smhandle,smaddress,SMP_SYSTEM_CONTROL,64);//reset device to DFU command
            if(stat!=SM_OK)
                return abortFWUpload(FWConnectionError,fwData,&state,300);
        }
        else
            state=StatFindDFUDevice;//search DFU device in brute force, fallback for older BL versions that don't preserve same smaddress than non-DFU mode
            //return abortFWUpload(FWConnectionError,fwData,&state,301);

        progress=1;
        DFUAddress=smaddress;
    }

    else if(state==StatEnterDFU)
    {
        sleep_ms(2500);//wait device to reboot in DFU mode. probably shorter delay would do.

        //check if device is in DFU mode already
        smint32 busMode;
        stat=smRead2Parameters(smhandle,smaddress, SMP_BUS_MODE,&busMode, SMP_DEVICE_TYPE, &deviceType);
        if(stat==SM_OK && busMode==0)//busmode 0 is DFU mode
        {
            state=StatLoadFile;
        }
        else
            state=StatFindDFUDevice;//search DFU device in brute force, fallback for older BL versions that don't preserve same smaddress than non-DFU mode

        progress=2;
    }

    else if(state==StatFindDFUDevice)
    {
        int i;
        for(i=245;i<=255;i++)
        {
            smint32 busMode;
            stat=smRead2Parameters(smhandle,i, SMP_BUS_MODE,&busMode, SMP_DEVICE_TYPE, &deviceType);
            if(stat==SM_OK && busMode==0)//busmode 0 is DFU mode
            {
                state=StatLoadFile;
                DFUAddress=i;
                break;//DFU found, break out of for loop
            }
        }

        if(i==256)//DFU device not found
            return abortFWUpload(CFGConnectingDFUModeFailed,fwData,&state,400);//setting DFU mode failed

        progress=3;
    }

    else if(state==StatLoadFile)
    {
        if(loadBinaryFile(firmware_filename,&fwData,&fwDataLength)!=smtrue)
            return FWFileNotReadable;

        FirmwareUploadStatus stat=verifyFirmwareData(fwData, fwDataLength, deviceType,
                                  &primaryMCUDataOffset, &primaryMCUDataLenth,
                                  &secondaryMCUDataOffset, &secondaryMCUDataLength);
        if(stat!=FWComplete)//error in verify
        {
            return abortFWUpload(stat,fwData,&state,100);
        }

        //all good, upload firmware
        state=StatUpload;

        progress=4;
    }

    else if(state==StatUpload)
    {
        smbool ret=flashFirmwarePrimaryMCU(smhandle,DFUAddress,fwData+primaryMCUDataOffset,primaryMCUDataLenth,&progress);
        if(ret==smfalse)//failed
        {
            return abortFWUpload(FWConnectionError,fwData,&state,1000);
        }
        else
        {
            if(progress>=95)
                state=StatLaunch;
        }
    }

    else if(state==StatLaunch)
    {
        smSetParameter(smhandle,DFUAddress,SMP_BOOTLOADER_FUNCTION,4);//BL func 4 = launch.
        sleep_ms(2000);
        progress=100;
        state=StatIdle;
    }

    return (FirmwareUploadStatus)progress;
}




