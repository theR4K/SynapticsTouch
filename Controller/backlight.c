/*++
    Copyright (c) Microsoft Corporation. All Rights Reserved. 
    Sample code. Dealpoint ID #843729.

    Module Name:

        backlight.c

    Abstract:

        Contains control routines for capacitive button backlights

    Environment:

        Kernel mode

    Revision History:

--*/

#define INITGUID
#include "rmiinternal.h"
#include "devguid.h"
#include "wdmguid.h"
#include "debug.h"
//#include "backlight.tmh"

//
// Default milllux-to-backlight-intensity-percentage table
//
static BKL_LUX_TABLE_ENTRY g_DefaultLuxMap[BKL_NUM_LEVELS_DEFAULT] =
{
    {
        0,
        100000,
        5
    },
    {
        100000,
        200000,
        10
    },
    {
        200000,
        400000,
        25
    },
    {
        400000,
        0xFFFFFFFF,
        0
    },
};

__inline
VOID 
TchBklSleepMillisec(
    IN ULONG TimeMsec
    )
/*++

Routine Description:

    This helper routine puts the caller to sleep

Arguments:

    TimeMsec - time to sleep in millisec
   
Return Value:

    None.

--*/
{
    LARGE_INTEGER Delay = {0};

    Delay.QuadPart = WDF_REL_TIMEOUT_IN_MS(TimeMsec);

    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}


NTSTATUS
TchBklGetDefaultLuxIntensityMap(
    IN BKL_CONTEXT* BklContext
    )
/*++

Routine Description:

    This helper routine copies the default lux table to device context.

Arguments:

    BklContext - Backlight control context structure
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    BklContext->BklNumLevels = BKL_NUM_LEVELS_DEFAULT;

    BklContext->BklLuxTable = (BKL_LUX_TABLE_ENTRY*) 
        ExAllocatePoolWithTag(
            NonPagedPool,
            sizeof(BKL_LUX_TABLE_ENTRY) * BklContext->BklNumLevels,
            TOUCH_POOL_TAG);

    if (BklContext->BklLuxTable == NULL)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not allocate memory for lux table, out of memory");

        return STATUS_UNSUCCESSFUL;
    }

    RtlCopyMemory(
        BklContext->BklLuxTable,
        g_DefaultLuxMap,
        sizeof(g_DefaultLuxMap));

    return STATUS_SUCCESS;
}

NTSTATUS
TchBklGetValueFromCollection(
    IN WDFCOLLECTION* Collection,
    IN ULONG Index,
    OUT PULONG Value
    )
/*++

Routine Description:

    This helper routine grabs a ULONG encoded as a UNICODE_STRING wrapped in
    a WDFSTRING object stored in a WDFCOLLECTION and stores it in "Value".

Arguments:

    Collection - must be a WDFCOLLECTION of WDFSTRINGs
    Index - the index in the collection of interest
    Value - pointer which receives the integer value of the string at Index
   
Return Value:

    NTSTATUS indicating success or failure

--*/

{
    NTSTATUS status;
    WDFSTRING stringHandle;
    UNICODE_STRING string;
    ULONG value;

    stringHandle = WdfCollectionGetItem(*Collection, Index);

    if (NULL == stringHandle)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "String %d in registry lux table is missing",
            Index);

        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    WdfStringGetUnicodeString(stringHandle, &string);

    status = RtlUnicodeStringToInteger(
        &string,
        10,
        &value);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "String %d in registry lux table is invalid - STATUS:%X",
            Index,
            status);

        goto exit;
    }

    *Value = value;

exit:

    return status;
}

NTSTATUS
TchBklGetCustomLuxIntensityMap(
    IN BKL_CONTEXT* BklContext
    )
