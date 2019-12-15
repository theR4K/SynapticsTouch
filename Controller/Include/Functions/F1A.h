#pragma once

#include <wdf.h>
#include <wdm.h>

//
// Function $1A - 0-D Capacitive Button Sensors
//

typedef struct _RMI4_F1A_QUERY_REGISTERS
{
    struct
    {
        BYTE MaxButtonCount : 3;
        BYTE Reserved0 : 5;
    };
    struct
    {
        BYTE HasGenControl : 1;
        BYTE HasIntEnable : 1;
        BYTE HasMultiButSel : 1;
        BYTE HasTxRxMapping : 1;
        BYTE HasPerButThresh : 1;
        BYTE HasRelThresh : 1;
        BYTE HasStrongButHyst : 1;
        BYTE HasFiltStrength : 1;
    };
} RMI4_F1A_QUERY_REGISTERS;


typedef struct _RMI4_F1A_CTRL_REGISTERS
{
    struct
    {
        BYTE MultiButtonRpt : 2;
        BYTE FilterMode : 2;
        BYTE Reserved0 : 4;
    };
    struct
    {
        BYTE IntEnBtn0 : 1;
        BYTE IntEnBtn1 : 1;
        BYTE IntEnBtn2 : 1;
        BYTE IntEnBtn3 : 1;
        BYTE Reserved1 : 4;
    };
    struct
    {
        BYTE MultiBtn0 : 1;
        BYTE MultiBtn1 : 1;
        BYTE MultiBtn2 : 1;
        BYTE MultiBtn3 : 1;
        BYTE Reserved2 : 4;
    };
    BYTE PhysicalTx0;
    BYTE PhysicalRx0;
    BYTE PhysicalTx1;
    BYTE PhysicalRx1;
    BYTE PhysicalTx2;
    BYTE PhysicalRx2;
    BYTE PhysicalTx3;
    BYTE PhysicalRx3;
    BYTE Threshold0;
    BYTE Threshold1;
    BYTE Threshold2;
    BYTE Threshold3;
    BYTE ReleaseThreshold;
    BYTE StrongButtonHyst;
    BYTE FilterStrength;
} RMI4_F1A_CTRL_REGISTERS;

typedef struct _RMI4_F1A_DATA_REGISTERS
{
    union
    {
        struct
        {
            BYTE Button0 : 1;
            BYTE Button1 : 1;
            BYTE Button2 : 1;
            BYTE Button3 : 1;
            BYTE Reserved0 : 4;
        };
        BYTE Raw;
    };
} RMI4_F1A_DATA_REGISTERS;

typedef struct _RMI4_F1A_CACHE
{
    BYTE prevButtonsState;

};

//Debug
/*
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "F1A query:\n\
            MaxButtonCount:		%x\n\
            Reserved0:			%x\n\
            HasGenControl:		%x\n\
            HasIntEnable:		%x\n\
            HasMultiButSel:		%x\n\
            HasTxRxMapping:		%x\n\
            HasPerButThresh:	%x\n\
            HasRelThresh:		%x\n\
            HasStrongButHyst:	%x\n\
            HasFiltStrength:	%x",
            (queryF1a.MaxButtonCount & 0x3),
            (queryF1a.Reserved0 & 0x5),
            (queryF1a.HasGenControl & 0x1),
            (queryF1a.HasIntEnable & 0x1),
            (queryF1a.HasMultiButSel & 0x1),
            (queryF1a.HasTxRxMapping & 0x1),
            (queryF1a.HasPerButThresh & 0x1),
            (queryF1a.HasRelThresh & 0x1),
            (queryF1a.HasStrongButHyst & 0x1),
            (queryF1a.HasFiltStrength & 0x1));

        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "F1A crtl:\n\
            MultiBtnRpt: %x\n\
            FilterMode: %x\n\
            Reserved0: %x\n\
            IntEnBtn0: %x\n\
            IntEnBtn1: %x\n\
            IntEnBtn2: %x\n\
            IntEnBtn3: %x\n\
            Reserved1: %x\n\
            MultiBtn0: %x\n\
            MultiBtn1: %x\n\
            MultiBtn2: %x\n\
            MultiBtn3: %x\n\
            Reserved2: %x\n\
            PhysicalTx0: %x\n\
            PhysicalRx0: %x\n\
            PhysicalTx1: %x\n\
            PhysicalRx1: %x\n\
            PhysicalTx2: %x\n\
            PhysicalRx2: %x\n\
            PhysicalTx3: %x\n\
            PhysicalRx3: %x\n\
            Threshold0: %x\n\
            Threshold1: %x\n\
            Threshold2: %x\n\
            Threshold3: %x\n\
            ReleaseThreshold: %x\n\
            StrongBtnHyst: %x\n\
            FilterStrength: %x",
            (controlF1a.MultiButtonRpt & 0x3),
            (controlF1a.FilterMode & 0x3),
            (controlF1a.Reserved0 & 0xf),
            (controlF1a.IntEnBtn0 & 0x1),
            (controlF1a.IntEnBtn1 & 0x1),
            (controlF1a.IntEnBtn2 & 0x1),
            (controlF1a.IntEnBtn3 & 0x1),
            (controlF1a.Reserved1 & 0xf),
            (controlF1a.MultiBtn0 & 0x1),
            (controlF1a.MultiBtn1 & 0x1),
            (controlF1a.MultiBtn2 & 0x1),
            (controlF1a.MultiBtn3 & 0x1),
            (controlF1a.Reserved2 & 0xf),
            (controlF1a.PhysicalTx0 & 0xff),
            (controlF1a.PhysicalRx0 & 0xff),
            (controlF1a.PhysicalTx1 & 0xff),
            (controlF1a.PhysicalRx1 & 0xff),
            (controlF1a.PhysicalTx2 & 0xff),
            (controlF1a.PhysicalRx2 & 0xff),
            (controlF1a.PhysicalTx3 & 0xff),
            (controlF1a.PhysicalRx3 & 0xff),
            (controlF1a.Threshold0 & 0xff),
            (controlF1a.Threshold1 & 0xff),
            (controlF1a.Threshold2 & 0xff),
            (controlF1a.Threshold3 & 0xff),
            (controlF1a.ReleaseThreshold & 0xff),
            (controlF1a.StrongButtonHyst & 0xff),
            (controlF1a.FilterStrength & 0xff));
*/