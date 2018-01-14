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
//! \addtogroup ddi_nand_mapper
//! @{
//! \file    ddi_nand_mapper_api.c
//! \brief   Common NAND Logical Block Address Mapper functions.
////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "Mapper.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/ddi_media.h"
#include "drivers/rtc/ddi_rtc.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "Block.h"
#include "Page.h"
#include "ZoneMapSectionPage.h"
#include "ZoneMapCache.h"
#include "PersistentPhyMap.h"
#include "BlockAllocator.h"
#include "NssmManager.h"
#include "NonsequentialSectorsMap.h"
#include "arm_ghs.h" // for __CLZ32

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
// Code
/////////////////////////////////////////////////////////////////////////////////

#pragma ghs section text=".init.text"
Mapper::Mapper(Media * media)
:   m_media(media),
    m_zoneMap(NULL),
    m_phyMapOnMedia(NULL),
    m_physMap(NULL),
    m_prebuiltPhymap(NULL),
    m_isInitialized(false),
    m_isZoneMapCreated(false),
    m_isPhysMapCreated(false),
    m_isMapDirty(false),
    m_isBuildingMaps(false),
    m_unallocatedBlockAddress(0),
    m_blockAllocator(NULL),
    m_mapAllocator(NULL)
{
    // Clear the reserved range info fields in one call.
    memset(&m_reserved, 0, sizeof(m_reserved));
}
#pragma ghs section text=default

