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
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal
//! @{
//! \file Region.h
//! \brief Definition of the nand::Region class and its subclasses.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__region_h__)
#define __region_h__

#include "types.h"
#include "errordefs.h"
#include "BadBlockTable.h"
#include "drivers/media/sectordef.h"
#include "drivers/media/include/ddi_media_internal.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \name Maximum region counts
//@{
//! Each chip may have one or more data regions.  One reason to
//! have more than one data region per chip is to allow multi-plane addressing.
#define MAX_DATA_REGIONS_PER_CHIP 4

//! The maximum number of system drives is based on the typical drive arrangement
//! for previous and current SDK releases.
#define MAX_NAND_SYSTEM_DRIVES 9 

//! A typical system will have just one hidden data drive.
//! Adding any vendor-specific drives will require increasing MAX_NAND_HIDDEN_DRIVES.
#define MAX_NAND_HIDDEN_DRIVES 2

//! Each chip will have MAX_DATA_REGIONS_PER_CHIP data drive regions (e.g. 2 to
//! cover 8-plane 128MB NANDs).
//! Each system drive also uses one region.
#define MAX_DATA_DRIVE_REGIONS (MAX_DATA_REGIONS_PER_CHIP * MAX_NAND_DEVICES)

//! The total maximum number of regions. Add in another region for each chip for the boot region.
#define MAX_NAND_REGIONS (MAX_DATA_DRIVE_REGIONS + MAX_NAND_SYSTEM_DRIVES + MAX_NAND_HIDDEN_DRIVES + MAX_NAND_DEVICES)
//@}

// Forward declarations.
typedef struct _BadBlockTableNand_t BadBlockTableNand_t;
class NandPhysicalMedia;

namespace nand {

// Forward declarations.
typedef struct _NandConfigBlockRegionInfo NandConfigBlockRegionInfo_t;
class Media;
class DiscoveredBadBlockTable;

//! \brief
typedef enum _region_types
{
    kUnknownRegionType = 0,
    kBootRegionType,
    kSystemRegionType,
    kDataRegionType
} RegionType_t;

/*!
 * \brief A region of the NAND media.
 *
 * A region is a subsection of one of the physical NAND chip enables. Regions never span multiple
 * chip enables, though they can encompass an entire one. Usually, regions are no larger than a
 * single die. A logical drive is composed of one or more regions that do not have to be
 * contiguous.
 */
class Region
{
public:

    /*!
     * \brief Iterator for NAND regions.
     */
    class Iterator
    {
    public:
        //! \brief Constructor.
        Iterator(Region ** regionList, unsigned count) : m_list(regionList), m_index(0), m_count(count) {}
        
        //! \brief Returns the next available region.
        Region * getNext()
        {
            return (m_list && m_index < m_count)
                ? m_list[m_index++]
                : NULL;
        }
        
        //! \brief Restarts the iterator so the next getNext() call will return the first region.
        void reset() { m_index = 0; }

    protected:
        Region ** m_list;    //!< The array of regions we're iterating.
        unsigned m_index;   //!< Current iterator index.
        unsigned m_count;   //!< Total number of regions in the list.
    };
    
    //! \brief Region factory function.
    static Region * create(NandConfigBlockRegionInfo_t * info);
    
    //! \brief Default constructor.
    Region();
    
    //! \brief Destructor.
    virtual ~Region() {}
    
    //! \brief
    void initFromConfigBlock(NandConfigBlockRegionInfo_t * info);
    
    //! \brief Return this region's number.
    unsigned getRegionNumber() const { return m_regionNumber; }
    
    //! \brief Return the chip on which the region resides.
    unsigned getChip() const { return m_iChip; }
    
    //! \brief Get the region's NAND object.
    NandPhysicalMedia * getNand() { return m_nand; }
    
    //! \brief Get the logical drive that the region belongs to.
    LogicalDrive * getLogicalDrive() { return m_pLogicalDrive; }
    
    //! \brief Get the region's start address.
    const BlockAddress & getStartBlock() const { return m_u32AbPhyStartBlkAddr; }
    
