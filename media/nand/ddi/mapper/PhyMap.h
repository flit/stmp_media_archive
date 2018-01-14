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
//! \file PhyMap.h
//! \brief Definition of the nand::PhyMap class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__phymap_h__)
#define __phymap_h__

#include "types.h"
#include "errordefs.h"
#include "drivers/media/sectordef.h"

namespace nand
{

/*!
 * \brief A bitmap of the occupied blocks on the NANDs.
 *
 * The phymap, or physical map, is a bitmap of all blocks on all NAND chip enables. The main
 * purpose of the phymap is to enable efficient searching for available blocks when writing new
 * data to a drive, or when relocating data from another block. Each block in the map can be
 * marked either free or used. A free block is erased and is not allocated to any purpose.
 *
 * If a block is marked as used, then it may actually be in one of several states,
 * but the point is that it is not available for use to hold new data. Used blocks may
 * contain valid data for any one of the drives, including system drives. They may be boot
 * blocks or other blocks used by the NAND driver for its own purposes. Finally, all bad blocks
 * are marked as used.
 */
class PhyMap
{
public:
    //! \brief Constants for phymap entries.
    enum _entry_constants
    {
        kEntrySizeInBytes = sizeof(uint32_t),
        kBlocksPerEntry = 32,
        kFullEntry = 0  //!< An entry with a value of 0 means that all blocks are occupied.
    };
    
    //! \brief Constants to use for marking blocks in the phymap.
    enum _free_or_used
    {
        kFree = true,  //!< The block is free and available for use.
        kUsed = false    //!< The block either contains valid data or is bad.
    };
    
    //! \brief Constants for the auto-erase parameter of markBlock().
    enum _auto_erase
    {
        kAutoErase = true,  //!< When marking a block free, automatically erase the block if it's not already erased.
        kDontAutoErase = false  //!< Never erase the block when marking it free.
    };
    
    //! \brief Callback used to signal changes to the dirty state.
    typedef void (*DirtyCallback_t)(PhyMap * thePhymap, bool wasDirty, bool isDirty, void * refCon);
    
    //! \brief Computes the number of entries required to hold a given number of blocks.
    static uint32_t getEntryCountForBlockCount(uint32_t blockCount) { return ROUND_UP_DIV(blockCount, kBlocksPerEntry); }
    
    //! \name Init and cleanup
    //@{
        //! \brief Initializer.
        RtStatus_t init(uint32_t totalBlockCount);
        
        //! \brief Destructor.
        ~PhyMap();
    //@}
    
    //! \name Marking entries
    //@{
        //! \brief Set all entries to one state.
        void markAll(bool isFree);
        
        //! \brief Mark a block as either free or used.
        RtStatus_t markBlock(uint32_t absoluteBlock, bool isFree, bool doAutoErase=kDontAutoErase);
        
        //! \brief Mark a block as free.
        inline RtStatus_t markBlockFree(uint32_t absoluteBlock) { return markBlock(absoluteBlock, kFree); }
        
        //! \brief Mark a block as free and perform the auto-erase function.
        inline RtStatus_t markBlockFreeAndErase(uint32_t absoluteBlock) { return markBlock(absoluteBlock, kFree, kAutoErase); }
        
        //! \brief Mark a block as used.
        inline RtStatus_t markBlockUsed(uint32_t absoluteBlock) { return markBlock(absoluteBlock, kUsed); }
        
        //! \brief Mark a range of blocks as either free or used.
        RtStatus_t markRange(uint32_t absoluteStartBlock, uint32_t blockCount, bool isFree, bool doAutoErase=kDontAutoErase);
    //@}
    
    //! \name Counts
    //@{
        //! \brief Returns the total number of blocks.
        inline uint32_t getBlockCount() const { return m_blockCount; }
        
        //! \brief Returns the total number of entries.
        inline uint32_t getEntryCount() const { return m_entryCount; }
        
        //! \brief Computes the number of free blocks.
        uint32_t getFreeCount() const;
    //@}
    
    //! \name Getting block state
    //@{
        //! \brief Returns the state of one block.
        bool isBlockFree(uint32_t absoluteBlock) const;
        
        //! \brief Returns true if the block is marked as used.
        inline bool isBlockUsed(uint32_t absoluteBlock) const { return !isBlockFree(absoluteBlock); }
    //@}
    
    //! \name Searching
    //@{
    
        //! \brief Find the first free block within a block range.
        //! \param startBlock The block number to start searching from.
        //! \param endBlock The last block to examine in the search.
        //! \param[out] freeBlock Filled in with the address of the first free block found
        //!     in the provided range. This parameter must not be NULL.
        //! \param planeMask Mask on block number to isolate the plane number. Optional.
        //! \param planeNumber The required plane that the result block must belong to. Optional.
        //! \retval true A free block was found and the value is available in \a freeBlock.
        //! \retval false All blocks in the provided range are in use.
        bool findFirstFreeBlock(uint32_t startBlock, uint32_t endBlock, uint32_t * freeBlock, unsigned planeMask=0, unsigned planeNumber=0);
    
    //@}
    
    //! \brief Direct entry access
    //@{
        //! \brief Returns a pointer to the entire map array.
        inline uint32_t * getAllEntries() { return m_entries; }
        
        //! \brief Read/write entry access operator.
        inline uint32_t & operator [] (uint32_t entryIndex) { return m_entries[entryIndex]; }
        
        //! \brief Gives up ownership of the map array.
        void relinquishEntries();
    //@}
    
    //! \brief Dirty flag
    //@{
        //! \brief Returns true if the map is dirty.
        inline bool isDirty() const { return m_isDirty; }
        
        //! \brief Sets the dirty flag.
        void setDirty();
        
        //! \brief Clears the dirty flag.
        void clearDirty();
        
        //! \brief Sets the dirty change callback.
        inline void setDirtyCallback(DirtyCallback_t callback, void * refCon) { m_dirtyListener = callback; m_dirtyRefCon = refCon; }
    //@}

protected:
    uint32_t m_blockCount;  //!< Total number of blocks represented in the map.
    uint32_t m_entryCount;  //!< Number of phymap entries.
    uint32_t * m_entries;   //!< Phymap entry array.
    bool m_isDirty;         //!< Whether the phymap has been modified recently.
    DirtyCallback_t m_dirtyListener;    //!< Callback function to invoke when the dirty state changes.
    void * m_dirtyRefCon;    //!< Arbitrary value passed to dirty listener.


    //! \brief Find the first free block within an entry
    int searchEntryBitField(uint32_t entryBitField, int startIndex, int endIndex, bool * foundFreeBlock, unsigned planeMask, unsigned planeNumber);

};

} // namespace nand

#endif // __phymap_h__
////////////////////////////////////////////////////////////////////////////////
// EOF
////////////////////////////////////////////////////////////////////////////////
