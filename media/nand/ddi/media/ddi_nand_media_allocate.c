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
//! \file ddi_nand_media_allocate.c
//! \brief This file allocates the drives on the media.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include <string.h>
#include <stdlib.h>
#include <memory>
#include "os/threadx/tx_api.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_ddi.h"
#include "Mapper.h"
#include "ddi_nand_media.h"
#include "DiscoveredBadBlockTable.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/drive_tags.h"
#include "drivers/rtc/ddi_rtc.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace nand {

// For now...
#define MINIMUM_DATA_DRIVE_SIZE 8

typedef struct _NandZipConfigBlockInfo {
    int iNumEntries;
    int iNumReservedBlocks[MAX_NAND_DEVICES];
    nand::NandConfigBlockRegionInfo_t Regions[MAX_NAND_REGIONS];
} NandZipConfigBlockInfo_t;

typedef struct {
    int i1stFreeBlock;
    int iLastFreeBlock;
} ChipAllocations_t;

} // namespace nand

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Allocate the drives on the NAND media.
//!
//! This function will carve up the NAND media into the number of drives
//! specified.  Each drive is a contiguous unit.  There are system drives
//! which are used for storing code and data drives which are used for storing
//! data.  Each drive may be broken into one or more regions.  A region
//! specifies a group of NAND blocks that have common characteristics.  For
//! instance, a data drive may reside on a NAND with 2 planes.  Since each
//! plane only addresses a given group of blocks, the region boundaries must
//! match these boundaries.
//!
//! Allocation performs the following tasks:
//!     - Find the Config Blocks for each chip.  The Config Blocks contain
//!       information about the start of each drive.
//!     - Allocate each drive, adjusting for Bad Blocks by tacking additional
//!       replacement blocks at the beginning or the end depending upon where
//!       the drive is allocated (before or after Data Drive).
//!     - Prepare the Block Descriptor for each chip and then write it out.
//!
//! \param[in]  pTable Pointer to Media Allocation Table structure.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_ERASED
//!
//! \pre    The NANDs have been fully erased, and pNandMediaInfoDesc->pMasterBBTable
//!         contains a list of all bad-blocks on the NANDs.
//!
//! \post The media has been divided into drives and the stored in the Config
//!       Block which is the first good block on each chip.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::allocate(MediaAllocationTable_t * pTable)
{
    int iChipCounter;
    int iAllocCounter;
    int iStartBlockNumber;
    int iNumberBlocksToAllocate;
    uint32_t iBlocksAllocated;
    int iBytesPerBlock;
    unsigned int uSystemDriveBytesPerBlock;
    bool bDataDriveFound = false;
    RtStatus_t Status;
    NandZipConfigBlockInfo_t zipConfigBlock;
    NandConfigBlockRegionInfo_t * zipRegionInfo = NULL;
    ChipAllocations_t ChipAllocs[MAX_NAND_DEVICES];
    uint32_t u32NumBootBlocksUsed;
    NandPhysicalMedia * thisNand;
    bool adjustedOK;
    
    if (m_bInitialized == false)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    if (m_eState != kMediaStateErased)
    {
        // can not be allocated if not erased
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_ERASED;
    }

    // There must be a global bad block table. It should have been allocated by
    // NANDMediaErase() prior to the allocation call.
    assert(m_badBlockTableMode == kNandBadBlockTableAllocationMode);
    
    // Reset the media state until we've succeeded.
    m_eState = kMediaStateUnknown;

    // Lock the NAND for our purposes.
    DdiNandLocker locker;

    // Calculate the number of bytes per block.  This is used throughout
    // the allocation process.
    iBytesPerBlock = m_params->wPagesPerBlock * m_params->pageDataSize;

    {
        int i;
        int iNumFirmwareBlocks;

        // Look through all the Allocation entries to find the Boot drive and then
        // return the number of blocks required for that firmware.
        for (i=0; i < pTable->u32NumEntries; i++)
        {
            if (pTable->Entry[i].u32Tag == DRIVE_TAG_BOOTMANAGER_S)
            {
                // Round up the number of blocks.
                iNumFirmwareBlocks = ROUND_UP_DIV(pTable->Entry[i].u64SizeInBytes, iBytesPerBlock);
                break;
            }
        }

        // This function lays out the boot firmware as if it were to immediately follow
        // the boot blocks, such as the DBBT. Firmware is never actually written to this
        // location. After allocating regions, in the call to ddi_nand_media_WriteBootControlBlockDescriptor(),
        // the boot firmware addresses are set correctly, just before the LDLB is written.
        Status = layoutBootBlocks(iNumFirmwareBlocks, &u32NumBootBlocksUsed);
        if (u32NumBootBlocksUsed == 0)
        {
            return Status;
        }
    }

    // Initialize NandConfigBlockInfo_t structure (header)
    zipConfigBlock.iNumEntries = 0;

    // We must use only 2K sectors for Type 8 Samsung 4K page 128-byte RA, because the
    // 37xx boot ROM cannot shift and mask to reach the second 2K of the 4K page.
    // We also use 2K sectors for firmware regions when using BCH, because the ROM
    // can't always get to 2K subpages other than the first.
    if (m_params->hasSmallFirmwarePages)
    {
        uSystemDriveBytesPerBlock = m_params->wPagesPerBlock * m_params->firmwarePageDataSize;
    }
    else
    {
        uSystemDriveBytesPerBlock = iBytesPerBlock;
    }
    
    // Allocate the prebuilt phymap so we can update it while allocating regions. It is wrapped
    // in auto_ptr so that it will automatically be deleted if we error out. The phymap object
    // inits itself so that all entries are marked as used.
    std::auto_ptr<nand::PhyMap> prebuiltPhymap(new nand::PhyMap);
    prebuiltPhymap->init(m_iTotalBlksInMedia);

    // Find out the config block addresses for all chips
    findConfigBlocks();

    // Initialize Chip Allocation Tracking
    for (iChipCounter = 0 ; iChipCounter < NandHal::getChipSelectCount() ; iChipCounter++)
    {
        // Note that the use of the primary and secondary firmware addresses is simply
        // a method of finding out how many boot blocks were allocated on those chip
        // enables. In fact, the firmware may not even be placed at the addresses used
        // below. Those addresses were chosen by LayoutBootBlocks, not the actual
        // allocation code. When allocation is complete, those addresses will be updated.
        if (iChipCounter == 0)
        {
            // Always allocate the primary firmware at the first chip.
            ChipAllocs[iChipCounter].i1stFreeBlock = (m_bootBlocks.m_primaryFirmware.b.bfBlockAddress);
            // Otherwise we have 2 or more, so we only need to worry about 1 and 2.
        }
        else if (iChipCounter == 1)
        {
            // In this case, we can use the secondary FW block.
            ChipAllocs[iChipCounter].i1stFreeBlock = (m_bootBlocks.m_secondaryFirmware.b.bfBlockAddress);
        }
        else
        {
            ChipAllocs[iChipCounter].i1stFreeBlock = m_ConfigBlkAddr[iChipCounter] + 1;
        }

        // Right at the end
        ChipAllocs[iChipCounter].iLastFreeBlock = NandHal::getNand(iChipCounter)->wTotalBlocks - 1;
        
        // Create the boot region for this chip enable.
        // If this assert hits, we're overflowing the regions map.  Investigate whether
        // adding more regions is necessary.
        assert(zipConfigBlock.iNumEntries < MAX_NAND_REGIONS);

        // We have all parameters for this drive => Update the Region Structure
        zipRegionInfo = &zipConfigBlock.Regions[zipConfigBlock.iNumEntries];

        zipRegionInfo->iChip = iChipCounter;
        zipRegionInfo->iNumBlks = ChipAllocs[iChipCounter].i1stFreeBlock;
        zipRegionInfo->iStartBlock = 0;
        zipRegionInfo->eDriveType = kDriveTypeUnknown;
        zipRegionInfo->wTag = NandConfigBlockRegionInfo_t::kBootRegionTag;

        zipConfigBlock.iNumEntries++;
    }

    // Switch to the last chip.
    iChipCounter = NandHal::getChipSelectCount() - 1;
    iStartBlockNumber = ChipAllocs[iChipCounter].i1stFreeBlock;

    // If we found hidden drive entries, allocate them to be the last drives physically on
    // the media. System drives have to come first on a given chip enable to work around a
    // ROM bug that prevents it from seeing bad blocks beyond block 325. The only restriction
    // is that all hidden drives must fit on the last chip.
    for (iAllocCounter = 0 ; iAllocCounter < pTable->u32NumEntries ; iAllocCounter++)
    {
        if (pTable->Entry[iAllocCounter].Type == kDriveTypeHidden)
        {
            // Allocates System Drives From Begin of Media
            iNumberBlocksToAllocate = ROUND_UP_DIV(pTable->Entry[iAllocCounter].u64SizeInBytes, iBytesPerBlock);
            if (iNumberBlocksToAllocate == 0)
            {
                iNumberBlocksToAllocate = MINIMUM_DATA_DRIVE_SIZE;
            }

            // Push this drive to the end of the chip.
            iStartBlockNumber = ChipAllocs[iChipCounter].iLastFreeBlock - iNumberBlocksToAllocate + 1;
            
            if (iStartBlockNumber < ChipAllocs[iChipCounter].i1stFreeBlock)
            {
                return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
            }

            thisNand = NandHal::getNand(iChipCounter);
            
            BlockAddress startBlock(iChipCounter, iStartBlockNumber);
            iBlocksAllocated = iNumberBlocksToAllocate;
            
            // startBlock and iBlockAllocated are modified.
            adjustedOK = m_globalBadBlockTable.adjustForBadBlocksInRange(startBlock, iBlocksAllocated, BadBlockTable::kGrowDown);

            // Update start block after adjustment.
            iStartBlockNumber = startBlock.getRelativeBlock();

            if (!adjustedOK
                || iBlocksAllocated > thisNand->wTotalBlocks
                || startBlock <= ChipAllocs[iChipCounter].i1stFreeBlock)
            {
                // Could Not Find Enough Good Blocks
                return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
            }
            
            // If this assert hits, we're overflowing the regions map.  Investigate whether
            // adding more regions is necessary.
            assert(zipConfigBlock.iNumEntries < MAX_NAND_REGIONS);

            // We have all parameters for this drive => Update the Region Structure
            zipRegionInfo = &zipConfigBlock.Regions[zipConfigBlock.iNumEntries];

            zipRegionInfo->iChip      = iChipCounter;
            zipRegionInfo->iNumBlks   = iBlocksAllocated;
            zipRegionInfo->iStartBlock  = iStartBlockNumber;
            zipRegionInfo->eDriveType = pTable->Entry[iAllocCounter].Type;
            zipRegionInfo->wTag       = pTable->Entry[iAllocCounter].u32Tag;

            // Mark the blocks in this region as free. We'll deal with bad blocks at the end.
            prebuiltPhymap->markRange(startBlock, iBlocksAllocated, PhyMap::kFree);

            zipConfigBlock.iNumEntries++;

            // Update end of this chip.
            ChipAllocs[iChipCounter].iLastFreeBlock = iStartBlockNumber - 1;
        }
    }

    // Move back to beginning of the first chip.
    iChipCounter = 0;
    iStartBlockNumber = ChipAllocs[0].i1stFreeBlock;

    // There is a difference between System and Data Drives
    // A System Drive Entry is completely defined in the Allocation Table Entry
    // A Data Drive System does not have a real size because the size is
    // the remainder of the media once all system drives were allocated
    // Therefore, the data drive if exists, will be dealt with at the end
    // of the loop

    // Allocate System Drives Located Before Data Drive
    // These Drives will be allocated starting from the begining of the media
    for (iAllocCounter = 0 ; iAllocCounter < pTable->u32NumEntries ; iAllocCounter++)
    {
        if( pTable->Entry[iAllocCounter].Type == kDriveTypeData )
        {
            // At this point a Data Drive was found to allocate from the MediaAllocation
            // table. If the current chip is the last one (i.e., only 1 chip enable),
            // we need to keep allocating
            // the remaining drives from the last allocated block in the current chip.
            //
            // Otherwise, the rest of the chip is automatically reserved for data drive
            // and the next system drive to allocate, if needed, must be allocated
            // in the second chip and the 1st free block to allocate is the block
            // after the secondary BCBs block. We have to allocate from the second chip
            // because the 37xx ROM cannot use chip enables 3 and 4.

            // We found the Data Drive. Note this fact and switch chips.
            bDataDriveFound = true;

            // If there are multiple chip enables and we've not already moved onto the last
            // chip, then place the second group of system drives on the second chip enable.
            if (!(iChipCounter == (NandHal::getChipSelectCount() - 1) || iChipCounter == 1))
            {
                // We were not previously allocating drives in last chip, so switch to
                // another chip. The start of free blocks is after boot blocks.
                // We can never be in this branch if there is only one chip.
                if (iChipCounter == 0)
                {
                    iChipCounter = 1;
                }
                else
                {
                    // Already filled up past the second chip (i.e., chip 0) (we trapped
                    // chip enable 1 above), so allocate the post-data drive system drives on the
                    // last chip. The 37xx ROM won't be able to access any of these drives since
                    // it only supports the first two chips, but the SDK will still work fine.
                    iChipCounter = NandHal::getChipSelectCount() - 1;
                }

                iStartBlockNumber = ChipAllocs[iChipCounter].i1stFreeBlock;
            }

        }
        else if (pTable->Entry[iAllocCounter].Type == kDriveTypeHidden)
        {
            // Skip hidden drives
            continue;
        }
        else if (pTable->Entry[iAllocCounter].Type == kDriveTypeSystem)
        {
            // Allocates System Drives From Begin of Media
            iNumberBlocksToAllocate = ROUND_UP_DIV(pTable->Entry[iAllocCounter].u64SizeInBytes, uSystemDriveBytesPerBlock);
            
            // Add some extra room to deal with future bad blocks, with a minimum of 1 extra.
            iNumberBlocksToAllocate += (iNumberBlocksToAllocate * m_params->maxBadBlockPercentage + 99) / 100;

            // Check if enough space in the chip
            if ((iStartBlockNumber + iNumberBlocksToAllocate) > ChipAllocs[iChipCounter].iLastFreeBlock)
            {
                iChipCounter++;

                // Need to get next chip
                if(iChipCounter >= NandHal::getChipSelectCount())
                {
                    // No more chips available
                    return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
                }
                iStartBlockNumber = ChipAllocs[iChipCounter].i1stFreeBlock;      // Points to the next 1st available block

                if ((iStartBlockNumber + iNumberBlocksToAllocate) > ChipAllocs[iChipCounter].iLastFreeBlock)
                {
                    // System Drive is too big to fit in one chip
                    return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
                }
            } // End If Check Enough Space In Current Chip

            thisNand = NandHal::getNand(iChipCounter);
            
            BlockAddress startBlock(iChipCounter, iStartBlockNumber);
            iBlocksAllocated = iNumberBlocksToAllocate;
            
            // startBlock and iBlockAllocated are modified.
            adjustedOK = m_globalBadBlockTable.adjustForBadBlocksInRange(startBlock, iBlocksAllocated, BadBlockTable::kGrowUp);

            iStartBlockNumber = startBlock.getRelativeBlock();

            if (!adjustedOK
            || iStartBlockNumber + iBlocksAllocated > ChipAllocs[iChipCounter].iLastFreeBlock)
            {
                // Could Not Find Enough Good Blocks
                return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
            }

            // If this assert hits, we're overflowing the regions map.  Investigate whether
            // adding more regions is necessary.
            assert(zipConfigBlock.iNumEntries < MAX_NAND_REGIONS);

            // We have all parameters for this drive => Update the Region Structure
            zipRegionInfo = &zipConfigBlock.Regions[zipConfigBlock.iNumEntries];

            zipRegionInfo->iChip = iChipCounter;
            zipRegionInfo->iNumBlks = iBlocksAllocated;
            zipRegionInfo->iStartBlock = iStartBlockNumber;
            zipRegionInfo->eDriveType = pTable->Entry[iAllocCounter].Type;
            zipRegionInfo->wTag = pTable->Entry[iAllocCounter].u32Tag;

            zipConfigBlock.iNumEntries++;

            // Compute new Start Block Number
            iStartBlockNumber += iBlocksAllocated;

            ChipAllocs[iChipCounter].i1stFreeBlock = iStartBlockNumber;
        }
    }

    // A Data Drive was found.
    if (bDataDriveFound == true)
    {
        // Now we deal with the Data Drive
        // For each chip we need to :
        //   1. Find Out the free memory
        //   2. Divide the free memory into regions
        for (iChipCounter = 0 ; iChipCounter < NandHal::getChipSelectCount() ; iChipCounter++)
        {
            iBlocksAllocated = 0;     // Track the number of blocks allocated on this chip.
            thisNand = NandHal::getNand(iChipCounter);
            uint32_t u32PlaneMask = thisNand->pNANDParams->planesPerDie - 1;
            
            int iDie,iBlockAlign;
            for (iDie=0; iDie < thisNand->wTotalInternalDice; iDie++)
            {
                // Determine 1st Free Block on this Die
                int iStartOfDie = iDie * thisNand->wBlocksPerDie;
                int i1stFreeBlock = std::max(iStartOfDie, ChipAllocs[iChipCounter].i1stFreeBlock);

                // Determine Last Free Block on this Die
                int iEndOfDie = (iDie+1) * thisNand->wBlocksPerDie - 1;
                int iLastFreeBlock = std::min(iEndOfDie, ChipAllocs[iChipCounter].iLastFreeBlock);

                int iNumFreeBlocks = iLastFreeBlock - i1stFreeBlock + 1;
                // Align blocks in region to plane boundary.
                iBlockAlign           = (iNumFreeBlocks & u32PlaneMask);        
                iNumFreeBlocks       -= iBlockAlign;        
                iLastFreeBlock       -= iBlockAlign;

                if (iNumFreeBlocks < MINIMUM_DATA_DRIVE_SIZE)
                {
                    continue;    // Skip this Die - there's not enough room
                }

                // If this assert hits, we're overflowing the regions map.  Investigate whether
                // adding more regions is necessary.
                assert(zipConfigBlock.iNumEntries < MAX_NAND_REGIONS);

                // Create Region from 1st to Last Free Blocks on this Die
                // Allocate the 1st region
                zipRegionInfo =
                  &zipConfigBlock.Regions[zipConfigBlock.iNumEntries];

                zipRegionInfo->iChip = iChipCounter;
                zipRegionInfo->eDriveType = kDriveTypeData;
                zipRegionInfo->wTag = DRIVE_TAG_DATA;
                zipRegionInfo->iStartBlock = i1stFreeBlock;
                zipRegionInfo->iNumBlks = iNumFreeBlocks;
                
                // Mark the blocks in this region as free. We'll deal with bad blocks at the end.
                prebuiltPhymap->markRange(thisNand->baseAbsoluteBlock() + i1stFreeBlock, iNumFreeBlocks, PhyMap::kFree);

                zipConfigBlock.iNumEntries++;

                // Keep track of how many blocks are allocated to the Data Drive.
                iBlocksAllocated += iNumFreeBlocks;

                ChipAllocs[iChipCounter].i1stFreeBlock = iLastFreeBlock + 1;
            }

            // Now that we know the size, let's calculate the number of
            // reserved blocks.
            zipConfigBlock.iNumReservedBlocks[iChipCounter] = (iBlocksAllocated * NandHal::getParameters().maxBadBlockPercentage / 100) + 1;
        }
    }   // End if(bDataDriveFound == true)

    // Allocate temporary sector and auxiliary buffers.
    SectorBuffer sectorBuffer;
    if ((Status = sectorBuffer.acquire()) != SUCCESS)
    {
        return Status;
    }

    AuxiliaryBuffer auxBuffer;
    if ((Status = auxBuffer.acquire()) != SUCCESS)
    {
        return Status;
    }

    // Write the NCBs and LDLBs. Note that these are written to ALL necessary chips in
    // this function, not just chip 0.
    Status = writeBootControlBlockDescriptor(&zipConfigBlock, sectorBuffer, auxBuffer);
    if (Status != SUCCESS)
    {
        return(Status);
    }
    
    Region * region;

    // Now write the config block for each chip.
    for (iChipCounter = 0 ; iChipCounter < NandHal::getChipSelectCount() ; iChipCounter++)
    {
        // Prepare the sectorBuffer with the contents of the config block.
        prepareBlockDescriptor(iChipCounter, &zipConfigBlock, sectorBuffer, auxBuffer);

        // For the second and third chip enables, we have to write to the page(s) prior to the
        // one containing the config page in order to keep the NAND happy. i.e., must write
        // pages sequentially from 0->N within a block. So, we just write another copy of the
        // config page since we already have it available.
        //
        // Since the config page is stored in the LDLB block for chip enables 0 and 1, the
        // LDLB itself is already written to page 0.
        if (iChipCounter >= 2)
        {
            // Write a copy of the config page out to page 0.

            BootPage page(PageAddress(iChipCounter, m_ConfigBlkAddr[iChipCounter], 0));
            page.setBuffers(sectorBuffer, auxBuffer);
            Status = page.writeAndMarkOnFailure();
            if (Status != SUCCESS)
            {
                if (Status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    region = getRegionForBlock(page.getBlock());
                    if (region)
                    {
                        region->addNewBadBlock(page.getBlock());
                    }
                }
                
                return Status;
            }
        }

        // Write the config page.
        BootPage page(PageAddress(iChipCounter, m_ConfigBlkAddr[iChipCounter], CONFIG_BLOCK_SECTOR_OFFSET));
        page.setBuffers(sectorBuffer, auxBuffer);
        Status = page.writeAndMarkOnFailure();
        if (Status != SUCCESS)
        {
            if (Status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                region = getRegionForBlock(page.getBlock());
                if (region)
                {
                    region->addNewBadBlock(page.getBlock());
                }
            }
            
            return Status;
        }
    }
    
    // This is a special case to handle single chip enable systems, where both LDLB1
    // and LDLB2 reside on the same chip. Because the config page is always read from the
    // block containing the LDLB, it is possible for the NAND driver to attempt to read
    // the config page from LDLB2, if LDLB1 could not be located/read for some reason.
    // So, we need to write a second copy of the config page into the LDLB2 block. At this
    // point, the sectorBuffer still contains the config page contents, so we don't need
    // to prepare it again.
    if (NandHal::getChipSelectCount() == 1)
    {
        assert(m_bootBlocks.m_ldlb2.b.bfNANDNumber == 0);
        
        // LDLB2 is already written to page 0 of the LDLB2 block, so we can go ahead
        // and write to page 1 without worry.
        BootPage page(PageAddress(m_bootBlocks.m_ldlb2.b.bfNANDNumber, m_bootBlocks.m_ldlb2.b.bfBlockAddress, CONFIG_BLOCK_SECTOR_OFFSET));
        page.setBuffers(sectorBuffer, auxBuffer);
        Status = page.writeAndMarkOnFailure();
        if (Status != SUCCESS)
        {
            if (Status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                region = getRegionForBlock(page.getBlock());
                if (region)
                {
                    region->addNewBadBlock(page.getBlock());
                }
            }
            
            return Status;
        }
    }

    // Erase the DBBT because it may be incorrect. At the next discover call
    // we'll build a valid table, then write the DBBT.
    DiscoveredBadBlockTable dbbt(this);
    dbbt.erase();

    // Free the temp buffers.
    sectorBuffer.release();
    auxBuffer.release();
    
    // We're done allocating!
    m_eState = kMediaStateAllocated;
    
    // Clear the persistent bits set by the ROM when something is wrong with the NAND, to ensure
    // that the next boot is from the primary firmware drive and that we don't needlessly
    // recover the firmware.
    ddi_rtc_WritePersistentField(RTC_NAND_SECONDARY_BOOT, 0);
    ddi_rtc_WritePersistentField(RTC_NAND_SDK_BLOCK_REWRITE, 0);

    // Mark bad blocks as used in the prebuilt phymap. Then pass the prebuilt phymap along to the
    // mapper, so it doesn't have to scan the entire NAND once again in order to build the phymap.
    updatePhymapWithBadBlocks(prebuiltPhymap.get());
    getMapper()->setPrebuiltPhymap(prebuiltPhymap.release());

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Find the Config Blocks on each chip.
//!
//! This function finds the address of the Config Block for each chip of the
//! media.  It also saves the results in the iConfigBlkPhysAddr array which
//! is an entry of NandMediaInfo_t.
////////////////////////////////////////////////////////////////////////////////
void Media::findConfigBlocks()
{
    int iChipCounter;

    // In the case of the 3700, the Config Block is the same as the LDLB block so it will get
    // filled in as part of the ddi_nand_media_LayoutBootBlocks() routine.
    for (iChipCounter = 2 ; iChipCounter < NandHal::getChipSelectCount() ; iChipCounter++)
    {
        // Search in this chip's bad-block table for the first good block.
        // That good block will be the config block.
        m_ConfigBlkAddr[iChipCounter] = m_globalBadBlockTable.skipBadBlocks(NandHal::getNand(iChipCounter)->m_firstAbsoluteBlock).getRelativeBlock();
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Find the next region in the chip.
//!
//! This function finds the allocated region that immediately follows the
//! block number passed in as an argument.
//!
//! \param[in]  iChip Chip Number
//! \param[out]  pNandZipConfigBlockInfo Pointer to NandZipConfigBlockInfo_t
//!              structure. This structure has all the regions information for
//!              the entire media.
//! \param[in]  iBlock Any block number allocated in the  preceeding region.
//!
//! \return Region Number if exists, else returns -1.
////////////////////////////////////////////////////////////////////////////////
int Media::findNextRegionInChip(int iChip, int iBlock,
                NandZipConfigBlockInfo_t * pNandZipConfigBlockInfo)
{
    int i;
    int iRegion = -1, iUpperLimitBlock = 0x7fffffff;

    for(i = 0 ; i < pNandZipConfigBlockInfo->iNumEntries ; i++)
    {
        if(pNandZipConfigBlockInfo->Regions[i].iChip == iChip)
        {
            if(pNandZipConfigBlockInfo->Regions[i].iStartBlock > iBlock)
            {
                // This Region is a good candidate
                if(pNandZipConfigBlockInfo->Regions[i].iStartBlock < iUpperLimitBlock)
                {
                    iUpperLimitBlock = pNandZipConfigBlockInfo->Regions[i].iStartBlock;
                    iRegion = i;
                }
            } // EndIf
        } // EndIf Region not in current chip
    } // End For looping thru all regions of media

    return(iRegion);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Prepare the Block Descriptor.
//!
//! This function prepares the Block descriptor for the specified chip
//! (not the first chip) by filling in the appropriate fields containing
//! region allocation information.
//!
//! \param[in]  iChip Chip Number
//! \param[in]  pNandZipConfigBlockInfo Pointer to NandZipConfigBlockInfo_t
//!              structure. This structure has all the regions information
//!              for the entire media.
//! \param[in,out]  pSectorBuffer The buffer to which the NandConfigBlockInfo_t structure
//!                                         is written.
//! \param[in,out]  pAuxBuffer Auxilliary buffer for pSectorBuffer.
////////////////////////////////////////////////////////////////////////////////
void Media::prepareBlockDescriptor(int iChip,
        NandZipConfigBlockInfo_t * pNandZipConfigBlockInfo,
        SECTOR_BUFFER * pSectorBuffer, SECTOR_BUFFER * pAuxBuffer)
{

    int iNumRegionsOnThisChip = 0;
    int iRegion;
    int iLastBlockFound;
    NandConfigBlockInfo_t * pNandCfgBlkInfo;
    NandConfigBlockRegionInfo_t *pNandCfgBlkRegionInfo;
    NandConfigBlockRegionInfo_t * pNandZipCfgBlkRegionInfo = pNandZipConfigBlockInfo->Regions;

    pNandCfgBlkInfo = (NandConfigBlockInfo_t *)pSectorBuffer;
    pNandCfgBlkRegionInfo = pNandCfgBlkInfo->Regions;

    // First Initialize the buffer to 0xffffff
    memset(pSectorBuffer, 0xff, m_params->pageDataSize);

    // There's never a Region at Block 0
    iLastBlockFound= 0;

    while ((iRegion = findNextRegionInChip(iChip, iLastBlockFound, pNandZipConfigBlockInfo)) >= 0)
    {
        pNandCfgBlkRegionInfo[iNumRegionsOnThisChip].eDriveType =
            pNandZipCfgBlkRegionInfo[iRegion].eDriveType;

        pNandCfgBlkRegionInfo[iNumRegionsOnThisChip].wTag =
            pNandZipCfgBlkRegionInfo[iRegion].wTag;

        pNandCfgBlkRegionInfo[iNumRegionsOnThisChip].iNumBlks =
            pNandZipCfgBlkRegionInfo[iRegion].iNumBlks;

        pNandCfgBlkRegionInfo[iNumRegionsOnThisChip].iChip =
            pNandZipCfgBlkRegionInfo[iRegion].iChip;

        pNandCfgBlkRegionInfo[iNumRegionsOnThisChip].iStartBlock =
            pNandZipCfgBlkRegionInfo[iRegion].iStartBlock;


        iLastBlockFound = pNandZipCfgBlkRegionInfo[iRegion].iStartBlock;

        // Increment the number of regions for the current chip
        iNumRegionsOnThisChip++;
    }

    // Fill In the rest of the NandConfigBlockInfo_t
    pNandCfgBlkInfo->iMagicCookie = NAND_CONFIG_BLOCK_MAGIC_COOKIE;
    pNandCfgBlkInfo->iVersionNum = NAND_CONFIG_BLOCK_VERSION;
    pNandCfgBlkInfo->iNumRegions = iNumRegionsOnThisChip;
    pNandCfgBlkInfo->iNumReservedBlocks = pNandZipConfigBlockInfo->iNumReservedBlocks[iChip];

    // Initialize the redundant area.
    nand::Metadata md(pAuxBuffer);
    md.prepare(BCB_SPACE_TAG);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Prepare and write all NCB and LDLB Boot Control Blocks.
//!
//! This function finds (in the the local NandZipConfigBlockInfo_t structure in RAM)
//! the block-addresses of the primary and secondary firmware images.
//! It copies these block-addresses into the NandMediaInfo_t structure, and then
//! passes this structure to functions that write the NCBs and LDLBs.
//!
//! \param[in]  pNandZipConfigBlockInfo Pointer to NandZipConfigBlockInfo_t
//!              structure. This structure has all the regions information
//!              for the entire media.
//! \param[in]  pNANDMediaInfo Pointer to NandMediaInfo_t structure.
//!
//! \return Status of write.
//!
//! \note  The block descriptor is prepared using the \a pSectorBuffer
//!         buffer which is passed into this function.
//!
//! \internal
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::writeBootControlBlockDescriptor(NandZipConfigBlockInfo_t * pNandZipConfigBlockInfo,
        SECTOR_BUFFER * pSectorBuffer, SECTOR_BUFFER * pAuxBuffer)
{
    RtStatus_t Status;
    int iRegion;
    NandConfigBlockRegionInfo_t * pNandZipCfgBlkRegionInfo = pNandZipConfigBlockInfo->Regions;

    // First Initialize the buffer to 0xffffffff
    memset(pSectorBuffer, 0xff, m_params->pageDataSize);

    {
        unsigned maxBlockCount = 0;

        // First clear primary and secondary firmware addresses, in
        // case we don't find them in the region table.
        m_bootBlocks.m_primaryFirmware.b.bfNANDNumber = 0;
        m_bootBlocks.m_primaryFirmware.b.bfBlockAddress = 0;
        m_bootBlocks.m_primaryFirmware.b.bfBlockProblem = kNandBootBlockInvalid;

        m_bootBlocks.m_secondaryFirmware.b.bfNANDNumber = 0;
        m_bootBlocks.m_secondaryFirmware.b.bfBlockAddress = 0;
        m_bootBlocks.m_secondaryFirmware.b.bfBlockProblem = kNandBootBlockInvalid;

        // The BCB needs to know the sector where the bootmanager is located.  Currently,
        // the bootmanager is indicated as 0x50 ('P').  Search the entries until bootmanger
        // is found.
        for (iRegion=0; iRegion < pNandZipConfigBlockInfo->iNumEntries; iRegion++)
        {
            // Skip non-system drive regions.
            if (pNandZipCfgBlkRegionInfo[iRegion].eDriveType != kDriveTypeSystem)
            {
                continue;
            }

            // Search for the BootManager tag.
            if (pNandZipCfgBlkRegionInfo[iRegion].wTag == DRIVE_TAG_BOOTMANAGER_S)
            {
                // Set the primary Firmware sector information.
                m_bootBlocks.m_primaryFirmware.b.bfNANDNumber = pNandZipCfgBlkRegionInfo[iRegion].iChip;
                m_bootBlocks.m_primaryFirmware.b.bfBlockAddress = pNandZipCfgBlkRegionInfo[iRegion].iStartBlock;
                m_bootBlocks.m_primaryFirmware.b.bfBlockProblem = kNandBootBlockValid;

                // Update maximum firmware blocks.
                if (pNandZipCfgBlkRegionInfo[iRegion].iNumBlks > maxBlockCount)
                {
                    maxBlockCount = pNandZipCfgBlkRegionInfo[iRegion].iNumBlks;
                }
            }
            // Search for the Secondary BootManager tag.
            else if (pNandZipCfgBlkRegionInfo[iRegion].wTag == DRIVE_TAG_BOOTMANAGER2_S)
            {
                // Set the secondary Firmware sector information.
                m_bootBlocks.m_secondaryFirmware.b.bfNANDNumber = pNandZipCfgBlkRegionInfo[iRegion].iChip;
                m_bootBlocks.m_secondaryFirmware.b.bfBlockAddress = pNandZipCfgBlkRegionInfo[iRegion].iStartBlock;
                m_bootBlocks.m_secondaryFirmware.b.bfBlockProblem = kNandBootBlockValid;

                // Update maximum firmware blocks.
                if (pNandZipCfgBlkRegionInfo[iRegion].iNumBlks > maxBlockCount)
                {
                    maxBlockCount = pNandZipCfgBlkRegionInfo[iRegion].iNumBlks;
                }
            }
        }

        // Only write the NCBs if either one does not already exist.
        if ( m_bootBlocks.m_ncb1.b.bfBlockProblem != kNandBootBlockValid ||
             m_bootBlocks.m_ncb2.b.bfBlockProblem != kNandBootBlockValid )
        {
            // Bail if writing the NCBs fails, since that will make the device unbootable.
            Status = writeNCB(ddi_gpmi_get_current_timings(), pSectorBuffer, pAuxBuffer);
            if (Status != SUCCESS)
            {
                return Status;
            }
        }

        // Always write the LDLBs.
        Status = writeLDLB(maxBlockCount, pSectorBuffer, pAuxBuffer);
    }

    return Status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Marks bad blocks in all chips as used in a phymap.
//!
//! \param phymap The phymap instance to modify.
//! \param mediaInfo Media info containing the global bad block table. The bad
//!     block table must be in allocation mode for this function to work.
//!
//! \retval SUCCESS The phymap was updated successfully.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::updatePhymapWithBadBlocks(PhyMap * phymap)
{
    assert(m_badBlockTableMode == kNandBadBlockTableAllocationMode);
    
    uint32_t i;
    for (i=0; i < m_globalBadBlockTable.getCount(); ++i)
    {
         phymap->markBlockUsed(m_globalBadBlockTable[i]);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
