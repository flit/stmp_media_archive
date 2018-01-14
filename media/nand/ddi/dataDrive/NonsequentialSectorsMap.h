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
//! \file   NonsequentialSectorsMap.h
//! \brief  Definition of the NSSM class and NSSM manager.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__nonsequential_sectors_map_h__)
#define __nonsequential_sectors_map_h__

#include "types.h"
#include "PageOrderMap.h"
#include "RedBlackTree.h"
#include "wlru.h"
#include "DeferredTask.h"
#include "ddi_nand_ddi.h"
#include "NssmManager.h"
#include "VirtualBlock.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand {

class Region;
class Mapper;

/*!
 * \brief Map of logical to physical sector order.
 *
 * The nonsequential sectors map (NSSM) is responsible for tracking the physical location within
 * a block of that block's logical sectors. It also manages the mechanism for updating block
 * contents in an efficient manner. All data drive sector reads and writes must utilize a
 * nonsequential sectors map in order to find the physical location of a logical sector, or
 * to get the page where a new sector should be written.
 *
 * The NSSM is composed of two key components. First, it has a map of logical sector to physical
 * page within the block. This allows logical sectors to be written to in any order to the block,
 * which is important in ensuring that pages are only written sequentially within the block as
 * required by NANDs. The map also enables logical sectors to be written to the block more than
 * once, with the most recent copy taking precedence.
 *
 * The second element is a backup block. This backup block contains the previous contents of
 * the block, and allows only new sectors to be written to the primary block. If a logical sector
 * is not present in the primary block it can be read from the backup block. When the primary
 * block becomes full, the primary and backup are merged into a new block. Merging takes the most
 * recent version of each logical sector from either the primary or backup and writes it into
 * the new block.
 *
 * Another important aspect of the NSSM is that each NSSM is associated with a virtual block
 * number, not a physical block. This allows the data associated with the virtual block to
 * move around on the media as necessary.
 *
 * \see NssmManager
 *
 * \ingroup ddi_nand_data_drive
 */
class NonsequentialSectorsMap : protected RedBlackTree::Node, protected WeightedLRUList::Node
{
public:
    
    //! Value used to indicate that no block is set for either the virtual block or backup
    //! physical block.
    static const uint32_t kInvalidAddress = 0xffffffffL;
    
    //! \brief Default constuctor.
    //!
    //! Make sure to call init() after construction.
    NonsequentialSectorsMap();
    
    //! \brief Destructor.
    virtual ~NonsequentialSectorsMap();
    
    //! \brief Initialize the map.
    //!
    //! \param mapIndex This map's index in the array of NSSMs.
    void init(NssmManager * manager);
    
    //! \brief Reinits the map for a new virtual block.
    //!
    //! Invalidates the map the sets the block number. The caller must have called flush() if
    //! the 
    RtStatus_t prepareForBlock(uint32_t blockNumber);
    
    //! This function resolves conflicting zone-map assignment arising from power-loss.
    //! We resolve this difficulty by counting the number of unique logical sector entries
    //! found in each block.  The one with more logical sector entries is likely the "old"
    //! block.  An alternative approach is to simply discard the "new" block and keep the
    //! "old" block.  
    //!
    //! \param[in] blockNumber Virtual block address.
    //! \param[in] physicalBlock1 Physical Block Number which belongs to u32LbaIdx.
    //! \param[in] physicalBlock2 Another Physical Block Number beloging to u32LbaIdx.
    RtStatus_t resolveConflict(uint32_t blockNumber, uint32_t physicalBlock1, uint32_t physicalBlock2);
    
    //! \brief Performs a block merge if necessary.
    RtStatus_t flush();
    
    //! \brief Clears all fields.
    //!
    //! Be careful to not invalidate a map that needs to be flushed.
    void invalidate();
    
    //! \brief Const access to the virtual block.
    const VirtualBlock & getVirtualBlock() const { return m_virtualBlock; }

    //! \brief Access to the virtual block.
    VirtualBlock & getVirtualBlock() { return m_virtualBlock; }
    
    //! \brief Returns whether this map has a valid association with a virtual block.
    bool isValid() const { return m_isVirtualBlockValid; }
    
    //! \brief Whether the virtual block has a backup block.
    bool hasBackup() const { return m_hasBackups; }
    
    //! \brief Returns the region associated with this map's virtual block.
    Region * getRegion();
    
    //! \brief Determines whether the pages of the block are in logical order.
    bool isInLogicalOrder();
    
