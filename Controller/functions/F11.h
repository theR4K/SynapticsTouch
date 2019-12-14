#pragma once

#include <wdf.h>
#include <wdm.h>
#include "spb.h"

//
// Function $11 - 2-D Touch Sensor
//

#define RMI4_FINGER_STATE_NOT_PRESENT                  0
#define RMI4_FINGER_STATE_PRESENT_WITH_ACCURATE_POS    1
#define RMI4_FINGER_STATE_PRESENT_WITH_INACCURATE_POS  2
#define RMI4_FINGER_STATE_RESERVED                     3

#define RMI4_F11_DEVICE_CONTROL_SLEEP_MODE_OPERATING   0
#define RMI4_F11_DEVICE_CONTROL_SLEEP_MODE_SLEEPING    1

typedef struct _RMI4_F11_QUERY0_REGISTERS
{
	BYTE NumberOfSensors : 3;
	BYTE Reserved0 : 1;
	BYTE HasQuery11 : 1;
	BYTE Reserved1 : 3;
} RMI4_F11_QUERY0_REGISTERS;

typedef struct _RMI4_F11_QUERY1_REGISTERS
{
	struct
	{
		BYTE NumberOfFingers : 3;
		BYTE HasRelative : 1;
		BYTE HasAbsolute : 1;
		BYTE HasGestures : 1;
		BYTE HasSensitivity : 1;
		BYTE Configurable : 1;
	};
	BYTE NumXElectrodes;
	BYTE NumYElectrodes;
	BYTE MaxElectrodes;
	struct
	{
		BYTE AbsDataSize : 2;
		BYTE HasAnchoredFin : 1;
		BYTE HasAdjHyst : 1;
		BYTE HasDribble : 1;
		BYTE Reserved2 : 3;
	};
	struct
	{
		BYTE HasZTuning : 1;
		BYTE HasAlgoSelect : 1;
		BYTE HasWTuning : 1;
		BYTE HasPitchInfo : 1;
		BYTE HasFingerSize : 1;
		BYTE HasObjSensAdj : 1;
		BYTE HasXYClip : 1;
		BYTE HasDrummingAdj : 1;
	};
}RMI4_F11_QUERY1_REGISTERS;

typedef struct _RMI4_F11_CTRL_REGISTERS
{
	struct
	{
		BYTE ReportingMode : 3;
		BYTE AbsPosFilt    : 1;
		BYTE RelPosFilt    : 1;
		BYTE RelBallistics : 1;
		BYTE Dribble       : 1;
		BYTE Reserved0     : 1;
	};
	struct
	{
		BYTE PalmDetectThreshold : 4;
		BYTE MotionSensitivity   : 2;
		BYTE ManTrackEn          : 1;
		BYTE ManTrackedFinger    : 1;
	};
	BYTE DeltaXPosThreshold;
	BYTE DeltaYPosThreshold;
	BYTE Velocity;
	BYTE Acceleration;
	BYTE SensorMaxXPosLo;
	struct
	{
		BYTE SensorMaxXPosHi : 4;
		BYTE Reserved1       : 4;
	};
	BYTE SensorMaxYPosLo;
	struct
	{
		BYTE SensorMaxYPosHi : 4;
		BYTE Reserved2       : 4;
	};
	//
	// Note gap in registers on 3200
	//
	BYTE ZTouchThreshold;
	BYTE ZHysteresis;
	BYTE SmallZThreshold;
	BYTE SmallZScaleFactor[2];
	BYTE LargeZScaleFactor[2];
	BYTE AlgorithmSelection;
	BYTE WxScaleFactor;
	BYTE WxOffset;
	BYTE WyScaleFactor;
	BYTE WyOffset;
	BYTE XPitch[2];
	BYTE YPitch[2];
	BYTE FingerWidthX[2];
	BYTE FingerWidthY[2];
	BYTE ReportMeasuredSize;
	BYTE SegmentationSensitivity;
	BYTE XClipLo;
	BYTE XClipHi;
	BYTE YClipLo;
	BYTE YClipHi;
	BYTE MinFingerSeparation;
	BYTE MaxFingerMovement;
} RMI4_F11_CTRL_REGISTERS;


typedef struct _RMI4_F11_DATA_POSITION
{
	BYTE XPosHi;
	BYTE YPosHi;
	struct
	{
		BYTE XPosLo : 4;
		BYTE YPosLo : 4;
	};
	struct
	{
		BYTE XWidth : 4;
		BYTE YWidth : 4;
	};
	BYTE ZAmplitude;
} RMI4_F11_DATA_POSITION, *PRMI4_F11_DATA_POSITION;

typedef struct _RMI4_F11_DATA_REGISTERS_STATUS_BLOCK_STATESET
{
	struct
	{
		BYTE FingerState0 : 2;
		BYTE FingerState1 : 2;
		BYTE FingerState2 : 2;
		BYTE FingerState3 : 2;
	};
} RMI4_F11_DATA_REGISTERS_STATUS_BLOCK_STATESET;

//
// Logical structure for getting registry config settings
//
typedef struct _RMI4_F11_CTRL_REGISTERS_LOGICAL
{
	UINT32 ReportingMode;
	UINT32 AbsPosFilt;
	UINT32 RelPosFilt;
	UINT32 RelBallistics;
	UINT32 Dribble;
	UINT32 PalmDetectThreshold;
	UINT32 MotionSensitivity;
	UINT32 ManTrackEn;
	UINT32 ManTrackedFinger;
	UINT32 DeltaXPosThreshold;
	UINT32 DeltaYPosThreshold;
	UINT32 Velocity;
	UINT32 Acceleration;
	UINT32 SensorMaxXPos;
	UINT32 SensorMaxYPos;
	UINT32 ZTouchThreshold;
	UINT32 ZHysteresis;
	UINT32 SmallZThreshold;
	UINT32 SmallZScaleFactor;
	UINT32 LargeZScaleFactor;
	UINT32 AlgorithmSelection;
	UINT32 WxScaleFactor;
	UINT32 WxOffset;
	UINT32 WyScaleFactor;
	UINT32 WyOffset;
	UINT32 XPitch;
	UINT32 YPitch;
	UINT32 FingerWidthX;
	UINT32 FingerWidthY;
	UINT32 ReportMeasuredSize;
	UINT32 SegmentationSensitivity;
	UINT32 XClipLo;
	UINT32 XClipHi;
	UINT32 YClipLo;
	UINT32 YClipHi;
	UINT32 MinFingerSeparation;
	UINT32 MaxFingerMovement;
} RMI4_F11_CTRL_REGISTERS_LOGICAL;
