
#include "rmiinternal.h"

#pragma once

NTSTATUS
GetTouchesFromF12(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
);

VOID
UpdateLocalFingerCacheF12(
	IN ULONG FingerStatusRegister,
	IN RMI4_F12_DATA_POSITION* FingerPosRegisters,
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext
);

NTSTATUS
RmiConfigureFunction12(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
);

NTSTATUS
RmiSetReportingMode(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext,
	IN UCHAR NewMode,
	OUT UCHAR* OldMode,
	IN PRMI_REGISTER_DESCRIPTOR ControlRegDesc
);

NTSTATUS
GetTouchesFromF12(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
);