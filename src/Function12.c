#include "Function12.h"
#include "debug.h"
#include "bitops.h"
#include "rmiinternal.h"

NTSTATUS
GetTouchesFromF12(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
)
{
	NTSTATUS status;

	ULONG index, i, x, y, fingers;

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

	if (index == ControllerContext->FunctionCount)
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

	if (!NT_SUCCESS(status))
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

	if (controllerData == NULL)
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

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INTERRUPT,
			"Error reading finger status data - Status=%X",
			status);

		goto free_buffer;
	}

	data1 = &controllerData[ControllerContext->Data1Offset];
	fingers = 0;

	if (data1 != NULL)
	{
		for (i = 0; i < ControllerContext->MaxFingers; i++)
		{
			switch (data1[0])
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

	UpdateLocalFingerCacheF12(FingerStatusRegister, FingerPosRegisters, ControllerContext);

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
	IN RMI4_F12_DATA_POSITION* FingerPosRegisters,
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
		for (; (j < Cache->FingerDownCount - 1) && (j < ControllerContext->MaxFingers - 1); j++)
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
		if ((((FingerStatusRegister >> i) & 0x1) != RMI4_FINGER_STATE_NOT_PRESENT) &&
			((Cache->FingerSlotValid & (1 << i)) == 0) &&
			(Cache->FingerDownCount < ControllerContext->MaxFingers))
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
		// When finger is down, update local cache with new information from
		// the controller. When finger is up, we'll use last cached value
		//
		Cache->FingerSlot[i].fingerStatus = (UCHAR)((FingerStatusRegister >> i) & 0x1);
		if (Cache->FingerSlot[i].fingerStatus)
		{
			Cache->FingerSlot[i].x = FingerPosRegisters[i].X;
			Cache->FingerSlot[i].y = FingerPosRegisters[i].Y;
		}

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
	ULONG index;
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
			"Could not read F12_2D_Ctrl20 register - Status=%X",
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
RmiConfigureFunction12(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
)
{
	ULONG index;
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

	if (index == ControllerContext->FunctionCount)
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

	if (!NT_SUCCESS(status))
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

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INIT,
			"Failed to read general info register - Status=%X",
			status);
		goto exit;
	}

	++queryF12Addr;

	if (!(buf & BIT(0)))
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
			"Failed to read the Query Register Descriptor - Status=%X",
			status);
		goto exit;
	}*/
	queryF12Addr += 3;

	status = RmiReadRegisterDescriptor(
		SpbContext,
		queryF12Addr,
		&ControlRegDesc
	);

	if (!NT_SUCCESS(status))
	{

		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INIT,
			"Failed to read the Control Register Descriptor - Status=%X",
			status);
		goto exit;
	}
	queryF12Addr += 3;

	status = RmiReadRegisterDescriptor(
		SpbContext,
		queryF12Addr,
		&DataRegDesc
	);

	if (!NT_SUCCESS(status))
	{

		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INIT,
			"Failed to read the Data Register Descriptor - Status=%X",
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
	if (item) data_offset += (USHORT)item->RegisterSize;

	item = RmiGetRegisterDescItem(&DataRegDesc, 1);
	if (item != NULL)
	{
		ControllerContext->Data1Offset = data_offset;
		ControllerContext->MaxFingers = item->NumSubPackets;
		if ((ControllerContext->MaxFingers * F12_DATA1_BYTES_PER_OBJ) >
			(BYTE)(ControllerContext->PacketSize - ControllerContext->Data1Offset))
		{
			ControllerContext->MaxFingers =
				(BYTE)(ControllerContext->PacketSize - ControllerContext->Data1Offset) /
				F12_DATA1_BYTES_PER_OBJ;
		}

		if (ControllerContext->MaxFingers > RMI4_MAX_TOUCHES)
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