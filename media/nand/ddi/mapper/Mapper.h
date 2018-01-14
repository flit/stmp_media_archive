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
//! \file    ddi_nand_mapper_api.h
//! \brief   NAND mapper API functions and other declarations.
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDI_NAND_MAPPER_H
#define _DDI_NAND_MAPPER_H

#include "ddi_nand_ddi.h"
#include "PhyMap.h"
#include "BlockAllocator.h"
#include "PageOrderMap.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand {

//! The maximum numbers of blocks that can be tracked by the mapper at once.
#define MAPPER_MAX_TOTAL_NAND_BLOCKS            (1<<24)

//! The number of blocks reserved to be used only for holding the zone and phy maps.
//! This value must be at least large enough to hold both the zone and phy map,
//! plus another copy of the zone map used during consolidation.
const unsigned kNandMapperReservedBlockCount = 12;

//! \brief Enumeration to indicate what type of blocks to be obtained.
typedef enum
{
    //! \brief Normal data block.
    //!
    //! These blocks are mapped and write-leveled.
    kMapperBlockTypeNormal,
    
    //! \brief Map block type.
    //!
    //! These blocks hold the virtual to physical mappings of normal blocks or other
    //! related information.
    kMapperBlockTypeMap
} MapperBlockTypes_t;

//! \brief Enumeration to indicate what type of maps.
typedef enum
{
    kMapperZoneMap,
    kMapperPhyMap
} MapperMapTypes_t;

//! Constant used for setting block status in the phymap.
const bool kNandMapperBlockUsed = PhyMap::kUsed;

//! Constant used for setting block status in the phymap.
const bool kNandMapperBlockFree = PhyMap::kFree;

// Forward declaration
class ZoneMapCache;
class PersistentPhyMap;

/*!
 * \brief The virtual to physical block mapper.
 *
 * This class is responsible for managing wear leveling of the data drive. It does this
 * primaily through mapping virtual block numbers to physical block numbers. This allows
 * the physical location on the media of a virtual block to change at any time. The mapper
 * also maintains the list of unused blocks. It only works with blocks; pages are handled
 * by the NonsequentialSectorsMap class.
 */
class Mapper
{
public:
    
    //! \brief Constraints for which blocks can be selected during block allocation.
    typedef BlockAllocator::Constraints AllocationConstraints;

    //! \brief Constructor.
    Mapper(Media * media);
    
    //! \brief Destructor.
    ~Mapper();

    RtStatus_t init();
    RtStatus_t shutdown();
    bool isInitialized() const { return m_isInitialized; }

    RtStatus_t rebuild();
    RtStatus_t flush();

    //! \brief Tests whether a block address matches the unallocated address.
    bool isBlockUnallocated(uint32_t physicalBlockAddress) { return physicalBlockAddress == m_unallocatedBlockAddress; }

    RtStatus_t getBlockInfo(uint32_t u32Lba, uint32_t *pu32PhysAddr);
    RtStatus_t setBlockInfo(uint32_t u32Lba, uint32_t u32PhysAddr);

    RtStatus_t  getPageInfo(
        uint32_t u32PageLogicalAddr,  // Logical page logical address
        uint32_t *pu32LogicalBlkAddr, // Logical Block address 
        uint32_t *pu32PhysBlkAddr,    // Physical blk address
        uint32_t *pu32PhysPageOffset);

    RtStatus_t getBlock(uint32_t * pu32PhysBlkAddr, MapperBlockTypes_t eBlkType, const AllocationConstraints * constraints=NULL);
    RtStatus_t getBlockAndAssign(uint32_t u32Lba, uint32_t * pu32PhysBlkAddr, MapperBlockTypes_t eBlkType, const AllocationConstraints * constraints=NULL);
    RtStatus_t markBlock(uint32_t u32Lba, uint32_t u32PhysBlkAddr, bool isUnused);

    //! \brief Store the given phymap for later use.
    void setPrebuiltPhymap(PhyMap * theMap);

    //! \brief Returns the current phymap object in use by the mapper.
    ZoneMapCache * getZoneMap() { return m_zoneMap; }
    PhyMap * getPhymap() { return m_physMap; }
    Media * getMedia() { return m_media; }
    
    bool isBuildingMaps() const { return m_isBuildingMaps; }

    RtStatus_t findMapBlock(MapperMapTypes_t eMapType, uint32_t * pu32PhysBlkAddr);
    
    //! \brief Processes a newly discovered bad block.
    void handleNewBadBlock(const BlockAddress & badBlockAddress);

protected:

    Media * m_media;    //!< The NAND logical media that we're mapping.
    ZoneMapCache * m_zoneMap;  //!< Our zone map cache.
    PersistentPhyMap * m_phyMapOnMedia;  //!< Object to save and load the phymap on the NAND.
    PhyMap * m_physMap; //!< The physical block map array.
    PhyMap * m_prebuiltPhymap;  //!< A phymap built during media erase.
    uint32_t m_unallocatedBlockAddress;   //!< Special value that represents an unallocated block, i.e. a logical block that doesn't have a physical block assigned to it.

