#include "rmiinternal.h"
#include "debug.h"
#include "bitops.h"
#include "Function11.h"
#include "Function12.h"

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
		if (item->Register == reg) return item;
	}

	return NULL;
}

UINT8 RmiGetRegisterIndex(
	PRMI_REGISTER_DESCRIPTOR Rdesc,
	USHORT reg
)
{
	UINT8 i;

	for (i = 0; i < Rdesc->NumRegisters; i++)
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

NTSTATUS
RmiReadRegisterDescriptor(
	IN SPB_CONTEXT* Context,
	IN UCHAR Address,
	IN PRMI_REGISTER_DESCRIPTOR Rdesc
)
{
	NTSTATUS Status;

	BYTE size_presence_reg;
	BYTE buf[35];
	int presense_offset = 1;
	BYTE* struct_buf;
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

	if (!NT_SUCCESS(Status)) goto i2c_read_fail;

	++Address;

	if (size_presence_reg < 0 || size_presence_reg > 35)
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
	if (!NT_SUCCESS(Status)) goto i2c_read_fail;
	++Address;

	if (buf[0] == 0)
	{
		presense_offset = 3;
		Rdesc->StructSize = buf[1] | (buf[2] << 8);
	}
	else
	{
		Rdesc->StructSize = buf[0];
	}

	for (i = presense_offset; i < size_presence_reg; i++)
	{
		for (b = 0; b < 8; b++)
		{
			//addr,0,1
			if (buf[i] & (0x1 << b)) bitmap_set(Rdesc->PresenceMap, map_offset, 1);
			//*map |= 0x1
			//*map |= 0x2
			++map_offset;
		}
	}

	Rdesc->NumRegisters = (UINT8)bitmap_weight(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS);
	Rdesc->Registers = ExAllocatePoolWithTag(
		NonPagedPoolNx,
		Rdesc->NumRegisters * sizeof(RMI_REGISTER_DESC_ITEM),
		TOUCH_POOL_TAG
	);

	if (Rdesc->Registers == NULL)
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
		TOUCH_POOL_TAG
	);

	if (struct_buf == NULL)
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

	if (!NT_SUCCESS(Status)) goto free_buffer;

	reg = find_first_bit(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS);
	for (i = 0; i < Rdesc->NumRegisters; i++)
	{
		PRMI_REGISTER_DESC_ITEM item = &Rdesc->Registers[i];
		int reg_size = struct_buf[offset];

		++offset;
		if (reg_size == 0)
		{
			reg_size = struct_buf[offset] |
				(struct_buf[offset + 1] << 8);
			offset += 2;
		}

		if (reg_size == 0)
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
			for (b = 0; b < 7; b++)
			{
				if (struct_buf[offset] & (0x1 << b))
					item->NumSubPackets++;
				bitmap_set(item->SubPacketMap, map_offset, 1);
				++map_offset;
			}
		} while (struct_buf[offset++] & 0x80);

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
		TOUCH_POOL_TAG
	);

exit:
	return Status;

i2c_read_fail:
	Trace(
		TRACE_LEVEL_ERROR,
		TRACE_INIT,
		"Failed to read general info register - Status=%X",
		Status);
	goto exit;
}

NTSTATUS
RmiGetTouchesFromController(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
)
{
	NTSTATUS status;

	if (ControllerContext->IsF12Digitizer)
	{
		status = GetTouchesFromF12(ControllerContext, SpbContext);
	}
	else
	{
		status = GetTouchesFromF11(ControllerContext, SpbContext);
	}

	return status;
}