/*++

Routine Description:

    This helper routine reads and parses registry strings to build a lux table
    as specified by an OEM, with an arbitrary number of levels. This routine
    validates that sane entries are provided.

Arguments:

    BklContext - Backlight control context structure
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    DECLARE_CONST_UNICODE_STRING(bklIntensityMappings, BKL_LUX_TABLE_INTENSITIES);
    DECLARE_CONST_UNICODE_STRING(bklLuxRangesValue, BKL_LUX_TABLE_RANGES);
    DECLARE_CONST_UNICODE_STRING(bklSettingsPath, BKL_REGISTRY_PATH);
    WDFCOLLECTION luxRangeStrings;
    ULONG i;
    WDFCOLLECTION intensityStrings;
    WDFKEY key;
    NTSTATUS status;
    ULONG value;

    key = NULL;
    luxRangeStrings = NULL;
    intensityStrings = NULL;

    status = WdfRegistryOpenKey(
        NULL,
        &bklSettingsPath,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_FLAG_OTHER,
            "Couldn't open registry path for cap button backlights, disabled");

        goto exit;
    }

    status = WdfCollectionCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        &luxRangeStrings);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Couldn't allocate a collection for lux ranges - STATUS:%X",
            status);

        goto exit;
    }

    status = WdfCollectionCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        &intensityStrings);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Couldn't allocate a collection for lux ranges - STATUS:%X",
            status);

        goto exit;
    }

    status = WdfRegistryQueryMultiString(
        key,
        &bklLuxRangesValue,
        WDF_NO_OBJECT_ATTRIBUTES,
        luxRangeStrings);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Couldn't retrieve lux range strings from registry - STATUS:%X",
            status);

        goto exit;
    }

    status = WdfRegistryQueryMultiString(
        key,
        &bklIntensityMappings,
        WDF_NO_OBJECT_ATTRIBUTES,
        intensityStrings);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Couldn't retrieve intensity strings from registry - STATUS:%X",
            status);

        goto exit;
    }

    //
    // Validate that at least one non-null string was provided
    //
    BklContext->BklNumLevels = WdfCollectionGetCount(luxRangeStrings);

    if (BklContext->BklNumLevels == 0)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "No range strings provided, registry lux table is invalid");

        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    //
    // Make sure the count of intensity strings matches the count of ranges
    //
    if (WdfCollectionGetCount(intensityStrings) != 
        BklContext->BklNumLevels)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Error in registry lux mapping table, expect %d levels, found %d",
            BklContext->BklNumLevels,
            WdfCollectionGetCount(intensityStrings));

        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    //
    // Allocate some space for the mapping table
    //
    BklContext->BklLuxTable = (BKL_LUX_TABLE_ENTRY*) 
        ExAllocatePoolWithTag(
            NonPagedPool,
            sizeof(BKL_LUX_TABLE_ENTRY) * BklContext->BklNumLevels+1,
            TOUCH_POOL_TAG);

    if (NULL == BklContext->BklLuxTable)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not allocate memory for lux table, out of memory");

        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    //
    // Walk the registry values and build the table, failing on error
    // TODO: validate more
    // 
    BklContext->BklLuxTable[0].Min = 0;

    for (i=0; i<BklContext->BklNumLevels; i++)
    {
        status = TchBklGetValueFromCollection(&luxRangeStrings, i, &value);

        if (!NT_SUCCESS(status))
        {
            goto exit;
        }

        BklContext->BklLuxTable[i].Max = value;
        BklContext->BklLuxTable[i+1].Min = value;

        status = TchBklGetValueFromCollection(&intensityStrings, i, &value);

        if (!NT_SUCCESS(status))
        {
            goto exit;
        }

        BklContext->BklLuxTable[i].Intensity = value;
    }

exit:

    if (luxRangeStrings != NULL)
    {
        WdfObjectDelete(luxRangeStrings);
    }
 
    if (intensityStrings != NULL);
    {
        WdfObjectDelete(intensityStrings);
    }

    if (key != NULL)
    {
        WdfRegistryClose(key);
    }

    if (!NT_SUCCESS(status))
    {
        if (BklContext->BklLuxTable != NULL)
        {
            ExFreePoolWithTag(
                BklContext->BklLuxTable,
                TOUCH_POOL_TAG);

            BklContext->BklLuxTable = NULL;
        }
    }

    return status;
}

NTSTATUS
TchBklGetRegistrySettings(
    IN BKL_CONTEXT* BklContext
    )
/*++

Routine Description:

    This helper routine reads and parses registry strings to see if any LEDs
    exist which require backlight control. If LEDs exist their indices are
    read and stored so they can be specified correctly to the HWN driver.

Arguments:

    BklContext - Backlight control context structure
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    DECLARE_CONST_UNICODE_STRING(bklNumLedsValue, BKL_NUM_LEDS);
    DECLARE_CONST_UNICODE_STRING(bklLedIndexListValue, BKL_LED_INDEX_LIST);
    DECLARE_CONST_UNICODE_STRING(bklSettingsPath, BKL_REGISTRY_PATH);
    DECLARE_CONST_UNICODE_STRING(bklTimeoutValue, BKL_INACTIVITY_TIMEOUT);
    ULONG i;
    WDFKEY key;
    WDFCOLLECTION ledIndexStrings;
    NTSTATUS status;
    ULONG value;

    key = NULL;
    ledIndexStrings = NULL;

    status = WdfRegistryOpenKey(
        NULL,
        &bklSettingsPath,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_FLAG_OTHER,
            "Couldn't open registry path for cap button backlights, disabled");

        goto exit;
    }

    status = WdfRegistryQueryULong(
        key,
        &bklNumLedsValue,
        &value);

    if (!NT_SUCCESS(status) || value == 0)
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_FLAG_OTHER,
            "Zero backlight LED count, backlighting disabled");

        goto exit;
    }

    BklContext->HwnNumLeds = value;

    status = WdfRegistryQueryULong(
       key,
       &bklTimeoutValue,
       &value);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_INFORMATION,
            TRACE_FLAG_OTHER,
            "No backlight inactivity timeout specified");

        value = 0;
    }

    BklContext->Timeout = value;

    status = WdfCollectionCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        &ledIndexStrings);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Couldn't allocate a collection for led index list - STATUS:%X",
            status);

        goto exit;
    }

    status = WdfRegistryQueryMultiString(
        key,
        &bklLedIndexListValue,
        WDF_NO_OBJECT_ATTRIBUTES,
        ledIndexStrings);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Couldn't retrieve led index list from registry - STATUS:%X",
            status);

        goto exit;
    }
     
    if (WdfCollectionGetCount(ledIndexStrings) < 
        BklContext->HwnNumLeds)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Only %d LED indices listed in registry, expected %d",
            WdfCollectionGetCount(ledIndexStrings),
            BklContext->HwnNumLeds);
  
        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    BklContext->HwnLedIndexList = (PULONG) ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(ULONG) * BklContext->HwnNumLeds,
        TOUCH_POOL_TAG);

    if (NULL == BklContext->HwnLedIndexList)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not allocate LED index list, out of memory");

        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    //
    // Build the LED index table
    //
    for (i=0; i<BklContext->HwnNumLeds; i++)
    {
        status = TchBklGetValueFromCollection(&ledIndexStrings, i, &value);

        if (!NT_SUCCESS(status))
        {
            goto exit;
        }

        BklContext->HwnLedIndexList[i] = value;   
    }

exit:

    if (ledIndexStrings != NULL)
    {
        WdfObjectDelete(ledIndexStrings);
    }

    if (key != NULL)
    {
        WdfRegistryClose(key);
    }

    if (!NT_SUCCESS(status))
    {
        if (BklContext->HwnLedIndexList != NULL)
        {
            ExFreePoolWithTag(
                BklContext->HwnLedIndexList, 
                TOUCH_POOL_TAG);

            BklContext->HwnLedIndexList = NULL;
            BklContext->HwnNumLeds = 0;
        }
    }

    return status;
}

ULONG
TchBklGetIntensity(
    IN BKL_CONTEXT* BklContext,
    IN ULONG LuxValue
    )
/*++

Routine Description:

    This helper routine takes the current light sensor reading and 
    looks up the corresponding intensity in the lux table.

Arguments:

    BklContext - backlight control context structure
    LuxValue - current lux value
   
Return Value:

    ULONG representing the percentage of backlight intensity to set

--*/
{
    ULONG i;

    for (i=0; i<BklContext->BklNumLevels; i++)
    {
        if (LuxValue <  BklContext->BklLuxTable[i].Max && 
            LuxValue >= BklContext->BklLuxTable[i].Min)
        {
            return BklContext->BklLuxTable[i].Intensity;
        }
    }

    return 0;
}

