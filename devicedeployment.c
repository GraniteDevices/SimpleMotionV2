#include "devicedeployment.h"
#include "user_options.h"
#include "crc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <simplemotion_private.h>
#include <math.h>


//wait some time after device is started/restarted. 500ms too little for some devices, 800ms was barely enough
#define SM_DEVICE_POWER_UP_WAIT_MS 1500
//waiting time after save config command
#define SM_DEVICE_SAVE_CONFIG_WAIT_MS 200



/* DRC file format versions:
 *
 * 110 - first public  version
 *
 * 111 - 5.7.2019 introduced witg Granity 1.14.3:
 * - Some values are represented in exponential format like 1.23e-5 (may happen also in 110 but it has not yet occurred).
 * - Adds FileFeatureBits field. Reading software must support all bits here to properly read file. See list of FileFeatureBits field bits in DRC file below.
 * - Adds FileFeatureBitsEssential field that is subset of FileFeatureBits and indicates bits that are required to be supported by loading app.
 */

/* List of FileFeatureBits field bits in DRC file */
#define DRC_FEATURE_V110_STRUCTURE 1 //key/value fomat is same as in v110 files
#define DRC_FEATURE_EXPONENTIAL_VALUE_FORMAT 2 //some values are represented in exponential format like 1.23e-5 (may happen also in vanilla 110 but it has not yet occurred)
#define DRC_FEATURE_HAS_FILE_COMMENT_FIELD 4 //has comment/description field

/* defines for this library */
#define DRC_LOAD_VERSION_MIN 110
#define DRC_LOAD_VERSION_MAX 111
#define DRC_FILE_LOAD_SUPPORTED_FEATUREBITS (DRC_FEATURE_V110_STRUCTURE|DRC_FEATURE_EXPONENTIAL_VALUE_FORMAT) //all bits that are accepted in file to be loaded in this app
#define DRC_FILE_LOAD_REQUIRED_FEATUREBITS (DRC_FEATURE_V110_STRUCTURE) //bits that are required in file to be loaded in this app


int globalErrorDetailCode=0;

bool loadBinaryFile( const char *filename, uint8_t **data, int *numbytes, bool addNullTermination );

int smGetDeploymentToolErrroDetail()
{
    return globalErrorDetailCode;
}

