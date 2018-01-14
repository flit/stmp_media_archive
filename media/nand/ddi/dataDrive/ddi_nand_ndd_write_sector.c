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
//! \addtogroup ddi_nand_data_drive
//! @{
//! \file ddi_nand_data_drive_ndd_write_sector.c
//! \brief Read routines for Nand Device Driver.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "hw/profile/hw_profile.h"
#include "Mapper.h"
#include "hw/core/mmu.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "MultiTransaction.h"
#include "NssmManager.h"
#include "NonsequentialSectorsMap.h"
#include "VirtualBlock.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

#ifndef DEBUG_DDI_NAND_WRITE_SECTOR
     #define DEBUG_DDI_NAND_WRITE_SECTOR
#endif

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#ifdef DEBUG_DDI_NAND_WRITE_SECTOR
bool bEnableWriteSectorDebug = false;
#endif

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Write a page to the NAND.
//!
//! This function will write a page to the NAND synchronously. The routine does
//! not return until the write is complete.
//!
//! \param[in] u32LogicalSectorNumber Sector number to write.
//! \param[out] pSectorData Pointer to the buffer containing data to be written. The
//!     buffer must be the full native sector sector size.
//!
//! \return Status of call or error.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::writeSector(uint32_t u32LogicalSectorNumber, const SECTOR_BUFFER * pSectorData)
{
    RtStatus_t RetValue;

    RetValue = writeSectorInternal(u32LogicalSectorNumber, pSectorData);

#ifdef DEBUG_DDI_NAND_WRITE_SECTOR
    if (RetValue != SUCCESS)
    {
        tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1), "NDDWS failure 0x%X, Drive %d, Lpage 0x%X\r\n",
            RetValue, m_u32Tag, u32LogicalSectorNumber);
    }
