/*++
	Copyright (c) Microsoft Corporation. All Rights Reserved.
	Sample code. Dealpoint ID #843729.

	Module Name:

		rmiinternal.h

	Abstract:

		Contains common types and defintions used internally
		by the multi touch screen driver.

	Environment:

		Kernel mode

	Revision History:

--*/

#pragma once

#include <wdm.h>
#include <wdf.h>
#include "controller.h"
#include "resolutions.h"
#include "backlight.h"

#include "F01.h"
#include "F11.h"
#include "F12.h"
#include "F1A.h"

//
// Defines from Synaptics RMI4 Data Sheet, please refer to
// the spec for details about the fields and values.
//
#define RMI4_MAX_TOUCHES                  10

#define RMI4_FIRST_FUNCTION_ADDRESS       0xE9
#define RMI4_PAGE_SELECT_ADDRESS          0xFF

#define RMI4_F34_FLASH_MEMORY_MANAGEMENT  0x34
#define RMI4_F54_TEST_REPORTING           0x54

#define RMI4_MAX_FUNCTIONS                10

#define LOGICAL_TO_PHYSICAL(LOGICAL_VALUE) ((LOGICAL_VALUE) & 0xff)

typedef struct _RMI4_FUNCTION_DESCRIPTOR
{
	BYTE QueryBase;
	BYTE CommandBase;
	BYTE ControlBase;
	BYTE DataBase;
	union
	{
		BYTE All;
		struct
		{
			BYTE IrqCount : 3;
			BYTE Reserved0 : 2;
			BYTE FuncVer : 2;
			BYTE Reserved1 : 1;
		};
	} VersionIrq;
	BYTE Number;
} RMI4_FUNCTION_DESCRIPTOR;

#define RMI4_MILLISECONDS_TO_TENTH_MILLISECONDS(n) n/10
#define RMI4_SECONDS_TO_HALF_SECONDS(n) 2*n

#define RMI4_INTERRUPT_BIT_2D_TOUCH               0x04
#define RMI4_INTERRUPT_BIT_0D_CAP_BUTTON          0x10
#define RMI4_INTERRUPT_BIT_0D_CAP_BUTTON_REVERSED 0x20

#define TOUCH_POOL_TAG_F12              (ULONG)'21oT'

//
// Driver structures
//

typedef struct _RMI4_CONFIGURATION
{
	RMI4_F01_CTRL_REGISTERS_LOGICAL DeviceSettings;
	RMI4_F11_CTRL_REGISTERS_LOGICAL TouchSettings;
	UINT32 PepRemovesVoltageInD3;
} RMI4_CONFIGURATION;

typedef struct _RMI4_FINGER_INFO
{
	int x;
	int y;
	UCHAR fingerStatus;
} RMI4_FINGER_INFO;

typedef struct _RMI4_FINGER_CACHE
{
	RMI4_FINGER_INFO FingerSlot[RMI4_MAX_TOUCHES];
	UINT32 FingerSlotValid;
	UINT32 FingerSlotDirty;
	int FingerDownOrder[RMI4_MAX_TOUCHES];
	int FingerDownCount;
	ULONG64 ScanTime;
} RMI4_FINGER_CACHE;

typedef struct _RMI4_CONTROLLER_CONTEXT
{
	WDFDEVICE FxDevice;
	WDFWAITLOCK ControllerLock;

	//
	// Controller state
	//
	int FunctionCount;
	RMI4_FUNCTION_DESCRIPTOR Descriptors[RMI4_MAX_FUNCTIONS];
	int FunctionOnPage[RMI4_MAX_FUNCTIONS];
	int CurrentPage;

	ULONG InterruptStatus;

	BOOLEAN HasButtons;
	BOOLEAN ResetOccurred;
	BOOLEAN InvalidConfiguration;
	BOOLEAN DeviceFailure;
	BOOLEAN UnknownStatus;
	BOOLEAN IsF12Digitizer;

	BYTE UnknownStatusMessage;

	RMI4_F01_QUERY_REGISTERS F01QueryRegisters;

	//
	// Power state
	//
	DEVICE_POWER_STATE DevicePowerState;

	//
	// Register configuration programmed to chip
	//
	TOUCH_SCREEN_PROPERTIES Props;
	RMI4_CONFIGURATION Config;

	//
	// Current touch state
	//
	int TouchesReported;
	int TouchesTotal;
	RMI4_FINGER_CACHE FingerCache;

	//
	// Backlight keys
	//
	BKL_CONTEXT* BklContext;

	//
	// RMI4 F12 state
	//

	/*
		!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	
		DO NOT remove these. Even if the variables are not
		used by any function down the line, these have an effect
		on memory allocation. If you remove these and make them
		local to a function, you will BREAK F12 and register
		readings.

		!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	*/

	RMI_REGISTER_DESCRIPTOR QueryRegDesc;
	RMI_REGISTER_DESCRIPTOR ControlRegDesc;
	RMI_REGISTER_DESCRIPTOR DataRegDesc;
	size_t PacketSize;

	USHORT Data1Offset;
	BYTE MaxFingers;

	//
	// Current button state
	//
	RMI4_F1A_CACHE ButtonsCache;

} RMI4_CONTROLLER_CONTEXT;

NTSTATUS
RmiCheckInterrupts(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext,
	IN ULONG* InterruptStatus
);

int
RmiGetFunctionIndex(
	IN RMI4_FUNCTION_DESCRIPTOR* FunctionDescriptors,
	IN int FunctionCount,
	IN int FunctionDesired
);

NTSTATUS
RmiChangePage(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext,
	IN int DesiredPage
);

NTSTATUS
RmiGetTouchesFromController(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
);

UINT8 RmiGetRegisterIndex(
	PRMI_REGISTER_DESCRIPTOR Rdesc,
	USHORT reg
);