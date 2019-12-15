/*++
    Copyright (c) Microsoft Corporation. All Rights Reserved. 
    Sample code. Dealpoint ID #843729.

    Module Name:

        report.c

    Abstract:

        Contains Synaptics specific code for reporting samples

    Environment:

        Kernel mode

    Revision History:

--*/

#include "controller.h"
#include "config.h"
#include "rmiinternal.h"
#include "spb.h"
#include "debug.h"
//#include "report.tmh"

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

    if(ControllerContext->capButtonsCache.PendingState >> 7)
    {
        HidReport->ReportID = REPORTID_CAPKEY_CONSUMER;
        hidKeys->bKeys = ControllerContext->capButtonsCache.PendingState & 0xF;
        ControllerContext->capButtonsCache.PendingState = 0;
        goto exit;
    }

    RMI4_F1A_DATA_REGISTERS prevDataF1A;
    prevDataF1A.Raw = ControllerContext->capButtonsCache.prevPhysicalState;
    ControllerContext->capButtonsCache.prevPhysicalState = dataF1A.Raw;


    if(dataF1A.Button1 != prevDataF1A.Button1)
    {
        HidReport->ReportID = REPORTID_CAPKEY_KEYBOARD;
        hidKeys->bKeys |= (dataF1A.Button1) ? (1 << 0) : 0;
        if(dataF1A.Button0 != prevDataF1A.Button0 || dataF1A.Button2 != prevDataF1A.Button2)
        {
            ControllerContext->capButtonsCache.PendingState |= (dataF1A.Button0) ? (1 << 0) : 0;
            ControllerContext->capButtonsCache.PendingState |= (dataF1A.Button2) ? (1 << 1) : 0;
            ControllerContext->capButtonsCache.PendingState |= (1 << 7); //indicate waiting buttons
            *PendingTouches = TRUE;
        }
        goto exit;
    }
    if(dataF1A.Button0 != prevDataF1A.Button0 || dataF1A.Button2 != prevDataF1A.Button2)
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

VOID
RmiFillNextHidReportFromCache(
    IN PHID_INPUT_REPORT HidReport,
    IN RMI4_FINGER_CACHE *Cache,
    IN PTOUCH_SCREEN_PROPERTIES Props,
    IN int *TouchesReported,
    IN int TouchesTotal
    )
/*++

Routine Description:

    This routine fills a HID report with the next one or two touch entries in
    the local device finger cache. 

    The routine also adjusts X/Y coordinates to match the desired display
    coordinates.

Arguments:

    HidReport - pointer to the HID report structure to fill
    Cache - pointer to the local device finger cache
    Props - information on how to adjust X/Y coordinates to match the display
    TouchesReported - On entry, the number of touches (against total) that
        have already been reported. As touches are transferred from the local
        device cache to a HID report, this number is incremented.
    TouchesTotal - total number of touches in the touch cache

Return Value:

    None.

--*/
{
    PHID_TOUCH_REPORT hidTouch = NULL;
    int currentlyReporting;

    HidReport->ReportID = REPORTID_MTOUCH;
    hidTouch = &(HidReport->TouchReport);

    //
    // There are only 16-bits for ScanTime, truncate it
    //
    hidTouch->InputReport.ScanTime = Cache->ScanTime & 0xFFFF;

    //
    // Report the next available finger
    //
    currentlyReporting = Cache->FingerDownOrder[*TouchesReported];

    hidTouch->InputReport.ContactId = (UCHAR) currentlyReporting;
    hidTouch->InputReport.wXData = (USHORT) Cache->FingerSlot[currentlyReporting].x;
    hidTouch->InputReport.wYData = (USHORT) Cache->FingerSlot[currentlyReporting].y;

    //
    // Perform per-platform x/y adjustments to controller coordinates
    //
    TchTranslateToDisplayCoordinates(
        &hidTouch->InputReport.wXData,
        &hidTouch->InputReport.wYData,
        Props);

    if (Cache->FingerSlot[currentlyReporting].fingerStatus)
    {
        hidTouch->InputReport.bStatus = FINGER_STATUS;
    }

    (*TouchesReported)++;

    //
    // A single HID report can contain two touches, so see if there's more
    //
    if (TouchesTotal - *TouchesReported > 0)
    {
        currentlyReporting = Cache->FingerDownOrder[*TouchesReported];

        hidTouch->InputReport.ContactId2 = (UCHAR) currentlyReporting;
        hidTouch->InputReport.wXData2 = (USHORT) Cache->FingerSlot[currentlyReporting].x;
        hidTouch->InputReport.wYData2 = (USHORT) Cache->FingerSlot[currentlyReporting].y;

        //
        // Perform per-platform x/y adjustments to controller coordinates
        //
        TchTranslateToDisplayCoordinates(
            &hidTouch->InputReport.wXData2,
            &hidTouch->InputReport.wYData2,
            Props);

        if (Cache->FingerSlot[currentlyReporting].fingerStatus)
        {
            hidTouch->InputReport.bStatus2 = FINGER_STATUS;
        }

        (*TouchesReported)++;
    }

    //
    // Though a single HID report can contain up to two touches, but more
    // can be on the screen, ActualCount reflects the total number
    // of touches that will be reported when the first report (of possibly
    // many) is sent up
    //
    if (*TouchesReported == 1 || *TouchesReported == 2)
    {
        hidTouch->InputReport.ActualCount = (UCHAR) TouchesTotal;
    }

    /*Trace(
        TRACE_LEVEL_NOISE,
        TRACE_FLAG_REPORTING,
        "ActualCount %d, Touch0 ContactId %u X %u Y %u Tip %u, Touch1 ContactId %u X %u Y %u Tip %u",
        hidTouch->InputReport.ActualCount,
        hidTouch->InputReport.ContactId,
        hidTouch->InputReport.wXData,
        hidTouch->InputReport.wYData,
        hidTouch->InputReport.bStatus,
        hidTouch->InputReport.ContactId2,
        hidTouch->InputReport.wXData2,
        hidTouch->InputReport.wYData2,
        hidTouch->InputReport.bStatus2);*/
}