//return -1 if EOF
//readPosition should be initialized to 0 and not touched by caller after that. this func will increment it after each call.
unsigned int readFileLine( const uint8_t *data, const int dataLen, int *readPosition, int charlimit, char *output, bool *eof)
{
    int len=0;
    char c;
    do
    {
        if((*readPosition)>=dataLen)//end of data buffer
        {
            *eof=true;
            c=0;
        }
        else
        {
            *eof=false;
            c=data[(*readPosition)];
            (*readPosition)++;
        }

        //eol or eof
        if(*eof || c=='\n' || c=='\r' || len>=charlimit-1)
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
    bool readOnly;
    double scale;
    double offset;
} Parameter;

#define maxLineLen 100

//returns byte offset where str starts, or -1 if not found before data ends
int findSubstring( const uint8_t *data, const int dataLen, const char *str )
{
	int strLen=strlen(str);
	int i;
	for(i=0;i<dataLen-strLen;i++)
	{
		if(strncmp((const char*)(data+i),str,strLen)==0)
			return i; // found
	}
	return -1;// not found
}

//parse real number out of null terminated string formatted like "-123.5436"
//read ends on first whitespace, \ņ \r \0 or 'e'
//returns 0 if fail, 1 if success
int decimalNumberToDouble( const char *str, double *output )
{
    double out=0, decimalcoeff=1.0;
    int i=0;
    bool decimalPointFound=false;
    bool done=false;
    while(!done)
    {
        char c=str[i];
        int number=c-'0';//char to value 0-9

        if(c=='-' && i==0) //is minus
        {
            decimalcoeff=-1.0;
        }
        else if(c=='+' && i==0) //is plus, ignore
        {
        }
        else if(number>=0 && number <=9) //is number
        {
            out=10.0*out+number;
            if(decimalPointFound)
                decimalcoeff*=0.1;
        }
        else if (c=='.')//is decimal point
        {
            decimalPointFound=true;
        }
        else if(c=='\n' || c=='\r' || c==' ' || c==0 || c=='e' )//end of string
        {
            done=true;
        }
        else//bad charachter
        {
            return 0;
        }

        i++;
    }

    *output=out*decimalcoeff;
    return 1;
}

//parse real number out of null terminated string formatted like "-123.5436" or "-12e+5"
//read ends on first whitespace, \ņ \r or \0
//returns 0 if fail, 1 if success
int stringToDouble( const char *str, double *output )
{
    int i=0;
    int exponentAt=-1;

    //detect format: decimal or exponent
    for(i=0;;i++)
    {
        char c=str[i];
        if(c=='e' )//has exponent format
        {
            exponentAt=i;
            break;
        }
        else if(c=='\n' || c=='\r' || c==' ' || c==0 )//end of string
            break;
    }

    if(exponentAt==-1)//no exponent
    {
        return decimalNumberToDouble(str,output);
    }
    else//with exponent, read base and exponent separately
    {
        double base, exponent;
        if( decimalNumberToDouble(str,&base)!=1 ) //read base
            return 0;//failed
        if( decimalNumberToDouble(str+i+1,&exponent)!=1 ) //read exponent starting after 'e'
            return 0;//failed

        *output=base*pow(10,exponent);
        return 1;
    }
}

//parse real number out of null terminated string formatted like "-123"
//read ends on first whitespace, \ņ \r or \0
//returns 0 if fail, 1 if success
int stringToInt( const char *str, int *output )
{
    int out=0;
    int i=0;
    int coeff=1;
    bool done=false;
    while(!done)
    {
        char c=str[i];
        int number=c-'0';//char to value 0-9

        if(c=='-' && i==0) //is minus
        {
            coeff=-1;
        }
        else if(number>=0 && number <=9) //is number
        {
            out=10*out+number;
        }
        else if(c=='\n' || c=='\r' || c==' ' || c==0 )//end of string
        {
            done=true;
        }
        else//bad charachter
        {
            return 0;
        }

        i++;
    }

    *output=out*coeff;
    return 1;
}

bool parseDRCIntKey( const uint8_t *drcData, const int drcDataLen, const char *key, int *outValue )
{
    int pos=findSubstring(drcData,drcDataLen,key);
    if(pos<0)
        return false; //key not found

    if( stringToInt((char*)(&drcData[pos]+strlen(key)),outValue) != 1)
        return false; //parse failed

    return true;
}

//read DRC file version nr and nubmer of params it contains. if succes, returns true. if invalid file, return false
bool parseDRCInfo( const uint8_t *drcData, const int drcDataLen, int *DRCVersion, int *numParams, int *DRCFileFeatureBits, int *DRCEssentialFileFeatureBits )
{
    const char *DRCFileFeatureBitsKey="FileFeatureBits=";
    const char *DRCEssentialFileFeatureBitsKey="FileFeatureBitsEssential=";

    if(!parseDRCIntKey(drcData,drcDataLen,"DRCVersion=",DRCVersion)) return false; //parse failed
    if(!parseDRCIntKey(drcData,drcDataLen,"size=",numParams)) return false; //parse failed

    //v 111 and beyond should have file feature bits defined
    if(*DRCVersion>=111)
    {
        if(!parseDRCIntKey(drcData,drcDataLen,"FileFeatureBits=",DRCFileFeatureBits)) return false; //parse failed
        if(!parseDRCIntKey(drcData,drcDataLen,"FileFeatureBitsEssential=",DRCEssentialFileFeatureBits)) return false; //parse failed
    }
    else//older format, set it based on knowledge:
    {
        *DRCFileFeatureBits=DRC_FEATURE_V110_STRUCTURE;
        *DRCEssentialFileFeatureBits=DRC_FEATURE_V110_STRUCTURE;
    }

    //extra sanity check
    if( *numParams<1 || *numParams>1000 )
        return false;

    //extra sanity check
    if( *DRCVersion<110 || *DRCVersion>10000 ) //110 is lowest released drc version
        return false;

    return true;
}

bool parseParameter( const uint8_t *drcData, const int drcDataLen, int idx, Parameter *param )
{
    char line[maxLineLen];
    char scanline[maxLineLen];
    bool gotaddr=false,gotvalue=false, gotreadonly=false, gotscale=false,gotoffset=false;
    unsigned int readbytes;
    int readPosition=0;
    bool eof;

    //optimization below: finds starting offset where the wanted idx parameter will be available in drcData.
    //i.e. find start offset of line that starts with "50\" (for idx=50)
    if(idx>999)
    	return false;//parseParam supports only idx 1-999 because startingTag has fixed length
    char startingTag[6];
    sprintf(startingTag,"\n%d\\",idx);
    readPosition=findSubstring(drcData,drcDataLen,startingTag);
    if(readPosition<0)//such parameter not found in data
    	return false;

    do//loop trhu all lines of file
    {
        readbytes=readFileLine(drcData,drcDataLen,&readPosition,maxLineLen,line,&eof);//read line
        if(readbytes<=1)//empty line or only linefeed character, read another to avoid wasting time below
        	readbytes=readFileLine(drcData,drcDataLen,&readPosition,maxLineLen,line,&eof);//read line

        //try read address
        sprintf(scanline,"%d\\addr=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(stringToInt(line+strlen(scanline),&param->address)==1)//parse number after the start of line
                gotaddr=true;//number parse success

        //try read value
        sprintf(scanline,"%d\\value=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(stringToDouble(line+strlen(scanline),&param->value)==1)//parse number after the start of line
                gotvalue=true;//number parse success

        //try read offset
        sprintf(scanline,"%d\\offset=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(stringToDouble(line+strlen(scanline),&param->offset)==1)//parse number after the start of line
                gotoffset=true;//number parse success

        //try read scale
        sprintf(scanline,"%d\\scaling=",idx);
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes > strlen(scanline))//string starts with correct line
            if(stringToDouble(line+strlen(scanline),&param->scale)==1)//parse number after the start of line
                gotscale=true;//number parse success

        //try read readonly status
        sprintf(scanline,"%d\\readonly=true",idx);//check if readonly=true
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes >= strlen(scanline))//line match
        {
            param->readOnly=true;
            gotreadonly=true;
        }
        sprintf(scanline,"%d\\readonly=false",idx);//check if readonly=false
        if(!strncmp(line,scanline,strlen(scanline)) && readbytes >= strlen(scanline))//line match
        {
            param->readOnly=false;
            gotreadonly=true;
        }
    }
    while ((!gotvalue || !gotaddr || !gotreadonly || !gotscale || !gotoffset) && !eof);

    if(gotvalue&&gotaddr&&gotoffset&&gotscale&&gotreadonly)
    {
        return true;
    }

    return false;//not found
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
    LoadConfigurationStatus ret;
    uint8_t *drcData=NULL;
    int drcDataLength;

    if(!loadBinaryFile(filename,&drcData,&drcDataLength,true))
        return CFGUnableToOpenFile;

    ret = smLoadConfigurationFromBuffer( smhandle, smaddress, drcData, drcDataLength, mode, skippedCount, errorCount );
    free(drcData);

    return ret;
}

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
LIB LoadConfigurationStatus smLoadConfigurationFromBuffer( const smbus smhandle, const int smaddress, const uint8_t *drcData, const int drcDataLength, unsigned int mode, int *skippedCount, int *errorCount )
{
    smDebug(smhandle,SMDebugLow,"smLoadConfigurationFromBuffer for SM address %d called\n",smaddress);

    //test connection
    int32_t devicetype;
    SM_STATUS stat;
    int ignoredCount=0;
    int setErrors=0;
    int32_t CB1Value;
    int changed=0;
    int numParams, DRCVersion;
    int DRCFileFeatureBits, DRCEssentialFileFeatureBits;
    *skippedCount=-1;
    *errorCount=-1;
    bool deviceDisabled=false;

    //parse DRC header
    if(!parseDRCInfo(drcData,drcDataLength,&DRCVersion,&numParams,&DRCFileFeatureBits,&DRCEssentialFileFeatureBits))
        return CFGInvalidFile;

    //check if essential bits have something that is not in feature bits (file sanity check error)
    if( (~DRCFileFeatureBits) & DRCEssentialFileFeatureBits)
    {
        smDebug(smhandle,SMDebugLow,"Broken DRC file (DRC version %d with feature bits %d and essential feature bits of %d)\n",DRCVersion,DRCFileFeatureBits,DRCEssentialFileFeatureBits);
        return CFGInvalidFile;
    }

    //check file version & flags
    if( (DRCVersion<DRC_LOAD_VERSION_MIN || DRCVersion>DRC_LOAD_VERSION_MAX) //version is not in correct range -> reject file
            || DRCEssentialFileFeatureBits&(~DRC_FILE_LOAD_SUPPORTED_FEATUREBITS) //unaccepted/unknown essential featurebits are present -> reject file
            || (DRCFileFeatureBits&DRC_FILE_LOAD_REQUIRED_FEATUREBITS)!=DRC_FILE_LOAD_REQUIRED_FEATUREBITS ) //required bits are not present -> reject file
    {
        smDebug(smhandle,SMDebugLow,"Unsupported DRC file type or version (DRC version %d with feature bits %d and essential feature bits of %d)\n",DRCVersion,DRCFileFeatureBits,DRCEssentialFileFeatureBits);
        return CFGUnsupportedFileVersion;
    }

    //test connection
    resetCumulativeStatus(smhandle);
    stat=smRead1Parameter(smhandle,smaddress,SMP_DEVICE_TYPE,&devicetype);
    if(stat!=SM_OK)
        return CFGCommunicationError;

    if(getCumulativeStatus( smhandle )!=SM_OK )
        return CFGCommunicationError;

    smDebug(smhandle,SMDebugLow,"Setting parameters\n");

    bool readOk;
    int i;
    for( i=1; i<numParams; i++ )
    {
        Parameter param;
        readOk=parseParameter(drcData,drcDataLength,i,&param);

        if(!readOk) //corrupted file
        {
            *skippedCount=ignoredCount;
            *errorCount=setErrors;
            return CFGInvalidFile;
        }

        if(!param.readOnly)
        {
            int32_t currentValue;

            int configFileValue=round(param.value*param.scale-param.offset);

            //set parameter to device
            if(smRead1Parameter( smhandle, smaddress, param.address, &currentValue )==SM_OK)
            {
                if(currentValue!=configFileValue  ) //set only if different
                {
                    smDebug(smhandle,SMDebugLow,"Config file parameter nr %d differs from taraget's value (target value %d, new value %d)\n",
                            param.address,currentValue,configFileValue);

                    //disable device only only if at least one parameter has changed, and disalbe is configured by CONFIGMODE_DISABLE_DURING_CONFIG flag.
                    //deviceDisabled is compared so it gets disabled only at first parameter, not consequent ones
                    if(mode&CONFIGMODE_DISABLE_DURING_CONFIG && !deviceDisabled)
                    {
                    	smDebug(smhandle,SMDebugLow,"Drive will be disabled as requested because of parameter change\n");

                        smRead1Parameter( smhandle, smaddress, SMP_CONTROL_BITS1, &CB1Value );
                        smSetParameter( smhandle, smaddress, SMP_CONTROL_BITS1, 0);//disable drive
                        deviceDisabled=true;
                    }

                    resetCumulativeStatus( smhandle );
                    int32_t dummy;
                    int32_t cmdSetAddressStatus;
                    int32_t cmdSetValueStatus;

                    smDebug(smhandle,SMDebugMid,"Writing parameter addr %d value %d\n",param.address,configFileValue);
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
                        smDebug(smhandle,SMDebugLow,"Failed to write parameter value %d to address %d (status: %d %d %d)\n",configFileValue,param.address,(int)stat,cmdSetAddressStatus,cmdSetValueStatus);
                    }

                    changed++;
                }
            }
            else//device doesn't have such parameter. perhaps wrong model or fw version.
            {
                ignoredCount++;
                smDebug(smhandle,SMDebugLow,"Ignoring read-only parameter parameter value %d to address %d\n",configFileValue,param.address);
            }
        }
    }

    *skippedCount=ignoredCount;
    *errorCount=setErrors;

    //if device supports SMP_SYSTEM_CONTROL_TRIGGER_PENDING_PARAMETER_ACTIVATION, call it to make sure that all parameters get effective without reboot
    {
        bool result;
        smCheckDeviceCapabilities( smhandle, smaddress, SMP_DEVICE_CAPABILITIES2, DEVICE_CAPABILITY2_SUPPORT_TRIGGER_PENDING_PARAMETER_ACTIVATION, &result);
        if(result)
            smSetParameter( smhandle, smaddress, SMP_SYSTEM_CONTROL, SMP_SYSTEM_CONTROL_TRIGGER_PENDING_PARAMETER_ACTIVATION );
    }

    resetCumulativeStatus( smhandle );

    //save to flash if some value was changed
    if(changed>0)
    {
        smSetParameter( smhandle, smaddress, SMP_SYSTEM_CONTROL, SMP_SYSTEM_CONTROL_SAVECFG );
        smSleepMs(200);//wait save command to complete on hardware before new commands
    }

    if(mode&CONFIGMODE_CLEAR_FAULTS_AFTER_CONFIG )
    {
        smDebug(smhandle,SMDebugLow,"Resetting faults\n");
        smSetParameter( smhandle, smaddress, SMP_FAULTS, 0 );//reset faults
    }

    //re-enable drive
    if(mode&CONFIGMODE_DISABLE_DURING_CONFIG && deviceDisabled)
    {
        smDebug(smhandle,SMDebugLow,"Restoring CONTROL_BITS1 to value 0x%x\n",CB1Value);
        smSetParameter( smhandle, smaddress, SMP_CONTROL_BITS1, CB1Value );//restore controbits1 (enable if it was enabled before)
    }

    int32_t statusbits;
    smRead1Parameter( smhandle, smaddress, SMP_STATUS, &statusbits );

    //restart drive if necessary or if forced
    if( (statusbits&STAT_PERMANENT_STOP) || (mode&CONFIGMODE_ALWAYS_RESTART_TARGET) )
    {
        if(statusbits&STAT_PERMANENT_STOP)
            smDebug(smhandle,SMDebugLow,"Restarting device because drive permanent stop status requires it to continue\n");
        if(mode&CONFIGMODE_ALWAYS_RESTART_TARGET)
            smDebug(smhandle,SMDebugLow,"Restarting device because caller has requested restart always\n");

        smSetParameter( smhandle, smaddress, SMP_SYSTEM_CONTROL, SMP_SYSTEM_CONTROL_RESTART );
        smSleepMs(SM_DEVICE_POWER_UP_WAIT_MS);//wait power-on
        smPurge(smhandle);
    }

    if(getCumulativeStatus(smhandle)!=SM_OK)
        return CFGCommunicationError;


    smDebug(smhandle,SMDebugMid,"smLoadConfigurationFromBuffer finished\n");

    return CFGComplete;
}

/**
 * @brief smGetDeviceFirmwareUniqueID Reads installed firmware binary checksum that can be used to verify whether a wanted FW version is installed
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address. Can be device in DFU mode or main operating mode. For Argon, one device in a bus must be started into DFU mode by DIP switches and smaddress must be set to 255.
 * @param UID result will be written to this pointer
 * @return true if success, false if failed (if communication otherwise works, then probably UID feature not present in this firmware version). Note: some devices' DFU/bootloader mode don't support this command but main firmware do.
 */
bool smGetDeviceFirmwareUniqueID( smbus smhandle, int deviceaddress, uint32_t *UID )
{
    smDebug(smhandle,SMDebugMid,"smGetDeviceFirmwareUniqueID called\n");

    int32_t fwBinaryChecksum, commandStatus;
    resetCumulativeStatus(smhandle);
    smSetParameter( smhandle, deviceaddress, SMP_CUMULATIVE_STATUS, 0);//reset any SM access error that might SM target might have active so we can test whether following command succeeds
    smSetParameter( smhandle, deviceaddress, SMP_SYSTEM_CONTROL, SMP_SYSTEM_CONTROL_GET_SPECIAL_DATA);
    smRead2Parameters( smhandle, deviceaddress, SMP_DEBUGPARAM1, &fwBinaryChecksum, SMP_CUMULATIVE_STATUS, &commandStatus );

    *UID=(uint32_t) fwBinaryChecksum;

    if(getCumulativeStatus(smhandle)==SM_OK && commandStatus==SMP_CMD_STATUS_ACK)//if commandStatus==SMP_CMD_STATUS_ACK fails, then FW or curren mode of device doesn't support reading this
        return true;
    else
        return false;
}

/* local helper functions to get 8, 16 or 32 bit value from data buffer.
 * these will increment *buf pointer on each call.
 *
 * FYI: motivation of using these, is that simple pointer cast to various sizes
 * might not work on all CPU architectures due to data alignment restrictions.
*/


uint8_t bufferGet8( uint8_t **buf )
{
    uint8_t ret=(*buf)[0];
    buf++;
    return ret;
}

uint16_t bufferGet16( uint8_t **buf )
{
    UnionOf4Bytes d;
    d.U8[0]=(*buf)[0];
    d.U8[1]=(*buf)[1];

    uint16_t ret=d.U16[0];

    *buf+=2;
    return ret;
}

uint32_t bufferGet32( uint8_t **buf )
{
    UnionOf4Bytes d;
    d.U8[0]=(*buf)[0];
    d.U8[1]=(*buf)[1];
    d.U8[2]=(*buf)[2];
    d.U8[3]=(*buf)[3];

    uint32_t ret=d.U32;

    *buf+=4;
    return ret;
}


FirmwareUploadStatus parseFirmwareFile(uint8_t *data, uint32_t numbytes, uint32_t connectedDeviceTypeId,
                                        uint32_t *primaryMCUDataOffset, uint32_t *primaryMCUDataLenth,
                                        uint32_t *secondaryMCUDataOffset,uint32_t *secondaryMCUDataLength,
                                        uint32_t *FWUniqueID )
{
    smDebug(-1,SMDebugMid,"parseFirmwareFile called\n");

    //see https://granitedevices.com/wiki/Firmware_file_format_(.gdf)

    uint32_t filetype;
    filetype=((uint32_t*)data)[0];
    if(filetype!=0x57464447) //check header string "GDFW"
        return FWInvalidFile;

    uint32_t filever, deviceid;
    *FWUniqueID=0;//updated later if available

    filever=((uint16_t*)data)[2];

    if(filever==300)//handle GDF version 300 here
    {
        uint32_t primaryMCUSize, secondaryMCUSize;

        deviceid=((uint16_t*)data)[3];
        primaryMCUSize=((uint32_t*)data)[2];
        secondaryMCUSize=((uint32_t*)data)[3];
        if(secondaryMCUSize==0xffffffff)
            secondaryMCUSize=0;//it is not present

        if(deviceid/1000!=connectedDeviceTypeId/1000)//compare only device and model family. AABBCC so AAB is compared value, ARGON=004 IONI=011
            return FWIncompatibleFW;

        //get checksum and check it
        uint32_t cksum,cksumcalc=0;
        uint32_t i;
        uint32_t cksumOffset=4+2+2+4+4+primaryMCUSize+secondaryMCUSize;
        if(cksumOffset>numbytes-4)
            return FWInvalidFile;
        cksum=((uint32_t*)(data+cksumOffset))[0];

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
    }
    else if(filever>=400 && filever<500)//handle GDF versions 400-499 here
    {
        /* GDF version 400 format
         * ----------------------
         *
         * Note: GDF v400 is not limited to firmware files any more,
         * it can be used as general purpose data container for up to 4GB data chunks.
         *
         * Binary file contents
         * --------------------
         *
         * bytes  meaning:
         *
         * 4	ASCII string = "GDFW"
         * 2	GDF version = 400
         * 2	GDF backwards compatible version = 400
         * 4	File category = 100 for firmware files  (value range <2^31 is reserved and managed by GD, range from 2^31 to 2^32-1 are free for use by anyone for any purpose)
         * 4	number of data chunks in file = N
         *
         * repeat N times:
         * 4	data chunk descriptive name string length in bytes = L
         * L	data chunk descriptive name string in UTF8
         * 4	data chunk type
         * 4	data chunk option bits
         * 4	data chunk size in bytes=S
         * S	data
         * end of repeat
         *
         * 4	file's CRC-32
         *
         * data chunk types
         * ----------------
         * 0=target device name, UTF8
         * 1=firmware name, UTF8
         * 2=firmware version string, UTF8
         * 3=remarks, UTF8
         * 4=manufacturer, UTF8
         * 5=copyright, UTF8
         * 6=license, UTF8
         * 7=disclaimer, UTF8
         * 8=circulation, UTF8 (i.e. customer name)
         * 20=unix timestamp divided by 4, S=4
         * 50=target device type ID range, S=8
         *   first 4 bytes are lowest supported target device ID, i.e. 11000=IONI
         *   second 4 bytes are highest supported target device ID's, i.e. 11200=IONI PRO HC
         * 100=main MCU FW binary, S=any
         * 101=main MCU FW unique identifier number, S=4
         * 102=main MCU FW required HW feature bits, S=4
         *     helps to determine whether FW works on target
         *     device version when compared to a value readable from the device.
         *     0 means no requirements, works on all target ID devices.
         * 200=secondary MCU FW binary, S=any
         *
         * note: firmware may contain many combinations of above chunks. in basic case, it contains just chunk type 100 and nothing else.
         *
         * data chunk option bits
         * ----------------------
         * bit 0: if 1, GDF loading application must support/understand the chunk type to use this file
         * bits 1-31: reserved
         *
         */
        uint8_t *dataPtr=&data[6];//start at byte 6 because first 6 bytes are already read
        uint8_t *dataCRCPtr=&data[numbytes-4];
        uint32_t numOfChunks;
        uint32_t deviceid_max;
        uint32_t chunkType;
        uint32_t chunkTypeStringLen;
        uint32_t chunkSize;
        uint16_t chunkOptions;
        uint32_t i;

        //check file compatibility
        if(bufferGet16(&dataPtr)!=400)
            return FWIncompatibleFW;//this version of library requires file that is backwards compatible with version 400

        //check file category (100=firmware file)
        if(bufferGet32(&dataPtr)!=100)
            return FWInvalidFile;//not FW GDF file

        //check file CRC
        crcInit();
        crc calculatedCRC=crcFast((const unsigned char*)data, numbytes-4);
        crc fileCRC=bufferGet32(&dataCRCPtr);
        if(calculatedCRC!=fileCRC)
            return FWInvalidFile;//CRC mismatch

        //read data chunks
        numOfChunks=bufferGet32(&dataPtr);
        for( i=0; i<numOfChunks; i++ )
        {
            chunkTypeStringLen=bufferGet32(&dataPtr);
            dataPtr+=chunkTypeStringLen;//skip type string, we don't use it for now
            chunkType=bufferGet32(&dataPtr);
            chunkOptions=bufferGet32(&dataPtr);
            chunkSize=bufferGet32(&dataPtr);

            //handle chunk
            if(chunkType==50 && chunkSize==8)
            {
                //check target device type
                deviceid=bufferGet32(&dataPtr);
                deviceid_max=bufferGet32(&dataPtr);

                if(connectedDeviceTypeId<deviceid || connectedDeviceTypeId>deviceid_max)
                    return FWUnsupportedTargetDevice;
            }
            else if(chunkType==100)//main MCU FW
            {
                *primaryMCUDataOffset=(uint32_t)(dataPtr-data);
                *primaryMCUDataLenth=chunkSize;
                dataPtr+=chunkSize;//skip to next chunk
            }
            else if(chunkType==200)//secondary MCU FW
            {
                *secondaryMCUDataOffset=(uint32_t)(dataPtr-data);
                *secondaryMCUDataLength=chunkSize;
                dataPtr+=chunkSize;//skip to next chunk
            }
            else if(chunkType==101 && chunkSize==4)//main MCU FW unique identifier
            {
                *FWUniqueID=bufferGet32(&dataPtr);
            }
            else if(chunkOptions&1) //bit nr 0 is 1, which means we should be able to handle this chunk to support the GDF file, so as we don't know what chunk it is, this is an error
            {
                return FWIncompatibleFW;
            }
            else //unsupported chunk that we can skip
            {
                dataPtr+=chunkSize;//skip to next chunk
            }
        }

        if((uint32_t)(dataPtr-data)!=numbytes-4)//check if chunks total size match file size
            return FWInvalidFile;
    }
    else
        return FWIncompatibleFW;//unsupported file version

    return FWComplete;
}

/**
 * @brief smFirmwareUploadStatusToString converts FirmwareUploadStatus enum to string.
 * @param string user supplied pointer where string will be stored. must have writable space for at least 100 characters.
 * @return void
 */
void smFirmwareUploadStatusToString(const FirmwareUploadStatus FWUploadStatus, char *string )
{
    int i;
    const int count=sizeof(FirmwareUploadStatusToString)/sizeof(FirmwareUploadStatusToStringType);

    for(i=0;i<count;i++)
    {
        if(FirmwareUploadStatusToString[i].FWUSEnum==FWUploadStatus)
        {
            strcpy(string,FirmwareUploadStatusToString[i].string);
            return;
        }
    }

    if((int)FWUploadStatus>=0 && (int)FWUploadStatus<=99 )
    {
        snprintf( string, 100, "Firmware install %d%% done", (int)FWUploadStatus);
        return;
    }

    snprintf( string, 100, "SimpleMotion lib error: unknown FW upload state (%d), please check input or report a bug", (int)FWUploadStatus);
}


bool loadBinaryFile(const char *filename, uint8_t **data, int *numbytes , bool addNullTermination)
{
    FILE *f;
    f=fopen(filename,"rb");
    if(f==NULL)
        return false;

    *numbytes=0;

    //get length
    fseek(f,0,SEEK_END);
    int length=ftell(f);
    fseek(f,0,SEEK_SET);

    //allocate buffer
    if(addNullTermination)
        *data=malloc(length+1);//+1 for 0 termination char
    else
        *data=malloc(length);

    if(*data==NULL)
    {
        fclose(f);
        return false;
    }

    if(addNullTermination)
        (*data)[length]=0;//add 0 termination character at the end, this 0 prevents parse function doing buffer overflow if file is corrupt

    //read
    *numbytes=fread(*data,1,length,f);
    if(*numbytes!=length)//failed to read it all
    {
        free(*data);
        *numbytes=0;
        fclose(f);
        return false;
    }

    fclose(f);
    return true;//successl
}



//flashing STM32 (host side mcu)
bool flashFirmwarePrimaryMCU( smbus smhandle, int deviceaddress, const uint8_t *data, int32_t size, int *progress )
{
    int32_t ret;
    static int32_t deviceType, fwVersion;
    static int uploadIndex;
    int c;
    const int BL_CHUNK_LEN=32;
    static enum {Init,Upload,Finish} state=Init;

    if(state==Init)
    {
        smDebug(smhandle,SMDebugLow,"flashFirmwarePrimaryMCU: Init\n");

        resetCumulativeStatus( smhandle );
        smRead2Parameters( smhandle, deviceaddress, SMP_FIRMWARE_VERSION, &fwVersion, SMP_DEVICE_TYPE,&deviceType );

        if(getCumulativeStatus(smhandle)!=SM_OK)
        {
            state=Init;
            return false;
        }

/*      kommentoitu pois koska ei haluta erasoida parskuja koska parametri SMO ei saisi nollautua mielellään

        if(deviceType!=4000)//argon does not support BL function 11
            smSetParameter(smhandle,deviceaddress,SMP_BOOTLOADER_FUNCTION,11);//BL func on ioni 11 = do mass erase on STM32, also confifuration
        else//does not reset on ioni and drives that support preserving settings. but resets on argon
*/
            smSetParameter(smhandle,deviceaddress,SMP_BOOTLOADER_FUNCTION,1);//BL func 1 = do mass erase on STM32. On Non-Argon devices it doesn't reset confifuration

        smSleepMs(SM_DEVICE_POWER_UP_WAIT_MS+2);

        //flash
        smSetParameter(smhandle,deviceaddress,SMP_RETURN_PARAM_LEN, SMPRET_CMD_STATUS);

        if(getCumulativeStatus(smhandle)!=SM_OK)
        {
            state=Init;
            return false;
        }

        state=Upload;
        uploadIndex=0;
        *progress=5;
    }
    else if(state==Upload)
    {
        smDebug(smhandle,SMDebugMid,"flashFirmwarePrimaryMCU: Upload\n");

        size/=2;//bytes to 16 bit words

        //upload data in 32=BL_CHUNK_LEN word chunks
        for(;uploadIndex<size;)
        {
            smAppendSMCommandToQueue( smhandle, SMPCMD_SETPARAMADDR, SMP_BOOTLOADER_UPLOAD );
            for(c=0;c<BL_CHUNK_LEN;c++)
            {
                uint16_t upword;
                //pad end of file with constant to make full chunk
                if(uploadIndex>=size)
                    upword=0xeeee;
                else
                    upword=((uint16_t*)data)[uploadIndex];
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
                    return false;
                }
            }

            *progress=5+90*uploadIndex/size;//gives value 5-95
            if(*progress>=94)//95 will indicate that progress is complete. dont let it indicate that yet.
                *progress=94;

            if(uploadIndex%256==0)
            {
                //printf("upload %d\n",uploadIndex);
                return true;//in progress. return often to make upload non-blocking
            }
        }
        if(uploadIndex>=size)//finished
        {
            state=Finish;
        }
    }
    else if(state==Finish)
    {
        smDebug(smhandle,SMDebugLow,"flashFirmwarePrimaryMCU: Finish\n");

        //verify STM32 flash if supported by BL version
        if(fwVersion>=1210)
        {
            smSetParameter(smhandle,deviceaddress,SMP_BOOTLOADER_FUNCTION,3);//BL func 3 = verify STM32 FW integrity
            int32_t faults;
            int32_t loc1;
            int32_t loc2;
            smRead3Parameters(smhandle, deviceaddress, SMP_FAULTS, &faults, SMP_FAULT_LOCATION1, &loc1, SMP_FAULT_LOCATION2, &loc2);

            if(getCumulativeStatus(smhandle)!=SM_OK)
            {
                state=Init;
                *progress=0;
                return false;
            }

            if(faults&FLT_FLASHING_COMMSIDE_FAIL)
            {
                // fault locations 1 and 2 provide additional context and will be asked for if you file
                // a support case on non-verifying firmware upload
                smDebug(smhandle, SMDebugLow, "flashFirmwarePrimaryMCU: verify failed (faults=%d, location1=%d, location2=%d)\n", faults, loc1, loc2);

                *progress=0;
                state=Init;
                return false;
            }
            else
            {
                smDebug(smhandle,SMDebugLow,"flashFirmwarePrimaryMCU: verify success\n");
            }
        }

        *progress=95;//my job is complete
        state=Init;
    }

    return true;
}


