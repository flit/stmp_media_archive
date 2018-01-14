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
//! \file ZoneMapCache.h
//! \brief Declaration of the virtual to physical map class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__zone_map_cache_h__)
#define __zone_map_cache_h__

#include "PersistentMap.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand {

class Mapper;

//! Set this macro to 1 to use 24-bit zone map entries regardless of the
//! total number of blocks in the NAND configuration.
#define NAND_MAPPER_FORCE_24BIT_ZONE_MAP_ENTRIES 0

//! \name Zone map cache
//@{
// The number of cache entries varies depending on whether we have an SDRAM or no-SDRAM build.
#if defined(NO_SDRAM)
    //! \brief Number of cached zone map sections.
    #define MAPPER_CACHE_COUNT          (1)
#else
    //! \brief Number of cached zone map sections.
    #define MAPPER_CACHE_COUNT          (2)
#endif
//@}

//! \name Zone map entry size constants
//@{
const unsigned kNandZoneMapSmallEntry = 2; //!< 16-bit entry.
const unsigned kNandZoneMapLargeEntry = 3;  //!< 24-bit entry.
    
//! Maximum number of blocks to use the small zone map entries for.
const unsigned kNandZoneMapSmallEntryMaxBlockCount = 32768;
    
const unsigned kNandMapperSmallUnallocatedBlockAddress = 0xffff;   //!< 16-bit unallocated block value.
const unsigned kNandMapperLargeUnallocatedBlockAddress = 0xffffff;  //!< 24-bit unallocated block value.
//@}

/*!
 * \brief Map of virtual to physical block numbers.
 */
class ZoneMapCache : public PersistentMap
{
public:
    //! \brief Constructor.
    ZoneMapCache(Mapper & mapper);
    
    //! \brief Destructor.
    virtual ~ZoneMapCache();

    void init();
    void shutdown();

    RtStatus_t writeEmptyMap();

    RtStatus_t findZoneMap();

    RtStatus_t flush();
    
    RtStatus_t getBlockInfo(uint32_t u32Lba, uint32_t *pu32PhysAddr);
    RtStatus_t setBlockInfo(uint32_t u32Lba, uint32_t u32PhysAddr);
    
    virtual RtStatus_t consolidate(
        bool hasValidSectionData,
        uint32_t sectionNumber,
        uint8_t * sectionData,
        uint32_t sectionDataEntryCount);
    
protected:

    /*!
     * \brief Information about a cached section of the zone map.
     */
    struct CacheEntry
    {
        bool m_isValid;         //!< True if this entry is valid.
        bool m_isDirty;         //!< True if this entry is dirty.
        uint64_t m_timestamp;   //!< The modification timestamp for this entry in microseconds since boot.
        uint32_t m_firstLBA;    //!< LBA number for the first entry in this section.
        uint32_t m_entryCount;  //!< Number of valid entries in this section.
        uint8_t * m_entries;    //!< Pointer to section entry data.
    };

    uint32_t m_cacheSectionCount; //!< Number of cached zone map sections.
    CacheEntry * m_descriptors;  //!< Information about each of the cached sections. This array is #sectionCount entries long.
    uint8_t * m_cacheBuffers;  //!< Dynamically allocated buffer for the cached zone map sections.
    bool m_wroteCacheEntryDuringConsolidate;

    RtStatus_t loadCacheEntry(uint32_t u32Lba, int32_t i32SelectedEntry);
    RtStatus_t lookupCacheEntry(uint32_t u32Lba, int32_t * pi32SelectedEntryNum);
    RtStatus_t evictAndLoad(uint32_t u32Lba, int32_t i32SelectedEntry);

    uint32_t readMapEntry(CacheEntry * zoneMapSection, uint32_t lba);
    void writeMapEntry(CacheEntry * zoneMapSection, uint32_t lba, uint32_t physicalAddress);

    virtual RtStatus_t getSectionForConsolidate(
        uint32_t u32EntryNum,
        uint32_t thisSectionNumber,
        uint8_t *& bufferToWrite,
        uint32_t & bufferEntryCount,
        uint8_t * sectorBuffer);
    
};

} // namespace nand

#endif // __zone_map_cache_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
