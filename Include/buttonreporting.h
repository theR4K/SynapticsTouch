#pragma once

#include "rmiinternal.h"

typedef enum _REPORTED_BUTTON
{
	BUTTON_NONE = 0,
    BUTTON_SEARCH,
	BUTTON_START,
	BUTTON_BACK,
	BUTTON_UNKNOWN
} REPORTED_BUTTON;

NTSTATUS
RmiServiceCapacitiveButtonInterrupt(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext,
	IN BOOLEAN ReversedKeys
);

REPORTED_BUTTON
TchHandleButtonArea(
	IN ULONG ControllerX,
	IN ULONG ControllerY,
	IN PTOUCH_SCREEN_PROPERTIES Props
);

NTSTATUS
FillButtonsReportFromCache(
    IN RMI4_CONTROLLER_CONTEXT* ControllerContext
);
