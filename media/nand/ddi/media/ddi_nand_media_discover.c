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
//! \file ddi_nand_media_discover.c
//! \brief This file discovers the allocated drives on the media.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_ddi.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand_media.h"
#include "DiscoveredBadBlockTable.h"
#include "ddi_nand_data_drive.h"
#include "ddi_nand_system_drive.h"
#include "drivers/media/nand/rom_support/rom_nand_boot_blocks.h"
#include "os/threadx/tx_api.h"
#include "drivers/rtc/ddi_rtc.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "components/telemetry/tss_logtext.h"
#include "hw/profile/hw_profile.h"
#include <stdlib.h>

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Discover the allocation of drives on the NAND media.
//!
//! This function will determine the partitions that the drives have been
//! allocated to.  Each drive is a contiguous unit.  There are system drives
//! which are used for storing code and data drives which are used for storing
//! data.  Each drive may be broken into one or more regions.  A region
//! specifies a group of NAND blocks that have common characteristics.  For
//! instance, a data drive may reside on a NAND with 2 planes.  Since each
//! plane only addresses a given group of blocks, the region boundaries must
//! match these boundaries.
//! Discovery requires the following tasks:
//!     - Read the Config Block for each chip.
//!     - Using the stored Config Block Structure, reconstruct each region
//!       of the NAND.
//!     - Rebuild the Bad Block Table in RAM.  This may happen in two ways:
//!         1.  DBBT exists on the NAND: Read the DBBT to form the bad-block
//!             table in RAM.
//!             This case occurs during a normal firmware boot.
//!         2.  DBBT does not exist on the NAND: Scan the NAND itself to
//!             rebuild the Bad Block Table.  Also save the DBBT to the NAND.
//!             This case occurs if NANDMediaAllocate() was called first, which
//!             is the situation during a firmware update.
//!     - Fill in the Drive information for all the NANDs.
//!
//! \return Status of call or error.
//!
//! \post The media has been partitioned into drives and is almost ready for
//!         use (each drive must be initialized).
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::discover()
{
    return discover(true /*bWriteToTheDevice*/ );
}


////////////////////////////////////////////////////////////////////////////////
//! \brief      Same as NANDMediaDiscoverAllocation, but allows the caller to choose whether to write to the storage device.
//!
//! See the documentation for NANDMediaDiscoverAllocation.
//!
//! \param[in]  pDescriptor         Pointer to the Logical Drive Descriptor.
//! \param[in]  bWriteToTheDevice   "true" value gives this function permission to write
//!                                 to the device if necessary, e.g. to recover a corrupt table.
//!                                 "False" means "don't write on the NAND", and also implies
//!                                 that this function is being called to print verbose information
//!                                 as it runs.
//!
//! \return Status of call or error.
//!
//! \post The media has been partitioned into drives and is almost ready for
//!         use (each drive must be initialized).
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::discover(bool bWriteToTheDevice)
{
    RtStatus_t Status;

    if (m_bInitialized == false)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    if (m_eState == kMediaStateErased)
    {
        // can not be discovered if erased
        return ERROR_DDI_LDL_LMEDIA_MEDIA_ERASED;
    }

    // Autolock the driver.
    DdiNandLocker locker;

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

    // Initalize some of the entries in the NandMediaInfo_t
    m_iNumRegions = 0;
    m_iNumBadBlks = 0;
    m_iNumReservedBlocks = 0;

    // The STMP3700 has boot control blocks on the NAND that are used for
    // booting:
    //      NCB
    //      LDLB + Config Block
    //      DBBT
    // This function finds and loads the contents of the NCB and LDLB
    // into the global NandMediaInfo structure.
    // In contrast, the Config Block and DBBT are only found but not used
    // to initialize anything.
    findBootControlBlocks(sectorBuffer, auxBuffer, (bWriteToTheDevice ? kAllowRecovery:kDontAllowRecovery));

    // Read the config block for all chips making up the media.
    int iChipCounter;
    for (iChipCounter = 0 ; iChipCounter < NandHal::getChipSelectCount(); iChipCounter++)
    {
        // Find the Configuration Block (LDLB) and read it into sectorBuffer
        Status = getConfigBlock1stSector(NandHal::getNand(iChipCounter),
            &(m_ConfigBlkAddr[iChipCounter]),
            true, sectorBuffer, auxBuffer);

        if (Status != SUCCESS)
        {
            // Fail to find a valid config block in at least one of the medias
            m_eState = kMediaStateUnknown;
            return Status;
        }

        // Using the global buffer that now holds the config block, grab the data
        // that covers all the regions.
        NandConfigBlockInfo_t * configBlockInfo = (NandConfigBlockInfo_t *)sectorBuffer.getBuffer();
        int iNumRegionsInChip = configBlockInfo->iNumRegions;
        m_iNumReservedBlocks += configBlockInfo->iNumReservedBlocks;

        // Allocate and init the regions described in the config block.
        int iRegionCounter;
        for (iRegionCounter = 0 ; iRegionCounter < iNumRegionsInChip ; iRegionCounter++)
        {
            NandConfigBlockRegionInfo_t * regionInfo = &configBlockInfo->Regions[iRegionCounter];
            
            // Create a region object of the required type.
            Region * newRegion = Region::create(regionInfo);
            assert(newRegion);
            
            // Add the region into the region array.
            assert(m_iNumRegions < MAX_NAND_REGIONS);
            m_pRegionInfo[m_iNumRegions++] = newRegion;
        }
    }

    // Make sure we found some Regions
    if (!m_iNumRegions)
    {
        // No Regions found in the media
        m_eState = kMediaStateUnknown;
        return ERROR_DDI_NAND_LMEDIA_NO_REGIONS_IN_MEDIA;
    }
    
    // Fill in the Bad Block Tables for all Regions. If we just allocated the media and
    // have the allocation mode bad block table (created by NandMediaErase()) available, then use
    // it instead of scanning for bad blocks all over again. In this mode, we know that there
    // won't be a DBBT, since NandMediaAllocate() makes sure of that.
    if (m_eState == kMediaStateAllocated && m_badBlockTableMode == kNandBadBlockTableAllocationMode)
    {
        // This function requires that the bad block table mode is allocation when it is called,
        // so we can't change the mode yet. But the function will change it for us when it
        // is done converting.
        Status = fillInBadBlocksFromAllocationModeTable(sectorBuffer, auxBuffer);
    }
    else
    {
        // Go ahead and switch the table mode to discovery.
        m_badBlockTableMode = kNandBadBlockTableDiscoveryMode;
        Status = fillInNandBadBlocksInfo(sectorBuffer, auxBuffer, 0 /*attempt*/, bWriteToTheDevice );
    }

    if (Status != SUCCESS)
    {
        m_eState = kMediaStateUnknown;
        return Status;
    }

    // Now, instantiate the NAND drives described by the regions.
    Status = createDrives();
    if (Status != SUCCESS)
    {
        m_eState = kMediaStateUnknown;
        return Status;
    }

    m_eState = kMediaStateAllocated;

    return SUCCESS;
}

