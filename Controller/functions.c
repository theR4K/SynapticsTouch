#include "rmiinternal.h"
#include "debug.h"
#include "bitops.h"



int
Ceil( IN int value, IN int divider)
{
    int b = ((value+divider-1) / divider);
    return b;
}

//F01

NTSTATUS
configureF01(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
)
{
    int index;
    NTSTATUS status;

    RMI4_F01_CTRL_REGISTERS controlF01 = { 0 };

    //
    // Find RMI device control function and configure it
    // 
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F01_RMI_DEVICE_CONTROL);

    if(index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Unexpected - RMI Function 01 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");

        goto exit;
    }

    RmiConvertF01ToPhysical(
        &ControllerContext->Config.DeviceSettings,
        &controlF01);

    //
    // Write settings to controller
    //
    status = SpbWriteDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].ControlBase,
        &controlF01,
        sizeof(controlF01)
    );

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error writing RMI F01 Ctrl settings - STATUS:%X",
            status);
        goto exit;
    }

    //
    // Note whether the device configuration settings initialized the
    // controller in an operating state, to prevent a double-start from 
    // the D0 entry dispatch routine (TchWakeDevice)
    //
    if(RMI4_F11_DEVICE_CONTROL_SLEEP_MODE_OPERATING ==
        controlF01.DeviceControl.SleepMode)
    {
        ControllerContext->DevicePowerState = PowerDeviceD0;
    }
    else
    {
        ControllerContext->DevicePowerState = PowerDeviceD3;
    }

exit:
    return status;
}

VOID
RmiConvertF01ToPhysical(
    IN RMI4_F01_CTRL_REGISTERS_LOGICAL* Logical,
    IN RMI4_F01_CTRL_REGISTERS* Physical
)
/*++

  Routine Description:

    Registry configuration values for F01 must be specified as
    4-byte REG_DWORD values logically, however the chip interprets these
    values as bits or bytes physically. This function converts
    the registry parameters into a structure that can be programmed
    into the controller's memory.

  Arguments:

    Logical - a pointer to the logical settings

    Physical - a pointer to the controller memory-mapped structure

  Return Value:

    None. Function may print warnings in the future when truncating.

--*/
{
    RtlZeroMemory(Physical, sizeof(RMI4_F01_CTRL_REGISTERS));

    //
    // Note that truncation of registry values is possible if 
    // the data was incorrectly provided by the OEM, we may
    // print warning messages in the future.
    // 

    Physical->DeviceControl.SleepMode = LOGICAL_TO_PHYSICAL(Logical->SleepMode);
    Physical->DeviceControl.NoSleep = LOGICAL_TO_PHYSICAL(Logical->NoSleep);
    Physical->DeviceControl.ReportRate = LOGICAL_TO_PHYSICAL(Logical->ReportRate);
    Physical->DeviceControl.Configured = LOGICAL_TO_PHYSICAL(Logical->Configured);

    Physical->InterruptEnable = LOGICAL_TO_PHYSICAL(Logical->InterruptEnable);
    Physical->DozeInterval = LOGICAL_TO_PHYSICAL(Logical->DozeInterval);
    Physical->DozeThreshold = LOGICAL_TO_PHYSICAL(Logical->DozeThreshold);
    Physical->DozeHoldoff = LOGICAL_TO_PHYSICAL(Logical->DozeHoldoff);
}

//F11
#define UnpackFingerState(FingerStatusRegister, i)\
    ((FingerStatusRegister >> (i * 2)) & 0x3)

NTSTATUS
GetTouchesFromF11(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
)
{
    NTSTATUS status;
    int index, i;
    int highestSlot;

    ULONG FingerStatusRegister;
    RMI4_F11_DATA_POSITION FingerPosRegisters[RMI4_MAX_TOUCHES];

    //
    // Locate RMI data base address of 2D touch function
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F11_2D_TOUCHPAD_SENSOR);

    if(index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Unexpected - RMI Function 11 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");
        goto exit;
    }

    //
    // Read finger statuses first, to determine how much data we need to read
    // 
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].DataBase,
        &FingerStatusRegister,
        Ceil(ControllerContext->MaxFingers,4));
    
    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error reading finger status data - STATUS:%X",
            status);

        goto exit;
    }

    //
    // Compute the last slot containing data of interest
    //
    highestSlot = 0;

    for(i = 0; i < ControllerContext->MaxFingers; i++)
    {
        //
        // Find the highest slot we know has finger data
        //
        if(ControllerContext->Cache.FingerSlotValid & (1 << i))
        {
            highestSlot = i;
        }
    }

    for(i = highestSlot + 1; i < ControllerContext->MaxFingers; i++)
    {
        //
        // New fingers in higher slots may need to be read
        //
        if(UnpackFingerState(FingerStatusRegister,i))
        {
            highestSlot = i;
        }
    }

    //
    // Read as much finger position data as we need to
    //
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].DataBase +
        Ceil(ControllerContext->MaxFingers, 4),
        &FingerPosRegisters[0],
        sizeof(RMI4_F11_DATA_POSITION) * (highestSlot + 1));

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error reading finger status data - STATUS:%X",
            status);

        goto exit;
    }

    UpdateLocalFingerCacheF11(FingerStatusRegister, FingerPosRegisters, ControllerContext);

