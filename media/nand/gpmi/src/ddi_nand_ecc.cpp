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
//! \addtogroup ddi_media_nand_hal_ecc_internal
//! @{
//! \file    ddi_nand_ecc.cpp
//! \brief   Functions for managing the ECC information.
//!
//! This file contains the abstract NAND HAL ECC interface functions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_gpmi_internal.h"
#include "ddi_nand_ecc.h"

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

//! \brief Storage for cached instances of EccTypeInfo.
static const EccTypeInfo * s_cachedEccTypeInfo[kNandEccType_Count] = {0};

#if defined(STMP378x)
//! \brief Number of bit errors that cause a page rewrite, for each BCH ECC level. 
const int kBchThresholds[] = { 0, 1, 3, 5, 6, 8, 9, 10, 12, 13, 15 };
#endif // defined(STMP378x)

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

//! By default, no instances of EccTypeInfo exist at startup. Only when a caller of
//! this function requests a type info object will one be created. Since most applications
//! and systems only use a single ECC type at runtime, lazily instantiating the type info
//! objects lets us save some heap space.
//!
//! First, the array of cached type info objects is consulted. If an instance for the
//! requested ECC type already exists, it is returned immediately. Otherwise the
//! appropriate object is created, stored in the cache, and then returned.
const EccTypeInfo * ddi_gpmi_get_ecc_type_info(NandEccType_t eccType)
{
    assert((int)eccType < kNandEccType_Count);
    
    // Return cached type info object, and handle the none type specially since it doesn't
    // have a type info object.
    if (s_cachedEccTypeInfo[eccType])
    {
        return s_cachedEccTypeInfo[eccType];
    }
    else if (eccType == kNandEccType_None)
    {
        return NULL;
    }
    
    // The type info object doesn't exist yet, so lazily instantiate it.
    EccTypeInfo * typeInfo = NULL;
    if (eccType == kNandEccType_RS4)
    {
        typeInfo = new ReedSolomonEccType(
            kNandEccType_RS4,                       // eccType
            BV_GPMI_ECCCTRL_ECC_CMD__DECODE_4_BIT,  // decodeCommand
            BV_GPMI_ECCCTRL_ECC_CMD__ENCODE_4_BIT,  // encodeCommand
            NAND_ECC_BYTES_4BIT,                    // parityBytes
            NAND_METADATA_SIZE_4BIT,                // metadataSize
            3);                                     // threshold
    }
    else if (eccType == kNandEccType_RS8)
    {
        typeInfo = new ReedSolomonEccType(
            kNandEccType_RS8,                       // eccType
            BV_GPMI_ECCCTRL_ECC_CMD__DECODE_8_BIT,  // decodeCommand
            BV_GPMI_ECCCTRL_ECC_CMD__ENCODE_8_BIT,  // encodeCommand
            NAND_ECC_BYTES_8BIT,                    // parityBytes
            NAND_METADATA_SIZE_8BIT,                // metadataSize
            6);                                     // threshold
    }
#if defined(STMP378x)
    else if (eccType >= kNandEccType_BCH0 && eccType <= kNandEccType_BCH20)
    {
        unsigned thresholdIndex = ddi_bch_GetLevel(eccType) / 2;
        typeInfo = new BchEccType(eccType, kBchThresholds[thresholdIndex]);
    }
#endif // STMP378x
    
    // Save the type info object we just created.
    s_cachedEccTypeInfo[eccType] = typeInfo;
    return typeInfo;
}

//! @}