NTSTATUS
RmiServiceTouchDataInterrupt(
    IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN PHID_INPUT_REPORT HidReport,
    IN UCHAR InputMode,
    OUT BOOLEAN* PendingTouches
    )
/*++

Routine Description:

    Called when a touch interrupt needs service. Because we fill HID reports
    with two touches at a time, if more than two touches were read from
    hardware, we may need to complete this request from local cached state.

Arguments:

    ControllerContext - Touch controller context
    SpbContext - A pointer to the current SPB context (I2C, etc)
    HidReport- Buffer to fill with a hid report if touch data is available
    InputMode - Specifies mouse, single-touch, or multi-touch reporting modes
    PendingTouches - Notifies caller if there are more touches to report, to 
        complete reporting the full state of fingers on the screen

Return Value:

    NTSTATUS indicating whether or not the current hid report buffer was filled

    PendingTouches also indicates whether the caller should expect more than
        one request to be completed to indicate the full state of fingers on 
        the screen
--*/
{
    NTSTATUS status;

    status = STATUS_SUCCESS;
    NT_ASSERT(PendingTouches != NULL);
    *PendingTouches = FALSE;

    //
    // If no touches are unreported in our cache, read the next set of touches
    // from hardware.
    //
    if (ControllerContext->TouchesReported == ControllerContext->TouchesTotal)
    {
        if(ControllerContext->F12Flag)
            status = GetTouchesFromF12(ControllerContext, SpbContext);
        else
            status = GetTouchesFromF11(ControllerContext,SpbContext);

        if(!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_SAMPLES,
                "Error. Can't GetTouches from controller - STATUS %x",
                status
            );

            goto exit;
        }

        //
        // Prepare to report touches via HID reports
        //
        ControllerContext->TouchesReported = 0;
        ControllerContext->TouchesTotal = 
            ControllerContext->Cache.FingerDownCount;

        //
        // If no touches are present return that no data needed to be reported
        //
        if (ControllerContext->TouchesTotal == 0)
        {
            status = STATUS_NO_DATA_DETECTED;
            goto exit;
        }
    }

    RtlZeroMemory(HidReport, sizeof(HID_INPUT_REPORT));

    //
    // Single-finger and HID-mouse input modes not implemented
    //
    if (MODE_MULTI_TOUCH != InputMode)
    {
        Trace(
            TRACE_LEVEL_VERBOSE,
            TRACE_FLAG_SAMPLES,
            "Unable to report touches, only multitouch mode is supported");

        status = STATUS_NOT_IMPLEMENTED;
        goto exit;
    }

    //
    // Fill report with the next (max of two) cached touches
    //
    RmiFillNextHidReportFromCache(
        HidReport,
        &ControllerContext->Cache,
        &ControllerContext->Props,
        &ControllerContext->TouchesReported,
        ControllerContext->TouchesTotal
        );

    //
    // Update the caller if we still have outstanding touches to report
    //
    if (ControllerContext->TouchesReported < ControllerContext->TouchesTotal)
    {
        *PendingTouches = TRUE;       
    }
    else
    {
        *PendingTouches = FALSE;
    }

exit:
    
    return status;
}


NTSTATUS
TchServiceInterrupts(
    IN VOID *ControllerContext,
    IN SPB_CONTEXT *SpbContext,
    IN PHID_INPUT_REPORT HidReport,
    IN UCHAR InputMode,
    IN BOOLEAN *ServicingComplete
    )
