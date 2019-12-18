#pragma once

#include "rmiinternal.h"

typedef enum _REPORTED_BUTTON
{
	BUTTON_NONE = 0,
	BUTTON_BACK,
	BUTTON_START,
	BUTTON_SEARCH,
	BUTTON_UNKNOWN
} REPORTED_BUTTON;

NTSTATUS
RmiServiceCapacitiveButtonInterrupt(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext,
	IN PHID_INPUT_REPORT HidReport,
	OUT BOOLEAN* PendingTouches
);

REPORTED_BUTTON
TchHandleButtonArea(
	IN ULONG ControllerX,
	IN ULONG ControllerY,
	IN PTOUCH_SCREEN_PROPERTIES Props
);