#endif

    return RetValue;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Internal implementation of writeSector().
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::writeSectorInternal(uint32_t u32LogicalSectorNumber, const SECTOR_BUFFER * pSectorData)
{
    RtStatus_t status;
    uint32_t u32LogicalSectorOffset;
    uint32_t virtualSectorOffset;
    bool isInLogicalOrder = false;
    bool checkForLogicalOrder = false;

    // Make sure we're initialized
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we won't go out of bounds
    if (u32LogicalSectorNumber >= m_u32NumberOfSectors)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    DdiNandLocker locker;

    // Disable auto sleep for the whole page write process.
    NandHal::SleepHelper disableSleep(false);
    
    // Convert logical sector to be region relative. Then find the NSSM for this virtual block.
    // If it isn't already in memory, the physical block(s) will be scanned in order to build it.
    NonsequentialSectorsMap * sectorMap;
    status = getSectorMapForLogicalSector(
        u32LogicalSectorNumber,
        NULL,
        &u32LogicalSectorOffset,
        &sectorMap,
        NULL);
    if (status != SUCCESS)
    {
        return status;
    }
    assert(sectorMap);
    
    // Get the virtual block from the NSSM.
    VirtualBlock & vblock = sectorMap->getVirtualBlock();
    
    // Check if this is part of a transaction.
    bool isPartOfTransaction = (m_transaction
                                && m_transaction->isLive()
                                && m_transaction->isWrite()
                                && vblock == m_transaction->getVirtualBlockAddress());
    
    // Get a buffer to hold the redundant area.
    AuxiliaryBuffer auxBuffer;
    status = auxBuffer.acquire();
    if (status != SUCCESS)
    {
        return status;
    }

    if (!isPartOfTransaction)
    {
        // If writing the last page in the block, we need to check whether the block is in logical
        // order so we can set the is-in-order metadata flag.
        if (u32LogicalSectorOffset == (VirtualBlock::getVirtualPagesPerBlock() - 1))
        {
            checkForLogicalOrder = true;
        }
        
        // Convert the logical offset into a virtual offset and a real physical page address. If
        // the physical block has not yet been allocated, then this method will allocate one for us.
        PageAddress physicalPageAddress;
        status = sectorMap->getNextPhysicalPage(u32LogicalSectorOffset, physicalPageAddress, &virtualSectorOffset);
        if (status != SUCCESS)
        {
            return status;
        }
    
        // See if the whole block is written in logical order, so we know whether to set the
        // is-in-order flag in the page metadata.
        //! \todo Deal with transactions here.
        if (checkForLogicalOrder)
        {
            isInLogicalOrder = sectorMap->isInLogicalOrder();
        }

        // Initialize the redundant area. Up until now, we have ignored u32LogicalSectorOffset.
        // We write the logical sector offset into redundant area so that NSSM may be reconstructed
        // from physical block. The block number stored in the metadata is the value that is passed
        // to the mapper to look up the physical block, which is the virtual block number plus the
        // plane index for the virtual sector offset.
        Metadata md(auxBuffer);
        md.prepare(vblock.getMapperKeyFromVirtualOffset(virtualSectorOffset), u32LogicalSectorOffset);
        
        // If this drive is a hidden data drive, then we need to set the RA flag indicating so.
        if (m_Type == kDriveTypeHidden)
        {
            // Clear the flag bit to set it. All metadata flags are set when the bit is 0.
            md.setFlag(Metadata::kIsHiddenBlockFlag);
        }
        
        // The pages of this block are written in logical order, we set kIsInLogicalOrderFlag 
        if (isInLogicalOrder)
        {
            md.setFlag(Metadata::kIsInLogicalOrderFlag);
            
            m_media->getNssmManager()->getStatistics().writeSetOrderedCount++;
        }

        // Loop until we have a successful write or an unexpected error occurs.
        while (1)
        {
            // Write the page.
            status = physicalPageAddress.getNand()->writePage(physicalPageAddress.getRelativePage(), pSectorData, auxBuffer);

            if (SUCCESS == status)
            {
                break;
            }
            else if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                BlockAddress badPhysicalBlock = physicalPageAddress.getBlock();

                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Write failed: new bad block %u (vblock %u, voffset %u)! ***\n", badPhysicalBlock.get(), vblock.get(), virtualSectorOffset);
                
                // Try to recover by copying data into a new block. We must skip the logical sector
                // that we were going to write.
                status = sectorMap->recoverFromFailedWrite(virtualSectorOffset, u32LogicalSectorOffset);
                if (status != SUCCESS)
                {
                    // Error exit
                    return status;
                }

                // Get the new physical page address and virtual sector offset again. When the block
                // contents were relocated, they were written in sequential order, skipping any
                // duplicate entries. So the next unoccupied virtual sector in the new block is not
                // necessarily the same virtual sector that we were going to write to in the old block.
                status = sectorMap->getNextPhysicalPage(u32LogicalSectorOffset, physicalPageAddress, &virtualSectorOffset);
                if (status != SUCCESS)
                {
                    return status;
                }
                
                // Recheck if the block is in sorted order since it has moved around and was
                // probably merged.
                bool isInLogicalOrderNew = false;
                if (checkForLogicalOrder)
                {
                    isInLogicalOrderNew = sectorMap->isInLogicalOrder();
                }

                // Metadata::kIsInLogicalOrderFlag is not set at the first time
                if (isInLogicalOrderNew && !isInLogicalOrder)
                {
                    md.setFlag(Metadata::kIsInLogicalOrderFlag);
                    m_media->getNssmManager()->getStatistics().writeSetOrderedCount++;
                }
                // Metadata::kIsInLogicalOrderFlag is set at the first time, we need clear it now.
                else if (!isInLogicalOrderNew && isInLogicalOrder)
                {
                    md.clearFlag(Metadata::kIsInLogicalOrderFlag);
                }
            }
            else
            {
                // Error exit
                return status;
            }
        }

        // Add the page to the nonsequential sectors map. The mapping is from logical sector
        // offset to physical sector offset.
        sectorMap->addEntry(u32LogicalSectorOffset, virtualSectorOffset);
    }
    else
    {
        // Save the address and buffers in the transaction object.
        m_transaction->pushSector(
            u32LogicalSectorNumber,
            u32LogicalSectorOffset,
            const_cast<SECTOR_BUFFER *>(pSectorData),
            auxBuffer);
        
        // Clear the buffer object but don't actually free the buffer. We need it to stick around
        // until the transaction is complete.
        auxBuffer.relinquish();
    }

    return status;
}


////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