VOID
TchBklSetIntensity(
    BKL_CONTEXT* BklContext,
    ULONG Intensity
    )
/*++

Routine Description:

    This helper routine sets any capacitive key backlights to the specified
    intensity, represented as 0-100%, where 0% corresponds to OFF.

Arguments:

    BklContext - backlight control context structure
    Intensity - desired intensity from 0-100 (representing percentage)
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    ULONG i;
    WDF_MEMORY_DESCRIPTOR memory;
    NTSTATUS status;

    if (BklContext->CurrentBklIntensity == Intensity)
    {
        return;
    }

    for (i=0; i<BklContext->HwnNumLeds; i++)
    {
        BklContext->HwnConfiguration->HwNSettingsInfo[i].HwNSettings[HWN_INTENSITY] =
            Intensity;
        BklContext->HwnConfiguration->HwNSettingsInfo[i].OffOnBlink =
            (Intensity == 0) ? HWN_OFF : HWN_ON;
    }

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
        &memory,
        BklContext->HwnConfiguration,
        BklContext->HwnConfigurationSize);

    status = WdfIoTargetSendIoctlSynchronously(
        BklContext->HwnIoTarget,
        NULL,
        IOCTL_HWN_SET_STATE,
        &memory,
        NULL,
        NULL,
        NULL);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Failed to set new HWN state intensity: %u- STATUS:%X",//ST//
            Intensity,
            status);
    }

    BklContext->CurrentBklIntensity = Intensity;
}

VOID
TchBklGetLightSensorValue(
    IN WDFWORKITEM WorkItem
    )
/*++

Routine Description:

    This work item polls the ambient light sensor for new readings, which
    are used to dim/fade capacitive key backlights to a level appropriate
    for the users eyes.

Arguments:

    WorkItem - WDFWORKITEM object
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    BKL_CONTEXT* context;
    ULONG intensity;
    WDF_MEMORY_DESCRIPTOR memory;
    NTSTATUS status;
    WORKITEM_CONTEXT* workItemContext;

    workItemContext = GetTouchBacklightContext(WorkItem);
    context = workItemContext->BklContext;

    //
    // Get an ALS sample. Note the read will be held for the sampling interval
    // that we've configured, so it is not necessary to sleep/wait here. 
    //
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
        &memory,
        &context->AlsData,
        sizeof(ALS_DATA));

    while (context->TchBklPollAls == TRUE)
    {
        status = WdfIoTargetSendReadSynchronously(
            context->AlsIoTarget,
            NULL,
            &memory,
            NULL,
            NULL,
            NULL);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_OTHER,
                "Als driver reported error getting data - STATUS:%X",
                status);

            context->AlsStatus = status;
            TchBklSleepMillisec(BKL_ALS_SAMPLING_INTERVAL);

            continue;
        }

#ifdef ALS_BACKLIGHT_DEBUG
        Trace(
            TRACE_LEVEL_INFORMATION,
            TRACE_FLAG_OTHER,
            "ALS reading of %d lux",
            context->AlsData.Sample);
#endif


        //
        // Did we turn off the backlights while waiting?
        //
        if (context->TchBklPollAls == FALSE)
        {
            return;
        }

        //
        // Do we have a timeout enabled, which has expired?
        //
        if (context->Timeout != 0 &&
            GetTickCount() - context->LastInputTime > context->Timeout)
        {
            //
            // Turn off backlights
            //
            WdfWaitLockAcquire(context->BacklightLock, NULL);

            if (!NT_SUCCESS(TchBklEnable(context, FALSE)))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    TRACE_FLAG_OTHER,
                    "Error disabling backlights, may be stuck on!");

                NT_ASSERT(FALSE);
            }

            WdfWaitLockRelease(context->BacklightLock);
            continue;
        }

        //
        // Initiate an intensity change if necessary
        //
        intensity = TchBklGetIntensity(context, context->AlsData.Sample);
        TchBklSetIntensity(context, intensity);
    }
}

NTSTATUS
TchBklEnable(
    IN BKL_CONTEXT* BklContext,
    IN BOOLEAN Enable
    )
/*++

Routine Description:

    Starts or stops ambient light-sensor controlled capacitive key
    backlights.

Arguments:

    BklContext - Backlight control context
    Enabled - Whether to enable or disable ALS-controlled cap key backlight
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    WDF_MEMORY_DESCRIPTOR memory;
    NTSTATUS status;

    status = STATUS_SUCCESS;

    //
    // Are our drivers ready? If not do nothing yet.
    //
    if (BklContext->HwnReady == FALSE || 
        BklContext->AlsReady == FALSE)
    {
        goto exit;
    }

    if (Enable == TRUE)
    {
        //
        // Reset last input timeout if one is enabled
        //
        if (BklContext->Timeout != 0)
        {
            BklContext->LastInputTime = (ULONG) GetTickCount();
        }

        //
        // Start ALS if not already started
        //
        if (BklContext->TchBklPollAls == TRUE)
        {
            goto exit;
        }
        
        RtlZeroMemory(
            &BklContext->AlsConfiguration,
            sizeof(SENSOR_NOTIFICATION));

        BklContext->AlsConfiguration.Size = sizeof(SENSOR_NOTIFICATION);
        BklContext->AlsConfiguration.Flags = 0;
        BklContext->AlsConfiguration.IntervalUs = BKL_ALS_SAMPLING_INTERVAL;

        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
            &memory,
            &BklContext->AlsConfiguration,
            sizeof(SENSOR_NOTIFICATION));

        // 
        // Configure ALS sampling interval
        //
        status = WdfIoTargetSendIoctlSynchronously(
            BklContext->AlsIoTarget,
            NULL,
            IOCTL_SENSOR_CLX_NOTIFICATION_CONFIGURE,
            &memory,
            NULL,
            NULL,
            NULL);
          
        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_OTHER,
                "Could not configure ALS notifications - STATUS:%X",
                status);

            BklContext->AlsStatus = status;

            goto exit;
        }

        //
        // Start the ALS sensor
        //
        status = WdfIoTargetSendIoctlSynchronously(
            BklContext->AlsIoTarget,
            NULL,
            IOCTL_SENSOR_CLX_NOTIFICATION_START,
            NULL,
            NULL,
            NULL,
            NULL);
          
        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_OTHER,
                "Could not start ALS notification streaming - STATUS:%X",
                status);

            BklContext->AlsStatus = status;

            goto exit;
        }

        //
        // Request all LEDs to fade to ON to an initial value, ALS
        // readings will adjust the intensity afterwards.
        //
        TchBklSetIntensity(BklContext, BKL_DEFAULT_INTENSITY);

        //
        // Kick off a work item to continuously monitor ambient light 
        // changes and adjust the backlight intensity accordingly
        //
        BklContext->TchBklPollAls = TRUE;


        WdfWorkItemEnqueue(BklContext->TchBklPollAlsWorkItem);
    }
    else
    {
        //
        // Stop the ALS sensor
        //
        BklContext->TchBklPollAls = FALSE;

        status = WdfIoTargetSendIoctlSynchronously(
            BklContext->AlsIoTarget,
            NULL,
            IOCTL_SENSOR_CLX_NOTIFICATION_STOP,
            NULL,
            NULL,
            NULL,
            NULL);
          
        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_OTHER,
                "Could not start ALS notification streaming - STATUS:%X",
                status);

            BklContext->AlsStatus = status;
        }

        //
        // Request all LEDs to fade to OFF state. 
        //
        TchBklSetIntensity(BklContext, 0);
    }

exit:

    return status;
}

NTSTATUS
TchBklOpenAlsDriver(
    IN BKL_CONTEXT* BklContext,
    IN PUNICODE_STRING AlsSymbolicLinkName
    )
/*++

Routine Description:

    Opens ALS driver to control capacitive button backlighting

Arguments:

    BklContext - Touch controller driver context
    AlsSymbolicLinkName - Name to use to access ALS driver
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    NTSTATUS status;

    status = STATUS_SUCCESS;

    //
    // We need to guard against creating multiple IO target handles to ALS.
    // From MSDN: if the caller specifies 
    // PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES, the system 
    // may call the notification callback routine twice for a single 
    // EventCategoryDeviceInterfaceChange event for an existing interface. 
    // You can safely ignore the second call to the callback.
    //
    // Also, we don't need to worry about a race condition if AlsReady 
    // hasn't been set to TRUE yet when a second arrival notification comes 
    // in on another thread since we're holding up the PnP manager while
    // executing this callback
    //
    if (BklContext->AlsReady == FALSE)
    {
        NT_ASSERT(BklContext->AlsIoTarget == NULL);

        //
        // Create a WDFIOTARGET object
        //
        status = WdfIoTargetCreate(
            BklContext->FxDevice,
            WDF_NO_OBJECT_ATTRIBUTES,
            &BklContext->AlsIoTarget);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_OTHER,
                "Error: Could not create WDFIOTARGET object - STATUS:%X",
                status);

            goto exit;
        }

        //
        // Open a handle to the HWN driver
        //
        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
            &openParams,
            AlsSymbolicLinkName,
            GENERIC_READ | GENERIC_WRITE);

        openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
            
        status = WdfIoTargetOpen(BklContext->AlsIoTarget, &openParams);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_OTHER,
                "Error: Could not open ALS driver target - STATUS:%X",
                status);

            WdfObjectDelete(BklContext->AlsIoTarget);
            goto exit;
        }

        //
        // Enable the backlight if both ALS and HWN are ready
        //
        WdfWaitLockAcquire(BklContext->BacklightLock, NULL);

        BklContext->AlsReady = TRUE;

        if (BklContext->HwnReady == TRUE &&
            BklContext->AlsReady == TRUE)
        {
            TchBklEnable(BklContext, TRUE);
        }

        WdfWaitLockRelease(BklContext->BacklightLock);
    }

exit:

    return status;
}

NTSTATUS
TchBklCloseAlsDriver(
    IN BKL_CONTEXT* BklContext
    )
/*++

Routine Description:

    Closes ALS driver and stops control of capacitive button backlighting

Arguments:

    BklContext - Backlight control context

Return Value:

    NTSTATUS indicating success or failure

*/
{
    if (BklContext->AlsReady != FALSE)
    {
        NT_ASSERT(BklContext->AlsIoTarget != NULL);

        WdfWaitLockAcquire(BklContext->BacklightLock, NULL);

        //
        // Turn off the backlights to indicate ALS was torn down
        //
        TchBklEnable(BklContext, FALSE);
        BklContext->AlsReady = FALSE;

        WdfWaitLockRelease(BklContext->BacklightLock);

        //
        // Deleting the object will close the I/O target if it's not already 
        // invalid.
        //
        WdfObjectDelete(BklContext->AlsIoTarget);
        BklContext->AlsIoTarget = NULL;
    }

    return STATUS_SUCCESS;    
}

