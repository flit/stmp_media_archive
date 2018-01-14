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
//! \addtogroup ddi_nand_media
//! @{
//! \file ddi_nand_media_regions.c
//! \brief This file contains functions used to manipulate and manage NAND regions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_ddi.h"
#include <string.h>
#include "ddi_nand_media.h"
#include "DiscoveredBadBlockTable.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
//  Code
/////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Get the region corresponding to a Physical Block.
//!
//! This function will return the region pointer parameter passed into it
//! by checking the physical sector parameter against the region boundaries.
//!
//! \param[in] physicalBlock Absolute physical block to match.
//! \return A pointer to the region holding the \a physicalBlock is returned.
////////////////////////////////////////////////////////////////////////////////
Region * Media::getRegionForBlock(const BlockAddress & physicalBlock)
{
    uint32_t u32AbsoluteOffset;
    int32_t i32NumBlocksInRegion;
    Region * pRegionInfo;  
    
    // Search through all the regions
    Region::Iterator it = createRegionIterator();
    while ((pRegionInfo = it.getNext()))
    {
        u32AbsoluteOffset = pRegionInfo->m_u32AbPhyStartBlkAddr;
        i32NumBlocksInRegion = pRegionInfo->m_iNumBlks;
        
        // We don't need to check for greater than start block address
        // because we're scanning sequentially.
        if ((physicalBlock < (u32AbsoluteOffset + i32NumBlocksInRegion))
            && (physicalBlock >= u32AbsoluteOffset))
        {
            // Return a pointer to the region found
            return pRegionInfo;
        }
    }
    
    return NULL;
}

void Region::setDirty()
{
    //! \todo Figure out how and when to clear region dirty flag, or if it's even necessary.
    m_bRegionInfoDirty = true;

    // Update DBBT.
    g_nandMedia->getDeferredQueue()->post(new SaveDbbtTask);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
