#pragma once

#include <wdf.h>
#include <wdm.h>

//
// Defines from Synaptics RMI4 Data Sheet, please refer to
// the spec for details about the fields and values.
//

#define RMI4_F12_2D_TOUCHPAD_SENSOR		  0x12

//
// Function $12 - 2-D Touch Sensor
//

#define RMI4_FINGER_STATE_NOT_PRESENT                  0
#define RMI4_FINGER_STATE_PRESENT_WITH_ACCURATE_POS    1
#define RMI4_FINGER_STATE_PRESENT_WITH_INACCURATE_POS  2
#define RMI4_FINGER_STATE_RESERVED                     3

#define RMI4_F12_DEVICE_CONTROL_SLEEP_MODE_OPERATING   0
#define RMI4_F12_DEVICE_CONTROL_SLEEP_MODE_SLEEPING    1

typedef struct _RMI4_F12_DATA_POSITION
{
	int X;
	int Y;
} RMI4_F12_DATA_POSITION;

typedef enum _RMI4_F12_OBJECT_TYPE
{
	RMI_F12_OBJECT_NONE = 0x00,
	RMI_F12_OBJECT_FINGER = 0x01,
	RMI_F12_OBJECT_STYLUS = 0x02,
	RMI_F12_OBJECT_PALM = 0x03,
	RMI_F12_OBJECT_UNCLASSIFIED = 0x04,
	RMI_F12_OBJECT_GLOVED_FINGER = 0x06,
	RMI_F12_OBJECT_NARROW_OBJECT = 0x07,
	RMI_F12_OBJECT_HAND_EDGE = 0x08,
	RMI_F12_OBJECT_COVER = 0x0A,
	RMI_F12_OBJECT_STYLUS_2 = 0x0B,
	RMI_F12_OBJECT_ERASER = 0x0C,
	RMI_F12_OBJECT_SMALL_OBJECT = 0x0D,
} RMI4_F12_OBJECT_TYPE;

#define F12_DATA1_BYTES_PER_OBJ			8

#define RMI_F12_REPORTING_MODE_CONTINUOUS   0
#define RMI_F12_REPORTING_MODE_REDUCED      1
#define RMI_F12_REPORTING_MODE_MASK         7

#define F12_2D_CTRL20   20

//
// Logical structure for getting registry config settings
//
typedef struct _RMI4_F12_CTRL_REGISTERS_LOGICAL
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
} RMI4_F12_CTRL_REGISTERS_LOGICAL;

/* describes a single packet register */
#define BITS_PER_BYTE		8

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

#define RMI_REG_DESC_PRESENSE_BITS	(32 * BITS_PER_BYTE)
#define RMI_REG_DESC_SUBPACKET_BITS	(37 * BITS_PER_BYTE)

typedef struct _RMI_REGISTER_DESC_ITEM
{
	USHORT Register;
	ULONG RegisterSize;
	BYTE NumSubPackets;
	ULONG SubPacketMap[BITS_TO_LONGS(RMI_REG_DESC_SUBPACKET_BITS)];
} RMI_REGISTER_DESC_ITEM, * PRMI_REGISTER_DESC_ITEM;

typedef struct _RMI_REGISTER_DESCRIPTOR
{
	ULONG StructSize;
	ULONG PresenceMap[BITS_TO_LONGS(RMI_REG_DESC_PRESENSE_BITS)];
	UINT8 NumRegisters;
	RMI_REGISTER_DESC_ITEM* Registers;
} RMI_REGISTER_DESCRIPTOR, * PRMI_REGISTER_DESCRIPTOR;