    //! \brief Merge primary and backup blocks without skipping any pages.
    RtStatus_t mergeBlocks() { return mergeBlocksSkippingPage(kInvalidAddress); }
    
    //! \brief Merge primary and backup blocks, but exclude a given logical sector offset.
    RtStatus_t mergeBlocksSkippingPage(unsigned u32NewSectorNumber);
    
    //! \brief Recover from a failed write to the primary block.
    RtStatus_t recoverFromFailedWrite(uint32_t failedVirtualOffset, uint32_t logicalOffsetToSkip);
    
    //! \brief Copy the data to new physical blocks.
    RtStatus_t relocateVirtualBlock();
    
    //! \name Entries
    //@{
    //! \brief
    void getEntry(uint32_t logicalSectorOffset,
        uint32_t * virtualSectorOffset,
        bool * isOccupied,
        VirtualBlock ** whichVirtuaBlock);
        
    //! \brief
    void addEntry(uint32_t logicalOffset, uint32_t virtualOffset);
    
    //! \brief Returns the virtual page offset within the primary block for the next page to be written.
    //!
    //! Use this method to get the virtual page offset at which a new logical page should be
    //! written. You then convert the virtual offset into a physical page address using the
    //! VirtualBlock class.
    //!
    //! This method also transparently handles the case where the virtual block has no more
    //! free pages. When this happens, the primary physical pages are made the backup physical
    //! pages. If there are already backups in place, then the backup and primary pages must
    //! be merged to make room.
    //!
    //! \param logicalSectorOffset The logical offset of the sector that will be written. If a
    //!     merge must be performed, any pages containing old copies of this logical sector will
    //!     be skipped.
    //! \param [out] offset Virtual page offset where a new page must be written.
    //! \return Either an error code or SUCCESS.
    RtStatus_t getNextOffset(uint32_t logicalSectorOffset, unsigned * offset);
    
    //! \brief Returns the number of pages that can be written before a merge or backup is necessary.
    unsigned getFreePagesInBlock();
    
    //! \brief Returns the number of currently filled pages in the virtual block.
    unsigned getCurrentPageCount() { return m_currentPageCount; }
    //@}
    
    //! \name Page addresses
    //@{
    
    //! \brief
    RtStatus_t getPhysicalPageForLogicalOffset(unsigned logicalOffset, PageAddress & physicalPage, bool * isOccupied, unsigned * virtualOffset);
    
    //! \brief
    RtStatus_t getNextPhysicalPage(unsigned logicalOffset, PageAddress & physicalPage, unsigned * virtualOffset);
    
    //@}
    
    //! \name Related objects
    //@{
    NssmManager * getManager() { return m_manager; }
    Media * getMedia() { assert(m_manager); return m_manager->getMedia(); }
    Mapper * getMapper() { assert(m_manager); return m_manager->getMapper(); }
    NssmManager::Statistics & getStatistics() { assert(m_manager); return m_manager->getStatistics(); }
    //@}
    
    //! \name Reference counting
    //@{
    void retain();
    void release();
    //@}

protected:
    
    //! \name LRU
    //@{
    void insertToLRU();
    void removeFromLRU();
    //@}
    
    //! \name LRU node methods
    //@{
    //! \brief Determines if the node is valid.
    virtual bool isNodeValid() const { return isValid(); }
    
    //! \brief Returns the node's weight value.
    //!
    //! The weight is always zero because weight is not currently used for NSSMs.
    virtual int getWeight() const { return 0; }
    //@}
    
    //! \name Tree node methods
    //@{
    virtual RedBlackTree::Key_t getKey() const;
    //@}

    //! \brief Status of last page of block.
    //!
    //! These status constants are used to track the state of the last page in the block
    //! when reading it to determine whether pages are in sorted logical order.
    enum _last_page_status
    {
        kNssmLastPageNotHandled,    //!< Haven't read the last page yet.
        kNssmLastPageErased,        //!< The last page was erased.
        kNssmLastPageOccupied       //!< The last page contains valid data.
    };

public:
    /*!
     * \brief Copy pages metadata filter for data drive blocks.
     *
     * This page filter class is used to adjust flags in the metadata of pages that are
     * copied. It can either set or clear the "in logical order" flag. By default, the flag
     * will be cleared if set. To enable setting the flag, call setLogicalOrderFlag() and
     * pass true.
     */
    class CopyPagesFlagFilter : public NandCopyPagesFilter
    {
    public:
        //! \brief Constructor.
        CopyPagesFlagFilter();
        