NTSTATUS
TchBklOpenHwnDriver(
    IN BKL_CONTEXT* BklContext,
    IN PUNICODE_STRING HwnSymbolicLinkName
    )
/*++

Routine Description:

    Opens HWN driver to control capacitive button backlighting

Arguments:

    BklContext - Backlight control context
    HwnSymbolicLinkName - Name to use to access HWN LED driver
   
Return Value:

    NTSTATUS indicating success or failure

--*/
{
    ULONG i;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    NTSTATUS status;

    //
    // Create a WDFIOTARGET object
    //
    status = WdfIoTargetCreate(
        BklContext->FxDevice,
        WDF_NO_OBJECT_ATTRIBUTES,
        &BklContext->HwnIoTarget);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Error: Could not create WDFIOTARGET object - STATUS:%X",
            status);

        goto exit;
    }

    //
    // Open a handle to the HWN driver
    //
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
        &openParams,
        HwnSymbolicLinkName,
        GENERIC_READ | GENERIC_WRITE);

    openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
        
    status = WdfIoTargetOpen(BklContext->HwnIoTarget, &openParams);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Error: Could not open HWN driver target - STATUS:%X",
            status);

        WdfObjectDelete(BklContext->HwnIoTarget);
        goto exit;
    }

    //
    // Initialize HWN_HEADER and HWN_SETTINGS, turn on LEDs
    //
    BklContext->HwnConfigurationSize = 
        HWN_HEADER_SIZE + (sizeof(HWN_SETTINGS) * BklContext->HwnNumLeds);

    BklContext->HwnConfiguration = ExAllocatePoolWithTag(
        NonPagedPool,
        BklContext->HwnConfigurationSize,
        TOUCH_POOL_TAG);

    if (BklContext->HwnConfiguration == NULL)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Error: Could not allocate HWN descriptor memory");

        goto exit;
    }

    RtlZeroMemory(
        BklContext->HwnConfiguration, 
        BklContext->HwnConfigurationSize);
    
    BklContext->HwnConfiguration->HwNPayloadSize = 
        BklContext->HwnConfigurationSize;
    BklContext->HwnConfiguration->HwNPayloadVersion = 1;
    BklContext->HwnConfiguration->HwNRequests = 3;

    for (i=0; i<BklContext->HwnNumLeds; i++)
    {
        BklContext->HwnConfiguration->HwNSettingsInfo[i].HwNId = 
            BklContext->HwnLedIndexList[i];
        BklContext->HwnConfiguration->HwNSettingsInfo[i].HwNType = HWN_LED;
        BklContext->HwnConfiguration->HwNSettingsInfo[i].HwNSettings[HWN_INTENSITY] = 100;
    }

    //
    // Enable the backlight if both ALS and HWN are ready
    //
    WdfWaitLockAcquire(BklContext->BacklightLock, NULL);

    BklContext->HwnReady = TRUE;

    if (BklContext->HwnReady == TRUE &&
        BklContext->AlsReady == TRUE)
    {
        TchBklEnable(BklContext, TRUE);
    }

    WdfWaitLockRelease(BklContext->BacklightLock);