exit:
    return status;
}

VOID
UpdateLocalFingerCacheF11(
    IN ULONG FingerStatusRegister,
    IN RMI4_F11_DATA_POSITION *FingerPosRegisters,
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext
)
/*++

Routine Description:

    This routine takes raw data reported by the Synaptics hardware and
    parses it to update a local cache of finger states. This routine manages
    removing lifted touches from the cache, and manages a map between the
    order of reported touches in hardware, and the order the driver should
    use in reporting.

Arguments:

    Data - A pointer to the new data returned from hardware
    Cache - A data structure holding various current finger state info

Return Value:

    None.

--*/
{
    int i, j;

    RMI4_FINGER_CACHE *Cache = &ControllerContext->Cache;
    //
    // Unpack the finger statuses into an array to ease dealing with each
    // finger uniformly in loops
    //

    //
    // When hardware was last read, if any slots reported as lifted, we
    // must clean out the slot and old touch info. There may be new
    // finger data using the slot.
    //
    for(i = 0; i < ControllerContext->MaxFingers; i++)
    {
        //
        // Sweep for a slot that needs to be cleaned
        //
        
        if(!(Cache->FingerSlotDirty & (1 << i)))
        {
            continue;
        }

        NT_ASSERT(Cache->FingerDownCount > 0);

        //
        // Find the slot in the reporting list 
        //
        for(j = 0; j < ControllerContext->MaxFingers; j++)
        {
            if(Cache->FingerDownOrder[j] == i)
            {
                break;
            }
        }

        NT_ASSERT(j != ControllerContext->MaxFingers);

        //
        // Remove the slot. If the finger lifted was the last in the list,
        // we just decrement the list total by one. If it was not last, we
        // shift the trailing list items up by one.
        //
        for(; j < Cache->FingerDownCount - 1; j++)
        {
            Cache->FingerDownOrder[j] = Cache->FingerDownOrder[j + 1];
        }
        Cache->FingerDownCount--;

        //
        // Finished, clobber the dirty bit
        //
        Cache->FingerSlotDirty &= ~(1 << i);
    }

    //
    // Cache the new set of finger data reported by hardware
    //
    for(i = 0; i < ControllerContext->MaxFingers; i++)
    {
        //
        // Take actions when a new contact is first reported as down
        //
        if((UnpackFingerState(FingerStatusRegister,i) != RMI4_FINGER_STATE_NOT_PRESENT) &&
            ((Cache->FingerSlotValid & (1 << i)) == 0))
        {
            Cache->FingerSlotValid |= (1 << i);
            Cache->FingerDownOrder[Cache->FingerDownCount++] = i;
        }

        //
        // Ignore slots with no new information
        //
        if(!(Cache->FingerSlotValid & (1 << i)))
        {
            continue;
        }

        //
        // Update local cache with new information from the controller
        //
        Cache->FingerSlot[i].fingerStatus = (UCHAR)UnpackFingerState(FingerStatusRegister, i);
        Cache->FingerSlot[i].x = (FingerPosRegisters[i].XPosLo & 0xF) |
            ((FingerPosRegisters[i].XPosHi & 0xFF) << 4);
        Cache->FingerSlot[i].y = (FingerPosRegisters[i].YPosLo & 0xF) |
            ((FingerPosRegisters[i].YPosHi & 0xFF) << 4);

        //
        // If a finger lifted, note the slot is now inactive so that any
        // cached data is cleaned out before we read hardware again.
        //
        if(Cache->FingerSlot[i].fingerStatus == RMI4_FINGER_STATE_NOT_PRESENT)
        {
            Cache->FingerSlotDirty |= (1 << i);
            Cache->FingerSlotValid &= ~(1 << i);
        }
    }

    //
    // Get current scan time (in 100us units)
    //
    ULONG64 QpcTimeStamp;
    Cache->ScanTime = KeQueryInterruptTimePrecise(&QpcTimeStamp) / 1000;
}

