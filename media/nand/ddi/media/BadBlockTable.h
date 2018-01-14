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
//! \addtogroup ddi_nand_media
//! @{
//! \file BadBlockTable.h
//! \brief Definition of the nand::BadBlockTable class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__badblocktable_h__)
#define __badblocktable_h__

#include "types.h"
#include "errordefs.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace nand
{

/*!
 * \brief Table to track bad blocks.
 *
 * When a bad block table is first instantiated, it will be empty and have no table. You can
 * use the allocate() method to set the table to a certain maximum size, if you know in advance
 * how many bad blocks there might be. The release() method does the opposite, and will deallocate
 * all memory used by the table.
 *
 * The table is always kept in increasing sorted order. The insert() method will ensure that a
 * new bad block is inserted in the correct position to maintain the order. Even if you do not
 * explicitly allocate entries, the table will automatically grow to accomodate new bad blocks
 * as they are inserted.
 */
class BadBlockTable
{
public:
    //! \brief Default constructor.
    BadBlockTable();
    
    //! \brief Destructor.
    ~BadBlockTable();
    
    //! \brief Allocate enough room for \a entryCount bad blocks.
    RtStatus_t allocate(uint32_t entryCount);
    
    //! \brief Free all memory owned by the object.
    void release();
    
    //! \brief Removes all bad blocks from the table.
    void clear();
    
    //! \name Accessors
    //@{
    uint32_t getCount() const { return m_badBlockCount; }
    uint32_t getMaxCount() const { return m_entryCount; }
    
    const BlockAddress & operator [] (int index) const { assert(index < m_badBlockCount); return m_entries[index]; }
    
    bool isBlockBad(const BlockAddress & theBlock) const;
    //@}
    
    //! \brief Dump the table contents to TSS.
    void print() const;
    
    //! \brief Add a new bad block into the table.
    //!
    //! Bad blocks are always inserted in sorted order. If there is no room left in the table,
    //! it will be reallocated to add a few new entries before the new bad block is inserted.
    bool insert(const BlockAddress & newBadBlock);

    //! \brief Skip bad blocks.
    //!
    //! \param absoluteBlockNumber Original absolute block number.
    //! \return Block number modified to be the next block after any bad blocks.
    BlockAddress skipBadBlocks(const BlockAddress & absoluteBlockNumber) const;
    
    //! \brief Options for how to adjust for bad blocks.
    typedef enum _grow_direction
    {
        kGrowUp,    //!< Increment the end of the block range to adjust for bad blocks.
        kGrowDown   //!< Adjust the beginning of the block range, decrementing as necessary.
    } GrowDirection_t;
    
    //! \brief Modify a block range to hold a minimum number of good blocks.
    //! \param blockCount On exit, the adjusted block count. If growing down, then the
    //!     \a startBlock argument will also be modified.
    //! \return A boolean indicating whether all bad blocks were accounted for. If the result
    //!     is false, then either the beginning or end, depending on the grow direction, of
    //!     all NANDs was hit and there was no more room to grow in the indicated direction.
    bool adjustForBadBlocksInRange(BlockAddress & startBlock, uint32_t & blockCount, GrowDirection_t whichDir) const;
    
    //! \brief Count the number of bad blocks within a certain block range.
    uint32_t countBadBlocksInRange(const BlockAddress & startBlock, uint32_t blockCount) const;

protected:
    
    //!
    enum _alloc_consts
    {
        //! Number of entries to add to the table when reallocating.
        kAllocChunkSize = 5
    };
    
    BlockAddress * m_entries;   //!< Pointer to array of bad block entries.
    uint32_t m_entryCount;      //!< Maximum number of entries in the array.
    uint32_t m_badBlockCount;   //!< Actual number of valid bad block entries in the array. Always <= m_entryCount.
    
    //! \brief Add room for more entries to the table.
    RtStatus_t growTable();
};

} // namespace nand

#endif // __badblocktable_h__
////////////////////////////////////////////////////////////////////////////////
// EOF
////////////////////////////////////////////////////////////////////////////////
