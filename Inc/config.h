
#define TOUCH_SCREEN_PROPERTIES_REG_KEY L"\\Registry\\Machine\\System\\TOUCH\\SCREENPROPERTIES"
#define TOUCH_DEVICE_RESOLUTION_X       768
#define TOUCH_DEVICE_RESOLUTION_Y       1280
#define TOUCH_PHYSICAL_RESOLUTION_X     718  //7.18
#define TOUCH_PHYSICAL_RESOLUTION_Y     1259  //12.59


//comment next line to use 10 fingers dara registers status
//needs for L630 
#define USE_SHORT_F11_DATA_REGISTERS_STATUS


//
// Global Data Declarations
//
#define gOEMVendorID   0x7379    // "sy"
#define gOEMProductID  0x726D    // "rm"
#define gOEMVersionID  3200

#define gpwstrManufacturerID L"Synaptics"
#define gpwstrProductID      L"3200"
#define gpwstrSerialNumber   L"4"