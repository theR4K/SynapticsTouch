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
#include "internal.h"

NTSTATUS
RmiServiceCapacitiveButtonInterrupt(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext,
	IN BOOLEAN ReversedKeys
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
	int index;
	NTSTATUS status;

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

    for(int i = 0; i < RMI4_MAX_BUTTONS; i++)
    {
        if(ReversedKeys)
        {
            ControllerContext->ButtonsCache.PhysicalState[i] = ((dataF1A.Raw >> i) & 0x1);
        }
        else
        {
            ControllerContext->ButtonsCache.PhysicalState[i] = ((dataF1A.Raw >> (RMI4_MAX_BUTTONS - i - 1)) & 0x1);
        }
    }

    status = FillButtonsReportFromCache(ControllerContext);

exit:
    return status;
}

NTSTATUS 
FillButtonsReportFromCache(
    IN RMI4_CONTROLLER_CONTEXT* ControllerContext
)
{
    NTSTATUS status = STATUS_SUCCESS;

    BOOLEAN* prevData = ControllerContext->ButtonsCache.prevPhysicalState;
    BOOLEAN* data = ControllerContext->ButtonsCache.PhysicalState;
    BOOLEAN* Logical = ControllerContext->ButtonsCache.LogicalState;
    //
    // Update button states. This mapping should be made registry configurable
    //

    //
    PHID_INPUT_REPORT hidReport = NULL;
    PHID_KEY_REPORT hidKeys;

    for(int i = 0; i < RMI4_MAX_BUTTONS; i++)
    {
        if(data[i] && !prevData[i])
        {
            Logical[i] = TRUE;
            WdfTimerStart(ControllerContext->ButtonsTimer, WDF_REL_TIMEOUT_IN_MS(1500));
        }
    }

    if(!data[1] && prevData[1]) //when up key
    {
        if(Logical[1]) //send key Down if Logical is true
        {
            //get hidReports from Queue
            GetNextHidReport(ControllerContext, &hidReport);
            hidReport->ReportID = REPORTID_CAPKEY_KEYBOARD;
            hidKeys = &(hidReport->KeyReport);
            hidKeys->bKeys |= (1 << 0);
        }

        GetNextHidReport(ControllerContext, &hidReport);
        hidReport->ReportID = REPORTID_CAPKEY_KEYBOARD; //send empty report(up all keys)

        Logical[1] = FALSE;
    }

    BOOLEAN b0 = !data[0] && prevData[0] && Logical[0];
    BOOLEAN b2 = !data[2] && prevData[2] && Logical[2];

    if(b0 || b2)
    {
        //get two hidReports from Queue
        GetNextHidReport(ControllerContext, &hidReport);
        hidReport->ReportID = REPORTID_CAPKEY_CONSUMER;
        hidKeys = &(hidReport->KeyReport);
        hidKeys->bKeys |= (b0) ? (1 << 0) : 0;
        hidKeys->bKeys |= (b2) ? (1 << 1) : 0;
        
        GetNextHidReport(ControllerContext, &hidReport);
        hidReport->ReportID = REPORTID_CAPKEY_CONSUMER;

        Logical[0] = FALSE;
        Logical[2] = FALSE;
    }

    //up ALT for alt+tab when button is up
    if(!data[2] && prevData[2] && !Logical[2])
    {
        GetNextHidReport(ControllerContext, &hidReport);
        hidReport->ReportID = REPORTID_CAPKEY_KEYBOARD;
    }

    //
    // On return of success, this request will be completed up the stack
    //

    for(int i = 0; i < RMI4_MAX_BUTTONS; i++)
        ControllerContext->ButtonsCache.prevPhysicalState[i] = ControllerContext->ButtonsCache.PhysicalState[i];

//exit:
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
                           
    ULONG BackAreaXMin =   0;
    ULONG BackAreaYMin =   1300;
    ULONG BackAreaXMax =   216;
    ULONG BackAreaYMax =   1390;
                           
    ULONG StartAreaXMin =  297;
    ULONG StartAreaYMin =  1300;
    ULONG StartAreaXMax =  472;
    ULONG StartAreaYMax =  1390;
                           
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

	if (ControllerX > ButtonAreaXMin && ControllerX < ButtonAreaXMax && ControllerY > ButtonAreaYMin && ControllerY < ButtonAreaYMax)
	{
		if (ControllerX > BackAreaXMin && ControllerX < BackAreaXMax && ControllerY > BackAreaYMin && ControllerY < BackAreaYMax)
		{
			return BUTTON_BACK;
		}
		if (ControllerX > StartAreaXMin && ControllerX < StartAreaXMax && ControllerY > StartAreaYMin && ControllerY < StartAreaYMax)
		{
			return BUTTON_START;
		}
		if (ControllerX > SearchAreaXMin && ControllerX < SearchAreaXMax && ControllerY > SearchAreaYMin && ControllerY < SearchAreaYMax)
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

void 
ButtonsTimerHandler(
    WDFTIMER Timer
)
{
    WDFDEVICE FxDevice = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    PDEVICE_EXTENSION devContext = GetDeviceContext(FxDevice);
    RMI4_CONTROLLER_CONTEXT* controller = (RMI4_CONTROLLER_CONTEXT*)devContext->TouchContext;

    BOOLEAN* Logical = controller->ButtonsCache.LogicalState;

    PHID_INPUT_REPORT hidReport = NULL;
    PHID_KEY_REPORT hidKeys;
    BOOLEAN flag = FALSE;

    if(Logical[2])
    {
        //get two hidReports from Queue
        //GetNextHidReport(controller, &hidReport);
        //hidReport->ReportID = REPORTID_CAPKEY_KEYBOARD;
        //hidKeys = &(hidReport->KeyReport);
        //hidKeys->bKeys |= (1 << 2);//alt

        GetNextHidReport(controller, &hidReport);
        hidReport->ReportID = REPORTID_CAPKEY_KEYBOARD;
        hidKeys = &(hidReport->KeyReport);
        hidKeys->bKeys |= (1 << 2);//alt
        hidKeys->bKeys |= (1 << 1);//tab

        GetNextHidReport(controller, &hidReport);
        hidReport->ReportID = REPORTID_CAPKEY_KEYBOARD;
        hidKeys = &(hidReport->KeyReport);
        hidKeys->bKeys |= (1 << 2);//alt
        //hidKeys->bKeys &= ~(1 << 1);//UP Tab

        Logical[2] = FALSE;
        flag = TRUE;
    }

    if(flag)
    {
        SendHidReports(
            devContext->PingPongQueue,
            controller->HidQueue,
            controller->HidQueueCount
        );
        controller->HidQueueCount = 0;
    }

    //Trace(TRACE_LEVEL_INFORMATION, TRACE_FLAG_HID, "Buttons Timer reached!");
}

void
ButtonsInitTimer(
    RMI4_CONTROLLER_CONTEXT* ControllerContext
)
{
    WDF_TIMER_CONFIG       timerConfig;
    WDF_OBJECT_ATTRIBUTES  timerAttributes;
    WDF_TIMER_CONFIG_INIT(&timerConfig, ButtonsTimerHandler);
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
    timerAttributes.ParentObject = ControllerContext->FxDevice;

    WdfTimerCreate(&timerConfig, &timerAttributes, &ControllerContext->ButtonsTimer);
}