    //! \name Block allocators
    //@{
    RandomBlockAllocator * m_blockAllocator;    //!< Allocator for data blocks.
    LinearBlockAllocator * m_mapAllocator;      //!< Allocator for map blocks.
    //@}

    //! \name Status flags
    //@{
    bool m_isInitialized;      //!< True if the mapper has been initialized.
    bool m_isZoneMapCreated;   //!< This flag indicates that zone map has been created.
    bool m_isPhysMapCreated;   //!< This flag indicates that phys map has been created.
    bool m_isMapDirty;         //!< This indicates that the map has been touched.
    bool m_isBuildingMaps;     //!< True if in the middle of createZoneMap().
    //@}
    
    //! \brief Reserved block range
    //!
    //! The reserved block range is a range of blocks that is only allowed to hold
    //! the zone and phy maps. No normal data blocks are allowed to placed within
    //! the range. This is to ensure that there is always a block available when
    //! the maps need to be written to media.
    struct {
        unsigned startBlock;   //!< Absolute physical block address for the first reserved block.
        unsigned blockCount;   //!< Number of blocks in the reserved range including bad blocks. So this value will be #kNandMapperReservedBlockCount plus the number of bad blocks.
        unsigned endBlock;  //!< Last block that is part of the reserved range.
    } m_reserved;

protected:

    RtStatus_t computeReservedBlockRange(bool* pbRangeMoved);
    RtStatus_t evacuateReservedBlockRange();

    void setDirtyFlag();
    void clearDirtyFlag();

    static void phymapDirtyListener(PhyMap * thePhymap, bool wasDirty, bool isDirty, void * refCon);

    RtStatus_t createZoneMap();
    RtStatus_t scanAndBuildPhyMap(AuxiliaryBuffer & auxBuffer);
    RtStatus_t scanAndBuildZoneMap(AuxiliaryBuffer & auxBuffer);

    void searchAndDestroy();

    bool isBlockMapBlock(uint32_t u32PhysicalBlockNum, MapperMapTypes_t eMapType, RtStatus_t *pRtStatus);

};

class HybridOrderedMap : public PageOrderMap
{
    static const int kMaxPhyBlocks = 8; // Atmost 8 page conflicts
    uint32_t m_phyBlocks[kMaxPhyBlocks];
    uint32_t m_NumUsedSectors[kMaxPhyBlocks];
    uint8_t  *m_PhyBlockIndexForPage;
    int m_count;
    int m_latestBlockIndex;
public:
    HybridOrderedMap();
    ~HybridOrderedMap();    
    int init(unsigned entryCount, unsigned maxEntryValue);
    int update(PageOrderMap &map,uint32_t physicalBlock, uint32_t number);
    int getPhyBlock(int logicalIndex);
    void cleanup();
    void clear();
}; 

class ConflictResolver
{
    // theoritical upper limit is 4
    static const int kMaxConflictingPhysicalBlocks = 8; 
    // theoritical upper limit is size of NSSM list. Otherwise make it some percent latteron.
    static const int kMaxConflicts = 32; 
    
    typedef struct ConflictingEntry {
        uint32_t m_Lba;
        uint32_t m_Lba2;
        uint16_t m_phy_count; // Number of physical page entries
        uint32_t m_phyBlocks[kMaxConflictingPhysicalBlocks];
    } ConflictingEntry_t;

    ConflictingEntry_t m_conflicts[kMaxConflicts];
    // Number of actual LBA conflicts
    int m_count;
    Mapper *m_mapper;
    HybridOrderedMap m_map;
    uint32_t startBlock;
    uint32_t endBlock;

protected:
    void addPhyBlock(ConflictingEntry_t &conflict, uint32_t phyBlock);
    int merge(ConflictingEntry_t &conflict);
    int buildPartialMapFromMetadata(
        uint32_t LBA,
        uint32_t physicalPage, 
        PageOrderMap &map, 
        uint32_t *filledSectorCount,
        uint32_t *otherBlock);
public:
    ConflictResolver(Mapper *mapper);
    ~ConflictResolver();
    // Invalidate internal lists
    void invalidate(void);  
    int addBlocks(uint32_t u32LogicalBlockAddr,uint32_t u32PhysicalBlockNumber);
    int simplify(ConflictingEntry_t &conflict);
    int resolve(void);
    void setRange(uint32_t start, uint32_t end);
};

} // namespace nand

/*!
 * \brief Helper class to automatically clear a flag.
 */
class AutoClearFlag
{
public:
    //! \brief Constructor takes the flag to be cleared.
    AutoClearFlag(bool & theFlag) : m_flag(theFlag) {}
    
    //! \brief Destructor clears the flag passed into the constructor.
    ~AutoClearFlag()
    {
        m_flag = false;
    }

protected:
    bool & m_flag;  //!< Reference to the flag we are controlling.
};

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
#endif // #ifndef _DDI_NAND_MAPPER_H
//! @}
