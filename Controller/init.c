/*++
    Copyright (c) Microsoft Corporation. All Rights Reserved. 
    Sample code. Dealpoint ID #843729.

    Module Name:

        init.c

    Abstract:

        Contains Synaptics initialization code

    Environment:

        Kernel mode

    Revision History:

--*/

#include "rmiinternal.h"
#include "spb.h"
#include "debug.h"
//#include "init.tmh"




#pragma warning(push)
#pragma warning(disable:4242) // Conversion, possible loss of data

//
// The logical values come from the registry and are hence DWORDs but the 
// physical registers are only 8 bits wide so we use the lower 8 bits of the
// logical value.
//



NTSTATUS
RmiChangePage(
    IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN int DesiredPage
    )
/*++
 
  Routine Description:

    This utility function changes the current register address page.

  Arguments:

    ControllerContext - A pointer to the current touch controller context
    SpbContext - A pointer to the current i2c context
    DesiredPage - The page the caller expects to be mapped in

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    BYTE page;
    NTSTATUS status;

    //
    // If we're on this page already return success
    //
    if (ControllerContext->CurrentPage == DesiredPage)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        page = (BYTE) DesiredPage;

        status = SpbWriteDataSynchronously(
            SpbContext,
            RMI4_PAGE_SELECT_ADDRESS,
            &page,
            sizeof(BYTE));

        if (NT_SUCCESS(status))
        {
            ControllerContext->CurrentPage = DesiredPage;
        }
    }

    return status;
}

int
RmiGetFunctionIndex(
    IN RMI4_FUNCTION_DESCRIPTOR* FunctionDescriptors,
    IN int FunctionCount,
    IN int FunctionDesired
    )
/*++
 
  Routine Description:

    Returns the descriptor table index that corresponds to the
    desired RMI function.

  Arguments:

    FunctionDescriptors - A pointer to the touch controllers
    full list of function descriptors
    
    FunctionCount - The count of function descriptors contained
    in the above FunctionDescriptors list

    FunctionDesired - The RMI function number (note they are always
    in hexadecimal the RMI4 specification)

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    UCHAR i;

    for (i=0; i < FunctionCount; i++)
    {
        //
        // Break if we found the index
        //
        if (FunctionDescriptors[i].Number == FunctionDesired)
        {     
             break;
        }
    }

    //
    // Return the count if the index wasn't found
    //
    return i;
}

NTSTATUS
RmiGetFirmwareVersion(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    This function queries the firmware version of the current chip for
    debugging purposes.

  Arguments:

    ControllerContext - A pointer to the current touch controller context
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    int index;
    NTSTATUS status;

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

    //
    // Store all F01 query registers, which contain the product ID
    //
    // TODO: Fix transfer size when SPB can support larger I2C 
    //       transactions
    //
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].QueryBase,
        &ControllerContext->F01QueryRegisters,
        sizeof(BYTE) * FIELD_OFFSET(RMI4_F01_QUERY_REGISTERS, ProductID10));

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error reading RMI F01 Query registers - STATUS:%X",
            status);

        goto exit;
    }

exit:

    return status;
}


NTSTATUS
RmiConfigureFunctions(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    RMI4 devices such as this Synaptics touch controller are organized
    as collections of logical functions. Discovered functions must be
    configured, which is done in this function (things like sleep 
    timeouts, interrupt enables, report rates, etc.)

  Arguments:

    ControllerContext - A pointer to the current touch controller
    context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
	NTSTATUS status =STATUS_SUCCESS;
	int i;
	BOOLEAN f01Flag = FALSE, 
		f11Flag = FALSE, 
		f1aFlag = FALSE;

	ControllerContext->F12Flag = FALSE;

	for(i = 0; i < RMI4_MAX_FUNCTIONS; i++)
	{
		switch(ControllerContext->Descriptors[i].Number)
		{
		case RMI4_F01_RMI_DEVICE_CONTROL:
			f01Flag = TRUE;
			break;
		case RMI4_F11_2D_TOUCHPAD_SENSOR:
			f11Flag = TRUE;
			break;
		case RMI4_F12_2D_TOUCHPAD_SENSOR:
			ControllerContext->F12Flag = TRUE;
			break;
		case RMI4_F1A_0D_CAP_BUTTON_SENSOR:
			f1aFlag = TRUE;
			break;
		default:
			break;
		}
	}
	
	if(f11Flag && ControllerContext->F12Flag)
		f11Flag = FALSE;

	if(f11Flag)
		status = configureF11(ControllerContext, SpbContext);
	
	if(!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"error can't configure f11 - STATUS %x",
			status
		);
		goto exit;
	}

	if(ControllerContext->F12Flag)
		status = configureF12(ControllerContext, SpbContext);

	if(f1aFlag)
		status = configureF1A(ControllerContext, SpbContext);

	if(f01Flag)
		status = configureF01(ControllerContext, SpbContext);
exit:

	return status;
}

NTSTATUS
RmiBuildFunctionsTable(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    RMI4 devices such as this Synaptics touch controller are organized
    as collections of logical functions. When initially communicating
    with the chip, a driver must build a table of available functions,
    as is done in this routine.

  Arguments:

    ControllerContext - A pointer to the current touch controller context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    UCHAR address;
    int function;
    int page;
    NTSTATUS status;


    //
    // First function is at a fixed address 
    //
    function = 0;
    address = RMI4_FIRST_FUNCTION_ADDRESS;
    page = 0;

    //
    // Discover chip functions one by one
    //
    do
    {
        //
        // Read function descriptor
        //
        status = SpbReadDataSynchronously(
            SpbContext,
            address,
            &ControllerContext->Descriptors[function],
            sizeof(RMI4_FUNCTION_DESCRIPTOR));

        if (!(NT_SUCCESS(status)))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INIT,
                "Error returned from SPB/I2C read attempt %d - STATUS:%X",
                function,
                status);
            goto exit;
        }

        //
        // Function number 0 implies "last function" on this register page,
        // and if this "last function" is the first function on the page, there
        // are no more functions to discover.
        //
        if (ControllerContext->Descriptors[function].Number == 0 &&
            address == RMI4_FIRST_FUNCTION_ADDRESS)
        {
            break;
        }
        //
        // If we've exhausted functions on this page, look for more functoins
        // on the next register page
        //
        else if (ControllerContext->Descriptors[function].Number == 0 &&
            address != RMI4_FIRST_FUNCTION_ADDRESS)
        {
            page++;
            address = RMI4_FIRST_FUNCTION_ADDRESS;

            status = RmiChangePage(
                ControllerContext,
                SpbContext,
                page);

            if (!NT_SUCCESS(status))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    TRACE_FLAG_INIT,
                    "Error attempting to change page - STATUS:%X",
                    status);
                goto exit;
            }
        }
        //
        // Descriptor stored, look for next or terminator
        //
        else
        {
            Trace(
                TRACE_LEVEL_VERBOSE,
                TRACE_FLAG_INIT,
                "Discovered function $%x",
                ControllerContext->Descriptors[function].Number);

            ControllerContext->FunctionOnPage[function] = page;
            function++;
            address = address - sizeof(RMI4_FUNCTION_DESCRIPTOR);
        }

    } while (
        (address > 0) && 
        (function < RMI4_MAX_FUNCTIONS));

    //
    // If we swept the address space without finding an "end function"
    // or maxed-out the total number of functions supported by the 
    // driver, note the error and exit.
    //
    if (function >= RMI4_MAX_FUNCTIONS)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error, encountered more than %d functions, must extend driver",
            RMI4_MAX_FUNCTIONS);

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }
    if (address <= 0) 
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error, did not find terminator function 0, address down to %d",
            address);

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    //
    // Note the total number of functions that exist
    //
    ControllerContext->FunctionCount = function;

    Trace(
        TRACE_LEVEL_VERBOSE,
        TRACE_FLAG_INIT,
        "Discovered %d RMI functions total",
        function);

exit:

    return status;
}

NTSTATUS
RmiCheckInterrupts(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext,
    IN ULONG* InterruptStatus
    )
/*++
 
  Routine Description:

    This function handles controller interrupts. It currently only
    supports valid touch interrupts. Any other interrupt sources (such as
    device losing configuration or being reset) are unhandled, but noted
    in the controller context.

  Arguments:

    ControllerContext - A pointer to the current touch controller
    context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    RMI4_F01_DATA_REGISTERS data;
    int index;
    NTSTATUS status;

    RtlZeroMemory(&data, sizeof(data));
    *InterruptStatus = 0;

    //
    // Locate RMI data base address
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

    //
    // Read interrupt status registers
    //
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].DataBase,
        &data,
        sizeof(data));

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error reading interrupt status - STATUS:%X",
            status);

        goto exit;
    }

    //
    // Check for catastrophic failures, simply store in context for
    // debugging should these errors occur.
    //
    switch (data.DeviceStatus.Status)
    {
        case RMI4_F01_DATA_STATUS_NO_ERROR:
        {
            break;
        }
        case RMI4_F01_DATA_STATUS_RESET_OCCURRED:
        {
            ControllerContext->ResetOccurred = TRUE;
            break;
        }
        case RMI4_F01_DATA_STATUS_INVALID_CONFIG:
        {
            ControllerContext->InvalidConfiguration = TRUE;

            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Received status code 2 - invalid configuration");

            break;
        }
        case RMI4_F01_DATA_STATUS_DEVICE_FAILURE:
        {
            ControllerContext->DeviceFailure = TRUE;

            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Received status code 4 - device failure");

            break;
        }
        default:
        {
            ControllerContext->UnknownStatus = TRUE;
            ControllerContext->UnknownStatusMessage = data.DeviceStatus.Status;

            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Received unknown status code - %d",
                ControllerContext->UnknownStatusMessage);

            break;
        }
    }

    //
    // If we're in flash programming mode, report an error
    //
    if (data.DeviceStatus.FlashProg)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error, device status indicates chip in programming mode");

        goto exit;
    }

    //
    // If the chip has lost it's configuration, reconfigure
    //
    if (data.DeviceStatus.Unconfigured)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error, device status indicates chip is unconfigured");

        status = RmiConfigureFunctions(
            ControllerContext,
            SpbContext);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Could not reconfigure chip - STATUS:%X",
                status);

            goto exit;
        }

    }

    if (data.InterruptStatus[0])
    {
        *InterruptStatus = data.InterruptStatus[0] & 0xFF;
    }
    else
    {
        Trace(
            TRACE_LEVEL_VERBOSE,
            TRACE_FLAG_INTERRUPT,
            "Unexpected -- no interrupt status bit set");
    }

exit:
    return status;
}

NTSTATUS 
TchStartDevice(
    IN VOID *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    This routine is called in response to the KMDF prepare hardware call
    to initialize the touch controller for use.

  Arguments:

    ControllerContext - A pointer to the current touch controller
    context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    RMI4_CONTROLLER_CONTEXT* controller;
    ULONG interruptStatus;
    NTSTATUS status;

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;
    interruptStatus = 0;
    status = STATUS_SUCCESS;

    //
    // Initialize capacitive button LED support
    //
    controller->BklContext = TchBklInitialize(controller->FxDevice);

    if (controller->BklContext == NULL)
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_FLAG_INIT,
            "Warning, failed to initialize touch button backlight control");
    }

    //
    // Populate context with RMI function descriptors
    //
    status = RmiBuildFunctionsTable(
        ControllerContext,
        SpbContext);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not build table of RMI functions - STATUS:%X",
            status);
        goto exit;
    }

    //
    // Initialize RMI function control registers
    //
    status = RmiConfigureFunctions(
        ControllerContext,
        SpbContext);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not configure RMI functions - STATUS:%X",
            status);
        goto exit;
    }

    //
    // Read and store the firmware version
    //
    status = RmiGetFirmwareVersion(
        ControllerContext,
        SpbContext);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not get RMI firmware version - STATUS:%X",
            status);
        goto exit;
    }

    //
    // Clear any pending interrupts
    //
    status = RmiCheckInterrupts(
        ControllerContext,
        SpbContext,
        &interruptStatus
        );

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not get interrupt status - STATUS:%X%",
            status);
    }

exit:
    
    return status;
}

NTSTATUS 
TchStopDevice(
    IN VOID *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++

Routine Description:

    This routine cleans up the device that is stopped.

Argument:

    ControllerContext - Touch controller context
    
    SpbContext - A pointer to the current i2c context

Return Value:

    NTSTATUS indicating sucess or failure
--*/
{
    RMI4_CONTROLLER_CONTEXT* controller;

    UNREFERENCED_PARAMETER(SpbContext);

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;

    if (NULL != controller->BklContext)
    {
        TchBklDeinitialize(controller->BklContext);
        controller->BklContext = NULL;
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS 
TchAllocateContext(
    OUT VOID **ControllerContext,
    IN WDFDEVICE FxDevice
    )
/*++

Routine Description:

    This routine allocates a controller context.

Argument:

    ControllerContext - Touch controller context
    FxDevice - Framework device object

Return Value:

    NTSTATUS indicating sucess or failure
--*/
{
    RMI4_CONTROLLER_CONTEXT* context;
    NTSTATUS status;

    context = ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(RMI4_CONTROLLER_CONTEXT),
        TOUCH_POOL_TAG);

    if (NULL == context)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not allocate controller context!");

        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    RtlZeroMemory(context, sizeof(RMI4_CONTROLLER_CONTEXT));
    context->FxDevice = FxDevice;

    //
    // Get screen properties and populate context
    //
    TchGetScreenProperties(&context->Props);

    //
    // Allocate a WDFWAITLOCK for guarding access to the
    // controller HW and driver controller context
    //
    status = WdfWaitLockCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        &context->ControllerLock);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not allocate controller context - STATUS:%X",
            status);

        goto exit;

    }

    *ControllerContext = context;

exit:
    
    return status;
}

NTSTATUS 
TchFreeContext(
    IN VOID *ControllerContext
    )
/*++

Routine Description:

    This routine frees a controller context.

Argument:

    ControllerContext - Touch controller context

Return Value:

    NTSTATUS indicating sucess or failure
--*/
{
    RMI4_CONTROLLER_CONTEXT* controller;

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;

    if (controller != NULL)
    {

        if (controller->ControllerLock != NULL)
        {
            WdfObjectDelete(controller->ControllerLock);
        }

        ExFreePoolWithTag(controller, TOUCH_POOL_TAG);
    }
    
    return STATUS_SUCCESS;
}

#pragma warning(pop)