    //! \brief Get the region's length in blocks.
    uint32_t getBlockCount() const { return m_iNumBlks; }
    
    //! \brief Get the address of the last block in the region.
    BlockAddress getLastBlock() const { return m_u32AbPhyStartBlkAddr + m_iNumBlks - 1; }
    
    //! \brief Returns the type of this region.
    virtual RegionType_t getRegionType() const = 0;

#pragma ghs section text=".static.text"

    //! \brief Returns true if the region belongs to a drive-type drive.
    virtual bool isDataRegion() const { return getRegionType() == kDataRegionType; }
    
    //! \brief Returns true if the region belongs to a system drive.
    virtual bool isSystemRegion() const { return getRegionType() == kSystemRegionType; }
    
#pragma ghs section text=default

    //! \brief Indicates that the given region uses entries in the BBTable.
    virtual bool usesBadBlockTable() const = 0;
    
    //! \brief Create the region's bad block table from a larger bad block table.
    virtual void setBadBlockTable(const BadBlockTable & table) = 0;

    //! \brief Direct access to the bad block table.
    //! \return A pointer to a bad block table is returned to the caller. If the region does not
    //!     have a bad block table, or if the region type does not support a full bad block table,
    //!     then NULL may be returned. Callers should be prepared to handle a NULL result.
    virtual BadBlockTable * getBadBlocks() { return NULL; }
    
    //! \brief Returns the number of bad blocks within the region.
    virtual uint32_t getBadBlockCount() const = 0;
    
    //! \brief Compute the number of extra blocks required to handle potential new bad blocks.
    unsigned getExtraBlocksForBadBlocks();
    
    virtual RtStatus_t fillInBadBlocksByScanning(SECTOR_BUFFER * auxBuffer) = 0;

    virtual RtStatus_t fillInBadBlocksFromDBBT(
        DiscoveredBadBlockTable & dbbt,
        uint32_t u32NAND,
        uint32_t u32DBBT_BlockAddress,
        SECTOR_BUFFER * sectorBuffer,
        SECTOR_BUFFER * auxBuffer) = 0;
    
    //! \brief Insert a new bad block into the region.
    virtual void addNewBadBlock(const BlockAddress & addr) = 0;

    //! \brief Mark the region as dirty.
    //!
    //! Setting the region dirty will force a background update of the DBBT on the NAND.
    void setDirty();
    
public:
    
    unsigned m_regionNumber;    //!< This region's region number.
    unsigned m_iChip;                 //!< Index of NAND Chip containing this Region
    NandPhysicalMedia * m_nand; //!< NAND descriptor
    LogicalDrive * m_pLogicalDrive;  //!< Pointer back to our grandparent
    LogicalDriveType_t m_eDriveType; //!< Some System Drive, or Data Drive
    uint32_t m_wTag;                 //!< Drive Tag
    BlockAddress m_u32AbPhyStartBlkAddr;  //!< Absolute Physical starting block within media.
    int32_t m_iStartPhysAddr;        //!< Starting Block number for region relative to chip
    int32_t m_iNumBlks;              //!< Size, in blocks, of whole region. Size includes embedded bad blocks.
    bool m_bRegionInfoDirty;    //!< If TRUE, the bad block information has updates

    //! \brief Utility method to test blocks within the region to see if they are bad.
    //! \param[out] iRegionBadBlocks Optional pointer to the number of bad blocks found in the region,
    //!     that will be filled in upon return if non-NULL.
    //! \param addBadBlocks This flag says whether to call addNewBadBlock() for every bad block
    //!     identified within the region. If this parameter is false but \a iRegionBadBlocks is
    //!     valid, bad blocks will still be counted and the count returned to the caller.
    //! \param auxBuffer Auxiliary buffer to use when checking for bad block marks.
    RtStatus_t scanNandForBadBlocks(uint32_t * iRegionBadBlocks, bool addBadBlocks, SECTOR_BUFFER * auxBuffer);
    
};

#pragma ghs section text=".static.text"

/*!
 * \brief Region for a system drive.
 *
 * A system region keeps a full bad block table that is accessible with the getBadBlocks()
 * method.
 */
class SystemRegion : public Region
{
public:
    //! \brief Default constructor.
    SystemRegion();
    