NTSTATUS
configureF11(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
)
{
    NTSTATUS status;
    int index;

    RMI4_F11_CTRL_REGISTERS controlF11 = { 0 };
    RMI4_F11_QUERY1_REGISTERS query1_F11;

    //
    // Find 2D touch sensor function and configure it
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F11_2D_TOUCHPAD_SENSOR);

    if(index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Unexpected - RMI Function 11 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");

        goto exit;
    }

    ControllerContext->MaxFingers = RMI4_MAX_TOUCHES;

    //reading first sensor query only!	
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].QueryBase +
        sizeof(RMI4_F11_QUERY0_REGISTERS),
        &query1_F11,
        sizeof(RMI4_F11_QUERY1_REGISTERS));

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");

        goto exit;
    }
    
    if(query1_F11.NumberOfFingers < 5)
    {
        ControllerContext->MaxFingers = (query1_F11.NumberOfFingers + 1);
    }
    else if(query1_F11.NumberOfFingers == 5)
    {
        ControllerContext->MaxFingers = 10;
    }
    else
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "unexpected Max Fingers Count. Value set to 10");
    }
    Trace(
        TRACE_LEVEL_INFORMATION,
        TRACE_FLAG_INIT,
        "Max Fingers Count. Value is %u", ControllerContext->MaxFingers);
    //end query


    RmiConvertF11ToPhysical(
        &ControllerContext->Config.TouchSettings,
        &controlF11);

    //
    // Write settings to controller
    //
    status = SpbWriteDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].ControlBase,
        &controlF11,
        sizeof(controlF11)
    );

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error writing RMI F11 Ctrl settings - STATUS:%X",
            status);
        goto exit;
    }

    //setup interupt
    ControllerContext->Config.DeviceSettings.InterruptEnable |= 0x1 << index;

exit:
    return status;
}

