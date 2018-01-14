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
//! \file ddi_nand_data_drive_erase.c
//! \brief This file handles erasing the data drive.
////////////////////////////////////////////////////////////////////////////////

#include <types.h>
#include "ddi_nand_ddi.h"
#include "ddi_nand_media.h"
#include "ddi_nand_data_drive.h"
#include "Mapper.h"
#include "NssmManager.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

// Comment this line out unless actively profiling this function.
#define PROFILE_NAND_DD_REPAIR

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Erase the Data Drive.
//!
//! This function will "Erase" the entire Data Drive which includes all blocks
//! which are not marked as Bad.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::erase()
{
    int32_t iBlk;
    DataRegion * pRegion;
    uint32_t u32AbsolutePhyBlockAddr;
    RtStatus_t ret;
    
    DdiNandLocker locker;
    
    // Make sure we're initialized
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
    
    Mapper * mapper = m_media->getMapper();

    // Add up the total number of virtual blocks for this drive.
    Region::Iterator it(m_ppRegion, m_u32NumRegions);
    while ((pRegion = (DataRegion *)it.getNext()))
    {
        // Loop through all the logical blocks of this region.
        for (iBlk=0; iBlk < pRegion->getLogicalBlockCount(); iBlk++)
        {
            // Convert the logical block to a mapper key block.
            uint32_t mapperKeyBlock = pRegion->getStartBlock() + iBlk;
            
            // Get the physical block associated with the virtual block.
            ret = mapper->getBlockInfo(mapperKeyBlock, &u32AbsolutePhyBlockAddr);
            if (ret)
            {
                return ret;
            }
            
            // if this block has not been mapped, skip the erase.
            if (mapper->isBlockUnallocated(u32AbsolutePhyBlockAddr))
            {
                continue;
            }

            // Create the block instance for our physical block.
            Block phyBlock(u32AbsolutePhyBlockAddr);
                
            // Erase it
            ret = phyBlock.erase();
            if (ret == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Erase failed: new bad block %u! ***\n", phyBlock.get());
                
                // Let the mapper deal with the new bad block.
                mapper->handleNewBadBlock(phyBlock);
            }
            else if (ret != SUCCESS)
            {
                // Some unexpected error occurred.
                return  ret;
            }
            else
            {
                // Erased block successfully.
                ret = mapper->markBlock(mapperKeyBlock, u32AbsolutePhyBlockAddr, kNandMapperBlockFree);
                if (ret != SUCCESS)
                {
                    return ret;
                }
            }
        }
    }
    
    // Invalidate all NSSMs for this drive.
    m_media->getNssmManager()->invalidateDrive(this);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Repair the Data Drive.
//!
//! This function will "Repair" an uninitialized Data Drive by erasing all
//! physical blocks associated with the data drive. What makes this function
//! different than NANDDataDriveErase() is that we have to assume that the
//! mapper failed initialization.
//!
//! \return Status of call or error.
//! \retval ERROR_DDI_LDL_LDRIVE_FS_FORMAT_REQUIRED The drive was erased.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::repair()
{
    RtStatus_t status;
    int iPhysBlockNum;

    if (!m_bPresent)
    {
        return ERROR_DDI_LDL_LDRIVE_MEDIA_NOT_ALLOCATED;
    }
    
    // Drive can't be reparied if it is already initialized.
    if (m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_ALREADY_INITIALIZED;
    }

    DdiNandLocker locker;

    // Invalidate sector map entries after a media erase.
    m_media->getNssmManager()->flushAll();
    m_media->getNssmManager()->invalidateAll();
    
    // Loop through all the regions on the media.
    Region::Iterator it = m_media->createRegionIterator();
    Region * pRegion;
    while ((pRegion = it.getNext()))
    {
        NandPhysicalMedia *pNandDesc;
        int iLastBlockNum;

        // Only hidden drive and data drive regions have the data drive
        // blocks we are interested in.
        if (!pRegion->isDataRegion())
        {
            continue;
        }

        pNandDesc = pRegion->m_nand;
        assert(pNandDesc);

        iLastBlockNum = pRegion->m_iStartPhysAddr + pRegion->m_iNumBlks - 1;

#ifdef PROFILE_NAND_DD_REPAIR
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "start region: %d\n", pRegion->m_iStartPhysAddr);
#endif // PROFILE_NAND_DD_REPAIR

        // Loop through all the blocks in this region.
        for (iPhysBlockNum = pRegion->m_iStartPhysAddr; iPhysBlockNum <= iLastBlockNum; iPhysBlockNum++)
        {
            // Check to see if we should erase this block.
            if (shouldRepairEraseBlock(iPhysBlockNum, pNandDesc))
            {
                Block block(BlockAddress(pNandDesc->wChipNumber, iPhysBlockNum));
                if (block.erase() == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    m_media->getMapper()->handleNewBadBlock(block);
                }
            }
            else
            {
#ifdef PROFILE_NAND_DD_REPAIR
                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                  "Skipping: %d\n", iPhysBlockNum);
#endif // PROFILE_NAND_DD_REPAIR
            }
        }
    }

    // Zone-map has been erased.  We need to recreate it.
    if ((status = m_media->getMapper()->rebuild()) != SUCCESS)
    {
        return status;
    }    

    return ERROR_DDI_LDL_LDRIVE_FS_FORMAT_REQUIRED;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Determine if a block should be erased.
