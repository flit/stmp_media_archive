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
//! \addtogroup ddi_nand_system_drive
//! @{
//! \file ddi_nand_nand_system_drive_write_sector.c
//! \brief Contains function to write to a system drive.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_system_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "os/threadx/tx_api.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "ddi_nand_media.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Writes a single sector to a system drive.
//!
//! \param[in] pDescriptor Pointer to the logical drive descriptor structure.
//! \param[in] wSectorNumber Sector number to write, relative to beginning of
//!     the system drive described by \a pDescriptor.
//! \param[in] pSectorData Pointer to data buffer that holds the contents
//!     of the sector to be written. This buffer must be in non-cached,
//!     non-buffered (NCNB) memory.
//!
//! \pre The system drive has been initialised.
//! \pre The system drive must have been erased and the sector indicated
//!     by \a wSectorNumber must never have been written to since then.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED
//! \retval ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS
//! \retval ERROR_DDI_LDL_LDRIVE_WRITE_ABORT
////////////////////////////////////////////////////////////////////////////////
RtStatus_t SystemDrive::writeSector(uint32_t wSectorNumber, const SECTOR_BUFFER * pSectorData)
{
    uint32_t absoluteBlockNumber;
    uint32_t wSectorOffsetBlock;
    RtStatus_t Status;
    NandPhysicalMedia * nand;
    uint32_t u32StTag;
    uint32_t logicalBlockNumber;

    // Make sure we're initialized
    if (m_bInitialized != TRUE)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we're not write protected
    if (m_bWriteProtected)
    {
        return ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED;
    }

    // Make sure the sector is within bounds
    if (wSectorNumber >= m_u32NumberOfSectors)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    DdiNandLocker locker;

    nand = m_pRegion->getNand();

    // Calculates the absolute sector number
    // 1. Find out number of bad blocks between the begin of the drive
    //    and the required sector
    // 2. Adjust the sector number with results from 1
    // 3. Adjust sector number related to the start of the chip

    // Get the logical block and sector offset.
    nand->pageToBlockAndOffset(wSectorNumber, &logicalBlockNumber, &wSectorOffsetBlock);

    // Step 1 - adjust block number to skip over bad blocks
    logicalBlockNumber = skipBadBlocks(logicalBlockNumber);

    // Convert logical to absolute physical.
    absoluteBlockNumber = logicalBlockNumber + m_pRegion->getStartBlock();

    // Make sure the block is within bounds
    if (absoluteBlockNumber > m_pRegion->getLastBlock())
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    // Convert back from absolute block to relative page.
    wSectorNumber = nand->blockAndOffsetToRelativePage(absoluteBlockNumber, wSectorOffsetBlock);

    // Produce the tag for this system drive.
    u32StTag = (STM_TAG<<8) | (m_u32Tag & 0xff);

    // Do basic RA buffer preparation.
    Metadata md(s_auxBuffer);
    md.prepare(u32StTag);
    md.setBlockNumber(logicalBlockNumber);

    // Perform the write
    Status = nand->writeFirmwarePage(wSectorNumber, pSectorData, s_auxBuffer);

    // Process the error
    // If error is type ERROR_DDI_NAND_HAL_WRITE_FAILED, then the block
    // we try to write to is BAD. We need to mark it physically
    // as such and to update the region info.
    if (Status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
    {
        // Mark the block BAD, on the NAND. Ignore errors, because there's nothing we can do.
        Block badBlock(absoluteBlockNumber);
        badBlock.markBad();

        // Update region structure and bad block table.
        m_pRegion->addNewBadBlock(badBlock);

        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Write failed: new bad block %u! ***\n", badBlock.get());
    }
    
    // The drive is no longer erased.
    m_bErased = false;

    return Status;
}

/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////  EOF  //////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
//! @}