VOID
RmiConvertF11ToPhysical(
    IN RMI4_F11_CTRL_REGISTERS_LOGICAL* Logical,
    IN RMI4_F11_CTRL_REGISTERS* Physical
)
/*++

  Routine Description:

    Registry configuration values for F11 must be specified as
    4-byte REG_DWORD values logically, however the chip interprets these
    values as bits or bytes physically. This function converts
    the registry parameters into a structure that can be programmed
    into the controller's memory.

  Arguments:

    Logical - a pointer to the logical settings

    Physical - a pointer to the controller memory-mapped structure

  Return Value:

    None. Function may print warnings in the future when truncating.

--*/
{
    RtlZeroMemory(Physical, sizeof(RMI4_F11_CTRL_REGISTERS));

    //
    // Note that truncation of registry values is possible if 
    // the data was incorrectly provided by the OEM, we may
    // print warning messages in the future.
    // 

    Physical->ReportingMode = LOGICAL_TO_PHYSICAL(Logical->ReportingMode);
    Physical->AbsPosFilt = LOGICAL_TO_PHYSICAL(Logical->AbsPosFilt);
    Physical->RelPosFilt = LOGICAL_TO_PHYSICAL(Logical->RelPosFilt);
    Physical->RelBallistics = LOGICAL_TO_PHYSICAL(Logical->RelBallistics);
    Physical->Dribble = LOGICAL_TO_PHYSICAL(Logical->Dribble);

    Physical->PalmDetectThreshold = LOGICAL_TO_PHYSICAL(Logical->PalmDetectThreshold);
    Physical->MotionSensitivity = LOGICAL_TO_PHYSICAL(Logical->MotionSensitivity);
    Physical->ManTrackEn = LOGICAL_TO_PHYSICAL(Logical->ManTrackEn);
    Physical->ManTrackedFinger = LOGICAL_TO_PHYSICAL(Logical->ManTrackedFinger);

    Physical->DeltaXPosThreshold = LOGICAL_TO_PHYSICAL(Logical->DeltaXPosThreshold);
    Physical->DeltaYPosThreshold = LOGICAL_TO_PHYSICAL(Logical->DeltaYPosThreshold);
    Physical->Velocity = LOGICAL_TO_PHYSICAL(Logical->Velocity);
    Physical->Acceleration = LOGICAL_TO_PHYSICAL(Logical->Acceleration);

    Physical->SensorMaxXPosLo = (Logical->SensorMaxXPos & 0xFF) >> 0;
    Physical->SensorMaxXPosHi = (Logical->SensorMaxXPos & 0xF00) >> 8;

    Physical->SensorMaxYPosLo = (Logical->SensorMaxYPos & 0xFF) >> 0;
    Physical->SensorMaxYPosHi = (Logical->SensorMaxYPos & 0xF00) >> 8;

    Physical->ZTouchThreshold = LOGICAL_TO_PHYSICAL(Logical->ZTouchThreshold);
    Physical->ZHysteresis = LOGICAL_TO_PHYSICAL(Logical->ZHysteresis);
    Physical->SmallZThreshold = LOGICAL_TO_PHYSICAL(Logical->SmallZThreshold);

    Physical->SmallZScaleFactor[0] = (Logical->SmallZScaleFactor & 0xFF) >> 0;
    Physical->SmallZScaleFactor[1] = (Logical->SmallZScaleFactor & 0xFF00) >> 8;

    Physical->LargeZScaleFactor[0] = (Logical->LargeZScaleFactor & 0xFF) >> 0;
    Physical->LargeZScaleFactor[1] = (Logical->LargeZScaleFactor & 0xFF00) >> 8;

    Physical->AlgorithmSelection = LOGICAL_TO_PHYSICAL((Logical->AlgorithmSelection));
    Physical->WxScaleFactor = LOGICAL_TO_PHYSICAL((Logical->WxScaleFactor));
    Physical->WxOffset = LOGICAL_TO_PHYSICAL((Logical->WxOffset));
    Physical->WyScaleFactor = LOGICAL_TO_PHYSICAL((Logical->WyScaleFactor));
    Physical->WyOffset = LOGICAL_TO_PHYSICAL((Logical->WyOffset));

    Physical->XPitch[0] = (Logical->XPitch & 0xFF) >> 0;
    Physical->XPitch[1] = (Logical->XPitch & 0xFF00) >> 8;

    Physical->YPitch[0] = (Logical->YPitch & 0xFF) >> 0;
    Physical->YPitch[1] = (Logical->YPitch & 0xFF00) >> 8;

    Physical->FingerWidthX[0] = (Logical->FingerWidthX & 0xFF) >> 0;
    Physical->FingerWidthX[1] = (Logical->FingerWidthX & 0xFF00) >> 8;

    Physical->FingerWidthY[0] = (Logical->FingerWidthY & 0xFF) >> 0;
    Physical->FingerWidthY[1] = (Logical->FingerWidthY & 0xFF00) >> 8;

    Physical->ReportMeasuredSize = LOGICAL_TO_PHYSICAL(Logical->ReportMeasuredSize);
    Physical->SegmentationSensitivity = LOGICAL_TO_PHYSICAL(Logical->SegmentationSensitivity);
    Physical->XClipLo = LOGICAL_TO_PHYSICAL(Logical->XClipLo);
    Physical->XClipHi = LOGICAL_TO_PHYSICAL(Logical->XClipHi);
    Physical->YClipLo = LOGICAL_TO_PHYSICAL(Logical->YClipLo);
    Physical->YClipHi = LOGICAL_TO_PHYSICAL(Logical->YClipHi);
    Physical->MinFingerSeparation = LOGICAL_TO_PHYSICAL(Logical->MinFingerSeparation);
    Physical->MaxFingerMovement = LOGICAL_TO_PHYSICAL(Logical->MaxFingerMovement);
}


//F12

NTSTATUS
GetTouchesFromF12(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
)
{
    NTSTATUS status;

    int index, i, x, y, fingers;

    BYTE* data1;
    BYTE* controllerData;

    ULONG FingerStatusRegister = { 0 };
    RMI4_F12_DATA_POSITION FingerPosRegisters[RMI4_MAX_TOUCHES];

    //
    // Locate RMI data base address of 2D touch function
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F12_2D_TOUCHPAD_SENSOR);

    if(index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Unexpected - RMI Function 12 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Could not change register page");

        goto exit;
    }

    controllerData = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        ControllerContext->PacketSize,
        TOUCH_POOL_TAG_F12
    );

    if(controllerData == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    // 
    // Packets we need is determined by context
    //
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].DataBase,
        controllerData,
        (ULONG)ControllerContext->PacketSize
    );

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INTERRUPT,
            "Error reading finger status data - %!STATUS!",
            status);

        goto free_buffer;
    }

    data1 = &controllerData[ControllerContext->Data1Offset];
    fingers = 0;

    if(data1 != NULL)
    {
        for(i = 0; i < ControllerContext->MaxFingers; i++)
        {
            switch(data1[0]) 
            {
            case RMI_F12_OBJECT_FINGER:
            case RMI_F12_OBJECT_STYLUS:
                FingerStatusRegister |= RMI4_FINGER_STATE_PRESENT_WITH_ACCURATE_POS << i;
                fingers++;
                break;
            default:
                //fingerStatus[i] = RMI4_FINGER_STATE_NOT_PRESENT;
                break;
            }

            x = (data1[2] << 8) | data1[1];
            y = (data1[4] << 8) | data1[3];

            FingerPosRegisters[i].X = x;
            FingerPosRegisters[i].Y = y;

            data1 += F12_DATA1_BYTES_PER_OBJ;
        }
    }
    else
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INTERRUPT,
            "Error reading finger status data - empty buffer"
        );

        goto free_buffer;
    }

    UpdateLocalFingerCacheF12(FingerStatusRegister,FingerPosRegisters,ControllerContext);

