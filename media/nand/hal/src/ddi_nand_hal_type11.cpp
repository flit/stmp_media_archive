///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor, Inc. All rights reserved.
// 
// Freescale Semiconductor, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of Freescale Semiconductor, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \file ddi_nand_hal_type11.cpp
//! \ingroup ddi_media_nand_hal_internals
//! \brief Functions for type11 devices.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_hal_internal.h"
#include "hw/core/mmu.h"

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

//! Type 11 Toshiba NANDs have holes in their address spaces. This function converts
//! a linear page address into an address that skips over the holes. We also skip
//! over the extended blocks since we don't use them; the NAND driver requires that
//! block and page counts are powers of 2.
//!
//! Actual address ranges for one chip enable with two dice:
//!  - 0x000000 - 0x07ffff : 4096 blocks
//!  - 0x080000 - 0x0819ff : 52 extended blocks
//!  - 0x081a00 - 0x0fffff : "chip gap"
//!  - 0x100000 - 0x17ffff : 4096 blocks
//!  - 0x180000 - 0x1819ff : 52 extended blocks
//!  - 0x181a00 - 0x1fffff : "chip gap"
__STATIC_TEXT uint32_t Type11Nand::adjustPageAddress(uint32_t pageAddress)
{
    const uint32_t kOneDieLinearPageCount = 0x80000;    // 4096 blocks at 128 pages per block.
    const uint32_t kOneDieActualPageCount = 0x100000;   // Address range of each die per chip enable.
    
    uint32_t result = pageAddress;
    
    // Is this address beyond the first 4096 linear blocks?
    if (pageAddress >= kOneDieLinearPageCount)
    {
        // Page 0x80000 becomes page 0x100000.
        // Page 0x81000 becomes page 0x101000.
        // Page 0xfffff becomes page 0x17ffff.
        // Page 0x165000 becomes page 0x265000.
        unsigned internalDieNumber = pageAddress / kOneDieLinearPageCount;
        unsigned internalDiePageOffset = pageAddress % kOneDieLinearPageCount;
        result = kOneDieActualPageCount * internalDieNumber + internalDiePageOffset;
    }
    
    return result;
}

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
