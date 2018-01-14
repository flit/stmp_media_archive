///////////////////////////////////////////////////////////////////////////////
// Copyright (c) SigmaTel, Inc. All rights reserved.
// 
// SigmaTel, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of SigmaTel, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_system_drive
//! @{
//! \file ddi_nand_system_drive_erase.c
//! \brief Implementation of the system drive erase API.
//!
///////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_system_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand_media.h"
#include "Block.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Erases all good blocks of a system drive.
//!
//! \return SUCCESS or Error code
////////////////////////////////////////////////////////////////////////////////
RtStatus_t SystemDrive::erase()
{
    int i;
    RtStatus_t Status;

	// Make sure we're initialized    
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
    
    // Make sure we're not write protected    
    if (m_bWriteProtected)
    {
        return ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED;
    }
    
    // Create our block instance.
    Block block(m_pRegion->getStartBlock());

    // For every block of the drive
    for (i = 0; i < m_pRegion->getBlockCount(); ++i, ++block)
    {
        DdiNandLocker lockForThisBlock;
        
        // Check if block is bad by scanning the region's bad block table. This assumes that
        // the bad block table was filled in by the discovery code.
        if (m_pRegion->getBadBlocks()->isBlockBad(block))
        {
            // This block is bad, so skip to the next one.
            continue;
        }
        
        // Erase the block.
        Status = block.erase();

        // If the block erasure failed, then mark the block bad.
        if (Status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Erase failed: new bad block %u! ***\n", block.get());
            
            block.markBad();
            
            // Update region structure and bad block table.
            m_pRegion->addNewBadBlock(block);
        }
    }

    // The drive is now erased.
    m_bErased = true;
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

//! @}
