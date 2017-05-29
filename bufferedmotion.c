#include "simplemotion.h"
#include "simplemotion_private.h"
#include "bufferedmotion.h"
#include "sm485.h"

/** initialize buffered motion for one axis with address and samplerate (Hz) */
SM_STATUS smBufferedInit(BufferedMotionAxis *newAxis, smbus handle, smaddr deviceAddress, smint32 sampleRate, smint16 readParamAddr, smuint8 readDataLength )
{
    //value out of range
    if(sampleRate<1 || sampleRate>2500)
        return recordStatus(handle,SM_ERR_PARAMETER);

    newAxis->initialized=smfalse;
    newAxis->bushandle=handle;
    newAxis->samplerate=sampleRate;
    newAxis->deviceAddress=deviceAddress;
    newAxis->readParamAddr=readParamAddr;
    newAxis->readParamLength=readDataLength;
    newAxis->readParamInitialized=smfalse;
    newAxis->numberOfDiscardableReturnDataPackets=0;
    newAxis->driveClock=0;
    newAxis->bufferFill=0;
    newAxis->numberOfPendingReadPackets=0;

    //discard any existing data in buffer, and to get correct reading of device buffer size
    smSetParameter( newAxis->bushandle, newAxis->deviceAddress, SMP_SYSTEM_CONTROL,SMP_SYSTEM_CONTROL_ABORTBUFFERED);

    //after abort, we can read the maximum size of data in device buffer
    smRead1Parameter(newAxis->bushandle,newAxis->deviceAddress,SMP_BUFFER_FREE_BYTES,&newAxis->bufferLength);
    newAxis->bufferFreeBytes=newAxis->bufferLength;

    //set input smoothing filter on [CIS] if samplerate is not maximum. with filter samplerates 250,500,750,1000,1250 etc run smooth.
    if(sampleRate<2500)
    {
        if(smRead1Parameter(handle,deviceAddress,SMP_DRIVE_FLAGS,&newAxis->driveFlagsBeforeInit)!=SM_OK)
            return getCumulativeStatus(handle);//if error happens in read, dont set the flags

        smSetParameter(handle,deviceAddress,SMP_DRIVE_FLAGS,newAxis->driveFlagsBeforeInit|FLAG_USE_INPUT_LP_FILTER);
    }

    //set buffer execution rate to max so init commands go fast
    smSetParameter(handle,deviceAddress,SMP_BUFFERED_CMD_PERIOD,10000/2500);

    //set acceleration to "infinite" to avoid modification of user supplied trajectory inside drive
    smRead1Parameter(handle,deviceAddress,SMP_TRAJ_PLANNER_ACCEL,&newAxis->driveAccelerationBeforeInit);//store original for restoration
    smSetParameter(handle,deviceAddress,SMP_TRAJ_PLANNER_ACCEL,32767);

    //set buffer execution rate
    smSetParameter(handle,deviceAddress,SMP_BUFFERED_CMD_PERIOD,10000/sampleRate);

    //FIXME can cause unnecessary initialized=false status if there was error flags in cumulative status before calling this func
    if(getCumulativeStatus(handle)==SM_OK)
        newAxis->initialized=smtrue;

    return getCumulativeStatus(handle);
}

/** uninitialize axis from buffered motion, recommended to call this before closing bus so drive's adjusted parameters are restored to originals*/
SM_STATUS smBufferedDeinit( BufferedMotionAxis *axis )
{
    smBufferedAbort(axis);

    //restore drive in pre-init state
    if(axis->initialized==smtrue)
    {
        smSetParameter(axis->bushandle,axis->deviceAddress,SMP_TRAJ_PLANNER_ACCEL,axis->driveAccelerationBeforeInit);
        smSetParameter(axis->bushandle,axis->deviceAddress,SMP_DRIVE_FLAGS,axis->driveFlagsBeforeInit);
    }

    axis->initialized=smfalse;
    axis->readParamInitialized=smfalse;

    return getCumulativeStatus(axis->bushandle);
}


/* this also starts buffered motion when it's not running*/
SM_STATUS smBufferedRunAndSyncClocks(BufferedMotionAxis *axis)
{
    return smGetBufferClock( axis->bushandle, axis->deviceAddress, &axis->driveClock );
}

SM_STATUS smBufferedGetFree(BufferedMotionAxis *axis, smint32 *numBytesFree )
{
    smint32 freebytes;

    if(smRead1Parameter(axis->bushandle,axis->deviceAddress,SMP_BUFFER_FREE_BYTES,&freebytes)!=SM_OK)
    {
        *numBytesFree=0;//read has failed, assume 0
        return getCumulativeStatus(axis->bushandle);
    }

    axis->bufferFreeBytes=freebytes;
    axis->bufferFill=100*(axis->bufferLength-freebytes)/axis->bufferLength;//calc buffer fill 0-100%

    *numBytesFree=freebytes;

    return getCumulativeStatus(axis->bushandle);
}

smint32 smBufferedGetMaxFillSize(BufferedMotionAxis *axis, smint32 numBytesFree )
{
    //even if we have lots of free space in buffer, we can only send up to SM485_MAX_PAYLOAD_BYTES bytes at once in one SM transmission
    if(numBytesFree>SM485_MAX_PAYLOAD_BYTES)
        numBytesFree=SM485_MAX_PAYLOAD_BYTES;

    //calculate number of points that can be uploaded to buffer (max size SM485_MAX_PAYLOAD_BYTES bytes and fill consumption is 2+4+2+3*(n-1) bytes)
    if(axis->readParamInitialized==smtrue)
        //*numPoints=(freebytes-2-4-2)/3+1;
        return numBytesFree/4;
    else
        //*numPoints=(freebytes-2-4-2 -2-2-2-2)/3+1;//if read data uninitialized, it takes extra 8 bytes to init on next fill, so reduce it here
        return (numBytesFree-2-3-2-3-2)/4;//if read data uninitialized, it takes extra n bytes to init on next fill, so reduce it here
}

