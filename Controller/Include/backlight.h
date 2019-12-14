/*++
    Copyright (c) Microsoft Corporation. All Rights Reserved. 
    Sample code. Dealpoint ID #843729.

    Module Name:

        backlight.h

    Abstract:

        Contains capacitive backlight control defines and types

    Environment:

        Kernel mode

    Revision History:


--*/

#pragma once

#include <hwn.h>
#include "winphoneabi.h"

#define BKL_REGISTRY_PATH          L"\\Registry\\Machine\\SYSTEM\\TOUCH\\BUTTONS\\BACKLIGHT"

#define BKL_NUM_LEDS               L"LedCount"
#define BKL_LED_INDEX_LIST         L"LedIndexList"
#define BKL_LUX_TABLE_RANGES       L"MilliLuxRanges"
#define BKL_LUX_TABLE_INTENSITIES  L"IntensityMappings"
#define BKL_INACTIVITY_TIMEOUT     L"InactivityTimeout"

#define BKL_NUM_LEVELS_DEFAULT     4
#define BKL_DEFAULT_INTENSITY      5        // percent
#define BKL_ALS_SAMPLING_INTERVAL  5000000  // usec

#define HUNDRED_NS_PER_MS 10000
#define GetTickCount() (KeQueryInterruptTime() / HUNDRED_NS_PER_MS)

#define MONITOR_IS_OFF 0
#define MONITOR_IS_ON  1

typedef struct _BKL_LUX_TABLE_ENTRY
{
    ULONG Min;
    ULONG Max;
    ULONG Intensity;
} BKL_LUX_TABLE_ENTRY;

typedef struct _SENSOR_NOTIFICATION
{
    ULONG Size;
    ULONG Flags;
    ULONG IntervalUs;
    ULONG ThreshMinSize;
    ULONG ThreshMaxSize;
    ULONG ThreshInfoSize;
} SENSOR_NOTIFICATION, *PSENSOR_NOTIFICATION;

#define IOCTL_SENSOR_CLX_NOTIFICATION_CONFIGURE   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SENSOR_CLX_NOTIFICATION_START       \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SENSOR_CLX_NOTIFICATION_STOP        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _BKL_CONTEXT
{
    WDFDEVICE FxDevice;

    WDFIOTARGET HwnIoTarget;
    PVOID HwnPnpNotificationEntry;
    BOOLEAN HwnReady;
    HWN_HEADER* HwnConfiguration;
    size_t HwnConfigurationSize;
    ULONG HwnNumLeds;
    PULONG HwnLedIndexList;

    WDFIOTARGET AlsIoTarget;
    PVOID AlsPnpNotificationEntry;
    BOOLEAN AlsReady;
    SENSOR_NOTIFICATION AlsConfiguration;
    ALS_DATA AlsData;
    NTSTATUS AlsStatus; 

    WDFWAITLOCK BacklightLock;
    WDFWORKITEM TchBklPollAlsWorkItem;
    BOOLEAN TchBklPollAls;
    ULONG CurrentBklIntensity;

    ULONG BklNumLevels;
    BKL_LUX_TABLE_ENTRY* BklLuxTable;

    ULONG Timeout;
    ULONG LastInputTime;

    PVOID MonitorChangeNotificationHandle;
} BKL_CONTEXT;

typedef struct _WORKITEM_CONTEXT
{
    BKL_CONTEXT* BklContext;
} WORKITEM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORKITEM_CONTEXT, GetTouchBacklightContext);

VOID
TchBklDeinitialize(
    IN BKL_CONTEXT* BklContext
    );

NTSTATUS
TchBklEnable(
    IN BKL_CONTEXT* BklContext,
    IN BOOLEAN Enable
    );

BKL_CONTEXT*
TchBklInitialize(
    IN WDFDEVICE FxDevice
    );

VOID
TchBklNotifyTouchActivity(
    IN BKL_CONTEXT* BklContext,
    IN DWORD Time
    );

EVT_WDF_WORKITEM TchBklGetLightSensorValue;
    
DRIVER_NOTIFICATION_CALLBACK_ROUTINE TchBklOnAlsDeviceReady;

DRIVER_NOTIFICATION_CALLBACK_ROUTINE TchBklOnHwnDeviceReady;

POWER_SETTING_CALLBACK TchOnMonitorStateChange;