Region * Region::create(NandConfigBlockRegionInfo_t * info)
{
    Region * newRegion;
    
    switch (info->eDriveType)
    {
        case kDriveTypeData:
        case kDriveTypeHidden:
            newRegion = new DataRegion;
            break;
        
        case kDriveTypeSystem:
            newRegion = new SystemRegion;
            break;
        
        case kDriveTypeUnknown:
            // Check if this region has the special boot region tag value.
            if (info->wTag == NandConfigBlockRegionInfo_t::kBootRegionTag)
            {
                newRegion = new BootRegion;
            }
            // else, fall through...
        
        default:
            // Don't know what to do with this drive type!
            assert(0);
            return NULL;
    };
    
    // Now, init the new region.
    assert(newRegion);
    newRegion->initFromConfigBlock(info);
    return newRegion;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Find the Configuration Block (LDLB) on a NAND.
//!
//! This function will find the configuration Block which is the first good
//! block on the NAND.  No data is loaded from the config block in this
//! function.
//!
//! \param[in]  pNandPhysicalMediaDesc Pointer to the NAND physical media
//!             descriptor.
//! \param[out]  piConfigBlockPhysAdd Address to save resulting Config Block
//!             Address.
//! \param[in]  bConfirmConfigBlock If true, the 1st good block is checked
//!             against the config block rules.  If false, the caller does
//!             not care if the block is a config block or not.
//! \param[in] sectorBuffer Buffer to use for reading from the media.
//! \param[in] auxBuffer Auxiliary buffer to use for reading from the media.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
//!
//! \internal
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::getConfigBlock1stSector(NandPhysicalMedia * pNandPhysicalMediaDesc, int * piConfigBlockPhysAdd, bool bConfirmConfigBlock, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer)
{
    int iBlockNum;
    RtStatus_t Status;
    bool bConfigBlockFound = false;
    bool isBlockBad;
    bool didSwitchChips = false;

    // Find the 1st good block in the specific chip
    for (iBlockNum = 0 ; iBlockNum < pNandPhysicalMediaDesc->wTotalBlocks ; iBlockNum++)
    {
        uint32_t pageAddress = pNandPhysicalMediaDesc->blockToPage(iBlockNum);

        // In the case of the STMP3700, we already know where the LDLB is so
        // force it. We don't care if block status is good or bad because we know
        // where the data should be. However, this only applies to the first two
        // chip enables. CE 3 and 4 on the 37xx work the same as before--the first
        // good block contains the config block.
        if (pNandPhysicalMediaDesc->wChipNumber == 0)
        {
            if (m_bootBlocks.m_ldlb1.b.bfBlockProblem == kNandBootBlockValid)
            {
                // Use the LDLB1 location.
                iBlockNum = m_bootBlocks.m_ldlb1.b.bfBlockAddress;
            }
            else if (m_bootBlocks.m_ldlb2.b.bfBlockProblem == kNandBootBlockValid)
            {
                // The LDLB1 is invalid, so use LDLB2.
                iBlockNum = m_bootBlocks.m_ldlb2.b.bfBlockAddress;
                
                if (NandHal::getChipSelectCount() > 1)
                {
                    // For multi-nand setups, LDLB2 will be on the second chip, so we
                    // have to switch chips too.
                    pNandPhysicalMediaDesc = NandHal::getNand(1);

                    didSwitchChips = true;
                }
            }
            else
            {
                // Neither LDLB1 or 2 are good (how did we get here, then?) so
                // return an error.
                return ERROR_DDI_NAND_CONFIG_BLOCK_NOT_FOUND;
            }
            pageAddress = pNandPhysicalMediaDesc->blockToPage(iBlockNum);
            isBlockBad = false;
        }
        else if (pNandPhysicalMediaDesc->wChipNumber == 1)
        {
            if (m_bootBlocks.m_ldlb2.b.bfBlockProblem == kNandBootBlockValid)
            {
                iBlockNum = m_bootBlocks.m_ldlb2.b.bfBlockAddress;
            }
            else if (m_bootBlocks.m_ldlb1.b.bfBlockProblem == kNandBootBlockValid)
            {
                // Invalid LDLB2, so use LDLB1.
                iBlockNum = m_bootBlocks.m_ldlb1.b.bfBlockAddress;
                
                if (NandHal::getChipSelectCount() > 1)
                {
                    // For multi-nand setups, LDLB1 will be on the first chip, so we
                    // have to switch chips too.
                    pNandPhysicalMediaDesc = NandHal::getNand(0);

                    didSwitchChips = true;
                }
            }
            else
            {
                // Neither LDLB1 or 2 are good (how did we get here, then?) so
                // return an error.
                return ERROR_DDI_NAND_CONFIG_BLOCK_NOT_FOUND;
            }
            pageAddress = pNandPhysicalMediaDesc->blockToPage(iBlockNum);
            isBlockBad = false;
        }
        else
        {
            isBlockBad = Block(PageAddress(pNandPhysicalMediaDesc, pageAddress)).isMarkedBad(auxBuffer);
        }

        // Check if block is good
        if (!isBlockBad)
        {
            uint32_t configPage = pageAddress + CONFIG_BLOCK_SECTOR_OFFSET;

            // Perform the sector read
            Status = pNandPhysicalMediaDesc->readPage(configPage, sectorBuffer, auxBuffer, 0);

            if (is_read_status_error_excluding_ecc(Status))
            {
                return Status;
            }

            // Validate the config block if the caller requested.
            if (bConfirmConfigBlock == true)
            {
                if (sectorBuffer[NAND_MAGIC_COOKIE_WORD_POS] != NAND_CONFIG_BLOCK_MAGIC_COOKIE)
                {
                    // The cookie marker is not present.
                    return ERROR_DDI_NAND_CONFIG_BLOCK_NOT_FOUND;
                }

                if ( sectorBuffer[NAND_VERSION_WORD_POS] != NAND_CONFIG_BLOCK_VERSION )
                {
                    // The version number does not match the one we expect.
                    return ERROR_DDI_NAND_CONFIG_BLOCK_VERSION_MISMATCH;
                }
            }

            // We found the config block!
            bConfigBlockFound = true;

            if (didSwitchChips)
            {
                // If we read the config block from a different chip because of a corrupt
                // LDLB, then we don't want to record an incorrect config block address.
                // Instead, return a value that indicates there isn't really a config block
                // on that chip. Later on, if ddi_nand_repair_boot_media() is called, the
                // correct value will be filled in after repairs are made.
                *piConfigBlockPhysAdd = -1;
            }
            else
            {
                // Return the config block's address.
            *piConfigBlockPhysAdd = iBlockNum;
            }
            break;
        }

        // Block was bad, lets keep searching
    }

    if (bConfigBlockFound == true)
    {
        return SUCCESS;
    }
    else
    {
        return ERROR_DDI_NAND_CONFIG_BLOCK_NOT_FOUND;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! Fills in the region's information from the contents of a region info
//! structure that is part of the config block.
////////////////////////////////////////////////////////////////////////////////
void Region::initFromConfigBlock(NandConfigBlockRegionInfo_t * info)
{
    m_iChip = info->iChip;
    m_nand = NandHal::getNand(m_iChip);

    m_pLogicalDrive = NULL;
    m_eDriveType = info->eDriveType;

    m_wTag = info->wTag;
    m_iNumBlks = info->iNumBlks;

    // Setup iStartPhysAddr
    m_iStartPhysAddr = info->iStartBlock;

    // Track the absolute (over all the media chips) block number.
    m_u32AbPhyStartBlkAddr = m_nand->baseAbsoluteBlock() + m_iStartPhysAddr;

    // Setup bRegionInfoDirty flag to false
    m_bRegionInfoDirty = false;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief Fills bad blocks related portion of NandRegionInfo_t
//!        structures for the specific chip.
//!
//! Here's what's going on:
//!
//! We need to set up a bad block table (BBTable), in RAM, for each
//! system drive region on every NAND chip. The data drive regions don't
//! need their own bad block table because they use the mapper's phymap
//! instead. But system drives need to keep track of bad blocks in order
//! to skip them.
//!
//! Each system drive region will have its own bad block table dynamically
//! allocated from the malloc pool. If a region has no bad blocks, the
//! pBBTable member of the region struct will be left NULL. The pBBTable
//! member of data drive regions is always set to NULL.
//!
//! Upon entry to this function, the NAND_BB_TABLE_VALID persistent bit (in
//! persistent register 1) is checked. If set, then this function will not
//! call FillInRegionBBTable() and instead reuse the contents of the bad block
//! table. The loop to process each of the NAND system drive
//! regions are still run. But in place of scanning the media for bad
//! blocks, the global bad block table layout descriptors are read and used
//! to rebuild the description of the portion of the bad block table for
//! each NAND region. The global layout descriptor is constructed by this
//! function when the persistent bit is not set. If while rebuilding the
//! table info any disagreement if found between NAND region types, the
//! entire bad block table is rebuilt from scratch with a recursive call to
//! this function.
//!
//! \param[in] sectorBuffer         Buffer to use for reading-from/writing-to the NAND.
//! \param[in] auxBuffer            Auxiliary buffer to use for reading from the NAND.
//! \param[in] attempt              The number of previous attempts to read the bad
//!                                 block information.
//! \param[in] bWriteToTheDevice    A boolean that indicates whether this function should
//!                                 actually write any data on the NAND.
//!                                 "False" means "don't write on the NAND", and also implies
//!                                 that this function is being called to print verbose information
//!                                 as it runs.
//!
//! \return SUCCESS or error code on failure.
//!
//! \see FillInRegionBBTable
//! \see FillInRegionBBTableFromDBBT
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::fillInNandBadBlocksInfo(SECTOR_BUFFER * sectorBuffer,
                                       SECTOR_BUFFER * auxBuffer,
                                       int attempt,
                                       bool bWriteToTheDevice )
{
    Region * pNandRegionInfo;
    int iMediaBadBlockCount = 0;
    RtStatus_t retCodeDBBTwasFound = ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND;
    RtStatus_t ret;
    uint32_t u32NAND = m_bootBlocks.m_dbbt1.b.bfNANDNumber;
    uint32_t u32DBBT_BlockAddress = m_bootBlocks.m_dbbt1.b.bfBlockAddress;
#if DEBUG
    uint32_t msec;
#endif
    DiscoveredBadBlockTable dbbt(this);
    dbbt.setBuffers(sectorBuffer, auxBuffer);

    // Now search for the Discovered Bad Block Table1. 
    retCodeDBBTwasFound = dbbt.scan(u32NAND, &u32DBBT_BlockAddress);
    // zDBBTLayout is filled in.
    // u32DBBT_BlockAddress is the block number that contains the DBBT.

    if (retCodeDBBTwasFound == SUCCESS)
    {
        // update the DBBT1 Address,
        m_bootBlocks.m_dbbt1.b.bfBlockAddress = (uint16_t)u32DBBT_BlockAddress;
    }
    else
    {
        // If can not find DBBT1, search for DBBT2. 
        u32NAND = m_bootBlocks.m_dbbt2.b.bfNANDNumber;
        u32DBBT_BlockAddress = m_bootBlocks.m_dbbt2.b.bfBlockAddress;   
        retCodeDBBTwasFound = dbbt.scan(u32NAND, &u32DBBT_BlockAddress);
        if (retCodeDBBTwasFound == SUCCESS)
        {
            // update the DBBT2 Address,
            m_bootBlocks.m_dbbt2.b.bfBlockAddress = (uint16_t)u32DBBT_BlockAddress;
        }
    }
    
    if (retCodeDBBTwasFound != SUCCESS)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Scanning for bad blocks in media...\n");
    }
    

#if DEBUG
    // take start timestamp
    msec = hw_profile_GetMilliseconds();
#endif // DEBUG

    if (retCodeDBBTwasFound == SUCCESS &&
        !bWriteToTheDevice )
    {
        //------------------------------------------------------------------
        // We successfully read the bad block table.
        //------------------------------------------------------------------

        tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1),
            "\nBB Table Block @ Block %d\n", u32DBBT_BlockAddress);
    }

    // Scan all regions for bad-blocks.
    Region::Iterator it = createRegionIterator();
    while ((pNandRegionInfo = it.getNext()))
    {
        // Use the DBBT (discovered bad-block table) from the NAND if it is available.
        if (retCodeDBBTwasFound == SUCCESS)
        {
            // Read bad blocks from DBBT on NAND and allocate this region's BB table (pNandRegionInfo->pBBTable) in RAM.
            ret = pNandRegionInfo->fillInBadBlocksFromDBBT(dbbt, u32NAND, u32DBBT_BlockAddress, sectorBuffer, auxBuffer);
            if (ret != SUCCESS)
            {
                if ( bWriteToTheDevice )
                {
                    // FORCE RE-SCAN OF BAD BLOCKS!!!
                    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Failed to load bad block table from media; scanning to rebuild table.\n");

                    // Erase the bad block table so it won't be found in the next attempt.
                    dbbt.erase();

                    // Only allow 2 retries.
                    if (attempt < 1)
                    {
                        retCodeDBBTwasFound = fillInNandBadBlocksInfo(sectorBuffer, auxBuffer, ++attempt, bWriteToTheDevice);
                    }

                    return retCodeDBBTwasFound;
                }
                else // !bWriteToTheDevice
                {
                    // Since we're not updating the device tables, we must have
                    // called FillInNandBadBlocksInfo() to print the bad-block info.
                    tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1), "\n"
                        "    Region not in DBBT.\n");
                } // if ( bWriteToTheDevice

            } // if failed FillInRegionBBTableFromNAND for iRegion

        } // retCodeDBBTwasFound
        else
        {
            // Scan device for bad blocks and allocate the BB table.
            ret = pNandRegionInfo->fillInBadBlocksByScanning(auxBuffer);
            if (ret != SUCCESS)
            {
                return ret;
            }
        } // !retCodeDBBTwasFound

        // Track the total bad block count.
        iMediaBadBlockCount += pNandRegionInfo->getBadBlockCount();

        if ( !bWriteToTheDevice )
        {
            // Since we're not updating the device tables, we must have
            // called FillInNandBadBlocksInfo() to print the bad-block info.
            if (retCodeDBBTwasFound && pNandRegionInfo->getBadBlocks())
            {
                pNandRegionInfo->getBadBlocks()->print();
            }
        }
    }