smint32 smBufferedGetBytesConsumed(BufferedMotionAxis *axis, smint32 numFillPoints )
{
    //calculate number of bytes that the number of fill points will consume from buffer
    if(axis->readParamInitialized==smtrue)
        return numFillPoints*4;
    else
        return numFillPoints*4 +2+3+2+3+2;//if read data uninitialized, it takes extra n bytes to init on next fill, so reduce it here
}


SM_STATUS smBufferedFillAndReceive(BufferedMotionAxis *axis, smint32 numFillPoints, smint32 *fillPoints, smint32 *numReceivedPoints, smint32 *receivedPoints, smint32 *bytesFilled )
{
    smint32 bytesUsed=0;

    //if(freeBytesInDeviceBuffer>=cmdBufferSizeBytes)
//        emit message(Warning,"Buffer underrun on axis "+QString::number(ax));

    //freeBytesInDeviceBuffer-=8;
//      if(drives[ax].bufferedStreamInitialized==false)
//            cmdBufferSizeBytes=freeBytesInDeviceBuffer;//get empty buffer size

    //first initialize the stream if not done yet
    if(axis->readParamInitialized==smfalse)
    {
        //set acceleration to "infinite" to avoid modification of user supplied trajectory inside drive
        smAppendSMCommandToQueue(axis->bushandle,SMPCMD_SETPARAMADDR,SMP_RETURN_PARAM_ADDR);
        smAppendSMCommandToQueue(axis->bushandle,SM_WRITE_VALUE_24B,axis->readParamAddr);
        smAppendSMCommandToQueue(axis->bushandle,SMPCMD_SETPARAMADDR,SMP_RETURN_PARAM_LEN);
        smAppendSMCommandToQueue(axis->bushandle,SM_WRITE_VALUE_24B,axis->readParamLength);
        smAppendSMCommandToQueue(axis->bushandle,SMPCMD_SETPARAMADDR,SMP_ABSOLUTE_SETPOINT);
        bytesUsed+=2+3+2+3+2;

        //next time we read return data, we discard first 4 return packets to avoid unexpected read data to user
        axis->numberOfDiscardableReturnDataPackets+=5;
        axis->readParamInitialized=smtrue;
    }

    if(numFillPoints>=1)//send first fill data
    {
        smAppendSMCommandToQueue(axis->bushandle,SM_WRITE_VALUE_32B,fillPoints[0]);
        bytesUsed+=4;
        axis->numberOfPendingReadPackets+=1;
    }

    //send rest of data as increments
    if(numFillPoints>=2)
    {
        int i;
        //smAppendSMCommandToQueue(axis->bushandle,SMPCMD_SETPARAMADDR,SMP_ABSOLUTE_SETPOINT);
        //smAppendSMCommandToQueue(axis->bushandle,SMPCMD_SETPARAMADDR,SMP_INCREMENTAL_SETPOINT);
        //axis->numberOfPendingReadPackets++;//FIXME ei toimi ihan oikein tää koska skippaa äskeisen writevaluen

        for(i=1;i<numFillPoints;i++)
        {
            //smAppendSMCommandToQueue(axis->bushandle,SM_WRITE_VALUE_24B, fillPoints[i]-fillPoints[i-1] );
            smAppendSMCommandToQueue(axis->bushandle,SM_WRITE_VALUE_32B,fillPoints[i]);
            bytesUsed+=4;
            axis->numberOfPendingReadPackets++;
        }
    }

    //send the commands that were added with smAppendSMCommandToQueue. this also reads all return packets that are available (executed already)
    smUploadCommandQueueToDeviceBuffer(axis->bushandle,axis->deviceAddress);

    //read all available return data from stream (commands that have been axecuted in drive so far)
    //return data works like FIFO for all sent commands (each sent stream command will produce return data packet that we fetch here)
    {
        smint32 bufferedReturnBytesReceived,readval;
        int n=0;

        //read return data buffer
        smBytesReceived(axis->bushandle,&bufferedReturnBytesReceived);//get amount of data available
        //qDebug()<<axis<<"return data bytes"<<bufferedReturnBytesReceived;
        while(bufferedReturnBytesReceived>1)//loop until we have read it all
        {
            smGetQueuedSMCommandReturnValue(axis->bushandle, &readval);
            smBytesReceived(axis->bushandle,&bufferedReturnBytesReceived);

            if(axis->numberOfDiscardableReturnDataPackets>0)
            {
                //discard this return data as it's intialization return packets
                axis->numberOfDiscardableReturnDataPackets--;
            }
            else//its read data that user expects
            {
                receivedPoints[n]=readval;
                n++;
                axis->numberOfPendingReadPackets--;
            }
        }
        *numReceivedPoints=n;
    }

    *bytesFilled=bytesUsed;
    return getCumulativeStatus(axis->bushandle);
}

/** this will stop executing buffered motion immediately and discard rest of already filled buffer on a given axis. May cause drive fault state such as tracking error if done at high speed because stop happens without deceleration.*/
SM_STATUS smBufferedAbort(BufferedMotionAxis *axis)
{
    return smSetParameter( axis->bushandle, axis->deviceAddress, SMP_SYSTEM_CONTROL,SMP_SYSTEM_CONTROL_ABORTBUFFERED);
}