typedef enum { StatIdle=0, StatFirstConnectAttempt, StatEnterDFU, StatFindDFUDevice, StatUpload, StatLaunch } UploadState;//state machine status

//handle error in FW upload
FirmwareUploadStatus abortFWUpload( FirmwareUploadStatus stat, UploadState *state, int errorDetailCode )
{
    smDebug(-1,SMDebugLow,"abortFWUpload called with error detail code %d\n",errorDetailCode);

    globalErrorDetailCode=errorDetailCode;
    *state=StatIdle;
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
    static uint8_t *fwData=NULL;
    static int fwDataLength;
    static bool fileLoaded=false;
    FirmwareUploadStatus state;

    //load file to buffer if not loaded yet
    if(!fileLoaded)
    {
        if(!loadBinaryFile(firmware_filename,&fwData,&fwDataLength,false))
            return FWFileNotReadable;
        fileLoaded=true;
    }

    //update FW, called multiple times per upgrade
    state=smFirmwareUploadFromBuffer( smhandle, smaddress, fwData, fwDataLength );

    //if process complete, due to finish or error -> unload file.
    if(((int)state<0 || state==FWComplete) && fileLoaded)
    {
        free(fwData);
        fileLoaded=false;
    }

    return state;
}


/**
 * @brief smFirmwareUpload Sets drive in firmware upgrade mode if necessary and uploads a new firmware. Call this many until it returns value 100 (complete) or a negative value (error).
 * @param smhandle SM bus handle, must be opened before call
 * @param smaddress Target SM device address. Can be device in DFU mode or main operating mode. For Argon, one device in a bus must be started into DFU mode by DIP switches and smaddress must be set to 255.
 * @param fwData pointer to memory address where .gdf file contents are loaded. Note: on some architectures (such as ARM Cortex M) fwData must be aligned to nearest 4 byte boundary to avoid illegal machine instructions.
 * @param fwDataLenght number of bytes in fwData
 * @return Enum FirmwareUploadStatus that indicates errors or Complete status. Typecast to integer to get progress value 0-100.
 */