free_buffer:
    ExFreePoolWithTag(
        controllerData,
        TOUCH_POOL_TAG_F12
    );

exit:
    return status;
}

VOID
UpdateLocalFingerCacheF12(
    IN ULONG FingerStatusRegister,
    IN RMI4_F12_DATA_POSITION *FingerPosRegisters,
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext
)
/*++

Routine Description:

    This routine takes raw data reported by the Synaptics hardware and
    parses it to update a local cache of finger states. This routine manages
    removing lifted touches from the cache, and manages a map between the
    order of reported touches in hardware, and the order the driver should
    use in reporting.

Arguments:

    Data - A pointer to the new data returned from hardware
    Cache - A data structure holding various current finger state info

Return Value:

    None.

--*/
{
    int i, j;
    RMI4_FINGER_CACHE *Cache = &ControllerContext->Cache;

    //
    // When hardware was last read, if any slots reported as lifted, we
    // must clean out the slot and old touch info. There may be new
    // finger data using the slot.
    //
    for(i = 0; i < ControllerContext->MaxFingers; i++)
    {
        //
        // Sweep for a slot that needs to be cleaned
        //
        if(!(Cache->FingerSlotDirty & (1 << i)))
        {
            continue;
        }

        NT_ASSERT(Cache->FingerDownCount > 0);

        //
        // Find the slot in the reporting list 
        //
        for(j = 0; j < ControllerContext->MaxFingers; j++)
        {
            if(Cache->FingerDownOrder[j] == i)
            {
                break;
            }
        }

        NT_ASSERT(j != ControllerContext->MaxFingers);

        //
        // Remove the slot. If the finger lifted was the last in the list,
        // we just decrement the list total by one. If it was not last, we
        // shift the trailing list items up by one.
        //
        for(; (j < Cache->FingerDownCount - 1) && (j < ControllerContext->MaxFingers - 1); j++)
        {
            Cache->FingerDownOrder[j] = Cache->FingerDownOrder[j + 1];
        }
        Cache->FingerDownCount--;

        //
        // Finished, clobber the dirty bit
        //
        Cache->FingerSlotDirty &= ~(1 << i);
    }

    //
    // Cache the new set of finger data reported by hardware
    //
    for(i = 0; i < ControllerContext->MaxFingers; i++)
    {
        //
        // Take actions when a new contact is first reported as down
        //
        if(( ((FingerStatusRegister>>i)& 0x1) != RMI4_FINGER_STATE_NOT_PRESENT) &&
            ((Cache->FingerSlotValid & (1 << i)) == 0) &&
            (Cache->FingerDownCount < ControllerContext->MaxFingers))
        {
            Cache->FingerSlotValid |= (1 << i);
            Cache->FingerDownOrder[Cache->FingerDownCount++] = i;
        }

        //
        // Ignore slots with no new information
        //
        if(!(Cache->FingerSlotValid & (1 << i)))
        {
            continue;
        }

        //
        // When finger is down, update local cache with new information from
        // the controller. When finger is up, we'll use last cached value
        //
        Cache->FingerSlot[i].fingerStatus = (UCHAR)((FingerStatusRegister>>i)&0x1);
        if(Cache->FingerSlot[i].fingerStatus)
        {
            Cache->FingerSlot[i].x = FingerPosRegisters[i].X;
            Cache->FingerSlot[i].y = FingerPosRegisters[i].Y;
        }

        //
        // If a finger lifted, note the slot is now inactive so that any
        // cached data is cleaned out before we read hardware again.
        //
        if(Cache->FingerSlot[i].fingerStatus == RMI4_FINGER_STATE_NOT_PRESENT)
        {
            Cache->FingerSlotDirty |= (1 << i);
            Cache->FingerSlotValid &= ~(1 << i);
        }
    }

    //
    // Get current scan time (in 100us units)
    //
    ULONG64 QpcTimeStamp;
    Cache->ScanTime = KeQueryInterruptTimePrecise(&QpcTimeStamp) / 1000;
}

