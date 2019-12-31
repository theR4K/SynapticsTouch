#include "Function12.h"
#include "debug.h"
#include "rmiinternal.h"

NTSTATUS
GetTouchesFromF12(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
)
{
	NTSTATUS status;

    int index;

    RMI4_F12_DATA_POSITION data1[RMI4_MAX_TOUCHES];

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
			TRACE_FLAG_INIT,
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
			TRACE_FLAG_INIT,
			"Could not change register page");

		goto exit;
	}

	// 
	// Packets we need is determined by context
	//
	status = SpbReadDataSynchronously(
		SpbContext,
        ControllerContext->Descriptors[index].DataBase +
            ControllerContext->Data1Offset,
		&data1[0],
		sizeof(data1)
	);

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INTERRUPT,
			"Error reading finger status data - Status=%X",
			status);

		goto exit;
	}

	if (data1 == NULL)
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INTERRUPT,
			"Error reading finger status data - empty buffer"
		);

		goto exit;
	}

	UpdateLocalFingerCacheF12(&data1[0], ControllerContext);

exit:
	return status;
}

VOID
UpdateLocalFingerCacheF12(
	IN RMI4_F12_DATA_POSITION* FingerDataRegisters,
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
		if ((FingerDataRegisters[i].Status != RMI4_FINGER_STATE_NOT_PRESENT) &&
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
		Cache->FingerSlot[i].fingerStatus = (UCHAR)FingerDataRegisters->Status;
		if (Cache->FingerSlot[i].fingerStatus)
		{
            Cache->FingerSlot[i].x = (FingerDataRegisters[i].XPosHi << 8) | FingerDataRegisters[i].XPosLo;
            Cache->FingerSlot[i].y = (FingerDataRegisters[i].YPosHi << 8) | FingerDataRegisters[i].YPosLo;
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
	OUT UCHAR* OldMode
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
			TRACE_FLAG_INIT,
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
			TRACE_FLAG_INIT,
			"Could not change register page");

		goto exit;
	}

	indexCtrl20 = RmiGetRegisterIndex(&ControllerContext->ControlRegDesc, F12_2D_CTRL20);

	if (indexCtrl20 == ControllerContext->ControlRegDesc.NumRegisters)
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Cannot find F12_2D_Ctrl20 offset");

		status = STATUS_INVALID_DEVICE_STATE;
		goto exit;
	}

	if (ControllerContext->ControlRegDesc.Registers[indexCtrl20].RegisterSize != sizeof(reportingControl))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Unexpected F12_2D_Ctrl20 register size, size=%lu, expected=%lu",
			ControllerContext->ControlRegDesc.Registers[indexCtrl20].RegisterSize,
			sizeof(reportingControl)
		);

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
			TRACE_FLAG_INIT,
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
			TRACE_FLAG_INIT,
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
	int index;
	NTSTATUS status;

	BYTE queryF12Addr = 0;
	char buf;
	UCHAR data_offset = 0;
	PRMI_REGISTER_DESC_ITEM item;

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
			TRACE_FLAG_INIT,
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
			TRACE_FLAG_INIT,
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
			TRACE_FLAG_INIT,
			"Failed to read general info register - Status=%X",
			status);
		goto exit;
	}
    queryF12Addr++;

	if (!(buf & 0x1))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Behavior of F12 without register descriptors is undefined."
		);

		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

    //skip query registers
	queryF12Addr += 3;

	status = RmiReadRegisterDescriptor(
		SpbContext,
		queryF12Addr,
		&ControllerContext->ControlRegDesc
	);

	if (!NT_SUCCESS(status))
	{

		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Failed to read the Control Register Descriptor - Status=%X",
			status);
		goto exit;
	}
	queryF12Addr += 3;

	status = RmiReadRegisterDescriptor(
		SpbContext,
		queryF12Addr,
		&ControllerContext->DataRegDesc
	);

	if (!NT_SUCCESS(status))
	{

		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Failed to read the Data Register Descriptor - Status=%X",
			status);
		goto exit;
	}
	queryF12Addr += 3;
	//ControllerContext->PacketSize = RmiRegisterDescriptorCalcSize(
	//	&ControllerContext->DataRegDesc
	//);

	// Skip rmi_f12_read_sensor_tuning for the prototype.

	/*
	* Figure out what data is contained in the data registers. HID devices
	* may have registers defined, but their data is not reported in the
	* HID attention report. Registers which are not reported in the HID
	* attention report check to see if the device is receiving data from
	* HID attention reports.
	*/
	item = RmiGetRegisterDescItem(&ControllerContext->DataRegDesc, 0);
	if (item) data_offset += (UCHAR)item->RegisterSize;

	item = RmiGetRegisterDescItem(&ControllerContext->DataRegDesc, 1);
	if (item != NULL)
	{
		ControllerContext->Data1Offset = data_offset;
		ControllerContext->MaxFingers = item->NumSubPackets;
        //not needed anymore, been used bcs bitMaps calculate wrong subPackets number
		/*if ((ControllerContext->MaxFingers * F12_DATA1_BYTES_PER_OBJ) >
			(BYTE)(ControllerContext->PacketSize - ControllerContext->Data1Offset))
		{
			ControllerContext->MaxFingers =
				(BYTE)(ControllerContext->PacketSize - ControllerContext->Data1Offset) /
				F12_DATA1_BYTES_PER_OBJ;
		}*/

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
		NULL);

exit:
	return status;
}