FirmwareUploadStatus smFirmwareUploadFromBuffer( const smbus smhandle, const int smaddress, uint8_t *fwData, const int fwDataLength )
{
    static uint32_t primaryMCUDataOffset, primaryMCUDataLenth;
    static uint32_t secondaryMCUDataOffset,secondaryMCUDataLength;
    static UploadState state=StatIdle;//state machine status
    static int32_t deviceType=0;
    static int DFUAddress;
    static int progress=0;
    static bool FW_already_installed=false;

    SM_STATUS stat;

    //state machine
    if(state==StatIdle)
    {
        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: Idle\n");
        progress=1;

        //try to read device type
        stat=smRead1Parameter(smhandle,smaddress,SMP_DEVICE_TYPE, &deviceType);
        if(stat!=SM_OK)
        {
            smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: failed to read target device type ID (the very first SM command in FirmwareUpload failed, no device there?)\n");
            return abortFWUpload(FWConnectionError,&state,30);
        }

        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: target device type of %d successfully read\n",deviceType);

        uint32_t GDFFileUID;
        FirmwareUploadStatus stat=parseFirmwareFile(fwData, fwDataLength, deviceType,
                                  &primaryMCUDataOffset, &primaryMCUDataLenth,
                                  &secondaryMCUDataOffset, &secondaryMCUDataLength,
                                  &GDFFileUID);
        if(stat!=FWComplete)//error in verify
        {
            smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: FW file verify failed\n");
            return abortFWUpload(stat,&state,100);
        }


        //all good, upload firmware, unless state is changed to StatLaunch later
        state=StatFirstConnectAttempt;
        FW_already_installed=false;

        //check if that FW is already installed
        if(GDFFileUID!=0)//check only if GDF has provided this value
        {
            uint32_t targetFWUID;
            if(smGetDeviceFirmwareUniqueID( smhandle, smaddress, &targetFWUID ))
            {
                //reset two upper bits because reading SMP will sign-extend them from 30 bits assuming so that we're reading signed 30 bit integer.
                //but this is unsigned 30 bit so reset top 2 bits to cancel sign extension.
                targetFWUID&=0x3fffffff;

                smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: Device provided firmware UID\n");
                if(GDFFileUID==targetFWUID)//FW is already installed
                {
                    smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: Same FW is already installed, skipping install\n");

                    //check if device is in NORMAL mode already
                    int32_t busMode;
                    stat=smRead1Parameter(smhandle,smaddress,SMP_BUS_MODE,&busMode);
                    if(stat==SM_OK && busMode==SMP_BUS_MODE_NORMAL)
                    {
                        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: application is already running in target, finishing up\n");
                        progress=FWAlreadyInstalled;
                        state=StatIdle;
                    }
                    else
                    {
                        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: device is in DFU mode, will launch app now\n");
                        state=StatLaunch;//launch app from DFU mode
                    }

                    FW_already_installed=true;
                }
                else
                    smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: firmware differs from the file (file UID %d, installed UID %d), uploading\n",GDFFileUID,targetFWUID);
            }
        }
    }

    if(state==StatFirstConnectAttempt)
    {
        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: StatFirstConnectAttempt\n");

        //check if device is in DFU mode already
        int32_t busMode;
        stat=smRead2Parameters(smhandle,smaddress,SMP_BUS_MODE,&busMode, SMP_DEVICE_TYPE, &deviceType);
        if(stat==SM_OK && busMode==SMP_BUS_MODE_DFU)
        {
            state=StatUpload;
        }
        else if(stat==SM_OK && busMode!=SMP_BUS_MODE_DFU)//device not in DFU mode
        {
            if(deviceType==4000)//argon does not support restarting in DFU mode by software
            {
                smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: ARGON devices not support booting in DFU mode automatically\n");
                return abortFWUpload(FWConnectionError,&state,200);
            }

            //restart device into DFU mode
            state=StatEnterDFU;

            stat=smSetParameter(smhandle,smaddress,SMP_SYSTEM_CONTROL,64);//reset device to DFU command
            if(stat!=SM_OK)
                return abortFWUpload(FWConnectionError,&state,300);
        }
        else
            state=StatFindDFUDevice;//search DFU device in brute force, fallback for older BL versions that don't preserve same smaddress than non-DFU mode
            //return abortFWUpload(FWConnectionError,fwData,&state,301);

        progress=2;
        DFUAddress=smaddress;
    }

    else if(state==StatEnterDFU)
    {
        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: StatEnterDFU\n");

        smSleepMs(SM_DEVICE_POWER_UP_WAIT_MS);//wait device to reboot in DFU mode. probably shorter delay would do.
        smPurge(smhandle);

        //check if device is in DFU mode already
        int32_t busMode;
        stat=smRead2Parameters(smhandle,smaddress, SMP_BUS_MODE,&busMode, SMP_DEVICE_TYPE, &deviceType);
        if(stat==SM_OK && busMode==SMP_BUS_MODE_DFU)//is DFU mode
        {
            smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: device is in DFU mode, continuing to upload\n");
            state=StatUpload;
        }
        else
        {
            //note older FW IONIs will appear in high SM addresses after DFU mode is activated (i.e. addresses become 245-255).
            //so it probably has happenend now, in the next state try to search a device in high address range
            smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: device is not found in DFU mode, continuing to search from high SM addresses\n");
            state=StatFindDFUDevice;//search DFU device in brute force, fallback for older BL versions that don't preserve same smaddress than non-DFU mode
        }

        progress=3;
    }

    else if(state==StatFindDFUDevice)
    {
        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: StatFindDFUDevice\n");

        int i;
        //scan thru addresses where SM device may appear in DFU mode if not appearing in it's original address
        for(i=245;i<=255;i++)
        {
            int32_t busMode;
            stat=smRead2Parameters(smhandle,i, SMP_BUS_MODE,&busMode, SMP_DEVICE_TYPE, &deviceType);
            if(stat==SM_OK && busMode==0)//busmode 0 is DFU mode
            {
                state=StatUpload;
                DFUAddress=i;
                smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: DFU device found at address %d\n",DFUAddress);
                break;//DFU found, break out of for loop
            }
        }

        if(i==256)//DFU device not found
        {
            smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: DFU device not found\n");
            return abortFWUpload(FWConnectingDFUModeFailed,&state,400);//setting DFU mode failed
        }

        progress=4;
    }

    else if(state==StatUpload)
    {
        smDebug(smhandle,SMDebugMid,"smFirmwareUploadFromBuffer: StatUpload\n");

        bool ret=flashFirmwarePrimaryMCU(smhandle,DFUAddress,fwData+primaryMCUDataOffset,primaryMCUDataLenth,&progress);
        if(!ret)//failed
        {
            smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: StatUpload failed\n");
            return abortFWUpload(FWConnectionError,&state,1000);
        }
        else
        {
            if(progress>=95)
                state=StatLaunch;
        }
    }

    else if(state==StatLaunch)
    {
        smDebug(smhandle,SMDebugLow,"smFirmwareUploadFromBuffer: StatLaunch\n");

        smSetParameter(smhandle,DFUAddress,SMP_BOOTLOADER_FUNCTION,4);//BL func 4 = launch.
        smSleepMs(SM_DEVICE_POWER_UP_WAIT_MS);
        if(FW_already_installed)
            progress=FWAlreadyInstalled;
        else
            progress=100;
        state=StatIdle;
    }

    return (FirmwareUploadStatus)progress;
}


static const struct
{
    LoadConfigurationStatus enumVal;
    const char *str;
} LoadConfigurationStatusStrings [] = {
    {CFGComplete, "DRC load complete"},
    {CFGInvalidFile, "Invalid DRC file"},
    {CFGCommunicationError, "Communication error"},
    {CFGIncompatibleFW, "Incompatible firmware"},
    {CFGUnsupportedTargetDevice, "Unsupported target device"},
    {CFGUnableToOpenFile, "Unable to open file"},
    {CFGUnsupportedFileVersion, "Unsupported file version"}
};

/**
 * @brief Convert LoadConfigurationStatus enum to descriptive string
 * @param stat is the LoadConfigurationStatus value
 * @return Return constant null terminated UTF8 string with the name of LoadConfigurationStatus enum
 */
const char *getLoadConfigurationStatusString( LoadConfigurationStatus stat )
{
    static const char unknown[] = "Unknown DCR load status";
    unsigned int i;
    for( i = 0; i < sizeof (LoadConfigurationStatusStrings) / sizeof (LoadConfigurationStatusStrings[0]);  i++)
    {
        if(LoadConfigurationStatusStrings[i].enumVal==stat)
            return LoadConfigurationStatusStrings[i].str;
    }
    return unknown;
}