exit:

    return status;
}

NTSTATUS
TchBklCloseHwnDriver(
    IN BKL_CONTEXT* BklContext
    )
/*++

Routine Description:

    Closes ALS driver and stops control of capacitive button backlighting

Arguments:

    BklContext - Backlgiht control context

Return Value:

    NTSTATUS indicating success or failure

*/
{
    WdfWaitLockAcquire(BklContext->BacklightLock, NULL);

    //
    // Turn off the backlights to indicate ALS was torn down
    //
    TchBklEnable(BklContext, FALSE);
    BklContext->HwnReady = FALSE;

    WdfWaitLockRelease(BklContext->BacklightLock);

    //
    // Deleting the object will close the I/O target if it's not already 
    // invalid.
    //
    WdfObjectDelete(BklContext->HwnIoTarget);
    BklContext->HwnIoTarget = NULL;

    //
    // Free HWN related pool allocations
    //
    if (NULL != BklContext->HwnConfiguration)
    {
        ExFreePoolWithTag(BklContext->HwnConfiguration, TOUCH_POOL_TAG);
        BklContext->HwnConfiguration = NULL;
    }

    return STATUS_SUCCESS;    
}

NTSTATUS
TchBklOnAlsDeviceReady(
    IN PVOID _DeviceChange,
    IN PVOID _BklContext
    )