const PRMI_REGISTER_DESC_ITEM RmiGetRegisterDescItem(
    PRMI_REGISTER_DESCRIPTOR Rdesc,
    USHORT reg
)
{
    PRMI_REGISTER_DESC_ITEM item;
    int i;

    for(i = 0; i < Rdesc->NumRegisters; i++)
    {
        item = &Rdesc->Registers[i];
        if(item->Register == reg) return item;
    }

    return NULL;
}

UINT8 RmiGetRegisterIndex(
    PRMI_REGISTER_DESCRIPTOR Rdesc,
    USHORT reg
)
{
    UINT8 i;

    for(i = 0; i < Rdesc->NumRegisters; i++)
    {
        if(Rdesc->Registers[i].Register == reg) return i;
    }

    return Rdesc->NumRegisters;
}

size_t
RmiRegisterDescriptorCalcSize(
    IN PRMI_REGISTER_DESCRIPTOR Rdesc
)
{
    PRMI_REGISTER_DESC_ITEM item;
    int i;
    size_t size = 0;

    for(i = 0; i < Rdesc->NumRegisters; i++)
    {
        item = &Rdesc->Registers[i];
        size += item->RegisterSize;
    }
    return size;
}

NTSTATUS
RmiReadRegisterDescriptor(
    IN SPB_CONTEXT *Context,
    IN UCHAR Address,
    IN PRMI_REGISTER_DESCRIPTOR Rdesc
)
{
    NTSTATUS Status;

    BYTE size_presence_reg;
    BYTE buf[35];
    int presense_offset = 1;
    BYTE *struct_buf;
    int reg;
    int offset = 0;
    int map_offset = 0;
    int i;
    int b;

    Status = SpbReadDataSynchronously(
        Context,
        Address,
        &size_presence_reg,
        sizeof(BYTE)
    );

    if(!NT_SUCCESS(Status)) goto i2c_read_fail;

    ++Address;

    if(size_presence_reg < 0 || size_presence_reg > 35)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "size_presence_reg has invalid size, either less than 0 or larger than 35");
        Status = STATUS_INVALID_PARAMETER;
        goto exit;
    }

    memset(buf, 0, sizeof(buf));

    /*
    * The presence register contains the size of the register structure
    * and a bitmap which identified which packet registers are present
    * for this particular register type (ie query, control, or data).
    */
    Status = SpbReadDataSynchronously(
        Context,
        Address,
        buf,
        size_presence_reg
    );
    if(!NT_SUCCESS(Status)) goto i2c_read_fail;
    ++Address;

    if(buf[0] == 0)
    {
        presense_offset = 3;
        Rdesc->StructSize = buf[1] | (buf[2] << 8);
    }
    else
    {
        Rdesc->StructSize = buf[0];
    }

    for(i = presense_offset; i < size_presence_reg; i++)
    {
        for(b = 0; b < 8; b++)
        {
            //addr,0,1
            if(buf[i] & (0x1 << b)) bitmap_set(Rdesc->PresenceMap, map_offset, 1);
            //*map |= 0x1
            //*map |= 0x2
            ++map_offset;
        }
    }

    Rdesc->NumRegisters = (UINT8)bitmap_weight(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS);
    Rdesc->Registers = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        Rdesc->NumRegisters * sizeof(RMI_REGISTER_DESC_ITEM),
        TOUCH_POOL_TAG_F12
    );

    if(Rdesc->Registers == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    /*
    * Allocate a temporary buffer to hold the register structure.
    * I'm not using devm_kzalloc here since it will not be retained
    * after exiting this function
    */
    struct_buf = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        Rdesc->StructSize,
        TOUCH_POOL_TAG_F12
    );

    if(struct_buf == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    /*
    * The register structure contains information about every packet
    * register of this type. This includes the size of the packet
    * register and a bitmap of all subpackets contained in the packet
    * register.
    */
    Status = SpbReadDataSynchronously(
        Context,
        Address,
        struct_buf,
        Rdesc->StructSize
    );

    if(!NT_SUCCESS(Status)) goto free_buffer;

    reg = find_first_bit(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS);
    for(i = 0; i < Rdesc->NumRegisters; i++)
    {
        PRMI_REGISTER_DESC_ITEM item = &Rdesc->Registers[i];
        int reg_size = struct_buf[offset];

        ++offset;
        if(reg_size == 0)
        {
            reg_size = struct_buf[offset] |
                (struct_buf[offset + 1] << 8);
            offset += 2;
        }

        if(reg_size == 0)
        {
            reg_size = struct_buf[offset] |
                (struct_buf[offset + 1] << 8) |
                (struct_buf[offset + 2] << 16) |
                (struct_buf[offset + 3] << 24);
            offset += 4;
        }

        item->Register = (USHORT)reg;
        item->RegisterSize = reg_size;

        map_offset = 0;

        item->NumSubPackets = 0;
        do
        {
            for(b = 0; b < 7; b++)
            {
                if(struct_buf[offset] & (0x1 << b))
                    item->NumSubPackets++;
                    bitmap_set(item->SubPacketMap, map_offset, 1);
                ++map_offset;
            }
        } while(struct_buf[offset++] & 0x80);

        item->NumSubPackets = (BYTE)bitmap_weight(item->SubPacketMap, RMI_REG_DESC_SUBPACKET_BITS);

        Trace(
            TRACE_LEVEL_INFORMATION,
            TRACE_INIT,
            "%s: reg: %d reg size: %ld subpackets: %d\n",
            __func__,
            item->Register, item->RegisterSize, item->NumSubPackets
        );

        reg = find_next_bit(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS, reg + 1);
    }

free_buffer:
    ExFreePoolWithTag(
        struct_buf,
        TOUCH_POOL_TAG_F12
    );

exit:
    return Status;

i2c_read_fail:
    Trace(
        TRACE_LEVEL_ERROR,
        TRACE_INIT,
        "Failed to read general info register - %!STATUS!",
        Status);
    goto exit;
}