NTSTATUS
RmiReadRegisterDescriptor(
	IN SPB_CONTEXT* Context,
	IN UCHAR Address,
	IN PRMI_REGISTER_DESCRIPTOR Rdesc
)
{
	NTSTATUS Status;

	BYTE regMapSize;
    BYTE* buf = NULL;
    BYTE* regMap;
    BYTE structBufSize;
	BYTE* structBuf;
	USHORT reg;
	int offset = 0;
	int i;
    char counter = 0;

	Status = SpbReadDataSynchronously(
		Context,
		Address++,
		&regMapSize,
		sizeof(BYTE)
	);
	if (!NT_SUCCESS(Status)) goto i2c_read_fail;

	if (regMapSize < 0 || regMapSize > 35)
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"size_presence_reg has invalid size, either less than 0 or larger than 35");
		Status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

    buf = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        regMapSize,
        TOUCH_POOL_TAG_F12
    );

    if(buf == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

	/*
	* The presence register contains the size of the register structure
	* and a bitmap which identified which packet registers are present
	* for this particular register type (ie query, control, or data).
	*/
	Status = SpbReadDataSynchronously(
		Context,
		Address++,
		buf,
        regMapSize
	);

	if (!NT_SUCCESS(Status)) goto i2c_read_fail;

    /* need for nebug registers offsets
    for(i = 0; i < size_presence_reg;i++)
        Trace(
            TRACE_LEVEL_INFORMATION,
            TRACE_FLAG_INIT,
            "buff %d: %x",
            i,buf[i]
        );*/

    //before regMap we have size of the register structure
	if (buf[0] == 0)
	{
        structBufSize = buf[1] | (buf[2] << 8);
        regMap = buf + 3;
        regMapSize -= 3;
	}
	else
	{
        structBufSize = buf[0];
        regMap = buf + 1;
        regMapSize--;
	}

    //calculate number of registers, not very fast but
	for (i = 0; i < regMapSize*8; i++)
	{
		if (regMap[i>>3] & (0x1 << (i & 0x7)))
		{
               counter++;
		}
	}
    Rdesc->NumRegisters = counter;

	Rdesc->Registers = ExAllocatePoolWithTag(
		NonPagedPoolNx,
		counter * sizeof(RMI_REGISTER_DESC_ITEM),
		TOUCH_POOL_TAG_F12
	);

	if (Rdesc->Registers == NULL)
	{
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	
	// Allocate a temporary buffer to hold the register structure.
	structBuf = ExAllocatePoolWithTag(
		NonPagedPoolNx,
		structBufSize,
		TOUCH_POOL_TAG_F12
	);

	if (structBuf == NULL)
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
		structBuf,
        structBufSize
	);

    if (!NT_SUCCESS(Status)) goto free_buffer2;

    /* need for debug registers
    for(i = 0; ((unsigned int)i) < Rdesc->StructSize; i++)
        Trace(
            TRACE_LEVEL_INFORMATION,
            TRACE_FLAG_INIT,
            "struct_buf %d: %x",
            i, struct_buf[i]
        );*/

    i = 0;
	for (reg =  0; reg < regMapSize*8; reg++)
	{
        if(regMap[reg >> 3] & (0x1 << (reg & 0x7)))
        {
            PRMI_REGISTER_DESC_ITEM item = &Rdesc->Registers[i++];
            item->Register = reg;

            //calculate reg size
            int reg_size = structBuf[offset++];

            if(reg_size == 0)
            {
                reg_size = structBuf[offset] |
                    (structBuf[offset + 1] << 8);
                offset += 2;
            }

            if(reg_size == 0)
            {
                reg_size = structBuf[offset] |
                    (structBuf[offset + 1] << 8) |
                    (structBuf[offset + 2] << 16) |
                    (structBuf[offset + 3] << 24);
                offset += 4;
            }

            item->RegisterSize = reg_size;

            //calculate subPackets count
            counter = 0;
            do
            {
                for(int b = 0; b < 7; b++)
                {
                    if(structBuf[offset] & (0x1 << b))
                        counter++;
                }
            } while(structBuf[offset++] & 0x80);
            item->NumSubPackets = counter;

            Trace(
                TRACE_LEVEL_INFORMATION,
                TRACE_FLAG_INIT,
                "%s: reg: %d reg size: %ld subpackets: %d num reg: %d",
                __func__,
                item->Register, item->RegisterSize, counter, Rdesc->NumRegisters
            );
        }
	}

free_buffer2:
	ExFreePoolWithTag(
		structBuf,
		TOUCH_POOL_TAG_F12
	);

exit:
    if(buf != NULL)
    {
        ExFreePoolWithTag(
            buf,
            TOUCH_POOL_TAG_F12
        );
    }
	return Status;

i2c_read_fail:
	Trace(
		TRACE_LEVEL_ERROR,
		TRACE_FLAG_INIT,
		"Failed to read general info register - Status=%X",
		Status);
	goto exit;
}

UINT8 RmiGetRegisterIndex(
	PRMI_REGISTER_DESCRIPTOR Rdesc,
	USHORT reg
)
{
	for (UINT8 i = 0; i < Rdesc->NumRegisters; i++)
	{
		if (Rdesc->Registers[i].Register == reg) return i;
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

	for (i = 0; i < Rdesc->NumRegisters; i++)
	{
		item = &Rdesc->Registers[i];
		size += item->RegisterSize;
	}
	return size;
}

const PRMI_REGISTER_DESC_ITEM RmiGetRegisterDescItem(
	PRMI_REGISTER_DESCRIPTOR Rdesc,
	USHORT reg
)
{
	PRMI_REGISTER_DESC_ITEM item;
	int i;

	for (i = 0; i < Rdesc->NumRegisters; i++)
	{
		item = &Rdesc->Registers[i];
		if (item->Register == reg) 
			return item;
	}

	return NULL;
}