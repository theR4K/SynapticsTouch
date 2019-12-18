
#include "rmiinternal.h"

#pragma once

NTSTATUS
GetTouchesFromF11(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
);

VOID
UpdateLocalFingerCacheF11(
	IN ULONG FingerStatusRegister,
	IN RMI4_F11_DATA_POSITION* FingerPosRegisters,
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext
);

NTSTATUS
RmiConfigureFunction11(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
);

VOID
RmiConvertF11ToPhysical(
	IN RMI4_F11_CTRL_REGISTERS_LOGICAL* Logical,
	IN RMI4_F11_CTRL_REGISTERS* Physical
);