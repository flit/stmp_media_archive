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
//! \file ZoneMapCache.cpp
//! \brief NAND mapper zone map cache implementation.
////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include "ZoneMapCache.h"
#include "Mapper.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_media.h"
#include "hw/core/vmemory.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "ZoneMapSectionPage.h"
#include "os/dmi/os_dmi_api.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
//  Defines
/////////////////////////////////////////////////////////////////////////////////

//! Set this macro to 1 to print ECC corrections of zone map pages to TSS.
#define LOG_ZONE_MAP_ECC_LEVELS 0

/////////////////////////////////////////////////////////////////////////////////
//  Code
/////////////////////////////////////////////////////////////////////////////////

#pragma ghs section text=".init.text"
ZoneMapCache::ZoneMapCache(Mapper & mapper)
:   PersistentMap(mapper, kNandZoneMapSignature, LBA_STRING_PAGE1),
    m_cacheSectionCount(0),
    m_descriptors(NULL),
    m_cacheBuffers(NULL)
{
}
#pragma ghs section text=default

ZoneMapCache::~ZoneMapCache()
{
    shutdown();
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   This function initializes zone-map cache.
//!
//! This function initializes the cache which will store a part or all of the
//! zone-map in RAM.  If the Nand is small enough, it is possible that all of
//! the zone-map will reside in RAM.
////////////////////////////////////////////////////////////////////////////////
void ZoneMapCache::init()
{
    int entrySize;
    
    // Figure out what size of zone map entry we'll be using based on the total number of blocks.
#if !NAND_MAPPER_FORCE_24BIT_ZONE_MAP_ENTRIES
    if (m_mapper.getMedia()->getTotalBlockCount() < kNandZoneMapSmallEntryMaxBlockCount)
    {
        entrySize = kNandZoneMapSmallEntry;
    }
    else
#endif // !NAND_MAPPER_FORCE_24BIT_ZONE_MAP_ENTRIES
    {
        entrySize = kNandZoneMapLargeEntry;
    }
    
    // Init our superclass.
    PersistentMap::init(entrySize, m_mapper.getMedia()->getTotalBlockCount());
    
    // The size of a single cache buffer is the NAND page size minus the zone map header.
    uint32_t u32SizeOfData = NandHal::getParameters().pageDataSize - sizeof(NandMapSectionHeader_t);

    m_cacheSectionCount = std::min<uint32_t>(m_totalSectionCount, MAPPER_CACHE_COUNT);
    
    // Allocate the cache itself. The total cache buffer size is the number of caches times the
    // size of a cache buffer, which is the NAND page size minus the zone map header.
    if (!m_cacheBuffers)
    {
        m_cacheBuffers = reinterpret_cast<uint8_t *>(os_dmi_malloc_phys_contiguous(u32SizeOfData * m_cacheSectionCount));
        assert(m_cacheBuffers);
    }
    
    // Allocate the cache entry descriptor array.
    if (m_descriptors == NULL)
    {
        m_descriptors = reinterpret_cast<CacheEntry *>(malloc(m_cacheSectionCount * sizeof(CacheEntry)));
        assert(m_descriptors);
    }

    int32_t i;
    uint32_t u32CurrentBufOffset = 0;
    for (i=0; i < m_cacheSectionCount; i++)
    {
        CacheEntry * entry = &m_descriptors[i];
        
        entry->m_timestamp = 0;
        entry->m_isValid = false;
        entry->m_isDirty = 0;
        entry->m_firstLBA = 0;
        entry->m_entryCount = 0;
        entry->m_entries = &m_cacheBuffers[u32CurrentBufOffset];

        u32CurrentBufOffset += u32SizeOfData;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Shuts down the zone-map cache and frees related memory.
//!
//! This function primarily frees the dynamically allocated memory associated
//! with the zone map cache. The cache descriptors and cached section buffers
//! are all deallocated.
////////////////////////////////////////////////////////////////////////////////
void ZoneMapCache::shutdown()
{
    if (m_cacheBuffers)
    {
        free(m_cacheBuffers);
        m_cacheBuffers = NULL;
    }
    
    if (m_descriptors)
    {
        free(m_descriptors);
        m_descriptors = NULL;
    }
    
    m_sectionPageOffsets.cleanup();
    
    m_cacheSectionCount = 0;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Writes a default, empty zone map to the NAND.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//!
//! \pre The phymap must be fully initialized.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ZoneMapCache::writeEmptyMap()
{
    int32_t i;
    uint32_t u32StartingEntryNum;
    uint32_t u32NumEntriesToWrite;
    uint32_t u32NumEntriesWritten;
    RtStatus_t ret;
    uint32_t u32BlockPhysAddr;
    uint32_t u32PagesPerBlock = NandHal::getParameters().wPagesPerBlock;
    
    // Use the phymap to allocate a block from the block range reserved for maps.
    // This call will also mark the new block used in the phymap and erase it for us.
    ret = m_mapper.getBlock(&u32BlockPhysAddr, kMapperBlockTypeMap, 0);
    if (ret != SUCCESS)
    {
        return ret;
    }

    m_block = u32BlockPhysAddr;
    m_topPageIndex = 0;

    u32StartingEntryNum = 0;
    u32NumEntriesToWrite = m_mapper.getMedia()->getTotalBlockCount();
    
    // Invalidate all cache entries.
    for (i=0; i < m_cacheSectionCount; i++)
    {
        CacheEntry * entry = &m_descriptors[i];
        
        entry->m_timestamp = 0;
        entry->m_isValid = false;
        entry->m_isDirty = 0;
        entry->m_firstLBA = 0;
        entry->m_entryCount = 0;
    }
    
    // Use one of our cache descriptors as a temporary buffer.
    // Fill it with unallocated entries (all f's).
    uint8_t * pu16ZoneMapBuffer = m_descriptors[0].m_entries;
    memset(pu16ZoneMapBuffer, 0xff, m_maxEntriesPerPage * m_entrySize);
    
    // Set section offset equal to page offset.
    m_sectionPageOffsets.setSortedOrder();

    ZoneMapSectionPage sectionPage(m_block.getPage());
    sectionPage.setEntrySize(m_entrySize);
    sectionPage.setMetadataSignature(LBA_STRING_PAGE1);
    sectionPage.setMapType(kNandZoneMapSignature);
    sectionPage.allocateBuffers();

    // For each section of the zone map, write out the buffer filled with unallocated entries.
    // This will ensure that there is at least one copy of each section in the zone map block.
    for (i=0; u32NumEntriesToWrite > 0; ++i, ++sectionPage)
    {
        //! \todo Handle write failure.
        ret = sectionPage.writeSection(u32StartingEntryNum, u32NumEntriesToWrite, (uint8_t *)pu16ZoneMapBuffer, &u32NumEntriesWritten);
        if (ret == ERROR_DDI_NAND_HAL_WRITE_FAILED)
        {
            // Mark the failed block bad.
            m_mapper.handleNewBadBlock(u32BlockPhysAddr);
        }
        else if (SUCCESS != ret)
        {
            return ret;
        }

        m_topPageIndex++;

        if (m_topPageIndex >= u32PagesPerBlock)
        {
            // If we already filled up the block with zone map sections, then there are too many
            // sections to fit in one block and we're screwed.
            return ERROR_DDI_NAND_MAPPER_ZONE_MAP_CACHE_INIT_FAILED;
        }

        if (0==u32NumEntriesWritten)
        {
            // If we are unable to write zone map entries, then we have a problem.
            return ERROR_DDI_NAND_MAPPER_ZONE_MAP_CACHE_INIT_FAILED;
        }

        u32StartingEntryNum += u32NumEntriesWritten;
        u32NumEntriesToWrite -= u32NumEntriesWritten;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Search for and init the zone map.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ZoneMapCache::findZoneMap()
{
    // Search the nand for the location of the zone map
    uint32_t u32StartZoneMapPhysAddr;
    RtStatus_t rtCode = m_mapper.findMapBlock(kMapperZoneMap, &u32StartZoneMapPhysAddr);
    if (rtCode != SUCCESS)
    {
        return rtCode;
    }

    // For Zone-map loading, initializing cache and
    // pointing g_MapperDescriptor.zoneMapPhysicalBlockNumber at block number
    // containing zone-map is sufficient.
    m_block = u32StartZoneMapPhysAddr;
    m_topPageIndex = 0;
    
    // Scan the zone map block and build the section offset table.
    return buildSectionOffsetTable();
}

////////////////////////////////////////////////////////////////////////////////
//! \brief    Loads given cache entry with section containing logical block, u32Lba
//!
//! This function simply loads the cache entry indicated by, i32SelectedEntry, with
//! cache section which contains the logical block u32Lba.
//!
//! \param[in]   Lba               Logical Block Address.
//! \param[in]   i32SelectedEntry  Entry of cache into which to store zone-map
//!                                section containing u32Lba.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ZoneMapCache::loadCacheEntry(uint32_t u32Lba, int32_t i32SelectedEntry)
{
    RtStatus_t ret;
    bool alreadyRebuiltMaps = false;
    
    // Get a temp buffer.
    SectorBuffer buffer;
    if ((ret = buffer.acquire()) != SUCCESS)
    {
        return ret;
    }
    
read_section:
    // The section header's signature and version are validated by this function.
    // We ask the function to auto-consolidate if it encounters a rewrite error.
    ret = retrieveSection(u32Lba, (uint8_t *)buffer, true);

    // If we get an uncorrectable ECC error while reading a section of the zone map, then
    // the only thing we can do is to rebuild the map from scratch. However, we don't want
    // to start this process if we've already tried once, or if we're in the middle of
    // building the maps (this function could be invoked while filling in the zone map).
    if (ret == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED && !alreadyRebuiltMaps && !m_mapper.isBuildingMaps())
    {
        tss_logtext_Print(LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1, ">>> Got uncorrectable ECC error reading zone map section; rebuilding maps\n");
        
        // Got an uncorrectable ECC error, so we have to completely rebuild the maps.
        ret = m_mapper.rebuild();
        if (ret != SUCCESS)
        {
            return ret;
        }
        
        // Set this flag to true so we can't ever end up in an infinite map building loop.
        alreadyRebuiltMaps = true;
        
        // Try reading the section from the zone map again.
        goto read_section;
    }
    else if (SUCCESS != ret)
    {
        return ret;      
    }
    
    NandMapSectionHeader_t * header = (NandMapSectionHeader_t *)buffer.getBuffer();
    CacheEntry * zone = &m_descriptors[i32SelectedEntry];
    
    // Verify that this section matches what we expect.
    if (header->entrySize != m_entrySize)
    {
        return ERROR_DDI_NAND_MAPPER_LBA_CORRUPTED;
    }
    
    // Fill in the zone map cache.
    zone->m_firstLBA = header->startLba; 
    zone->m_entryCount = header->entryCount;
    zone->m_timestamp = hw_profile_GetMicroseconds();
    zone->m_isDirty = false;            
    zone->m_isValid = true;

    // Copy the entry data from the section into the zone map cache.
    uint8_t * pu8BufPtr = (uint8_t *)buffer + sizeof(*header);
    memcpy(zone->m_entries, pu8BufPtr, header->entryCount * header->entrySize);

    return SUCCESS;
}

RtStatus_t ZoneMapCache::consolidate(
    bool hasValidSectionData,
    uint32_t sectionNumber,
    uint8_t * sectionData,
    uint32_t sectionDataEntryCount)
{
    // Clear this flag.
    m_wroteCacheEntryDuringConsolidate = false;
    
    // Call superclass implementation of consolidate.
    RtStatus_t status = PersistentMap::consolidate(hasValidSectionData, sectionNumber, sectionData, sectionDataEntryCount);
    if (status != SUCCESS)
    {
        return status;
    }
    
    // If we use the in-memory cache for writing any part of the zone map, we need to go
    // back and clear the dirty flag. This is postponed until after the consolidation is done
    // because if we get a write error during the page copying and have to start the consolidation
    // over again then we need to know the dirty cache entries again.
    if (m_wroteCacheEntryDuringConsolidate)
    {
        uint32_t i;
        for (i=0; i < m_cacheSectionCount; i++)
        {
            m_descriptors[i].m_isDirty = false;
        }
    }

    return SUCCESS;    
}

RtStatus_t ZoneMapCache::getSectionForConsolidate(
    uint32_t u32EntryNum,
    uint32_t thisSectionNumber,
    uint8_t *& bufferToWrite,
    uint32_t & bufferEntryCount,
    uint8_t * sectorBuffer)
{
    int32_t cacheEntryIndex;
    CacheEntry * cacheEntry;
    
    // Lookup the cache entry for this section.
    RtStatus_t ret = lookupCacheEntry(u32EntryNum, &cacheEntryIndex);
    if (ret)
    {
        return ret;
    }
    
    // Is the cache entry valid and dirty? If so, we must use the cached copy
    // rather than read from the media.
    cacheEntry = &m_descriptors[cacheEntryIndex];
    if (cacheEntry->m_isValid
        && cacheEntry->m_isDirty
        && u32EntryNum >= cacheEntry->m_firstLBA
        && u32EntryNum < cacheEntry->m_firstLBA + cacheEntry->m_entryCount)
    {
        // Just write the contents of the cache entry.
        bufferToWrite = (uint8_t *)cacheEntry->m_entries;
        bufferEntryCount = cacheEntry->m_entryCount;
        m_wroteCacheEntryDuringConsolidate = true;
    }
    else
    {
        // We don't have a valid cache entry, so just read the section from the map
        // block like normal.
        return PersistentMap::getSectionForConsolidate(u32EntryNum, thisSectionNumber, bufferToWrite, bufferEntryCount, sectorBuffer);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Write a value into a zone map section.
//!
//! \param zoneMapSection The section of the zone map containing the entry for \a lba.
//! \param lba The \em "true" LBA number being modified. \a zoneMapSection must
//!     contain this LBA.
//! \param physicalAddress The value to write into the entry for \a lba.
////////////////////////////////////////////////////////////////////////////////
void ZoneMapCache::writeMapEntry(CacheEntry * zoneMapSection, uint32_t lba, uint32_t physicalAddress)
{
    uint32_t u32StartingEntry = zoneMapSection->m_firstLBA;
    
#if DEBUG
    uint32_t u32NumEntries = zoneMapSection->m_entryCount;
    assert((lba >= u32StartingEntry) && (lba < (u32StartingEntry + u32NumEntries)));
#endif

    // Write the physical address for this LBA.
    uint8_t * entries = zoneMapSection->m_entries;
    uint32_t entryIndex = lba - u32StartingEntry;
    
    // Handle the write differently depending on the entry size.
    switch (m_entrySize)
    {
        // 16-bit entries
        case kNandZoneMapSmallEntry:
            // Make sure the value fits in 16 bits.
            assert((physicalAddress & 0xffff0000) == 0);
        
            ((uint16_t *)entries)[entryIndex] = physicalAddress;
            break;
            
        // 24-bit entries
        case kNandZoneMapLargeEntry:
            // Make sure the value fits in 24 bits.
            assert((physicalAddress & 0xff000000) == 0);
        
            entries += entryIndex * kNandZoneMapLargeEntry;
            
            entries[0] = physicalAddress & 0xff;
            entries[1] = (physicalAddress >> 8) & 0xff;
            entries[2] = (physicalAddress >> 16) & 0xff;
            break;
    }
}

RtStatus_t ZoneMapCache::flush()
{
    // Clear this flag so we can watch for addSection() to set it.
    m_didConsolidateDuringAddSection = false;
            
    // Store the entire contents of Cache out to the LBA designated Block.
    unsigned i;
    for (i=0; i < m_cacheSectionCount; ++i)
    {
        CacheEntry * entry = &m_descriptors[i];

        // Flush this cache entry if it is both valid and dirty.
        if (entry->m_isValid && entry->m_isDirty)
        {
            RtStatus_t ret = addSection(entry->m_entries, entry->m_firstLBA, entry->m_entryCount);
            if (ret || m_didConsolidateDuringAddSection)
            {
                return ret;
            }
        }
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   This function sets zone-map entry to given physical block address.
//!
//! \param[in]   u32DriveNumber Which Data Drive is being referred to.
//! \param[in]   Lba Logical Block Address to inquire about.
//! \param[out]  u32PhysAddr Physical address to which to set given LBA entry.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ZoneMapCache::setBlockInfo(uint32_t u32Lba, uint32_t u32PhysAddr)
{
    int32_t i32SelectedEntryNum;
    RtStatus_t ret;

    assert(m_block.isValid());
    assert(m_topPageIndex!=0);
    
    // Make sure that we are not go out of bound
    if (u32Lba >= (uint32_t)MAPPER_MAX_TOTAL_NAND_BLOCKS)
    {
        return ERROR_DDI_NAND_MAPPER_LBA_OUTOFBOUND;  // LBA is out of range
    }

    ret = lookupCacheEntry(u32Lba, &i32SelectedEntryNum);
    if (ret)
    {
        return ret;
    }

    ret = evictAndLoad(u32Lba, i32SelectedEntryNum);
    if (ret)
    {
        return ret;
    }
    
    CacheEntry * zoneMapSection = &m_descriptors[i32SelectedEntryNum];

    // Modify the zone map entry for this LBA.
    writeMapEntry(zoneMapSection, u32Lba, u32PhysAddr);

    zoneMapSection->m_isDirty = true;  // Mark this zone map section as dirty.
    zoneMapSection->m_timestamp = hw_profile_GetMicroseconds();   // Update the timestamp.

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