/*++

Routine Description:

    This routine is called in response to an interrupt. The driver will
    service chip interrupts, and if data is available to report to HID,
    fill the Request object buffer with a HID report.

Arguments:

    ControllerContext - Touch controller context
    SpbContext - A pointer to the current i2c context
    HidReport - Pointer to a HID_INPUT_REPORT structure to report to the OS
    InputMode - Specifies mouse, single-touch, or multi-touch reporting modes
    ServicingComplete - Notifies caller if there are more reports needed to 
        complete servicing interrupts coming from the hardware.

Return Value:

    NTSTATUS indicating whether or not the current HidReport has been filled

    ServicingComplete indicates whether or not a new report buffer is required
        to complete interrupt processing.
--*/
{
    NTSTATUS status = STATUS_NO_DATA_DETECTED;
    RMI4_CONTROLLER_CONTEXT* controller;

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;

    NT_ASSERT(ServicingComplete != NULL);

    //
    // Grab a waitlock to ensure the ISR executes serially and is 
    // protected against power state transitions
    //
    WdfWaitLockAcquire(controller->ControllerLock, NULL);

    //
    // Check the interrupt source if no interrupts are pending processing
    //
    if (controller->InterruptStatus == 0)
    {
        status = RmiCheckInterrupts(
            controller,
            SpbContext, 
            &controller->InterruptStatus);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Error servicing interrupts - STATUS:%X",
                status);

            *ServicingComplete = FALSE;
            goto exit;
        }
    }

    //
    // Driver only services 0D cap button and 2D touch messages currently
    //
    if (controller->InterruptStatus & 
        ~(RMI4_INTERRUPT_BIT_0D_CAP_BUTTON | RMI4_INTERRUPT_BIT_2D_TOUCH))
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_FLAG_INTERRUPT,
            "Ignoring following interrupt flags - STATUS:%X",
            controller->InterruptStatus & 
                ~(RMI4_INTERRUPT_BIT_0D_CAP_BUTTON | 
                RMI4_INTERRUPT_BIT_2D_TOUCH));

        //
        // Mask away flags we don't service
        //
        controller->InterruptStatus &=
            (RMI4_INTERRUPT_BIT_0D_CAP_BUTTON | 
            RMI4_INTERRUPT_BIT_2D_TOUCH);
    }

    //
    // RmiServiceXXX routine will change status to STATUS_SUCCESS if there
    // is a HID report to process.
    //
    status = STATUS_UNSUCCESSFUL;

    //
    // Service a capacitive button event if indicated by hardware
    //
    if (controller->InterruptStatus & RMI4_INTERRUPT_BIT_0D_CAP_BUTTON)
    {
        BOOLEAN pendingTouches = FALSE;

        status = RmiServiceCapacitiveButtonInterrupt(
            ControllerContext,
            SpbContext,
            HidReport,
            &pendingTouches);
        
        //
        // If there are more touches to report, servicing is incomplete
        //
        if(pendingTouches == FALSE)
        {
            controller->InterruptStatus &= ~RMI4_INTERRUPT_BIT_0D_CAP_BUTTON;
        }
        
        //
        // Success indicates the report is ready to be sent, otherwise,
        // continue to service interrupts.
        //
        if (NT_SUCCESS(status))
        {
            goto exit;
        }
        else
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Error processing cap button event - STATUS:%X",
                status);
        }
    }

    //
    // Service a touch data event if indicated by hardware 
    //
    if (controller->InterruptStatus & RMI4_INTERRUPT_BIT_2D_TOUCH)
    {
        BOOLEAN pendingTouches = FALSE;

        status = RmiServiceTouchDataInterrupt(
            ControllerContext,
            SpbContext,
            HidReport,
            InputMode,
            &pendingTouches);

        //
        // If there are more touches to report, servicing is incomplete
        //
        if (pendingTouches == FALSE)
        {
            controller->InterruptStatus &= ~RMI4_INTERRUPT_BIT_2D_TOUCH;
        }

        //
        // Success indicates the report is ready to be sent, otherwise,
        // continue to service interrupts.
        //
        if (NT_SUCCESS(status))
        {
            goto exit;
        }
        else
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Error processing touch event - STATUS:%X",
                status);
        }
    }

    //
    // Add servicing for additional touch interrupts here
    //

exit:

    //
    // Indicate whether or not we're done servicing interrupts
    //
    if (controller->InterruptStatus == 0)
    {
        *ServicingComplete = TRUE;
    }
    else
    {
        *ServicingComplete = FALSE;
    }

    //
    // Turn on capacitive key backlights that may have timed out
    // due to user inactivity
    //
    if (NT_SUCCESS(status) && (controller->BklContext != NULL))
    {
        TchBklNotifyTouchActivity(controller->BklContext, (DWORD) GetTickCount());
    }

    WdfWaitLockRelease(controller->ControllerLock);

    return status;
}