NTSTATUS
RmiSetReportingMode(
    IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN UCHAR NewMode,
    OUT UCHAR* OldMode,
    IN PRMI_REGISTER_DESCRIPTOR ControlRegDesc
)
/*++

    Routine Description:

        Changes the F12 Reporting Mode on the controller as specified

    Arguments:

        ControllerContext - Touch controller context

        SpbContext - A pointer to the current i2c context

        NewMode - Either RMI_F12_REPORTING_MODE_CONTINUOUS
                 or RMI_F12_REPORTING_MODE_REDUCED

        OldMode - Old value of reporting mode

    Return Value:

        NTSTATUS indicating success or failure

--*/
{
    UCHAR reportingControl[3];
    int index;
    NTSTATUS status;
    UINT8 indexCtrl20;

    //
    // Find RMI F12 function
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F12_2D_TOUCHPAD_SENSOR);

    if (index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Set ReportingMode failure - RMI Function 12 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Could not change register page");

        goto exit;
    }

    indexCtrl20 = RmiGetRegisterIndex(ControlRegDesc, F12_2D_CTRL20);

    if (indexCtrl20 == ControlRegDesc->NumRegisters)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Cannot find F12_2D_Ctrl20 offset");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    if (ControlRegDesc->Registers[indexCtrl20].RegisterSize != sizeof(reportingControl))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Unexpected F12_2D_Ctrl20 register size");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    //
    // Read Device Control register
    //
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].ControlBase + indexCtrl20,
        &reportingControl,
        sizeof(reportingControl)
    );

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Could not read F12_2D_Ctrl20 register - %!STATUS!",
            status);

        goto exit;
    }

    if (OldMode)
    {
        *OldMode = reportingControl[0] & RMI_F12_REPORTING_MODE_MASK;
    }

    //
    // Assign new value
    //
    reportingControl[0] &= ~RMI_F12_REPORTING_MODE_MASK;
    reportingControl[0] |= NewMode & RMI_F12_REPORTING_MODE_MASK;

    //
    // Write setting back to the controller
    //
    status = SpbWriteDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].ControlBase + indexCtrl20,
        &reportingControl,
        sizeof(reportingControl)
    );

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Could not write F12_2D_Ctrl20 register - %X",
            status);

        goto exit;
    }

exit:

    return status;
}

