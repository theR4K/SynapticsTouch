#include "debug.h"
#include "bitops.h"
#include "Function11.h"

#define UnpackFingerState(FingerStatusRegister, i)\
    ((FingerStatusRegister >> (i * 2)) & 0x3)

int
Ceil(IN int value, IN int divider)
{
	int b = ((value + divider - 1) / divider);
	return b;
}

NTSTATUS
GetTouchesFromF11(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
)
{
	NTSTATUS status;
	int index, i;
	ULONG highestSlot;

	ULONG FingerStatusRegister;
	RMI4_F11_DATA_POSITION FingerPosRegisters[RMI4_MAX_TOUCHES];

	//
	// Locate RMI data base address of 2D touch function
	//
	index = RmiGetFunctionIndex(
		ControllerContext->Descriptors,
		ControllerContext->FunctionCount,
		RMI4_F11_2D_TOUCHPAD_SENSOR);

	if (index == ControllerContext->FunctionCount)
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

	if (!NT_SUCCESS(status))
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
		Ceil(ControllerContext->MaxFingers, 4));

	if (!NT_SUCCESS(status))
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

	for (i = 0; i < ControllerContext->MaxFingers; i++)
	{
		//
		// Find the highest slot we know has finger data
		//
		if (ControllerContext->FingerCache.FingerSlotValid & (1 << i))
		{
			highestSlot = i;
		}
	}

	for (i = highestSlot + 1; i < ControllerContext->MaxFingers; i++)
	{
		//
		// New fingers in higher slots may need to be read
		//
		if (UnpackFingerState(FingerStatusRegister, i))
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
		sizeof(RMI4_F11_DATA_POSITION) * (highestSlot + 1lu));

	if (!NT_SUCCESS(status))
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
	IN RMI4_F11_DATA_POSITION* FingerPosRegisters,
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext
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

	RMI4_FINGER_CACHE* Cache = &ControllerContext->FingerCache;
	//
	// Unpack the finger statuses into an array to ease dealing with each
	// finger uniformly in loops
	//

	//
	// When hardware was last read, if any slots reported as lifted, we
	// must clean out the slot and old touch info. There may be new
	// finger data using the slot.
	//
	for (i = 0; i < ControllerContext->MaxFingers; i++)
	{
		//
		// Sweep for a slot that needs to be cleaned
		//

		if (!(Cache->FingerSlotDirty & (1 << i)))
		{
			continue;
		}

		NT_ASSERT(Cache->FingerDownCount > 0);

		//
		// Find the slot in the reporting list 
		//
		for (j = 0; j < ControllerContext->MaxFingers; j++)
		{
			if (Cache->FingerDownOrder[j] == i)
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
		for (; j < Cache->FingerDownCount - 1; j++)
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
	for (i = 0; i < ControllerContext->MaxFingers; i++)
	{
		//
		// Take actions when a new contact is first reported as down
		//
		if ((UnpackFingerState(FingerStatusRegister, i) != RMI4_FINGER_STATE_NOT_PRESENT) &&
			((Cache->FingerSlotValid & (1 << i)) == 0))
		{
			Cache->FingerSlotValid |= (1 << i);
			Cache->FingerDownOrder[Cache->FingerDownCount++] = i;
		}

		//
		// Ignore slots with no new information
		//
		if (!(Cache->FingerSlotValid & (1 << i)))
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
		if (Cache->FingerSlot[i].fingerStatus == RMI4_FINGER_STATE_NOT_PRESENT)
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
RmiConfigureFunction11(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
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

	if (index == ControllerContext->FunctionCount)
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

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Could not change register page");

		goto exit;
	}

	ControllerContext->MaxFingers = RMI4_MAX_TOUCHES;

	//
	// Reading first sensor query only!
	//
	status = SpbReadDataSynchronously(
		SpbContext,
		ControllerContext->Descriptors[index].QueryBase +
		sizeof(RMI4_F11_QUERY0_REGISTERS),
		&query1_F11,
		sizeof(RMI4_F11_QUERY1_REGISTERS));

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Could not change register page");

		goto exit;
	}

	if (query1_F11.NumberOfFingers < 5)
	{
		ControllerContext->MaxFingers = (query1_F11.NumberOfFingers + 1);
	}
	else if (query1_F11.NumberOfFingers == 5)
	{
		ControllerContext->MaxFingers = 10;
	}
	else
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Unexpected Max Fingers Count. Value set to 10");
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

	if (!NT_SUCCESS(status))
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

