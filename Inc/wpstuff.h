#pragma once


//wpsensors.h

//
// Data types for supported sensors.
//

typedef enum
{
	SENSOR_DATA_ACCELEROMETER_3D,
	SENSOR_DATA_PROXIMITY,
	SENSOR_DATA_AMBIENT_LIGHT,
	SENSOR_DATA_MAGNETOMETER3D,
	SENSOR_DATA_GYRO_3D,
	SENSOR_DATA_FUSION_3D,
	SENSOR_DATA_DEVICE_ORIENTATION,
}SENSOR_DATA_TYPE;

//
// Basic information provided with every sensor reading. 
//

typedef struct _SENSOR_DATA_HEADER
{
	//
	// size of SENSOR_DATA_HEADER + the specific sensor's SENSOR_DATA
	//                                    
	ULONG         Size;

	//
	// Sample time in 100's of ns
	//
	ULONGLONG TimeStamp;

	SENSOR_DATA_TYPE DataType;
} SENSOR_DATA_HEADER;

// {97F115C8-599A-4153-8894-D2D12899918A}
DEFINE_GUID(SENSOR_TYPE_AMBIENT_LIGHT,
	0X97F115C8, 0X599A, 0X4153, 0X88, 0X94, 0XD2, 0XD1, 0X28, 0X99, 0X91, 0X8A);

// end wpsensors.h

//wpambient.h

typedef struct _ALS_DATA
{
	SENSOR_DATA_HEADER Header;

	//
	// Readings in milli Lux
	//
	ULONG Sample;
} ALS_DATA, *PALS_DATA;

//end wpambient.h