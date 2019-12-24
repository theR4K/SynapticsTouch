#include "debug.h"
//#include "bitops.h"
#include "Function01.h"

//F01
NTSTATUS
RmiConfigureFunction01(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
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

	if (index == ControllerContext->FunctionCount)
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

	if (!NT_SUCCESS(status))
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

	if (!NT_SUCCESS(status))
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
	if (RMI4_F11_DEVICE_CONTROL_SLEEP_MODE_OPERATING ==
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