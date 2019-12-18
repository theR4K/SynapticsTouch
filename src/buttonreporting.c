/*++
	Copyright (c) Microsoft Corporation. All Rights Reserved.
	Sample code. Dealpoint ID #843729.

	Module Name:

		buttonreporting.c

	Abstract:

		Contains Synaptics specific code for reporting samples

	Environment:

		Kernel mode

	Revision History:

--*/

#include "debug.h"
#include "buttonreporting.h"

NTSTATUS
RmiServiceCapacitiveButtonInterrupt(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext,
	IN PHID_INPUT_REPORT HidReport,
	OUT BOOLEAN* PendingTouches
)
/*++

Routine Description:

	This routine services capacitive button (F$1A) interrupts, it reads
	button data and fills a HID keyboard report with the relevant information

Arguments:

	ControllerContext - Touch controller context
	SpbContext - A pointer to the current i2c context
	HidReport- A HID report buffer to be filled with button data

Return Value:

	NTSTATUS, where success indicates the request memory was updated with
	button press information.

--*/
{
	RMI4_F1A_DATA_REGISTERS dataF1A;
	PHID_KEY_REPORT hidKeys;
	int index;
	NTSTATUS status;
	//USHORT timeNow = 0;

	//
	// If the controller doesn't support buttons, ignore this interrupt
	//
	if (ControllerContext->HasButtons == FALSE)
	{
		status = STATUS_NOT_IMPLEMENTED;
		goto exit;
	}

	//
	// Get the the key press/release information from the controller
	//
	index = RmiGetFunctionIndex(
		ControllerContext->Descriptors,
		ControllerContext->FunctionCount,
		RMI4_F1A_0D_CAP_BUTTON_SENSOR);

	if (index == ControllerContext->FunctionCount)
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Unexpected - RMI Function 1A missing");
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
	// Read button press/release data
	// 
	status = SpbReadDataSynchronously(
		SpbContext,
		ControllerContext->Descriptors[index].DataBase,
		&dataF1A,
		sizeof(dataF1A));

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INTERRUPT,
			"Error reading finger status data - STATUS:%X",
			status);

		goto exit;
	}

	RtlZeroMemory(HidReport, sizeof(HID_INPUT_REPORT));

	//
	// Update button states. This mapping should be made registry configurable
	//

	hidKeys = &(HidReport->KeyReport);

	if (ControllerContext->ButtonsCache.PendingState >> 7)
	{
		HidReport->ReportID = REPORTID_CAPKEY_CONSUMER;
		hidKeys->bKeys = ControllerContext->ButtonsCache.PendingState & 0xF;
		ControllerContext->ButtonsCache.PendingState = 0;
		goto exit;
	}

	RMI4_F1A_DATA_REGISTERS prevDataF1A;
	prevDataF1A.Raw = ControllerContext->ButtonsCache.prevPhysicalState;
	ControllerContext->ButtonsCache.prevPhysicalState = dataF1A.Raw;


	if (dataF1A.Button1 != prevDataF1A.Button1)
	{
		HidReport->ReportID = REPORTID_CAPKEY_KEYBOARD;
		hidKeys->bKeys |= (dataF1A.Button1) ? (1 << 0) : 0;
		if (dataF1A.Button0 != prevDataF1A.Button0 || dataF1A.Button2 != prevDataF1A.Button2)
		{
			ControllerContext->ButtonsCache.PendingState |= (dataF1A.Button0) ? (1 << 0) : 0;
			ControllerContext->ButtonsCache.PendingState |= (dataF1A.Button2) ? (1 << 1) : 0;
			ControllerContext->ButtonsCache.PendingState |= (1 << 7); //indicate waiting buttons
			*PendingTouches = TRUE;
		}
		goto exit;
	}
	if (dataF1A.Button0 != prevDataF1A.Button0 || dataF1A.Button2 != prevDataF1A.Button2)
	{
		HidReport->ReportID = REPORTID_CAPKEY_CONSUMER;
		hidKeys->bKeys |= (dataF1A.Button0) ? (1 << 0) : 0;
		hidKeys->bKeys |= (dataF1A.Button2) ? (1 << 1) : 0;
	}
	//
	// On return of success, this request will be completed up the stack
	//

exit:
	return status;
}

REPORTED_BUTTON
TchHandleButtonArea(
	IN ULONG ControllerX,
	IN ULONG ControllerY,
	IN PTOUCH_SCREEN_PROPERTIES Props
)
{
#ifdef EXPERIMENTAL_LEGACY_BUTTON_SUPPORT
	//
	// Hardcoded values now for RX100
	//
	ULONG ButtonAreaXMin = 0;
	ULONG ButtonAreaYMin = 1280;
	ULONG ButtonAreaXMax = 768;
	ULONG ButtonAreaYMax = 1390;

	ULONG BackAreaXMin = 0;
	ULONG BackAreaYMin = 1300;
	ULONG BackAreaXMax = 216;
	ULONG BackAreaYMax = 1390;

	ULONG StartAreaXMin = 297;
	ULONG StartAreaYMin = 1300;
	ULONG StartAreaXMax = 472;
	ULONG StartAreaYMax = 1390;

	ULONG SearchAreaXMin = 553;
	ULONG SearchAreaYMin = 1300;
	ULONG SearchAreaXMax = 768;
	ULONG SearchAreaYMax = 1390;

	//
	// Swap the axes reported by the touch controller if requested
	//
	if (Props->TouchSwapAxes)
	{
		ULONG temp = ControllerY;
		ControllerY = ControllerX;
		ControllerX = temp;
	}

	//
	// Invert the coordinates as requested
	//
	if (Props->TouchInvertXAxis)
	{
		if (ControllerX >= Props->TouchPhysicalWidth)
		{
			ControllerX = Props->TouchPhysicalWidth - 1u;
		}

		ControllerX = Props->TouchPhysicalWidth - ControllerX - 1u;
	}
	if (Props->TouchInvertYAxis)
	{
		if (ControllerY >= Props->TouchPhysicalHeight)
		{
			ControllerY = Props->TouchPhysicalHeight - 1u;
		}

		ControllerY = Props->TouchPhysicalHeight - ControllerY - 1u;
	}

	if (ControllerX > ButtonAreaXMin&& ControllerX < ButtonAreaXMax && ControllerY > ButtonAreaYMin&& ControllerY < ButtonAreaYMax)
	{
		if (ControllerX > BackAreaXMin&& ControllerX < BackAreaXMax && ControllerY > BackAreaYMin&& ControllerY < BackAreaYMax)
		{
			return BUTTON_BACK;
		}
		if (ControllerX > StartAreaXMin&& ControllerX < StartAreaXMax && ControllerY > StartAreaYMin&& ControllerY < StartAreaYMax)
		{
			return BUTTON_START;
		}
		if (ControllerX > SearchAreaXMin&& ControllerX < SearchAreaXMax && ControllerY > SearchAreaYMin&& ControllerY < SearchAreaYMax)
		{
			return BUTTON_SEARCH;
		}
		return BUTTON_UNKNOWN;
	}
#else
	UNREFERENCED_PARAMETER((ControllerX, ControllerY, Props));
#endif
	return BUTTON_NONE;
}