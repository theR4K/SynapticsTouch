#include "debug.h"
#include "Function1A.h"

NTSTATUS
RmiConfigureFunction1A(
	IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
	IN SPB_CONTEXT* SpbContext
)
{
	UNREFERENCED_PARAMETER(SpbContext);

	int index;
	//
	// Find 0D capacitive button sensor function and configure it if it exists
	//
	index = RmiGetFunctionIndex(
		ControllerContext->Descriptors,
		ControllerContext->FunctionCount,
		RMI4_F1A_0D_CAP_BUTTON_SENSOR);

	if (index != ControllerContext->FunctionCount)
	{
		// TODO: Get configuration data from registry once Synaptics
		//       provides sane default values. Until then, assume the
		//       object is configured for the desired product scenario
		//       by default.
		//

        ControllerContext->ButtonsReverced =
            ControllerContext->InterruptCapButtonsMask & RMI4_INTERRUPT_BIT_0D_CAP_BUTTON_REVERSED;

	}

	return 0;
}