Mapper::~Mapper()
{
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Initialize the mapper.
//!
//! This function initializes the mapper. It will perform the following tasks:
//!    - Initialize and obtain all necessary memory
//!    - Create the zone and phy maps in RAM, either by
//!         1. Loading from archived maps on the NAND
//!         2. Rebuilding by scanning addresses from RA on the NAND.
//!
//! \param[in]  None.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
//! \post  If successful, the Zone Map Table and Phy Map have
//!        been initialized.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::init()
{
    RtStatus_t retCode = SUCCESS;
    uint32_t trustMediaResidentMaps;
    bool bRangeMoved=false;
    
    // Only need to initialize these values once.
    if (!m_isInitialized)
    {
        // The value for the unallocated block address depends on the zone map entry size.
#if !NAND_MAPPER_FORCE_24BIT_ZONE_MAP_ENTRIES
        if (m_media->getTotalBlockCount() < kNandZoneMapSmallEntryMaxBlockCount)
        {
            m_unallocatedBlockAddress = kNandMapperSmallUnallocatedBlockAddress;
        }
        else
#endif // !NAND_MAPPER_FORCE_24BIT_ZONE_MAP_ENTRIES
        {
            m_unallocatedBlockAddress = kNandMapperLargeUnallocatedBlockAddress;
        }
    }
    
    // Allocate the phy map, unless we were provided with a prebuilt one.
    if (!m_physMap && !m_prebuiltPhymap)
    {
        m_physMap = new PhyMap;
        m_physMap->init(m_media->getTotalBlockCount());
        m_physMap->setDirtyCallback(phymapDirtyListener, this);
    }
    
    // Allocate the zone map.
    if (!m_zoneMap)
    {
        m_zoneMap = new ZoneMapCache(*this);
        m_zoneMap->init();
    }
    
    // Allocate the persistent phymap.
    if (!m_phyMapOnMedia)
    {
        m_phyMapOnMedia = new PersistentPhyMap(*this);
        m_phyMapOnMedia->init();
        
        if (m_physMap)
        {
            m_phyMapOnMedia->setPhyMap(m_physMap);
        }
    }

    // We need the know the reserved block range before doing anything that touches
    // the zone or phy maps on the media.
    retCode = computeReservedBlockRange(&bRangeMoved);
    if (retCode)
    {
        return retCode;
    }
    
    // Create allocator for data blocks.
    if (!m_blockAllocator)
    {
        // If the phymap doesn't exist yet then we'll update it in the allocator
        // when it is created.
        m_blockAllocator = new RandomBlockAllocator(m_physMap);
        assert(m_blockAllocator);
        
        // Set the allocator's range to the whole NAND.
        m_blockAllocator->setRange(m_reserved.endBlock + 1, m_media->getTotalBlockCount() - 1);
    }
    
    // Create allocator for map blocks.
    if (!m_mapAllocator)
    {
        m_mapAllocator = new LinearBlockAllocator(m_physMap);
        assert(m_mapAllocator);
        
        // Set the range to just the reserved range.
        m_mapAllocator->setRange(m_reserved.startBlock, m_reserved.endBlock);
    }

    // Check to see if we already inialized
    if ((bRangeMoved == false) && m_isInitialized)
    {
        return (SUCCESS);
    }

    // We are here if either isInitialized is false or the reserved block range is different from
    // previous allocation. Following state variables may not be false if reserved block range is
    // moved but isInitialized is true. They should be set to false in order for proper zone map
    // recreation.
    m_isInitialized = false;
    m_isZoneMapCreated = false;
    m_isPhysMapCreated = false;
    m_isBuildingMaps = false;
    
    if (bRangeMoved || m_prebuiltPhymap)
    {
        // If the range has moved then we want to always recreate zone map so we can force relocation of map.
        trustMediaResidentMaps = false;
    }
    else
    {
        // If this persistent bit is set, it means that the device was gracefully shutdown
        // and we should trust maps stored on the media.
        ddi_rtc_ReadPersistentField(RTC_NAND_LOAD_ZONE_MAP_FROM_MEDIA, &trustMediaResidentMaps);
    }

    if (trustMediaResidentMaps)
    {
        // Try to load the zone and phy maps from media.
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Loading maps from media\n");
        
        // Find and load the phy map.
        retCode = m_phyMapOnMedia->load();
        
        if (retCode == SUCCESS)
        {
            // Locate and init the zone map.
            retCode = m_zoneMap->findZoneMap();
        }

        if (retCode == SUCCESS)
        {
            m_isZoneMapCreated = true;
            m_isPhysMapCreated = true;
        }
        else
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Loading maps failed with error 0x%08x\n", retCode);
        }
    }
    
    if (!trustMediaResidentMaps || retCode != SUCCESS)
    {
        // The maps are corrupted or can not be found on the media, or the system was
        // shutdown uncleanly and we cannot trust the maps.
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Scanning media to create maps\n");

        // Rebuild the Zone Map and Phy Map from RA data on the media.
        // This function will also erase any pre-existing maps which are stored
        // on the media.
        retCode = createZoneMap();
        
        if (retCode != SUCCESS)
        {
            return retCode;
                                                     
        }
    }
    
    // Update the map allocator so it starts from the current map location instead of
    // the beginning of the reserved range. The highest map block address is selected
    // as the new search start location.
    BlockAddress zoneMapAddress = m_zoneMap->getAddress();
    BlockAddress phyMapAddress = m_phyMapOnMedia->getAddress();
    if (zoneMapAddress > phyMapAddress)
    {
        m_mapAllocator->setCurrentPosition(zoneMapAddress);
    }
    else
    {
        m_mapAllocator->setCurrentPosition(phyMapAddress);
    }

    // We're done initing now!
    m_isInitialized = true;
    
    // Go clean out the reserved block range of any blocks that shouldn't be there.
    // This is necessary because the reserved block range may potentially move or grow
    // between boots due to new bad blocks.
    retCode = evacuateReservedBlockRange();
    if (retCode)
    {
        return retCode;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Reinits the mapper.
//!
//! If the zone and phy maps in RAM have already been initialized,
//! then this function reinitializes them.
//! 
//! They are reinitialized either by loading them from archived
//! copies on the media, or by scanning the RA of the media
//! and rebuilding them.
//! 
//! On the other hand, if the maps in RAM are currently uninitialized, then no
//! action is taken.
//!
//! \retval SUCCESS             If the maps become initialized or are left initialized.
//! \retval non-SUCCESS         If the maps fail to initialize.
//!
//! \note   Generally, you would want to call this function if the
//!         maps in RAM do not match the true state of the media.
//!         This function repairs the maps in RAM.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::rebuild()
{
    RtStatus_t ret = SUCCESS;

    // If zone-map was not created, there is nothing to
    // re-create.
    if (m_isInitialized)
    {
        // Must flush NSSMs before rebuilding to avoid conflicts.
        m_media->getNssmManager()->flushAll();

        m_isInitialized    = FALSE;
        m_isZoneMapCreated = FALSE;
        m_isPhysMapCreated = FALSE;

        // Set the dirty flag to make sure we actually recreate the zone map
        // instead of just loading it from media.
        setDirtyFlag();

        // Allocate needed buffers, and fill in the zone and
        // phy maps in RAM.
        ret = init();
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Determines the block range reserved for the zone and phy maps.
//!
//! The requirements for the reserved block range are as follows:
//! - The range must contain at least #kNandMapperReservedBlockCount good
//!     blocks that are allocated to a data or hidden drive.
//! - It must start after all boot blocks.
//! - The reserved range must never extend beyond LBA search range, the first
//!     200 blocks on the first chip.
//!
//! It is alright for the reserved block range to span across system drives.
//! This is because system drive regions are marked as used or bad in the
//! phy map, so the mapper will never attempt to allocate those blocks.
//!
//! The pbRangeMoved is added to let the caller know that the reserved block ranges
//! has moved since last allocation. The range will be different if there is a change
//! in config blocks layout or blocks within the reserved block range gone bad.
//!
//! \param pbRangeMoved, returns true if reserved blocks range has moved else false
//!
//! \retval SUCCESS Reserved range computed.
//! \retval ERROR_DDI_NAND_LMEDIA_NO_REGIONS_IN_MEDIA
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::computeReservedBlockRange(bool * pbRangeMoved)
{
    int startBlock;
    int blockNumber;
    int count;
    Region * region = NULL;
    bool foundGoodBlock;
    
    // There must be at least one region.
    assert(m_media->getRegionCount());
    
    // We start by finding the first data-type region.
    Region::Iterator it = m_media->createRegionIterator();
    while ((region = it.getNext()))
    {
        // Exit loop if this is a data-type region.
        if (region->isDataRegion())
        {
            break;
        }
    }
    
    // Validate the region.
    if (!(region && region->m_iChip == 0))
    {
        return ERROR_DDI_NAND_LMEDIA_NO_REGIONS_IN_MEDIA;
    }

    // The reserved range starts with the first block of the first data-type region.
    startBlock = region->m_u32AbPhyStartBlkAddr;
    
    // Prepare for the search loop.
    blockNumber = startBlock;
    count = 0;
    foundGoodBlock = false;
    
    // Get a buffer to hold the redundant area. We allocate the buffer here and pass it
    // to isMarkedBad() instead of letting it continually reallocate the buffer.
    AuxiliaryBuffer auxBuffer;
    RtStatus_t status = auxBuffer.acquire();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Scan and count up the required number of good reserved blocks. This loop also observes
    // regions, so that system drive blocks are skipped over. Until the first good block is
    // found, the start of the reserved region is moved forward each bad block.
    while (count < kNandMapperReservedBlockCount)
    {
        // Have we moved beyond the end of the current region?
        if (blockNumber - region->m_u32AbPhyStartBlkAddr >= region->m_iNumBlks)
        {
            // Move to the next data-type region.
            while ((region = it.getNext()))
            {
                // Break if the region is a data-type region. System regions are skipped.
                if (region->isDataRegion())
                {
                    break;
                }
            }
            
            // Make sure we still have a valid region.
            if (!region)
            {
                return ERROR_DDI_NAND_LMEDIA_NO_REGIONS_IN_MEDIA;
            }
            
            // Update block number to start at this region.
            blockNumber = region->m_u32AbPhyStartBlkAddr;
        }
        
        // Check if this block is bad.
        if (!Block(BlockAddress(blockNumber)).isMarkedBad(auxBuffer));
        {
            // This is a good block, so include it in the reserved block count.
            count++;
            foundGoodBlock = true;
        }
        
        // Move to the next block.
        blockNumber++;
        
        // Adjust the start of the reserved region until the first good block is found.
        if (!foundGoodBlock)
        {
            startBlock = blockNumber;
        }
    }

    // Initialize default value in return parameter
    *pbRangeMoved = false;    

    // We chose two parameters that can tell if there is a change to reserved blocks from last
    // configuration, the start block and reserved block count. The start block will be different
    // if there is a change in layout of boot blocks and reserved block count will increase if
    // blocks within the reserved block range gone bad. Any of the two changes then we need to
    // return true in pbRangeMoved.
    //
    // m_isInitialized should be true before verifying reserved blocks range has moved. 
    if (m_isInitialized && ((m_reserved.startBlock != startBlock) ||
        (m_reserved.blockCount != (blockNumber - startBlock))))
    {
        *pbRangeMoved = true;    
    }
        
    // Record a bunch of precomputed information about the reserved blocks, all to be
    // used to speed up looking for an available block.
    m_reserved.startBlock = startBlock;
    m_reserved.blockCount = blockNumber - startBlock;
    m_reserved.endBlock = startBlock + m_reserved.blockCount - 1;
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Evicts any undesired blocks from the reserved block range.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_LMEDIA_NO_REGIONS_IN_MEDIA
//!
//! \pre The mapper must be fully initialised before this function is called.
//!     In particular, either CreateZoneMap or LoadZoneMap must have been
//!     called.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::evacuateReservedBlockRange()
{
    unsigned blockNumber;
    RtStatus_t status;
    Region * region = NULL;
    unsigned reservedStartBlock = m_reserved.startBlock;
    unsigned regionStart;
    unsigned regionEnd;

    // Find the region that holds the first block of the reserved range.
    Region::Iterator it = m_media->createRegionIterator();
    while ((region = it.getNext()))
    {
        regionStart = region->m_u32AbPhyStartBlkAddr;
        regionEnd = regionStart + region->m_iNumBlks;
        
        // Exit loop if this is the matching region.
        if (reservedStartBlock >= regionStart && reservedStartBlock < regionEnd)
        {
            break;
        }
    }
    
    // Validate the region.
    if (!region)
    {
        return ERROR_DDI_NAND_LMEDIA_NO_REGIONS_IN_MEDIA;
    }
    
    // Get a buffer to hold the redundant area.
    AuxiliaryBuffer auxBuffer;
    if ((status = auxBuffer.acquire()) != SUCCESS)
    {
        return status;
    }

    // Iterate over all blocks in the reserved block range.
    Block scanBlock(reservedStartBlock);
    for (blockNumber = 0; blockNumber < m_reserved.blockCount; ++blockNumber, ++scanBlock)
    {
        bool isMapBlock;
        unsigned blockPhysicalAddress = reservedStartBlock + blockNumber;
        
        // Have we gone beyond the current region's end?
        if (blockPhysicalAddress >= regionEnd)
        {
            // Advance the region while skipping over system regions.
            while ((region = it.getNext()))
            {
                // Exit the loop unless this is a system region.
                if (region->m_eDriveType != kDriveTypeSystem)
                {
                    break;
                }
                
                // We're skipping over a region, so we need to advance the block counter to match.
                blockNumber += region->m_iNumBlks;
            }
            
            // Make sure we still have a valid region.
            if (!region)
            {
                return ERROR_DDI_NAND_LMEDIA_NO_REGIONS_IN_MEDIA;
            }
            
            // Update region info.
            regionStart = region->m_u32AbPhyStartBlkAddr;
            regionEnd = regionStart + region->m_iNumBlks;
            
            // Recompute the current block address.
            blockPhysicalAddress = reservedStartBlock + blockNumber;
            scanBlock = BlockAddress(blockPhysicalAddress);
        }
        
        // We can just ignore bad blocks.
        if (scanBlock.isMarkedBad(auxBuffer))
        {
            continue;
        }
        
        // Check if this is a zone map block.
        isMapBlock = isBlockMapBlock(blockPhysicalAddress, kMapperZoneMap, &status);
        if (status)
        {
            break;
        }
        
        // Leave the current zone map block in place.
        if (isMapBlock && m_zoneMap && m_zoneMap->isMapBlock(blockPhysicalAddress))
        {
            continue;
        }
        
        // Check for a phy map block.
        if (!isMapBlock)
        {
            isMapBlock = isBlockMapBlock(blockPhysicalAddress, kMapperPhyMap, &status);
            if (status)
            {
                break;
            }
            
            // Don't erase the current phy map block.
            if (isMapBlock && m_phyMapOnMedia && m_phyMapOnMedia->isMapBlock(scanBlock))
            {
                continue;
            }
        }
        
        // Handle different block types separately.
        if (isMapBlock)
        {
            // Map blocks get erased and marked unused. This is OK because we've already
            // made sure that we're not erasing the current zone or phy map blocks above.
            status = m_physMap->markBlockFreeAndErase(blockPhysicalAddress);
            if (status)
            {
                break;
            }
        }
        else
        {
            // We have a potential data block here, so we need to read its metadata. This will
            // both tell us if the block is erased and its LBA if not.
            
            // Read the metadata of the data block's first page so we can determine its LBA.
            status = scanBlock.readMetadata(kFirstPageInBlock, auxBuffer);
            if (!is_read_status_success_or_ecc_fixed(status))
            {
                break;
            }
        
            // Check if this is an erased block.
            Metadata md(auxBuffer);
            if (!md.isErased())
            {
                // Evacuate this data block to somewhere out of the reserved range.
                VirtualBlock vblock(this);
                NonsequentialSectorsMap * map;
                status = m_media->getNssmManager()->getMapForVirtualBlock(vblock.getVirtualBlockFromMapperKey(md.getLba()), &map);
                if (status == SUCCESS && map)
                {
                    // This call will pick a new physical block for us.
                    status = map->relocateVirtualBlock();
                }
                else
                {
                    // We didn't get an NSSM for the virtual block, so the block must be
                    // invalid or something. Just erase it.
                    status = m_physMap->markBlockFreeAndErase(blockPhysicalAddress);
                }
                
                if (status)
                {
                    break;
                }
            }
        }
    }
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Handler for dirty state changes of the phymap.
////////////////////////////////////////////////////////////////////////////////
void Mapper::phymapDirtyListener(PhyMap * thePhymap, bool wasDirty, bool isDirty, void * refCon)
{
    Mapper * _this = (Mapper *)refCon;
    assert(_this);
    
    assert(thePhymap == _this->m_physMap);
    
    // We only need to handle the case where the map is becoming dirty for the first time
    // after being clean.
    if (isDirty && !wasDirty)
    {
        _this->setDirtyFlag();
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Records that the maps have been modified.
//!
//! Call this function any time either the zone map or phy map is modifed
//! in order to set the dirty flag. This will cause the maps to be written
//! to media when flush() is called.
////////////////////////////////////////////////////////////////////////////////
void Mapper::setDirtyFlag()
{
    if (!m_isMapDirty)
    {
        // Indicate that the zone map has been touched.
        m_isMapDirty = true;
        
        // Clear the persistent bit that says it's safe to load from media.
        ddi_rtc_WritePersistentField(RTC_NAND_LOAD_ZONE_MAP_FROM_MEDIA, 0);
        
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand mapper is dirty\n");
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Records that the maps match those on the media.
//!
////////////////////////////////////////////////////////////////////////////////
void Mapper::clearDirtyFlag()
{
    if (m_isMapDirty)
    {
        m_isMapDirty = false;
        
        // Set the persistent bit that says we can trust the maps resident on the media.
        // This bit will get cleared when the map is marked dirty.
        ddi_rtc_WritePersistentField(RTC_NAND_LOAD_ZONE_MAP_FROM_MEDIA, 1);
        
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand mapper is clean\n");
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Shutdown the mapper.
//!
//! This function shuts down the mapper. It will perform the following tasks:
//!    - Free up all memory
//!    - Flush the Zone map to Nand
//!    - Remove all system resources such as mutex, semaphore, etc. if necessary.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//!
//! \post  If successful, the Zone Map Table and Erase Block Table have been
//!       flushed to the NAND.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::shutdown()
{
    RtStatus_t ret;
    
    if (!m_isInitialized)
    {
        return SUCCESS;
    }

    // Flush the zone map to nand.
    ret = flush();
    if (ret)
    {
        return ret;
    }
    
    // Free the block allocators.
    if (m_mapAllocator)
    {
        delete m_mapAllocator;
        m_mapAllocator = NULL;
    }
    
    if (m_blockAllocator)
    {
        delete m_blockAllocator;
        m_blockAllocator = NULL;
    }
    
    // Shutdown the zone map cache.
    if (m_zoneMap)
    {
        delete m_zoneMap;
        m_zoneMap = NULL;
    }
    
    // Free the dynamically allocated phy map.
    if (m_physMap)
    {
        delete m_physMap;
        m_physMap = NULL;
    }
    
    // Clear the prebuilt phymap.
    m_prebuiltPhymap = NULL;
    
    // Mark as uninitialized.
    m_isInitialized = false;
    m_isZoneMapCreated = false;
    m_isPhysMapCreated = false;

    return SUCCESS;
}

RtStatus_t Mapper::setBlockInfo(uint32_t u32Lba, uint32_t u32PhysAddr)
{
    // Update the zone map.
    RtStatus_t ret = m_zoneMap->setBlockInfo(u32Lba, u32PhysAddr);
    if (ret)
    {
        return ret;
    }

    // Mark this block as used in the phymap.
    if (!isBlockUnallocated(u32PhysAddr))
    {
        ret = m_physMap->markBlockUsed(u32PhysAddr);
        if (ret)
        {
            return ret;
        }
    }

    // Indicate that the zone map has been touched
    setDirtyFlag();

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Allocate a new physical block.
//!
//! This function allocates a physical block from the pool of currently unused
//! blocks. You can optionally provide a set of constraints to ensure that the allocated
//! block is within a certain area of the NAND, such as a certain plane. The block is
//! guaranteed to be erased and ready for use when the call returns. The block will also
//! have already been marked as used in the phy map.
//!
//! \param[out] pu32PhysBlkAddr Pointer to the result absolute block address.
//! \param eBlkType Class of block to allocate. The possible values:
//!     - \em #kMapperBlockTypeMap - Allocate a block to hold one of the persistent
//!         maps used by the mapper, such as the zone map.
//!     - \em #kMapperBlockTypeNormal - Allocate a regular data drive block.
//! \param constraints Optional constraints on which blocks can be chosen.
//!     The constraints let the caller limit result blocks to a given chip,
//!     die, and/or plane. This is essential in supporting multiplane and other
//!     NAND features.
//!
//! \retval SUCCESS If no error has occurred. The value pointed to by \a pu32PhysBlkAddr
//!     is a valid block number ready for use.
//! \retval ERROR_DDR_NAND_MAPPER_PHYMAP_MAPFULL No more blocks are available.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::getBlock(uint32_t * pu32PhysBlkAddr, MapperBlockTypes_t eBlkType, const AllocationConstraints * constraints)
{
    assert(pu32PhysBlkAddr);
    
    RtStatus_t rtCode;
    BlockAllocator * allocator;
    
    // The requested block type determines which allocator we use.
    switch (eBlkType)
    {
        case kMapperBlockTypeMap:
            allocator = m_mapAllocator;
            break;

        case kMapperBlockTypeNormal:
            allocator = m_blockAllocator;
            break;
    }
    
    assert(allocator);
    
    // Apply constraints if they were given to us.
    if (constraints)
    {
        allocator->setConstraints(*constraints);
    }
    else
    {
        // No constraints were provided, so make sure the allocator isn't using any.
        allocator->clearConstraints();
    }
    
    // Try to allocate a block and erase it if necessary. If the erase fails, then we
    // handle the bad block and try again.
    do
    {
        // Try to allocate a new block.
        if (!allocator->allocateBlock(*pu32PhysBlkAddr))
        {
            return ERROR_DDR_NAND_MAPPER_PHYMAP_MAPFULL;
        }
        
        // Mark the location in the available block as taken.
        rtCode = m_physMap->markBlockUsed(*pu32PhysBlkAddr);
        if (rtCode != SUCCESS)
        {
            return rtCode;
        }

        // Create block instance.
        Block newBlock(*pu32PhysBlkAddr);

        // As well as setting the bit, erase physical block.
        if (!newBlock.isErased())
        {
            // If the erase fails, then loop again and try again with another block. We have
            // already marked the bad block as used in the phy map, so we just need to mark the
            // block itself as bad.
            rtCode = newBlock.erase();
            if (rtCode == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                // This will mark the block used in the phymap again, but not a big deal.
                handleNewBadBlock(newBlock);
            }
        }
    }
    while (rtCode != SUCCESS);

    return rtCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Allocate a new physical block and map it to the given logical block.
//!
//! This function allocates a physical nand block to a LBA. The application can
//! request the allocated physical block to have to following characteristics:
//!    - LBA: this block is to be used to store the ZONE map. In order to speed
//!     up the search for ZONE map during startup, it is better to allocate
//!     this Zone map block in the first 200 blocks of the NAND. In the case
//!     that all the blocks in the first 200 blocks have been occupied, then
//!     this function must evict a block within this area to another area.
//!    - Odd: Allocate an odd Physical block. This might be used for multi-page
//!     programming.
//!    - Even: Allocate an even Physical block. This might be used for multi-page
//!     programming.
//!
//! \param[in] u32Lba Logical Block Address to convert
//! \param[in] pu32PhysBlkAddr Pointer to actual Physical Block Address
//! \param[in] eBlkType Characteristic (shown above)
//! \param[in] constraints
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t  Mapper::getBlockAndAssign(
    uint32_t u32Lba,
    uint32_t * pu32PhysBlkAddr,
    MapperBlockTypes_t eBlkType,
    const AllocationConstraints * constraints)
{
    RtStatus_t rtCode;

    // Allocate the block.
    rtCode = getBlock(pu32PhysBlkAddr, eBlkType, constraints);
    if (rtCode)
    {
        return rtCode;
    }

    // Assign this physical block to LBA in zone-map
    rtCode = setBlockInfo(u32Lba, *pu32PhysBlkAddr);
    if (rtCode)
    {
        return rtCode;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Mark a block as bad/used or unused.
//!
//! A block has been allocated by the mapper. The mapper should mark this
//! physical address as bad or unallocated in the Erased Block Table (essentially this block
//! is never released.  It should be noted that the LBA should never be marked
//! bad. The Erased Block Table must be updated .  Thus in this case, the
//! mapper will:
//!    - Mark the physical block as bad or unused; deallocate the LBA <-> physical block
//!     association, so that it looks like this LBA location has not been
//!     allocated yet.
//!    - Update the Erased Block table to show this block as unerased so that
//!     it won't be allocated.
//!    - Same as above. In addition to that, it will allocate a new physical
//!     block and returns this new address to the caller.
//!
//! \param[in] u32Lba Logical block address to mark as bad.
//! \param[in] u32PhysBlkAddr Pointer to actual physical block address.
//! \param[in] bUsedorUnused Unused == true, used = false.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR
//!
//! \todo Do we really need to compare the phy block?
//! \todo Should we really always be setting the LBA to be unallocated even if
//!     marking the block as used?
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::markBlock(uint32_t u32Lba, uint32_t u32PhysBlkAddr, bool bUsedorUnused)
{
    uint32_t u32ComparePhysBlkAddr;
    RtStatus_t ret;

    // Read the Physical block address from the zone map and confirm that the two values
    // one from the API and one the Zone map are identical
    ret = getBlockInfo(u32Lba, &u32ComparePhysBlkAddr);
    if (ret)
    {
        return ret;
    }

    // Verify that the physical block associated with the LBA is what we expect.
    if (u32PhysBlkAddr != u32ComparePhysBlkAddr)
    {
        return ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR;
    }

    // This does not mean that the lba is bad, it really means that the physical block
    // associated with this lba is bad. As a result, we should only mark the
    // the corresponding location in the available phy block location as allocated
    ret = m_physMap->markBlock(u32PhysBlkAddr, bUsedorUnused, PhyMap::kAutoErase);
    if (ret)
    {
        return ret;
    }

    // We also mark the Zone map associated with this LBA as unallocated.
    ret = setBlockInfo(u32Lba, m_unallocatedBlockAddress);
    if (ret)
    {
        return ret;
    }

#ifdef DEBUG_MAPPER2
    if (bUsedorUnused)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                "Marking P%d as Used.\n", u32PhysBlkAddr);
    }
    else
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                "Marking P%d as Unused.\n", u32PhysBlkAddr);
    }
#endif

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Flush the contents of the zone map and phy map to the NAND.
//!
//! Writes any dirty sections of the zone map held in the cache to the zone map
//! block. This will trigger a consolidation of the zone map if the block
//! becomes full. The phy map is also written to media in its own block. A new
//! block is allocated and erased by this function for the phy map.
//!
//! \return Status of call or error.
//! \retval SUCCESS Zone map cache has been flushed successfully.
//!
//! \pre The zone map cache is dirty.
//! \post The zone map dirty flag is cleared.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::flush()
{
    RtStatus_t ret;

    // Don't do anything if the cache is clean.
    if (!m_isMapDirty)
    {
         return SUCCESS;
    }

start_flush:
    
    // Maps are no longer dirty.
    bool wasPhyMapDirty = m_physMap->isDirty();
    m_physMap->clearDirty();
    clearDirtyFlag();

    // Flush out the zone map.
    ret = m_zoneMap->flush();
    if (ret != SUCCESS)
    {
        return ret;
    }
    
    // Save the phy map to media if it's dirty.
    if (wasPhyMapDirty)
    {
        ret = m_phyMapOnMedia->save();
        if (ret)
        {
            return ret;
        }
    }
    
    // Handle the case where writing one of the maps caused the other map to become
    // dirty by flushing everything again. This can happen if one of the maps is full
    // and has to be consolidated into a newly allocated block.
    if (m_isMapDirty)
    {
        tss_logtext_Print(~0, "maps were dirtied during flush! trying to flush again...\n");
        goto start_flush;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Search the NAND for a zone map.
//!
//! Search the reserved block range for a zone map block.
//!
//! \param[in]  eMapType Which Map type (Zone Map or Erased Block Table)?
//! \param[out]  pu32PhysBlkAddr Pointer to actual Physical Block Address
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::findMapBlock(MapperMapTypes_t eMapType, uint32_t * pu32PhysBlkAddr)
{
    int32_t i;
    bool bBlockIsLBA;
    RtStatus_t RtStatus;

    // Start searching at the first region.
    for (i = m_reserved.startBlock; i <= m_reserved.endBlock; i++)
    {
        bBlockIsLBA = isBlockMapBlock(i, eMapType, &RtStatus);

        if (SUCCESS != RtStatus)
        {
            return RtStatus;
        }
        
        // If there isn't a match, continue search.
        if (bBlockIsLBA)
        {
            *pu32PhysBlkAddr = i;
            return SUCCESS;
        }
    }

    return ERROR_DDI_NAND_MAPPER_FIND_LBAMAP_BLOCK_FAILED;
}

////////////////////////////////////////////////////////////////////////////////
//! This method should be called whenever a new bad block is encountered in
//! the area of the NAND managed by the mapper. It updates the phymap, marks
//! the block itself as bad, and updates the region that owns the block. For
//! data regions this only means incrementing the bad block count. The DBBT is
//! scheduled for update as a result of updating the region.
//!
//! \param badBlockAddress Absolute physical address of the block that went bad.
////////////////////////////////////////////////////////////////////////////////
void Mapper::handleNewBadBlock(const BlockAddress & badBlockAddress)
{
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** New bad block %u! ***\n", badBlockAddress.get());
    
    // Mark the block as bad in the phymap.
    m_physMap->markBlockUsed(badBlockAddress);

    // Now write the bad block markers.
    Block(badBlockAddress).markBad();
    
    // Add the bad block to the appropriate region. For data regions this will only
    // increment the region count. This also causes the DBBT to be rewritten.
    Region * region = m_media->getRegionForBlock(badBlockAddress);
    if (region)
    {
        region->addNewBadBlock(badBlockAddress);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
//! This function searches for and erases all occurrences of zone-map and phymap.
//! This is done when we find out that power was lost.  Consequently, we cannot trust
//! zone-map and phys-map which is stored in Nand.
/////////////////////////////////////////////////////////////////////////////////////////
void Mapper::searchAndDestroy()
{
    int32_t    i;
    RtStatus_t retCode;
    
    //! Start searching at the first region.
    for (i = m_reserved.startBlock; i <= m_reserved.endBlock; i++)
    {
        // If there isn't a match, continue search.
        if (isBlockMapBlock(i, kMapperZoneMap, &retCode)
            || isBlockMapBlock(i, kMapperPhyMap, &retCode))
        {
            m_physMap->markBlockFreeAndErase(i);
        }

    }

    // Clear the valid flags for the maps.
    m_isZoneMapCreated = false;
    m_isPhysMapCreated = false;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Rebuild the Zone Map and phy map from RA data.
//!
//! This function will search the entire NAND,
//! by reading the RA, and extract the LBA <-> Physical Block Address
//! information so that a Zone map can be created.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::createZoneMap()
{
    RtStatus_t ret;
    
    // Mark that we are building the maps from scratch.
    m_isBuildingMaps = true;
    AutoClearFlag buildingMapsFlagController(m_isBuildingMaps);
    
    // Erase any pre-existing map blocks from the media. This is necessary, for instance,
    // if the phy map was written successfully but upon init the zone map could not be
    // found for some reason, thus causing CreateZoneMap() to be called. Repeat this
    // process over and over, and you leak phy map blocks.
    searchAndDestroy();

    // Get a buffer to hold the redundant area.
    AuxiliaryBuffer auxBuffer;
    if ((ret = auxBuffer.acquire()) != SUCCESS)
    {
        return ret;
    }
    
    // This is needed to satisfy check at beginning of setBlockInfo(),
    // which is called from inside the following loop.
    m_isInitialized = true;

    // Don't let the NAND go to sleep during the scans.
    NandHal::SleepHelper disableSleep(false);
    
    if (m_prebuiltPhymap)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
            "Using phymap built during allocation instead of scanning again\n");

        // Validate the number of entries.
        assert(m_prebuiltPhymap->getEntryCount() == PhyMap::getEntryCountForBlockCount(m_media->getTotalBlockCount()));
        
        // Make use of the prebuilt phymap that someone so kindly provided us.
        m_physMap = m_prebuiltPhymap;
        m_prebuiltPhymap = NULL;
        
        assert(m_phyMapOnMedia);
        m_phyMapOnMedia->setPhyMap(m_physMap);
        
        assert(m_blockAllocator);
        m_blockAllocator->setPhyMap(m_physMap);
        
        assert(m_mapAllocator);
        m_mapAllocator->setPhyMap(m_physMap);

        // Set our dirty change listener in the phymap, since it won't be set since we didn't
        // create this phymap instance.
        m_physMap->setDirtyCallback(phymapDirtyListener, this);
    }
    else
    {
        // Nobody gave us a phymap, so we have to build one of our own.
        ret = scanAndBuildPhyMap(auxBuffer);
        if (ret != SUCCESS)
        {
            return ret;
        }
    }
    
    // The phymap has been filled in, so we want to write it out to the NAND. We have to
    // save a new copy because we erased all resident maps above.
    ret = m_phyMapOnMedia->saveNewCopy();
    if (ret)
    {
        return ret;
    }

    // This function writes the cache buffer with all unallocated entries for every
    // section of the zone map. This is done so that there is at least a default entry
    // for every zone map section and entry.
    ret = m_zoneMap->writeEmptyMap();
    if (ret)
    {
        return ret;
    }

    // Scan the NAND to build the zone map.
    ret = scanAndBuildZoneMap(auxBuffer);
    if (ret != SUCCESS)
    {
        return ret;
    }

    // The maps have now been created.
    m_isZoneMapCreated = true;
    m_isPhysMapCreated = true;

    // We want zone-map to be written out during FlushToNand
    // regardless of whether or not anything has changed.
    // Otherwise the next time device boots up, zone-map
    // will be created again instead of being loaded.
    setDirtyFlag();

    return SUCCESS;
}

RtStatus_t Mapper::scanAndBuildPhyMap(AuxiliaryBuffer & auxBuffer)
{
    assert(m_physMap);

    RtStatus_t ret;

    // Zero out the phys map so that all blocks are marked used.
    m_physMap->markAll(PhyMap::kUsed);
    
    // Create an iterator over all of the media's regions.
    Region::Iterator it = m_media->createRegionIterator();
    Region * region;
    
    SimpleTimer timer;
    
    // first loop to fill in phy-map
    while ((region = it.getNext()))
    {
        // System Drives need to be marked as used in the map so only check Data Drives.
        if (!region->isDataRegion())
        {
            continue;
        }
        
        int32_t numBlocksInRegion = region->m_iNumBlks;
        Block blockInRegion(region->m_u32AbPhyStartBlkAddr);
        
        for (; numBlocksInRegion; --numBlocksInRegion, ++blockInRegion)
        {
            assert(blockInRegion < m_media->getTotalBlockCount());
            
            // Check to see if the block is bad or not
            if (blockInRegion.isMarkedBad(auxBuffer))
            {
                // mark the block bad in phys map
                // Since this array contains the map across all chips, we need to add the
                // offset from all previous chips.
                ret = m_physMap->markBlockUsed(blockInRegion);
                if (ret)
                {
                    return ret;
                }
                
                continue;
            }
            
            // The block is good, so what kind of block is it?
            ret = blockInRegion.readMetadata(kFirstPageInBlock, auxBuffer);
            if (ret == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
            {
                // Mark the location in the available block as unused, which will also erase it.
                // Note that this will destroy data, but there is no other choice at this point.
                ret = m_physMap->markBlockFreeAndErase(blockInRegion);
                
                // On to the next block
                continue;
            }
            else if (!is_read_status_success_or_ecc_fixed(ret))
            {
                // Some other error occurred, that we cannot process.
                #ifdef DEBUG_MAPPER2
                tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                            "Problem reading first page of block %u, ret=0x%08x\n", blockInRegion.get(), ret);
                #endif
                
                return ret;
            }

            // Get Logical Block Address and Relative Sector Index from RA
            Metadata md(auxBuffer);

            // if Erased, the this block has not been allocated
            if (md.isErased())
            {
                // Mark the location in the available block as free. No need to erase since
                // we've already checked that.
                ret = m_physMap->markBlockFree(blockInRegion);
            }
            else
            {
                // Mark the location in the available block as taken
                ret = m_physMap->markBlockUsed(blockInRegion);
            }

            if (ret)
            {
                return ret;
            }
        }
    }

    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
        "Scanning to build phy map took %d ms\n", uint32_t(timer.getElapsed() / 1000));
    
    return SUCCESS;
}

RtStatus_t Mapper::scanAndBuildZoneMap(AuxiliaryBuffer & auxBuffer)
{
    RtStatus_t ret;

    Region::Iterator it = m_media->createRegionIterator();
    Region * region;
    
    ConflictResolver m_cr(this);
    m_cr.setRange(m_reserved.endBlock + 1, m_media->getTotalBlockCount() - 1);
    m_cr.invalidate();

    VirtualBlock::determinePlanesToUse(); 
    unsigned int L = 32 - __CLZ32(VirtualBlock::getVirtualPagesPerBlock()-1);
    unsigned int u32Mask = (-1) << L;
    uint8_t u8Mask =  ( u32Mask >> 8 );

    SimpleTimer timer;
    
    // second loop to fill in zone-map
    while ((region = it.getNext()))
    {
        if (!region->isDataRegion())
        {
            continue;
        }
        
        int32_t numBlocksInRegion = region->m_iNumBlks;
        Block blockInRegion(region->m_u32AbPhyStartBlkAddr);
        
        for (; numBlocksInRegion; --numBlocksInRegion, ++blockInRegion)
        {
            assert(blockInRegion < m_media->getTotalBlockCount());
        
            // Skip over blocks that are not marked as used in the phymap or are marked bad.
            if (!m_physMap->isBlockUsed(blockInRegion) || blockInRegion.isMarkedBad(auxBuffer))
            {
                continue;
            }
            
            ret = blockInRegion.readMetadata(kFirstPageInBlock, auxBuffer);
            if (ret == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
            {
                // Mark the location in the available block as unused, which will also erase it.
                // Note that this will destroy data, but there is no other choice at this point.
                m_physMap->markBlockFreeAndErase(blockInRegion);
            
                // On to the next block
                continue;
            }
            else if (!is_read_status_success_or_ecc_fixed(ret))
            {
                // Some other error occurred, that we cannot process.
                #ifdef DEBUG_MAPPER2
                tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                            "Problem reading first page of block %u, ret=0x%08x\n", blockInRegion.get(), ret);
                #endif
            
                return ret;
            }

            // Get Logical Block Address and Relative Sector Index from RA
            Metadata md(auxBuffer);
            uint32_t u32LogicalBlockAddr = md.getLba();

            // if Erased, the this block has not been allocated
            if (md.isErased())
            {
                continue;
            }
            
            // Check to see if this is a system block or not. If it is then ignore the LBA.
            // The bottom half-word of the Stmp code is equivalent to the RSI.
            uint16_t rsiFull = md.getSignature() & 0xffff;
            //uint8_t rsi1 = rsiFull & 0xff;  // Low byte of the Stmp code.
            //Note: Permissible LSI value is 0-511 as a result 9 bits are required
            // Mask = (~((1<<L)-1)) = 0xfffffe00
            //rsi1 = rsiFull & (Mask>>8)
            // Where 
            //      A generic equation can be
            //      L = log 2 of ( PagesPerBlock * Plane ) 
            // For PagePerBlock = 256 and Plane = 2. L = 9 
            // As a result 1st byte should use 0xfe            
            uint8_t rsi1 = rsiFull & u8Mask;  // Low byte of the Stmp code. 

            // If this block is the zone or phy map (indicated by a valid Stmp code),
            // then skip it. It's not the zone map if either the full RSI half-word is 0,
            // or the high byte of the RSI is 0 and the LBA is valid (within range).
            if (((rsi1 == 0) && (u32LogicalBlockAddr < m_media->getTotalBlockCount())) || (rsiFull == 0))
            {
                // Allocated this block in the zone map
                if (u32LogicalBlockAddr > m_media->getTotalBlockCount())
                {
                    // Something is seriously wrong with what was in
                    // redundant area.  Ignore for now and continue.
                    // Mark the location in the available block as unused, which will also erase it.
                    // Note that this will destroy data, but there is no other choice at this point.
                    m_physMap->markBlockFreeAndErase(blockInRegion);
                    
                    continue;
                }

                uint32_t u32PhysicalBlockNumber;
                ret = getBlockInfo(u32LogicalBlockAddr, &u32PhysicalBlockNumber);
                if (ret)
                {
                    return ret;
                }

                if (!isBlockUnallocated(u32PhysicalBlockNumber) && (u32PhysicalBlockNumber != blockInRegion))
                {
                    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                        "LBA conflict for virtual block %u between physical blocks %u and %u\n", 
                        u32LogicalBlockAddr, u32PhysicalBlockNumber, blockInRegion);

                    m_cr.addBlocks(u32LogicalBlockAddr, u32PhysicalBlockNumber);
                    m_cr.addBlocks(u32LogicalBlockAddr, blockInRegion);                    
                }
                else
                {
                    ret = setBlockInfo(u32LogicalBlockAddr, blockInRegion);
                } 

                if (ret)
                {
                    return ret;
                }
            }
        }
    }

    m_cr.resolve();

    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
        "Scanning to build zone map took %d ms\n", uint32_t(timer.getElapsed() / 1000));
    
    return SUCCESS;
}

void ConflictResolver::setRange(uint32_t start, uint32_t end)
{
    startBlock = start;
    endBlock   = end;
}

int ConflictResolver::resolve(void)
{
    uint32_t planeCount = VirtualBlock::getPlaneCount();
    int ret=0;
    // Initialize hybrid map
    m_map.init(VirtualBlock::getVirtualPagesPerBlock(),0);
    for (int i=0; i<m_count; i++)
    {
        m_map.clear(); 
        simplify(m_conflicts[i]);
        // Perform quick merge whenever possible
        // Fastest way to solve 2 block conflict in 2 plane configuration is to assign physical blocks in zonemap.
        if ( m_conflicts[i].m_phy_count == 2 && m_conflicts[i].m_Lba != m_conflicts[i].m_Lba2 && planeCount == 2)
        {
            uint32_t uBlk;
            m_mapper->getBlockInfo(m_conflicts[i].m_Lba, &uBlk);
            if ( uBlk == m_conflicts[i].m_phyBlocks[0] )
            {   // update only other
                m_mapper->setBlockInfo(m_conflicts[i].m_Lba2, m_conflicts[i].m_phyBlocks[1]);        
            }
            else
            {
                m_mapper->setBlockInfo(m_conflicts[i].m_Lba,  m_conflicts[i].m_phyBlocks[1]);
                m_mapper->setBlockInfo(m_conflicts[i].m_Lba2, m_conflicts[i].m_phyBlocks[0]);        
            }
            // Just update offsets into zonemap
        }
        else
        {
            // Handles all cases for single plane.
            // Handles all cases where physical block conflicts are more than 2 in multi-block configuration.
            // Perform complete merge
            merge(m_conflicts[i]);        
        }
    }
    m_map.cleanup();
    return ret;
}

#include "PageOrderMap.h"

void ConflictResolver::addPhyBlock(ConflictingEntry_t &conflict, uint32_t phyBlock)
{
    // Check for boundry condition
    if ( conflict.m_phy_count == kMaxConflictingPhysicalBlocks )
        return;
        
    uint32_t *phyBlocks = &conflict.m_phyBlocks[0];
    for (int index = 0; index < kMaxConflictingPhysicalBlocks; index++ )
    {
        // Avoid duplicate
        if ( phyBlocks[index] == phyBlock )
            return;
    }
    phyBlocks[conflict.m_phy_count] = phyBlock;
    conflict.m_phy_count++; 
}

HybridOrderedMap::HybridOrderedMap()
{
    m_count                 = 0;
    m_latestBlockIndex      = 0;
    m_PhyBlockIndexForPage  = NULL;
    memset(m_phyBlocks, 0,  sizeof(m_phyBlocks));
    memset(m_NumUsedSectors,    0,  sizeof(m_NumUsedSectors));
}
HybridOrderedMap::~HybridOrderedMap()
{
}
    
int HybridOrderedMap::init(unsigned entryCount, unsigned maxEntryValue) 
{
    PageOrderMap::init(entryCount,maxEntryValue);
    m_PhyBlockIndexForPage = (uint8_t *)malloc(m_entryCount);
    if ( !m_PhyBlockIndexForPage )
    {
        return ERROR_OUT_OF_MEMORY; 
    }
    memset(m_PhyBlockIndexForPage, -1, m_entryCount);
    return SUCCESS; 
}

void HybridOrderedMap::cleanup() 
{
    if ( m_occupied)
    {
        free(m_PhyBlockIndexForPage);
        m_PhyBlockIndexForPage = NULL;
    }
    PageOrderMap::cleanup();
}

int HybridOrderedMap::update(PageOrderMap &map,uint32_t u32PhysicalBlock, uint32_t u32NumUsedSectors)
{
    uint32_t u32PagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    uint32_t logicalSector;
    
    if ( m_count == kMaxPhyBlocks )
        return -1;
    
    m_phyBlocks[m_count]      = u32PhysicalBlock;
    m_NumUsedSectors[m_count] = u32NumUsedSectors;
    
    for (logicalSector=0; logicalSector < u32PagesPerBlock; logicalSector++)
    {
        // If page is only in new map, save it.
        if ( map.isOccupied(logicalSector) && !isOccupied(logicalSector) )
        {
            // Add entry in new map
            setEntry(logicalSector,map.getEntry(logicalSector) );
            setOccupied(logicalSector);
            m_PhyBlockIndexForPage[logicalSector] = m_count;
        }
        // If entry is present in both maps
        else if ( map.isOccupied(logicalSector) && isOccupied(logicalSector) )
        {
            uint8_t index = m_PhyBlockIndexForPage[logicalSector];
            // Give presedence to least updated block.
            if ( m_NumUsedSectors[index] > u32NumUsedSectors )
            {
                setEntry(logicalSector,map.getEntry(logicalSector) );
                setOccupied(logicalSector);
                m_PhyBlockIndexForPage[logicalSector] = m_count;
            } 
        }
    }
    m_count++;
    return SUCCESS;
}

int HybridOrderedMap::getPhyBlock(int logicalSector)
{
    return m_phyBlocks[m_PhyBlockIndexForPage[logicalSector]];
}

void HybridOrderedMap::clear(void)
{
    m_count = 0;
    if ( m_PhyBlockIndexForPage )
    {
        memset(m_PhyBlockIndexForPage,0xff,m_entryCount);
    }
    
    PageOrderMap::clear();
}

int ConflictResolver::simplify(ConflictingEntry_t &conflict)
{
    int ret,badBlockCount=0;
    uint32_t u32NumUsedSectors, u32PagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    uint32_t *phyBlocks;
    PageOrderMap map;
    bool bOtherBlockAdded = false;
    
    // Initialize temporary map
    map.init(u32PagesPerBlock);
    map.clear();
    conflict.m_Lba2 = conflict.m_Lba;
    
    phyBlocks = &conflict.m_phyBlocks[0];
    
    // Allocate and build necessary PageOrderMaps for physical blocks
    for (int index = 0 ; index < conflict.m_phy_count-badBlockCount; index++ )
    {
        ret = buildPartialMapFromMetadata(
                conflict.m_Lba,
                phyBlocks[index], 
                map, 
                &u32NumUsedSectors,
                &conflict.m_Lba2);
        if (ret == SUCCESS)
        {
            m_map.update(map,phyBlocks[index],u32NumUsedSectors);
            map.clear();
            // Add other block for analysis as well.    
            if ( conflict.m_Lba2 != conflict.m_Lba && !bOtherBlockAdded)
            {
                uint32_t otherBlock;
                if ( m_mapper->getBlockInfo(conflict.m_Lba2,&otherBlock) == SUCCESS )
                {
                    if ( !m_mapper->isBlockUnallocated(otherBlock) )
                        addBlocks(conflict.m_Lba,otherBlock);
                }
                bOtherBlockAdded = true;
            }
        }
        else if (ret != SUCCESS)
        {
            // Mark this entry bad. Possible options
            // 1. Try to recover as many pages as possible
            // 2. Mark block bad, and forget it.
            badBlockCount++;
            
            // For now choosing 2nd option
            Block badBlock(phyBlocks[index]);
            m_mapper->handleNewBadBlock(badBlock);                
            
            // Remove this element from block analysis
            for (int i=index; i<conflict.m_phy_count-1; i++)
            {
                 conflict.m_phyBlocks[i] = conflict.m_phyBlocks[index+1];
            }                
            continue;
        }
    }
    conflict.m_phy_count -= badBlockCount;

    return 0;
} 


ConflictResolver::ConflictResolver(Mapper *mapper)
{
    m_count  = 0;
    m_mapper = mapper;
}
ConflictResolver::~ConflictResolver() {}

// Invalidate internal lists
void ConflictResolver::invalidate(void)
{
    m_count = 0;
    memset(&m_conflicts[0],0xff,sizeof(m_conflicts));
    for (int index = 0; index < kMaxConflicts; index++)
    {
        m_conflicts[index].m_phy_count = 0;
    }
}

int ConflictResolver::addBlocks(uint32_t u32LogicalBlockAddr,uint32_t u32PhysicalBlockNumber)
{
    int index;
    uint32_t planeCount = VirtualBlock::getPlaneCount();
        
    if ( m_count == kMaxConflicts )
        return -1; //TODO: Add some meaningful error code with description here and handle error in upper stack.
        
    // Find plane-0 LBA of virtual block
    // 1st block in region is LBA0 or plane-0, so 
    // Find 1st block based on block allocator
    // This equation is fine for 2 planes
    // Actual equation is 
    // diff = (u32LogicalBlockAddr - startBlock) % planeCount;
    // u32LogicalBlockAddr -= diff;
    if ( ( (u32LogicalBlockAddr - startBlock ) & 1) == 1 && planeCount != 1)
        u32LogicalBlockAddr--;    
    
    // Search if LBA entry is already present
    for (index  = 0 ; index  < m_count; index ++)
    {
         if ( m_conflicts[index].m_Lba == u32LogicalBlockAddr )
         {
             break;
         }
    }
    // Add or update entry
    m_conflicts[index].m_Lba = u32LogicalBlockAddr;
    addPhyBlock(m_conflicts[index], u32PhysicalBlockNumber);
    // Increment conflict count
    if ( index == m_count)
        m_count++;
    return SUCCESS;
}

#include "NonsequentialSectorsMap.h"

int ConflictResolver::merge(ConflictingEntry_t &conflict)
{
    uint32_t pagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    uint32_t planeCount = VirtualBlock::getPlaneCount();
    uint32_t u32RetryCount = 0;
    RtStatus_t status;
    
    // Time the whole merge.
    SimpleTimer mergeTimer;
                                       
    // Get a sector buffer.
    SectorBuffer sectorBuffer;
    if ((status = sectorBuffer.acquire()) != SUCCESS)
    {
        return status;
    }

    AuxiliaryBuffer auxBuffer;
    if ((status = auxBuffer.acquire()) != SUCCESS)
    {
        return status;
    }
    
    // Allocate the order map for the new block we're merging into.
    PageOrderMap targetMap;
    status = targetMap.init(pagesPerBlock);
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Create a copy of our virtual block and allocate new physical blocks to merge into. The
    // source physical blocks will still be saved in m_virtualBlock.
    VirtualBlock m_virtualBlock(m_mapper);// = conflict.m_Lba;
    m_virtualBlock = conflict.m_Lba;
    VirtualBlock targetBlock = m_virtualBlock;
    status = targetBlock.allocateAllPlanes();
    if (status != SUCCESS)
    {
        return status;
    }

    // Create our filter.
    NonsequentialSectorsMap::CopyPagesFlagFilter copyFilter;

    // For each sector, first look up the sector in new Non-sequential sector map.
    // If entry in new non-sequential sector map is invalid, look up in old
    // non-sequential sector map.
copy_loop_start:
    int32_t logicalSector;
    uint32_t runPageCount = 0;
    int32_t runStartPage = -1;
    uint32_t targetVirtualPageOffset = 0;
    uint32_t startEntry = 0;    // Logical sector offset for the start of the run.
    
    // Clear the set-logical-order flag in case we had to start the loop over due to a failed write.
    copyFilter.setLogicalOrderFlag(false);
    
    for (logicalSector=0; logicalSector < pagesPerBlock; logicalSector++)
    {
        // Write page only if it is occupied.
        if ( m_map.isOccupied(logicalSector) == false )
            continue;
        
        PageAddress sourcePage;
        runPageCount = 1;
        // Copy the current run if there is at least one page in it.
        // Even though we compute runs of sequential virtual page offsets to copy, we currently
        // only copy one page at a time.
        while (runPageCount)
        {
            uint32_t pageOffset;
            if ( planeCount == 2 || planeCount == 1 )
            {
                pageOffset = (m_map[logicalSector] >> (planeCount - 1));
            }
            else
            {
                pageOffset = m_map[logicalSector] / planeCount;
            }
            BlockAddress baddr(m_map.getPhyBlock(logicalSector));
            sourcePage = PageAddress(baddr, pageOffset);

            PageAddress targetPage;
            if (targetBlock.getPhysicalPageForVirtualOffset(targetVirtualPageOffset, targetPage) != SUCCESS)
            {
                break;
            }
            
            NandPhysicalMedia * sourceNand = sourcePage.getNand();
            NandPhysicalMedia * targetNand = targetPage.getNand();
            
            // Initialize metadata for movePage operation
            Metadata md(auxBuffer);            
            md.prepare(m_virtualBlock.getMapperKeyFromVirtualOffset(targetVirtualPageOffset),startEntry); //runStartPage);
            md.clearFlag(Metadata::kIsInLogicalOrderFlag);
            if (startEntry == pagesPerBlock - 1 && targetMap.isInSortedOrder(pagesPerBlock - 1))
            {
                md.setFlag(Metadata::kIsInLogicalOrderFlag);
            }
            else
            {
                md.clearFlag(Metadata::kIsInLogicalOrderFlag);
            }
            
            // See if we need to set the logical order flag. We only want to do this when
            // copying the last logical page and all previous pages were in order.
            if (startEntry == pagesPerBlock - 1 && targetMap.isInSortedOrder(pagesPerBlock - 1))
            {
                copyFilter.setLogicalOrderFlag(true);
                
                //++getStatistics().mergeSetOrderedCount;
            }
            copyFilter.setLba(m_virtualBlock.getMapperKeyFromVirtualOffset(targetVirtualPageOffset));

            // Copy a single page.
            uint32_t successfulCopies = 0;
            status = sourceNand->copyPages(
                targetNand,
                sourcePage.getRelativePage(),
                targetPage.getRelativePage(),
                1,
                sectorBuffer,
                auxBuffer,
                &copyFilter,
                &successfulCopies);
            
            // Handle benign ECC stati. It doesn't matter if we get a rewrite sector status
            // because we are already copying into a new block.
            if (is_read_status_success_or_ecc_fixed(status))
            {
                status = SUCCESS;
            }
            
            // Update target map and page offset based on how many pages were copied.
            if (successfulCopies)
            {
                targetMap.setSortedOrder(startEntry, successfulCopies, targetVirtualPageOffset);
                targetVirtualPageOffset += successfulCopies;
                runStartPage += successfulCopies;
                runPageCount -= successfulCopies;
                startEntry += successfulCopies;
            }

            // Deal with different error codes from the page copy.
            if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
            
                tss_logtext_Print(~0,"ECC failure at time of resolve conflict\n");
                // Writing to the third block failed, so mark the block as bad, pick a
                // new target block, and restart the merge sequence. We'll repeat this up
                // to 10 times.
                u32RetryCount++;
                if (u32RetryCount>10)
                {
                    return status;
                }
                
                unsigned failedPlane = targetBlock.getPlaneForVirtualOffset(targetVirtualPageOffset);

                BlockAddress physicalBlockAddress;
                
                // Handle the bad block and allocate a new block for the failed plane. Also,
                // we have to erase blocks for the other planes that are still good before
                // we can restart the merge. Unfortunately, since we are erasing, it's possible
                // for more blocks to go bad and we have to handle that!
                unsigned thePlane;
                for (thePlane=0; thePlane < VirtualBlock::getPlaneCount(); ++thePlane)
                {
                    // This address should already be cached, so we shouldn't be getting any
                    // errors here.
                    status = targetBlock.getPhysicalBlockForPlane(thePlane, physicalBlockAddress);
                    if (status != SUCCESS)
                    {
                        return status;
                    }
                    
                    // Reallocate the failed plane.
                    bool doReallocate = true;
                    
                    // For other planes we try to erase, and only reallocate if the erase fails.
                    if (thePlane != failedPlane)
                    {
                        // We cannot just pass a Block instance into the above call because the
                        // methods are not virtual.
                        Block thisBlock(physicalBlockAddress);
                        doReallocate = (thisBlock.erase() == ERROR_DDI_NAND_HAL_WRITE_FAILED);
                    }
                    
                    if (doReallocate)
                    {
                        // Deal with the new bad block.
                        //TODO: Let mapper handle bad block
                        //getMapper()->handleNewBadBlock(physicalBlockAddress);
                        
                        // Now reallocate the phy block for this plane.
                        status = targetBlock.allocateBlockForPlane(thePlane, physicalBlockAddress);
                        if (status != SUCCESS)
                        {
                            return status;
                        }
                    }
                }

                // Reset the target block map.
                targetVirtualPageOffset = 0;
                targetMap.clear();
                
                // Restart the whole merge loop.
                goto copy_loop_start;
            }
            else if (status == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
            {
                //! \todo If we have a copy of this sector in the backup block, then we could use
                //! that as a replacement. This really isn't ideal, though, as data will still be
                //! lost. Also, there may be prior versions of the sector in the new block as well,
                //! and those would be more recent than any copy in the backup block.
                //!
                //! \todo We should probably finish the merge first so we don't lose even more data!
                return status;
            }
            else if (status)
            {
                // Got some other error while copying pages, so just return it.
                return status;
            }
        }
    }
    
    // Free physical blocks for which there was conflict.
    for (int j=0; j<conflict.m_phy_count; j++)
    {
        Block tempBlock(conflict.m_phyBlocks[j]);
        tempBlock.erase();
    }
    
    return SUCCESS;
}

int ConflictResolver::buildPartialMapFromMetadata(
    uint32_t blockNumber,
    uint32_t physicalBlock, 
    PageOrderMap &map, 
    uint32_t * filledSectorCount,
    uint32_t *otherBlock)
{
    uint32_t virtualPagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    uint32_t thisVirtualOffset;
    RtStatus_t retCode;
	RtStatus_t retCodeLocal;
    uint32_t u32LogicalSectorIdx;
    uint32_t topVirtualOffsetToRead;
    int iReads;
//    RelocateVirtualBlockTask * relocateTask = NULL;
    PageAddress tempPageAddress;
    VirtualBlock m_virtualBlock(m_mapper);

    m_virtualBlock = blockNumber;  //TODO: Question: Is this correct ?
    Block phyBlock(physicalBlock);
    m_virtualBlock.setPhysicalBlockForPlane(0,phyBlock);
    
    // Time the building of the map.
    SimpleTimer buildTimer;

    // Create the page object and get a buffer to hold the metadata.
    Page thePage;
    retCode = thePage.allocateBuffers(false, true);
    if (retCode != SUCCESS)
    {
        return retCode;
    }
    
    // Go ahead and get our metadata instance since the buffer addresses won't change.
    Metadata & md = thePage.getMetadata();

    // First, clear the map before we fill it in.
    map.clear();
    
    // RA of last page is read already, we don't need read it in the below loop
    topVirtualOffsetToRead = virtualPagesPerBlock/2;// - 1;
    
    for (thisVirtualOffset=0; thisVirtualOffset < topVirtualOffsetToRead; thisVirtualOffset++)
    {
        thePage = PageAddress(physicalBlock,thisVirtualOffset);

        // Reading this information is very important.  If there is
        // some kind of failure, we will re-try.
        iReads = 0;
        do
        {
			NandEccCorrectionInfo_t eccInfo;

        	// read Redundant Area of Sector
            retCodeLocal = thePage.readMetadata(&eccInfo);

#if DEBUG && NSSM_INDUCE_ONE_PAGE_FAILURE
            // A flag to cause one sector to be omitted from the NSSM.
            if ( stc_bNSSMInduceOnePageFailure )
            {
                retCodeLocal = ERROR_GENERIC;
            }
#endif
            
#if LOG_NSSM_METADATA_ECC_LEVELS
            if (retCodeLocal)
            {
                log_ecc_failures(pRegion, physicalBlock, u32NS_SectorIdx, &eccInfo);
            }
#endif // LOG_NSSM_METADATA_ECC_LEVELS

/*
            // We are anyway going to erase this block so it is fine to ignore ECC_FIXED case            
            if (retCodeLocal == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    ">>> Got ECC_FIXED_REWRITE_SECTOR reading metadata of vblock %u pblock %u voffset %u\n", m_virtualBlock.get(), thePage.getBlock().get(), thisVirtualOffset);
                
                // Post a deferred task to rewrite this virtual block since it is now marginal.
                if (!relocateTask)
                {
                    relocateTask = new RelocateVirtualBlockTask(m_manager, m_virtualBlock);
                    getMedia()->getDeferredQueue()->post(relocateTask);
                }
            }
*/
            // Convert ECC_FIXED or ECC_FIXED_REWRITE_SECTOR to SUCCESS...
            if (is_read_status_success_or_ecc_fixed(retCodeLocal))
            {
                retCodeLocal = SUCCESS;
            }
        
            // ...and note other errors.
            if (retCodeLocal != SUCCESS)
            {
                // Print an advisory message that there was an error on one page.
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    "buildMapFromMetadata: read %d failed on page 0x%x, status 0x%x\n", 
                    iReads, thePage.get(), retCodeLocal);
            }
        } while ( retCodeLocal != SUCCESS  &&  ++iReads < 1); //TODO: patch max tries here MAX_BUILD_NSSM_READ_TRIES );

#if DEBUG && NSSM_INDUCE_ONE_PAGE_FAILURE
        // A flag to cause one sector to be omitted from the NSSM.
        stc_bNSSMInduceOnePageFailure = false;
#endif

        // Okay, did the reads work?
        if (SUCCESS != retCodeLocal)
        {
            // No, the reads did not work.
            // We still want to use any remaining sectors, so we will continue on.
            continue;
        }

        // If we got here, then we were successful reading the sector.
        // We set retCode accordingly, to indicate that SOMETHING worked.
        retCode = SUCCESS;

        // If erased, then exit the loop. Physical pages are written sequentially within a
        // block, so we know there's no more data beyond this.
        if (md.isErased())
        {
            break;
        }
        
        // Get the virtual block address and logical sector index from the page's metadata.
        u32LogicalSectorIdx = md.getLsi();
        
        if ( md.getLba() != blockNumber )
            *otherBlock = md.getLba();

        // Another sanity check
        if (u32LogicalSectorIdx >= virtualPagesPerBlock)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "buildMapFromMetadata: LSI out of range (%d >= %d)\n", u32LogicalSectorIdx, virtualPagesPerBlock);

            return ERROR_DDI_NAND_DATA_DRIVE_UBLOCK_HSECTORIDX_OUT_OF_RANGE;
        }

        // Stuff the map bytes
        map.setEntry(u32LogicalSectorIdx, thisVirtualOffset);
    }
    
    if (filledSectorCount)
    {
        // The last page is not used, get the last used page here
        *filledSectorCount = thisVirtualOffset;
    }

    // The return-code is as follows:
    // If any of the reads worked, then retCode was set to SUCCESS, and that is what gets returned.
    // If none of the reads worked, then retCode is not SUCCESS, and retCodeLocal contains the
    // code from the last failure.
    return (SUCCESS == retCode ? retCode : retCodeLocal);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Determine whether or not Block is LBA block.
//!
//! Figure out whether or not the given block number is of a block which
//! contains zone-map (LBA).
//!
//! \param[in]  u32PhysicalBlockNum  Number of physical block to consider
//! \param[in]  eMapType             Type of Map
//! \param[out] *pRtStatus           Pointer to receive RtStatus_t of this function.
//!
//! \return FALSE        If block is not zone-map.
//! \retval TRUE         If block is zone-map.
//!
////////////////////////////////////////////////////////////////////////////////
bool Mapper::isBlockMapBlock(uint32_t u32PhysicalBlockNum, MapperMapTypes_t eMapType, RtStatus_t * pRtStatus)
{
    // Read the redundant area of the first page.
    Page firstPage(PageAddress(u32PhysicalBlockNum, 0));
    firstPage.allocateBuffers(false, true);
    RtStatus_t status = firstPage.readMetadata();
    
    if (pRtStatus)
    {
        *pRtStatus = status;
    }

    if (status != SUCCESS)
    {
        return false;
    }

    // Determine the map type
    uint32_t u32LbaCode1;
    switch (eMapType)
    {
        case kMapperZoneMap:
            u32LbaCode1 = (uint32_t)LBA_STRING_PAGE1;
            break;
        case kMapperPhyMap:
            u32LbaCode1 = (uint32_t)PHYS_STRING_PAGE1;
            break;
    }

    // Read the Stmp code
    return (firstPage.getMetadata().getSignature() == u32LbaCode1);
}

void Mapper::setPrebuiltPhymap(PhyMap * theMap)
{
    m_prebuiltPhymap = theMap;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
