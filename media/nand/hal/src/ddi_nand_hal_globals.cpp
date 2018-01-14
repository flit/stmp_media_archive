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
//! \addtogroup ddi_media_nand_hal_internals
//! @{
//! \file ddi_nand_hal_globals.cpp
//! \brief Contains global definitions for the low level NAND driver.
///////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/nand/hal/src/ddi_nand_hal_internal.h"

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! Global context for the NAND HAL.
NandHalContext_t g_nandHalContext;

#pragma alignvar(32)
//! \brief Shared cache aligned and sized result buffer. 
uint8_t g_nandHalResultBuffer[32];

//! @}
