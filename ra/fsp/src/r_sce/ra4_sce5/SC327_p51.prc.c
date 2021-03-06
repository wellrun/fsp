/////////////////////////////////////////////////////////////////////////

// <SC32#7 Generate hidden key for AES-256bit>                         //
// Procedure number: 51                                                //
// File name      : SC327_p51L.prc                                     //
// State Diagram  : main(FSM1)                                         //
// Start State    : main03                                             //
// End State      : main03                                             //
// Input Data     : void                                               //
// Output Data    : OutData_KeyIndex[13]                               //
// Return Value   : Pass or Resource_Conflict                          //
// ---------------------------------------------------------------------//
// total cycle    : polling + write access + read access               //
// polling        : 595 cycle                                          //
// polling access :  20 times                                          //
// write access   :  56 times                                          //
// read  access   :  14 times                                          //
/////////////////////////////////////////////////////////////////////////

#include "hw_sce_private.h"
#include "hw_sce_aes_private.h"
#include "SCE_ProcCommon.h"

fsp_err_t HW_SCE_AES_256CreateEncryptedKey (uint32_t * OutData_KeyIndex)
{
    if (RD1_MASK(REG_1BCH, 0x0000001f) != 0x0000000)
    {
        // Busy now then retry later;
        // Because  1_xxxx other processing box is in duty.
        // x_1xxx resource D is in duty,then retry after you released D.
        // x_x1xx resource C is in duty,then retry after you released C.
        // x_xx1x resource B is in duty,then retry after you released B.
        // x_xxx1 resource A is in duty,then retry after you released A.

        // A,B,C,D: see TrustedSystem datasheet
        return FSP_ERR_CRYPTO_SCE_RESOURCE_CONFLICT;
    }

    WR1_PROG(REG_84H, 0x00005101);
    WR1_PROG(REG_108H, 0x00000000);

    WR1_PROG(REG_1C0H, 0x00010001);

    WR1_PROG(REG_A4H, 0xc02006bc);
    WAIT_STS(REG_104H, 31, 1);
    WR1_PROG(REG_100H, 0x00000000);

    WR1_PROG(REG_E0H, 0x80040000);
    WR1_PROG(REG_1D0H, 0x00000000);
    WR1_PROG(REG_00H, 0x00008113);
    WAIT_STS(REG_00H, 25, 0);
    WR1_PROG(REG_1CH, 0x00001800);

    SC327_function001(0x5a8241b2, 0xbf00e7d4, 0x3ef6e705, 0x142973d5);
    WR1_PROG(REG_A4H, 0x00000884);
    WR1_PROG(REG_E0H, 0x81010000);
    WR1_PROG(REG_1D0H, 0x00000000);
    WR1_PROG(REG_00H, 0x00001807);
    WAIT_STS(REG_00H, 25, 0);
    WR1_PROG(REG_1CH, 0x00001800);
    WR1_PROG(REG_04H, 0x00000107);
    WAIT_STS(REG_04H, 30, 1);
    RD1_ADDR(REG_A0H, &OutData_KeyIndex[0]);
    WR1_PROG(REG_00H, 0x0000010f);
    WAIT_STS(REG_00H, 25, 0);
    WR1_PROG(REG_1CH, 0x00001800);

    WR1_PROG(REG_A4H, 0x600c3a0c);
    WR1_PROG(REG_E0H, 0x81010000);
    WR1_PROG(REG_1D0H, 0x00000000);
    WR1_PROG(REG_00H, 0x00001807);
    WAIT_STS(REG_00H, 25, 0);
    WR1_PROG(REG_1CH, 0x00001800);

    WR1_PROG(REG_A4H, 0x400c0a0c);
    WAIT_STS(REG_104H, 31, 1);
    WR1_PROG(REG_100H, 0x615e8e97);

    SC327_function001(0xcf83df70, 0x7598f9d0, 0xcb8fff58, 0x8c2e1dbb);
    WR1_PROG(REG_A4H, 0xc02006bc);
    WAIT_STS(REG_104H, 31, 1);
    WR1_PROG(REG_100H, 0x00000000);

    WR1_PROG(REG_A4H, 0xc02006bc);
    WAIT_STS(REG_104H, 31, 1);
    WR1_PROG(REG_100H, 0x00000000);

    WR1_PROG(REG_B0H, 0x00000100);
    WR1_PROG(REG_A4H, 0x42e486bf);
    WR1_PROG(REG_00H, 0x00001123);
    WAIT_STS(REG_00H, 25, 0);
    WR1_PROG(REG_1CH, 0x00001800);

    WR1_PROG(REG_104H, 0x00000351);
    WR1_PROG(REG_A4H, 0x400009cd);
    WAIT_STS(REG_104H, 31, 1);
    WR4_PROG(REG_100H, 0x00000000, 0x00000000, 0x00000000, 0x00000002);

    WR1_PROG(REG_04H, 0x00000133);
    WAIT_STS(REG_04H, 30, 1);
    RD4_ADDR(REG_A0H, &OutData_KeyIndex[1]);
    WAIT_STS(REG_04H, 30, 1);
    RD4_ADDR(REG_A0H, &OutData_KeyIndex[5]);
    WAIT_STS(REG_04H, 30, 1);
    RD4_ADDR(REG_A0H, &OutData_KeyIndex[9]);

    WR1_PROG(REG_1C0H, 0x00010000);

    SC327_function003(0xc70fd8e4, 0x7685cfd4, 0x4931d3b7, 0x8e1fe9e9);
    WR1_PROG(REG_1BCH, 0x00000040);
    WAIT_STS(REG_18H, 12, 0);

    return FSP_SUCCESS;
}
