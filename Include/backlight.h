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
#include <wdf.h>
#include <wdm.h>

#define BKL_REGISTRY_PATH            L"\\Registry\\Machine\\SYSTEM\\TOUCH\\BUTTONS\\BACKLIGHT"

#define BKL_NUM_LEDS                 L"LedCount"
#define BKL_LED_INDEX_LIST           L"LedIndexList"
#define BKL_LUX_TABLE_RANGES         L"MilliLuxRanges"
#define BKL_LUX_TABLE_INTENSITIES    L"IntensityMappings"
#define BKL_LUX_TABLE_INTENSITIES_0  L"IntensityMappings0"
#define BKL_LUX_TABLE_INTENSITIES_1  L"IntensityMappings1"
#define BKL_LUX_TABLE_INTENSITIES_2  L"IntensityMappings2"
#define BKL_INACTIVITY_TIMEOUT       L"InactivityTimeout"

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


	WDFWAITLOCK BacklightLock;
	ULONG CurrentBklIntensity;

	ULONG BklNumLevels;
	BKL_LUX_TABLE_ENTRY* BklLuxTable;

	PVOID MonitorChangeNotificationHandle;
} BKL_CONTEXT;

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

DRIVER_NOTIFICATION_CALLBACK_ROUTINE TchBklOnAlsDeviceReady;

DRIVER_NOTIFICATION_CALLBACK_ROUTINE TchBklOnHwnDeviceReady;

POWER_SETTING_CALLBACK TchOnMonitorStateChange;
