
#include "rmiinternal.h"

#pragma once

NTSTATUS
RmiConfigureFunction01(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
);

VOID
RmiConvertF01ToPhysical(
	IN RMI4_F01_CTRL_REGISTERS_LOGICAL* Logical,
	IN RMI4_F01_CTRL_REGISTERS* Physical
);