#if DEBUG
    msec = hw_profile_GetMilliseconds() - msec;

    if (retCodeDBBTwasFound == SUCCESS)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                               "Reading bad block table from block %u took %lu ms\n", u32DBBT_BlockAddress, msec);
    }
    else
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                "Scanning for bad blocks took %lu ms\n", msec);
    }
#endif

    // Save out the DBBT if we scanned the media (implying the DBBT was not found).
    if (retCodeDBBTwasFound != SUCCESS &&
        bWriteToTheDevice )
    {
        getDeferredQueue()->post(new SaveDbbtTask);
    }

    m_iNumBadBlks = iMediaBadBlockCount;

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Converts bad block tables from allocation to discovery mode.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::fillInBadBlocksFromAllocationModeTable(SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer)
{
    // This function only works when the BB table is in allocation mode.
    assert(m_badBlockTableMode == kNandBadBlockTableAllocationMode);
    
    unsigned totalBadBlockCount = 0;
    
    // Iterate over all of our regions.
    Region * region;
    Region::Iterator it = createRegionIterator();
    while ((region = it.getNext()))
    {
        region->setBadBlockTable(m_globalBadBlockTable);
        
        // Add up the total number of bad blocks.
        totalBadBlockCount += region->getBadBlockCount();
    }
    
    // Dispose of the global BB table memory.
    m_globalBadBlockTable.release();

    // Store the global bad block count, and change the table mode to discovery before
    // writing the DBBT.
    m_iNumBadBlks = totalBadBlockCount;
    m_badBlockTableMode = kNandBadBlockTableDiscoveryMode;
    
    // Save out the DBBT.
    getDeferredQueue()->post(new SaveDbbtTask);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Fill in a NAND region's bad block table.
//!
//! This function will allocate and fill in the bad block table for a specified
//! region with the addresses of actual bad blocks. This is done by actively
//! scanning the media for bad blocks. Two scan passes are made. One to count
//! the number of bad blocks so that the table can be dynamically allocated.
//! Then a second scan to fill in the table. When finished, the region's
//! iNumBadBlks member is set to the final count of bad blocks.
//!
//! Region pBBTable:
//!   allocation mode: this points into the global per-chip bad block table. contains a fixed
//!     number of bad blocks.
//!   discovery mode: and for system regions only,
//!     this points to a block allocated with malloc() that holds only bad blocks for this region.
//!     If the region has no bad blocks, this will be NULL. Data drive regions will also have
//!     this field set to NULL since their bad blocks are tracked by the mapper's phymap.
//!
//! \param[in] auxBuffer Auxiliary buffer to use for checking if a block is bad.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_NAND_LMEDIA_BAD_BLOCKS_MAX_OUT
////////////////////////////////////////////////////////////////////////////////
RtStatus_t SystemRegion::fillInBadBlocksByScanning(SECTOR_BUFFER * auxBuffer)
{
    uint32_t iBadBlockCounter = 0;
    RtStatus_t status;
    
    // Make sure the bad block table is unallocated.
    m_badBlocks.release();

    // Initial scan to count bad blocks in this region.
    status = scanNandForBadBlocks(&iBadBlockCounter, false, auxBuffer);
    if (status)
    {
        return status;
    }

    // Allocate this region's bad block table, with some extra room for new
    // bad blocks.
    uint32_t entriesToAllocate = iBadBlockCounter;
    entriesToAllocate += getExtraBlocksForBadBlocks();
    m_badBlocks.allocate(entriesToAllocate);
        
    // Fill in the bad block table for this region if there were any bad blocks.
    if (iBadBlockCounter)
    {
        // Scan again to fill the bad block table.
        status = scanNandForBadBlocks(NULL, true, auxBuffer);
        if (status)
        {
            return status;
        }
    }

    return SUCCESS;
}

RtStatus_t DataRegion::fillInBadBlocksByScanning(SECTOR_BUFFER * auxBuffer)
{
    // Scan to count bad blocks in this region.
    return scanNandForBadBlocks(NULL, true, auxBuffer);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Scan the NAND for bad blocks belonging to a given region.
//!
//! If the NandRegionInfo_t::pBBTable field is NULL, then bad blocks are only counted.
//! The count is returned through the \a iRegionBadBlocks argument, if
//! it is non-NULL. If the pBBTable field is valid then each matching bad block
//! is placed into the next empty slot in the region's bad block table,
//! starting at the first slot for the first matching bad block.
//!
//! \param[out] iRegionBadBlocks On exit the value pointed to by this argument
//!     is set to the number of bad blocks found in the region. You may set
//!     this to NULL if you don't need the count returned.
//! \param[in] auxBuffer Auxiliary buffer to use when checking for a bad block.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_LMEDIA_BAD_BLOCKS_MAX_OUT
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Region::scanNandForBadBlocks(uint32_t * iRegionBadBlocks, bool addBadBlocks, SECTOR_BUFFER * auxBuffer)
{
    uint32_t iBadBlockCounter = 0;
    uint32_t iBlockCounter;

    // Find bad blocks in the region.
    Block testBlock(m_u32AbPhyStartBlkAddr);
    for (iBlockCounter = 0; iBlockCounter < m_iNumBlks; ++iBlockCounter, ++testBlock)
    {
        // Test the current block.
        bool bBlockIsBad = testBlock.isMarkedBad(auxBuffer);

        // Check Status of block and adjust for it
        if (bBlockIsBad)
        {
            if (addBadBlocks)
            {
                addNewBadBlock(testBlock);
            }

            // Increment Bad Block Counter
            iBadBlockCounter++;
        }
    }

    // Return bad block count.
    if (iRegionBadBlocks)
    {
        *iRegionBadBlocks = iBadBlockCounter;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Fill in the Drive information.
//!
//! This function will create LogicalDrive instances for each of the drives
//! described by the set of regions loaded from the config block(s).
//!
//! \return Status of call or error.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::createDrives()
{
    bool didFindDataDrive = false;
    Region *regionInfo;
    RtStatus_t status;

    // We have to cross all regions to account for all drives
    Region::Iterator it = createRegionIterator();
    while ((regionInfo = it.getNext()))
    {
        // Check Drive Type
        switch (regionInfo->m_eDriveType)
        {
            case kDriveTypeData:
            {
                // Drive Type is Data Drive

                // The 1st time we find a kDriveTypeData, we fill up all the
                // LogicalDrive_t for this drive.
                // Subsequently, we will only add to the size-related parameters.
                if (!didFindDataDrive)
                {
                    DataDrive * newDataDrive = new DataDrive(this, regionInfo);
                    if (!newDataDrive)
                    {
                        return ERROR_OUT_OF_MEMORY;
                    }
                    
                    // Add our new data drive.
                    status = DriveAdd(newDataDrive);
                    if (status != SUCCESS)
                    {
                        return status;
                    }

                    didFindDataDrive = true;
                }
                else
                {
                    // Add to drive size only
                    LogicalDrive * genericDrive = DriveGetDriveFromTag(regionInfo->m_wTag);
                    assert(genericDrive->m_u32Tag == regionInfo->m_wTag && genericDrive->m_Type == kDriveTypeData);
                    DataDrive * dataDrive = static_cast<DataDrive *>(genericDrive);
                    
                    if (dataDrive)
                    {
                        dataDrive->addRegion(regionInfo);
                    }
                }
                
                break;
            }
            
            case kDriveTypeHidden:
            {
                DataDrive * newHiddenDrive = new DataDrive(this, regionInfo);
                if (!newHiddenDrive)
                {
                    return ERROR_OUT_OF_MEMORY;
                }
                
                // Add the new system or hidden drive.
                status = DriveAdd(newHiddenDrive);
                if (status != SUCCESS)
                {
                    return status;
                }
                
                break;
            }
            
            case kDriveTypeSystem:
            {
                SystemDrive * newSystemDrive = new SystemDrive(this, regionInfo);
                if (!newSystemDrive)
                {
                    return ERROR_OUT_OF_MEMORY;
                }
                
                // Add the new system or hidden drive.
                status = DriveAdd(newSystemDrive);
                if (status != SUCCESS)
                {
                    return status;
                }
                
                break;
            }
        }
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Fill in a region's bad block table using the DBBT.
//!
//! This function will allocate (in RAM, at pNandRegionInfo->pBBTable) and fill in the bad block table for a specified
//! region with the addresses of actual bad blocks. Instead of actively scanning
//! the NAND media like FillInRegionFromBBTable() does, this function uses the
//! pre-built discovered bad block table (DBBT) that resides on the NAND. It
//! is up to the caller to make sure that the DBBT is valid. Once the
//! appropriate section of the DBBT is read into memory, it is examined to count
//! the number of bad blocks so that the region's bad block table can be
//! dynamically allocated. Then the table is filled-in from the DBBT section.
//! When finished, the region's iNumBadBlks member is set to the final count
//! of bad blocks.
//!
//! \param[in] dbbt The DBBT object.
//! \param[in] pNandRegionInfo  This region's bad block table is allocated and filled-in.
//! \param[in] u32NAND              Nand Chip Number which DBBT locates
//! \param[in] u32DBBT_BlockAddress Block Address of DBBT.
//! \param[in] sectorBuffer         Sector buffer to hold the bad block table page.
//!                                 On SUCCESSful exit, hold the BadBlockTableNand_t that was read.
//! \param[in] auxBuffer            Auxiliary buffer to use when reading from the NAND.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_NAND_LMEDIA_BAD_BLOCKS_MAX_OUT
//! \retval ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND
////////////////////////////////////////////////////////////////////////////////
RtStatus_t SystemRegion::fillInBadBlocksFromDBBT(
    DiscoveredBadBlockTable & dbbt,
    uint32_t u32NAND,
    uint32_t u32DBBT_BlockAddress,
    SECTOR_BUFFER * sectorBuffer,
    SECTOR_BUFFER * auxBuffer)
{
    int iBadBlockCounter = 0;
    RtStatus_t Status;
    uint32_t u32PageOffset;
    
    // Compute the page-offset to the desired contents of the DBBT.
    u32PageOffset = dbbt.getDbbtPageOffset(m_iChip, DiscoveredBadBlockTable::kDBBT);
    
    // Read the bad block table from the DBBT.
    Page dbbtPage(PageAddress(u32NAND, u32DBBT_BlockAddress, u32PageOffset));
    dbbtPage.setBuffers(sectorBuffer, auxBuffer);
    Status = dbbtPage.read();

    if (!is_read_status_success_or_ecc_fixed(Status))
    {
        return Status;
    }
    
    // We will be trying to fill in a Bad-block table for this region.

    // These tables may not be sorted, so we need to scan through them.
    BadBlockTableNand_t * pNandBadBlockTable = (BadBlockTableNand_t *)sectorBuffer;

    // Double check to make sure the data read matches the chip we expect.
    if (pNandBadBlockTable->uNAND != m_iChip)
    {
        return ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND;
    }

    m_badBlocks.release();
    
    // Scan the DBBT for blocks in this region, to see if the region has any bad-blocks.
    Status = scanDBBTPage(&iBadBlockCounter, pNandBadBlockTable);
    if (Status)
    {
        return Status;
    }

    // Allocate enough room for the bad blocks we know are there plus some
    // extra slots for new bad blocks.
    uint32_t entriesToAllocate = iBadBlockCounter + getExtraBlocksForBadBlocks();
    m_badBlocks.allocate(entriesToAllocate);

    if (iBadBlockCounter)
    {
        // The region has bad-blocks.

        // Scan again to fill the region's bad block table now that it is allocated in RAM.
        // Scan the DBBT for blocks in this region, filling the region's BBTable in RAM.
        Status = scanDBBTPage(NULL, pNandBadBlockTable);
        if (Status)
        {
            return Status;
        }
    }

    return SUCCESS;
}

RtStatus_t DataRegion::fillInBadBlocksFromDBBT(
    DiscoveredBadBlockTable & dbbt,
    uint32_t u32NAND,
    uint32_t u32DBBT_BlockAddress,
    SECTOR_BUFFER * sectorBuffer,
    SECTOR_BUFFER * auxBuffer)
{
    RtStatus_t Status;
    uint32_t u32PageOffset;

    // Compute the page-offset to the desired contents of the DBBT.
    u32PageOffset = dbbt.getDbbtPageOffset(m_iChip, DiscoveredBadBlockTable::kBBRC);
    
    // Read the bad block table from the DBBT.
    Page dbbtPage(PageAddress(u32NAND, u32DBBT_BlockAddress, u32PageOffset));
    dbbtPage.setBuffers(sectorBuffer, auxBuffer);
    Status = dbbtPage.read();

    if (!is_read_status_success_or_ecc_fixed(Status))
    {
        return Status;
    }
    
    // We will be trying to read the count of bad-blocks for this region.
    uint32_t            *pu32NumBadBlksInRegion;
    BootBlockStruct_t   *pBootBlockStruct;
    bool                bFoundBootBlock;

    // Depending on the firmware that was previously run on this
    // NAND, the BadBlocksPerRegionCounts_t may or may not be present
    // after the DBBT.  The BadBlocksPerRegionCounts_t is embedded in
    // a BootBlockStruct_t so that fingerprints can be used to recognize it.
    pBootBlockStruct = (BootBlockStruct_t *)sectorBuffer;

    bFoundBootBlock = ddi_nand_media_doFingerprintsMatch(pBootBlockStruct, &zBBRCFingerPrints);
    if ( !bFoundBootBlock )
    {
        // The BBRC does not exist.
        // The simplest option is just to return the failure to the caller.
        // The caller can then rebuild the DBBT, which should add the BBRC.

        Status = ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND;
        return (Status);
    }

    // Get the count of bad blocks for this region from the BBRC.
    pu32NumBadBlksInRegion = DiscoveredBadBlockTable::getPointerToBbrcEntryForRegion(pBootBlockStruct, getRegionNumber());

    m_badBlockCount = *pu32NumBadBlksInRegion;

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Search a DBBT page for bad blocks belonging to the region.
//!
//! This function searches a bad block table in the format stored on the NAND
//! for bad blocks belonging to the given region. It can either count bad blocks
//! or store them in the region's bad block table. If the
//! \a regionBadBlockCount parameter is not NULL, then bad blocks are only counted
//! and the count is returned through \a regionBadBlockCount.
//! If \a regionBadBlockCount parameter is NULL then each matching bad block
//! is inserted into the region's bad block table.
//!
//! \param[out] regionBadBlockCount Points to storage to hold the number
//!     of matching bad blocks on exit. May be NULL, in which case the count is
//!     not returned to the caller and bad blocks are inserted into the
//!     region's bad block table instead.
//! \param[in] pNandBadBlockTable Pointer to a section of the bad block table
//!     as stored on the media.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_LMEDIA_BAD_BLOCKS_MAX_OUT
////////////////////////////////////////////////////////////////////////////////
RtStatus_t SystemRegion::scanDBBTPage(int * regionBadBlockCount, BadBlockTableNand_t * pNandBadBlockTable)
{
    int iBlockCounter;
    int iBadBlockCounter = 0;
    uint32_t u32RegionStartBlock;
    uint32_t u32RegionEndBlock;

    // Subtract 1 from the end because the start block is included in the calculation.
    u32RegionStartBlock = m_iStartPhysAddr;
    u32RegionEndBlock = u32RegionStartBlock + m_iNumBlks - 1;

    for (iBlockCounter = 0; iBlockCounter < pNandBadBlockTable->uNumberBB; iBlockCounter++)
    {
        uint32_t u32BBAddress = pNandBadBlockTable->u32BadBlock[iBlockCounter];

        // Only deal with bad blocks within the given region.
        if ((u32BBAddress >= u32RegionStartBlock) && (u32BBAddress <= u32RegionEndBlock))
        {
            if (!regionBadBlockCount)
            {
                // Convert bad block address to absolute and insert it into the table.
                BlockAddress absoluteAddress(m_iChip, u32BBAddress);
                if (!m_badBlocks.insert(absoluteAddress))
                {
                    return ERROR_DDI_NAND_LMEDIA_BAD_BLOCKS_MAX_OUT;
                }
            }

            // Increment bad block counter for this region.
            iBadBlockCounter++;
        }
    }

    // Return the number of bad blocks.
    if (regionBadBlockCount)
    {
        *regionBadBlockCount = iBadBlockCounter;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! A minimum of one extra block will be returned.
//!
////////////////////////////////////////////////////////////////////////////////
unsigned Region::getExtraBlocksForBadBlocks()
{
    return (m_iNumBlks * NandHal::getParameters().maxBadBlockPercentage + 99) / 100;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Set the Bad Block Table Pointers..
//!
//! This function prepares the Bad Block Table pointers for each Region. It
//! sets the region's bad block pointer to point to the first bad block entry
//! that is within the block range of the region.
//!
//! \param[in]  bbTable Pointer to the full bad block table.
//! \param[in]  tableEntries Total number of Bad Blocks in the array.
//!
//! \return SUCCESS
////////////////////////////////////////////////////////////////////////////////
void SystemRegion::setBadBlockTable(const BadBlockTable & table)
{
    int i;
    BlockAddress u32StartBlock = getStartBlock();
    BlockAddress u32EndBlock = getLastBlock();
    uint32_t count = 0;
    
    // Count the bad blocks in this region.
    count = table.countBadBlocksInRange(m_u32AbPhyStartBlkAddr, m_iNumBlks);
    
    // Add spare entries for new bad blocks based on bad block percentage,
    // with a minimum of 1 spare.
    m_badBlocks.allocate(count + getExtraBlocksForBadBlocks());

    // If there were any matching bad blocks, run through the entire global table and
    // insert the matching blocks into our local table.
    if (count)
    {
        for (i=0; i < table.getCount(); i++)
        {
            const BlockAddress & entry = table[i];
            if ((entry >= u32StartBlock) && (entry <= u32EndBlock))
            {
                m_badBlocks.insert(entry);
            }
        }
    }
}

void DataRegion::setBadBlockTable(const BadBlockTable & table)
{
    // Data-type regions have no local bad block table, just a bad block count.
    m_badBlockCount = table.countBadBlocksInRange(m_u32AbPhyStartBlkAddr, m_iNumBlks);
}

void SystemRegion::addNewBadBlock(const BlockAddress & addr)
{
    m_badBlocks.insert(addr);
    setDirty();
}

void DataRegion::addNewBadBlock(const BlockAddress & addr)
{
    ++m_badBlockCount;
    setDirty();
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
