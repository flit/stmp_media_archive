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
////////////////////////////////////////////////////////////////////////////////
//! \file
//! \brief Common header for the NAND driver.
////////////////////////////////////////////////////////////////////////////////
#ifndef _NANDDDI_H
#define _NANDDDI_H

#include "drivers/media/include/ddi_media_internal.h"
#include "components/telemetry/tss_logtext.h"
#include "os/threadx/tx_api.h"
#include "DdiNandLocker.h"
#include "ddi_nand_media.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

#if !defined(ALLOW_BB_TABLE_READ_SKIP)
//     #define ALLOW_BB_TABLE_READ_SKIP
#endif

#if !defined(DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER)
//     #define DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
#endif
#if !defined(DEBUG_BOOT_BLOCK_SEARCH)
//     #define DEBUG_BOOT_BLOCK_SEARCH
#endif

//! Define setting the NAND Chip Enable to GPMI_CE0.
#define NAND0   0

//! Define setting the NAND Chip Enable to GPMI_CE1.
#define NAND1   1

//! Size in bytes of the data area of a 2K page.
#define NAND_PAGE_SIZE_2K    (2048)

#endif // #ifndef _NANDDDI_H
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