/*++

Routine Description:

    This routine is called when the SENSOR_TYPE_AMBIENT_LIGHT is available or
    is going away. We initialize or stop communication with the driver here.

Arguments:

    _DeviceChange - Structure that provides us with a symbolic name used
        to open the Als driver for reading
    _BklContext - Backlight control context

Return Value:

    NTSTATUS indicating success or failure

*/
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION DeviceChange = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION) _DeviceChange;
    BKL_CONTEXT* BklContext = (BKL_CONTEXT*) _BklContext;

    if (IsEqualGUID(&DeviceChange->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
    {
        TchBklOpenAlsDriver(BklContext, DeviceChange->SymbolicLinkName);
    }
    else 
    {
        TchBklCloseAlsDriver(BklContext);
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS
TchBklOnHwnDeviceReady(
    IN PVOID _DeviceChange,
    IN PVOID _BklContext
    )
/*++

Routine Description:

    This routine is called when the HWN_DEVINTERFACE_NLED is available or
    is going away. We initialize or stop communication with the driver here.

Arguments:

    _DeviceChange - Structure that provides us with a symbolic name used
        to open the HWN device for control
    _BklContext - Backlight control context

Return Value:

    NTSTATUS indicating success or failure

*/
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION DeviceChange = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION) _DeviceChange;
    BKL_CONTEXT* BklContext = (BKL_CONTEXT*) _BklContext;
    if (IsEqualGUID(&DeviceChange->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
    {
        TchBklOpenHwnDriver(BklContext, DeviceChange->SymbolicLinkName);
    }
    else 
    {
        TchBklCloseHwnDriver(BklContext);
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS
TchOnMonitorStateChange(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_ PVOID _BklContext
    )
/*++

Routine Description:

    This callback is invoked on monitor state changes. Here we turn on/off
    adaptive capacitive button backlighting based on the monitor state.

Arguments:

    SettingGuid - GUID_MONITOR_POWER_ON (the only notification registered)
    Value - Either MONITOR_IS_ON or MONITOR_IS_OFF
    ValueLength - Ignored, always sizeof(ULONG)
    _BklContext - Device backlight control context    
   
Return Value:

    Ignored, always returns STATUS_SUCCESS

--*/
{
    BOOLEAN enable;
    ULONG monitorState;
    NTSTATUS status;
    BKL_CONTEXT* BklContext = (BKL_CONTEXT*) _BklContext;

    //
    // Should never happen, but we don't care about events unrelated to display
    //
    if (!InlineIsEqualGUID(SettingGuid, &GUID_MONITOR_POWER_ON))
    {
        goto exit;
    }

    //
    // Should never happen, but check for bad parameters for this notification
    //
    if (Value == NULL || ValueLength != sizeof(ULONG) || BklContext == NULL)
    {
        goto exit;
    }

    monitorState = *((PULONG) Value);
    enable = (monitorState == MONITOR_IS_ON) ? TRUE : FALSE;

    WdfWaitLockAcquire(BklContext->BacklightLock, NULL);

    //
    // Illuminate/deluminate the capacitive keys
    //
    status = TchBklEnable(BklContext, enable);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not set capacitive key backlights to %d - STATUS:%X",
            monitorState,
            status);
    }

    WdfWaitLockRelease(BklContext->BacklightLock);

exit:

    return STATUS_SUCCESS;
}

BKL_CONTEXT*
TchBklInitialize(
    IN WDFDEVICE FxDevice
    )
/*++

Routine Description:

    Set-up driver to control capacitive button backlighting

Arguments:

    FxDevice - Framework device object for this device instance
   
Return Value:

    BKL_CONTEXT* where non-null indicates success

--*/
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG config;
    BKL_CONTEXT* context;
    NTSTATUS status;
    WORKITEM_CONTEXT *workItemContext;

    status = STATUS_UNSUCCESSFUL;

    //
    // Allocate capacitive button backlight control context
    //
    context = (BKL_CONTEXT*) ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(BKL_CONTEXT),
        TOUCH_POOL_TAG);

    if (context == NULL)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not allocate BKL_CONTEXT structure");

        goto exit;
    }

    //
    // Initialize backlight context members
    //
    RtlZeroMemory(context, sizeof(BKL_CONTEXT));
    context->FxDevice = FxDevice;
    context->HwnReady = FALSE;
    context->AlsReady = FALSE;
    context->LastInputTime = (ULONG) GetTickCount();

    status = WdfWaitLockCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        &context->BacklightLock);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not create WDFWAITLOCK object - STATUS:%X",
            status);

        goto exit;
    }

    //
    // See if there are any LEDs to enable
    //
    status = TchBklGetRegistrySettings(context);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_FLAG_OTHER,
            "No LEDs found to control - STATUS:%X",
            status);

        goto exit;
    }

    //
    // Allocate a work item which will poll the ALS sensor for light changes
    //
    WDF_WORKITEM_CONFIG_INIT(&config, TchBklGetLightSensorValue);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, WORKITEM_CONTEXT);
    attributes.ParentObject = context->FxDevice;

    status = WdfWorkItemCreate(
        &config,
        &attributes,
        &context->TchBklPollAlsWorkItem);
 
    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not create WDFWORKITEM object - STATUS:%X",
            status);

        goto exit;
    }

    workItemContext = 
        GetTouchBacklightContext(context->TchBklPollAlsWorkItem);
    workItemContext->BklContext = context;

    //
    // Read the Milliux <-> Intensity table from the registry
    //
    status = TchBklGetCustomLuxIntensityMap(context);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_FLAG_OTHER,
            "Warning, no platform-configured lux mapping table in registry - STATUS:%X",
			status);//ST//

        //
        // And grab the default table if it's not specified or specified wrong
        //
        TchBklGetDefaultLuxIntensityMap(context);
    }

    //
    // Check for the HWN driver to become available (if not already)
    //
    status = IoRegisterPlugPlayNotification(
        EventCategoryDeviceInterfaceChange,
        PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
        (PVOID) &HWN_DEVINTERFACE_NLED,
        WdfDriverWdmGetDriverObject(WdfGetDriver()),
        (PDRIVER_NOTIFICATION_CALLBACK_ROUTINE) TchBklOnHwnDeviceReady,
        context,
        &context->HwnPnpNotificationEntry);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not register for HWN driver interface arrival - STATUS:%X",
            status);

        goto exit;
    }

    //
    // Check for the ALS driver to become available (if not already)
    //
    status = IoRegisterPlugPlayNotification(
        EventCategoryDeviceInterfaceChange,
        PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
        (PVOID) &SENSOR_TYPE_AMBIENT_LIGHT,
        WdfDriverWdmGetDriverObject(WdfGetDriver()),
        (PDRIVER_NOTIFICATION_CALLBACK_ROUTINE) TchBklOnAlsDeviceReady,
        context,
        &context->AlsPnpNotificationEntry);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not register for ALS driver interface arrival - STATUS:%X",
            status);

        goto exit;
    }

    //
    // Register for monitor state changes (used to illuminate and deluminate
    // the capacitive buttons). As D-state changes may not coincide with 
    // monitor state changes.
    //
    status = PoRegisterPowerSettingCallback(
        WdfDeviceWdmGetDeviceObject(FxDevice),
        &GUID_MONITOR_POWER_ON,
        TchOnMonitorStateChange,
        (PVOID) context,
        &context->MonitorChangeNotificationHandle);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_OTHER,
            "Could not register for monitor state changes - STATUS:%X",
            status);

        goto exit;
    }
        