        //! \brief Filter method.
        virtual RtStatus_t filter(
            NandPhysicalMedia * fromNand,
            NandPhysicalMedia * toNand,
            uint32_t fromPage,
            uint32_t toPage,
            SECTOR_BUFFER * sectorBuffer,
            SECTOR_BUFFER * auxBuffer,
            bool * didModifyPage);
        
        //! \brief Change whether the logical order flag should be set.
        void setLogicalOrderFlag(bool setIt) { m_setLogicalOrder = setIt; }
        void setLba(uint32_t u32LBA) { m_LBA = u32LBA; }
    
    protected:
        bool m_setLogicalOrder; //!< Whether to set the isInLogicalOrder metadata flag on copied pages.
        uint32_t m_LBA; //!< LBA value to be injected into Metadata during copyPage operation
    };
protected:
    //! \brief Build the sector order map by reading metadata from pages.
    RtStatus_t buildMapFromMetadata(PageOrderMap & map, uint32_t * filledSectorCount);

    //! \brief Build the sector order map by reading metadata from pages.
    RtStatus_t buildMapFromMetadataMultiplane(PageOrderMap & map, uint32_t * filledSectorCount);
    
    RtStatus_t mergeBlocksCore(uint32_t u32NewSectorNumber);
    RtStatus_t shortCircuitMerge();
    RtStatus_t quickMerge();
    
    RtStatus_t getNewBlock();
    RtStatus_t preventThrashing(uint32_t u32NewSectorNumber);

protected:

    NssmManager * m_manager;    //!< Manager object that owns me.
    unsigned m_referenceCount;  //!< Number of references to this map.
    VirtualBlock m_virtualBlock;//!< Primary virtual block and cached physical addresses.
    VirtualBlock m_backupBlock; //!< Holds the cached physical addresses for backup blocks.
    bool m_isVirtualBlockValid; //!< True if the #m_virtualBlock address is valid.
    bool m_hasBackups;          //!< Whether there are backup physical blocks.
    PageOrderMap m_map;         //!< Map for the primary blocks.
    PageOrderMap m_backupMap;   //!< Map for the backup (original) physical blocks.
    uint32_t m_currentPageCount;//!< The number of actual pages that have been written. They
                                //! are written sequentially, so this is also the page offset for
                                //! the next write. This value is a virtual offset.

    friend class NssmManager;
    
    typedef struct _VirtualPageRange {
        unsigned start;     //! start point for range
        unsigned end;       //! end point for range
        unsigned uTargetPlane; //!< Target plane for this range
        unsigned planeMask; //!< Plane mask for this range
        // Initialize internal parameters
        void init(int reqd_plane)
        {
            uTargetPlane = reqd_plane;
            start       = 0;
            end         = VirtualBlock::getVirtualPagesPerBlock();
            planeMask   = VirtualBlock::getPlaneCount() - 1; 
        }
    } VirtualPageRange_t;

    inline int scanPlane_quickMerge(VirtualPageRange_t &range);
    int scanPlane_mergeBlocksCore(VirtualPageRange_t &range, unsigned uOffsetToSkip, VirtualBlock **sourceBlock);
    
};

/*!
 * \brief Task to move a virtual block to a new physical block.
 *
 * This task is used to copy the contents of a virtual block to a new physical block when
 * the data drive read sector method sees the bit error level has reached a threshold.
 *
 * \ingroup ddi_nand_data_drive
 */
class RelocateVirtualBlockTask : public DeferredTask
{
public:
    
    //! \brief Constants for the block update task.
    enum _task_constants
    {
        //! \brief Unique ID for the type of this task.
        kTaskTypeID = 'blkr',
        
        //! \brief Priority for this task type.
        kTaskPriority = 15
    };

    //! \brief Constructor.
    RelocateVirtualBlockTask(NssmManager * manager, uint32_t virtualBlockToRelocate);
    
    //! \brief Return a unique ID for this task type.
    virtual uint32_t getTaskTypeID() const;
    
    //! \brief Check for preexisting duplicate tasks in the queue.
    virtual bool examineOne(DeferredTask * task);

    //! \brief Return the logical block that needs to be refreshed.
    uint32_t getVirtualBlock() const { return m_virtualBlock; }

protected:

    //! The manager for the virtual block we're working with.
    NssmManager * m_manager;
    
    //! Virtual block number whose contents will be relocated to a new physical block.
    uint32_t m_virtualBlock;

    //! \brief The relocate task implementation.
    virtual void task();
    
};

} // namespace nand

#endif // __nonsequential_sectors_map_h__
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