//!
//! This function will return true if a block should be erased during the repair
//! process. The block should be erased if it is NOT marked bad, is NOT a hidden
//! drive block, or if the redundant area cannot be read.
//!
//! \param[in]  u32BlockFirstPage Page address of the first page of the block.
//! \param[in]  pNandDesc NAND physical descriptor.
//!
//! \return True if the block should be erased.
////////////////////////////////////////////////////////////////////////////////
bool DataDrive::shouldRepairEraseBlock(uint32_t u32BlockFirstPage, NandPhysicalMedia *pNandDesc)
{
    bool        bIsHiddenBlock;
    RtStatus_t  ReadFailErrorVal;
    bool        bIsBad;

    // If this block is marked bad, it should not be erased.
    Block theBlock(BlockAddress(pNandDesc->wChipNumber, u32BlockFirstPage));
    bIsBad = theBlock.isMarkedBad(NULL, &ReadFailErrorVal);

         // The block did not contain garbage...
    if ( ReadFailErrorVal != ERROR_DDI_NAND_HAL_ECC_FIX_FAILED &&
         // ...and block was marked bad...
         bIsBad )
    {
        // ...so we believe it was bad.  The caller
        // should not erase a bad block.
        #ifdef PROFILE_NAND_DD_REPAIR
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "<bad> ");
        #endif // PROFILE_NAND_DD_REPAIR
        return false;
    }

    // See if this block belongs to a hidden drive. This mapper function doesn't really
    // use the map, it just reads the block's metadata.
    bIsHiddenBlock = isBlockHidden(theBlock);

    #ifdef PROFILE_NAND_DD_REPAIR
    if (bIsHiddenBlock)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "<hidden> ");
    }
    #endif // PROFILE_NAND_DD_REPAIR

    // Return true if this is NOT a hidden block (i.e. it should be erased).
    return !bIsHiddenBlock;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Determines whether or not given block is part of hidden drive.
//!
//! \param[in]  u32BlockPhysAddr  Block number of block to consider.
//!
//! \return TRUE  if Block is part of hidden drive.
//!         FALSE if Block is not part of hidden drive.
//!
////////////////////////////////////////////////////////////////////////////////
bool DataDrive::isBlockHidden(uint32_t u32BlockPhysAddr)
{
    // Create a page object and allocate just an aux buffer.
    Page thePage(BlockAddress(u32BlockPhysAddr).getPage());
    thePage.allocateBuffers(false, true);
    
    // Read the metadata for the first page of the block.
    RtStatus_t retCode = thePage.readMetadata();
    if (retCode != SUCCESS)
    {
        return false;
    }

    // Obviously, the block is not a hidden drive block if it is erased.
    bool bRetVal = false;
    Metadata & md = thePage.getMetadata();
    if (!md.isErased())
    {
        // Read the flag that indicates whether the block belongs to a hidden drive.
        bRetVal = md.isFlagSet(Metadata::kIsHiddenBlockFlag);
    }

    return bRetVal;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
