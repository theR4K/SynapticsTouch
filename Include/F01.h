#pragma once

#include <wdf.h>
#include <wdm.h>


//
// Defines from Synaptics RMI4 Data Sheet, please refer to
// the spec for details about the fields and values.
//

#define RMI4_F01_RMI_DEVICE_CONTROL       0x01

//
// Function $01 - RMI Device Control
//

#define RMI4_F01_DATA_STATUS_NO_ERROR             0
#define RMI4_F01_DATA_STATUS_RESET_OCCURRED       1
#define RMI4_F01_DATA_STATUS_INVALID_CONFIG       2
#define RMI4_F01_DATA_STATUS_DEVICE_FAILURE       3
#define RMI4_F01_DATA_STATUS_CONFIG_CRC_FAILURE   4
#define RMI4_F01_DATA_STATUS_FW_CRC_FAILURE       5
#define RMI4_F01_DATA_STATUS_CRC_IN_PROGRESS      6

typedef struct _RMI4_F01_QUERY_REGISTERS
{
	BYTE ManufacturerID;
	union
	{
		BYTE All;
		struct
		{
			BYTE CustomMap : 1;
			BYTE NonCompliant : 1;
			BYTE Reserved0 : 1;
			BYTE HasSensorID : 1;
			BYTE Reserved1 : 1;
			BYTE HasAdjDoze : 1;
			BYTE HasAdJDozeHold : 1;
			BYTE Reserved2 : 1;
		};
	} ProductProperties;
	BYTE ProductInfo0;
	BYTE ProductInfo1;
	BYTE Date0;
	BYTE Date1;
	BYTE WaferLotId0Lo;
	BYTE WaferLotId0Hi;
	BYTE WaferLotId1Lo;
	BYTE WaferLotId1Hi;
	BYTE WaferLotId2Lo;
	BYTE ProductID1;
	BYTE ProductID2;
	BYTE ProductID3;
	BYTE ProductID4;
	BYTE ProductID5;
	BYTE ProductID6;
	BYTE ProductID7;
	BYTE ProductID8;
	BYTE ProductID9;
	BYTE ProductID10;
	BYTE Reserved21;
	BYTE SendorID;
	BYTE Reserved23;
	BYTE Reserved24;
	BYTE Reserved25;
	BYTE Reserved26;
	BYTE Reserved27;
	BYTE Reserved28;
	BYTE Reserved29;
	BYTE Reserved30;
	BYTE Reserved31;
} RMI4_F01_QUERY_REGISTERS;


typedef struct _RMI4_F01_CTRL_REGISTERS
{
	union
	{
		BYTE All;
		struct
		{
			BYTE SleepMode : 2;
			BYTE NoSleep : 1;
			BYTE Reserved0 : 3;
			BYTE ReportRate : 1;
			BYTE Configured : 1;
		};
	} DeviceControl;
	BYTE InterruptEnable;
	BYTE DozeInterval;
	BYTE DozeThreshold;
	BYTE DozeHoldoff;
} RMI4_F01_CTRL_REGISTERS;

typedef struct _RMI4_F01_DATA_REGISTERS
{
	union
	{
		BYTE All;
		struct
		{
			BYTE Status : 4;
			BYTE Reserved0 : 2;
			BYTE FlashProg : 1;
			BYTE Unconfigured : 1;
		};
	} DeviceStatus;
	BYTE InterruptStatus[1];
} RMI4_F01_DATA_REGISTERS;


typedef struct _RMI4_F01_COMMAND_REGISTERS
{
	BYTE Reset;
} RMI4_F01_COMMAND_REGISTERS;

//
// Logical structure for getting registry config settings
//
typedef struct _RM4_F01_CTRL_REGISTERS_LOGICAL
{
	UINT32 SleepMode;
	UINT32 NoSleep;
	UINT32 ReportRate;
	UINT32 Configured;
	UINT32 InterruptEnable;
	UINT32 DozeInterval;
	UINT32 DozeThreshold;
	UINT32 DozeHoldoff;
} RMI4_F01_CTRL_REGISTERS_LOGICAL;