    //! \brief Returns the type of this region.
    virtual RegionType_t getRegionType() const { return kSystemRegionType; }

    //! \brief Direct access to the bad block table.
    virtual BadBlockTable * getBadBlocks() { return &m_badBlocks; }

    //! \brief System regions use a full bad block table.
    virtual bool usesBadBlockTable() const { return true; }
    
    //! \brief Create the region's bad block table from a larger bad block table.
    virtual void setBadBlockTable(const BadBlockTable & table);
    
    virtual uint32_t getBadBlockCount() const { return m_badBlocks.getCount(); }

    virtual RtStatus_t fillInBadBlocksByScanning(SECTOR_BUFFER * auxBuffer);

    virtual RtStatus_t fillInBadBlocksFromDBBT(
        DiscoveredBadBlockTable & dbbt,
        uint32_t u32NAND,
        uint32_t u32DBBT_BlockAddress,
        SECTOR_BUFFER * sectorBuffer,
        SECTOR_BUFFER * auxBuffer);
    
    //! \brief Insert a new bad block into the region.
    virtual void addNewBadBlock(const BlockAddress & addr);

protected:

    BadBlockTable m_badBlocks;  //!< Bad block table for this region.

    RtStatus_t scanDBBTPage(int * regionBadBlockCount, BadBlockTableNand_t * pNandBadBlockTable);
};

#pragma ghs section text=default

/*!
 * \brief Region representing an area of the NAND containing boot blocks.
 */
class BootRegion : public SystemRegion
{
public:
    //! \brief Constructor.
    BootRegion() {}
    
    //! \brief Returns the type of this region.
    virtual RegionType_t getRegionType() const { return kBootRegionType; }

};

/*!
 * \brief Region for a data drive or hidden data drive.
 *
 * Data regions form either the main data drive or hidden data drives. Because the mapper uses
 * the phy map for block allocation, data regions do not have to maintain a full bad block table.
 * Thus, the getBadBlocks() method will always return NULL. However, a count of the bad blocks
 * within the region is kept. When a new bad block is added by calling addNewBadBlock(), the
 * region's bad block count will be incremented.
 */
class DataRegion : public Region
{
public:
    //! \brief Default constructor.
    DataRegion();
    
    //! \brief Returns the type of this region.
    virtual RegionType_t getRegionType() const { return kDataRegionType; }

    //! \brief Data regions only maintain a bad block count.
    virtual bool usesBadBlockTable() const { return false; }
    
    //! \brief Create the region's bad block table from a larger bad block table.
    virtual void setBadBlockTable(const BadBlockTable & table);
    
    virtual uint32_t getBadBlockCount() const { return m_badBlockCount; }

    virtual RtStatus_t fillInBadBlocksByScanning(SECTOR_BUFFER * auxBuffer);

    virtual RtStatus_t fillInBadBlocksFromDBBT(
        DiscoveredBadBlockTable & dbbt,
        uint32_t u32NAND,
        uint32_t u32DBBT_BlockAddress,
        SECTOR_BUFFER * sectorBuffer,
        SECTOR_BUFFER * auxBuffer);
    
    //! \brief Insert a new bad block into the region.
    virtual void addNewBadBlock(const BlockAddress & addr);
    
    //! \brief Get the current number of logical blocks for this data region.
    uint32_t getLogicalBlockCount() const { return m_u32NumLBlks; }
    
    //! \brief Update the number of logical blocks.
    void setLogicalBlockCount(uint32_t count) { m_u32NumLBlks = count; }

protected:

    uint32_t m_badBlockCount;   //!< Number of bad blocks in this region.
    uint32_t m_u32NumLBlks;     //!< Number of blocks in this region that contain data.

};

} // namespace nand

#endif // __region_h__
////////////////////////////////////////////////////////////////////////////////
// EOF
////////////////////////////////////////////////////////////////////////////////