exit:

    if (!NT_SUCCESS(status))
    {
        if (NULL != context)
        {
            TchBklDeinitialize(context);
            context = NULL;
        }
    }

    return context;
}

VOID
TchBklDeinitialize(
    IN BKL_CONTEXT* BklContext
    )
/*++

Routine Description:

    Tears down control of capacitive button backlighting

Arguments:

    BklContext - Backlight control context
   
Return Value:

    None

--*/
{
    //
    // Disable timeout if enabled to prevent TchBklNotifyTouchActivity
    // from accessing context
    //
    BklContext->Timeout = 0;

    //
    // Deregister for monitor state notifications
    //
    if (BklContext->MonitorChangeNotificationHandle != NULL)
    {
        PoUnregisterPowerSettingCallback(
            BklContext->MonitorChangeNotificationHandle);
        BklContext->MonitorChangeNotificationHandle = NULL;
    }

    //
    // Cut the lights
    //
    TchBklEnable(BklContext, FALSE);

    //
    // Unsubscribe from Pnp notifications for ALS
    //
    if (BklContext->AlsPnpNotificationEntry != NULL)
    {
        IoUnregisterPlugPlayNotificationEx(
            BklContext->AlsPnpNotificationEntry);
        BklContext->AlsPnpNotificationEntry = NULL;
    }

    //
    // Close ALS driver if it was open
    //
    if (BklContext->AlsReady == TRUE)
    {
        TchBklCloseAlsDriver(BklContext);
    }

    //
    // Unsubscribe from Pnp notifications for HWN
    //
    if (BklContext->HwnPnpNotificationEntry != NULL)
    {
        IoUnregisterPlugPlayNotificationEx(
            BklContext->HwnPnpNotificationEntry);
        BklContext->HwnPnpNotificationEntry = NULL;
    }

    //
    // Close HWN driver if it was open
    //
    if (BklContext->HwnReady == TRUE)
    {
        TchBklCloseHwnDriver(BklContext);
    }

    //
    // Deallocate Lux table if allocated
    //
    if (BklContext->BklLuxTable != NULL)
    {
        ExFreePoolWithTag(BklContext->BklLuxTable, TOUCH_POOL_TAG);
        BklContext->BklLuxTable = NULL;
    }

    //
    // Deallocate LED list if allocated
    //
    if (BklContext->HwnLedIndexList != NULL)
    {
        ExFreePoolWithTag(BklContext->HwnLedIndexList, TOUCH_POOL_TAG);
        BklContext->HwnLedIndexList = NULL;
    }

    //
    // Free the wait lock object
    //
    if (BklContext->BacklightLock != NULL)
    {
        WdfObjectDelete(BklContext->BacklightLock);
        BklContext->BacklightLock = NULL;
    }

    //
    // Free context
    //
    ExFreePoolWithTag(BklContext, TOUCH_POOL_TAG);
}

VOID
TchBklNotifyTouchActivity(
    IN BKL_CONTEXT* BklContext,
    IN DWORD Time
    )
/*++

Routine Description:

    Re-enables the backlights if they timed out and the user touched the screen

Arguments:

    BklContext - Backlight control context
    Time - Time of user input (touch or button)
   
Return Value:

    None.

--*/
{
    NTSTATUS status;

    //
    // If no backlights are controlled or no timeout is specified, ignore
    //
    if ((BklContext == NULL) || (BklContext->Timeout == 0))
    {
        return;
    }

    BklContext->LastInputTime = Time;

    //
    // If ALS monitoring is disabled, re-enable it
    //
    if (BklContext->TchBklPollAls == FALSE)
    {
        WdfWaitLockAcquire(BklContext->BacklightLock, NULL);
        
        status = TchBklEnable(BklContext, TRUE);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_OTHER,
                "Error enabling backlights, may be stuck off!");

            NT_ASSERT(FALSE);
        }

        WdfWaitLockRelease(BklContext->BacklightLock);
    }
}