NTSTATUS
configureF12(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
)
{
    int index;
    NTSTATUS status;

    BYTE queryF12Addr = 0;
    char buf;
    USHORT data_offset = 0;
    PRMI_REGISTER_DESC_ITEM item;
    
    RMI_REGISTER_DESCRIPTOR ControlRegDesc;
    RMI_REGISTER_DESCRIPTOR DataRegDesc;

    //
    // Find 2D touch sensor function and configure it
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F12_2D_TOUCHPAD_SENSOR);

    if(index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Unexpected - RMI Function 12 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Could not change register page");

        goto exit;
    }

    // Retrieve base address for queries
    queryF12Addr = ControllerContext->Descriptors[index].QueryBase;
    status = SpbReadDataSynchronously(
        SpbContext,
        queryF12Addr,
        &buf,
        sizeof(char)
    );

    if(!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Failed to read general info register - %!STATUS!",
            status);
        goto exit;
    }

    ++queryF12Addr;

    if(!(buf & BIT(0)))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Behavior of F12 without register descriptors is undefined."
        );

        status = STATUS_INVALID_PARAMETER;
        goto exit;
    }

    //ControllerContext->HasDribble = !!(buf & BIT(3));

    /*status = RmiReadRegisterDescriptor(
        SpbContext,
        queryF12Addr,
        &ControllerContext->QueryRegDesc
    );

    if(!NT_SUCCESS(status))
    {

        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Failed to read the Query Register Descriptor - %!STATUS!",
            status);
        goto exit;
    }*/
    queryF12Addr += 3;

    status = RmiReadRegisterDescriptor(
        SpbContext,
        queryF12Addr,
        &ControlRegDesc
    );

    if(!NT_SUCCESS(status))
    {

        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Failed to read the Control Register Descriptor - %!STATUS!",
            status);
        goto exit;
    }
    queryF12Addr += 3;

    status = RmiReadRegisterDescriptor(
        SpbContext,
        queryF12Addr,
        &DataRegDesc
    );

    if(!NT_SUCCESS(status))
    {

        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Failed to read the Data Register Descriptor - %!STATUS!",
            status);
        goto exit;
    }
    queryF12Addr += 3;
    ControllerContext->PacketSize = RmiRegisterDescriptorCalcSize(
        &DataRegDesc
    );

    // Skip rmi_f12_read_sensor_tuning for the prototype.

    /*
    * Figure out what data is contained in the data registers. HID devices
    * may have registers defined, but their data is not reported in the
    * HID attention report. Registers which are not reported in the HID
    * attention report check to see if the device is receiving data from
    * HID attention reports.
    */
    item = RmiGetRegisterDescItem(&DataRegDesc, 0);
    if(item) data_offset += (USHORT)item->RegisterSize;

    item = RmiGetRegisterDescItem(&DataRegDesc, 1);
    if(item != NULL)
    {
        ControllerContext->Data1Offset = data_offset;
        ControllerContext->MaxFingers = item->NumSubPackets;
        if((ControllerContext->MaxFingers * F12_DATA1_BYTES_PER_OBJ) >
            (BYTE)(ControllerContext->PacketSize - ControllerContext->Data1Offset))
        {
            ControllerContext->MaxFingers =
                (BYTE)(ControllerContext->PacketSize - ControllerContext->Data1Offset) /
                F12_DATA1_BYTES_PER_OBJ;
        }

        if(ControllerContext->MaxFingers > RMI4_MAX_TOUCHES)
        {
            ControllerContext->MaxFingers = RMI4_MAX_TOUCHES;
        }
    }
    else
    {
        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    //
    // Try to set continuous reporting mode during touch
    //
    RmiSetReportingMode(
        ControllerContext,
        SpbContext,
        RMI_F12_REPORTING_MODE_CONTINUOUS,
        NULL,
        &ControlRegDesc);

    //setup interupt
    ControllerContext->Config.DeviceSettings.InterruptEnable |= 0x1 << index;

exit:
    return status;
}


//F1A
NTSTATUS
configureF1A(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
)
{
    int index;
    UNREFERENCED_PARAMETER(SpbContext);
//
// Find 0D capacitive button sensor function and configure it if it exists
//
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F1A_0D_CAP_BUTTON_SENSOR);

    if(index != ControllerContext->FunctionCount)
    {
        ControllerContext->HasButtons = TRUE;

        //
        // TODO: Get configuration data from registry once Synaptics
        //       provides sane default values. Until then, assume the
        //       object is configured for the desired product scenario
        //       by default.
        //

        //setup interupts
        ControllerContext->Config.DeviceSettings.InterruptEnable |= 0x1 << index;
    }

    return 0;
}
