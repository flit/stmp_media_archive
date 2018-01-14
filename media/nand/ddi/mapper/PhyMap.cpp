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
//! \addtogroup ddi_nand_mapper
//! @{
//! \file PhyMap.cpp
//! \brief Implementation of the physical NAND block occupied status bitmap.
///////////////////////////////////////////////////////////////////////////////

#include "PhyMap.h"
#include "Mapper.h"
#include "Block.h"
#include <string.h>

using namespace nand;

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

//! For speed:
//! Each array element contains the number of ones in its nibble sized index.
const uint8_t kNumBitsHighForAnIndex[16] =
{
    0, // index 0x0==0000 binary: 0 bits high.
    1, // index 0x1==0001 binary: 1 bit  high.
    1, // index 0x2==0010 binary: 1 bit  high.
    2, // index 0x3==0011 binary: 2 bits high, so store a 2 here as num high bits for that index.
    1, // index 0x4==0100 binary: 1 bit  high
    2, // index 0x5==0101 binary: 2 bits high
    2, // index 0x6==1010 binary: 2 bits high
    3, // index 0x7==1011 binary: 3 bits high
    1, // index 0x8==1000 binary: 1 bit  high
    2, // index 0x9==1001 binary: 2 bits high
    2, // index 0xA==1010 binary: 2 bits high
    3, // index 0xB==1011 binary: 3 bits high
    2, // index 0xC==1100 binary: 2 bits high
    3, // index 0xD==1101 binary: 3 bits high
    3, // index 0xE==1110 binary: 3 bits high
    4  // index 0xF==1111 binary: 4 bits high
};

///////////////////////////////////////////////////////////////////////////////
// Source
///////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Count the number of bits which are set in given 16-bit word.
//!
//! This function counts the number of bits in given 16-bit word which are set.
//! It accomplishes this by table look-up of all constituent nibbles.
//!
//! \param[in]  u16Value  The word whose set bits this function will count.
//! \return Number of bits which are set in u16Value.
////////////////////////////////////////////////////////////////////////////////
uint32_t count_ones_16(uint32_t u16Value)
{
    uint32_t u32CountFirstNibble  = kNumBitsHighForAnIndex[ u16Value        & 0xF];
    uint32_t u32CountSecondNibble = kNumBitsHighForAnIndex[(u16Value >> 4)  & 0xF];
    uint32_t u32CountThirdNibble  = kNumBitsHighForAnIndex[(u16Value >> 8)  & 0xF];
    uint32_t u32CountFourthNibble = kNumBitsHighForAnIndex[(u16Value >> 12) & 0xF];

    return (u32CountFirstNibble + u32CountSecondNibble + u32CountThirdNibble + u32CountFourthNibble);
}

//! \brief Count the number of set bits in a 32-bit word.
uint32_t count_ones_32(uint32_t value)
{
    return count_ones_16(value & 0xffff) + count_ones_16(value >> 16);
}

RtStatus_t PhyMap::init(uint32_t totalBlockCount)
{
    // Clear listener callback so we don't try to call it when marking all below.
    m_dirtyListener = NULL;
    m_dirtyRefCon = 0;
    
    // Save block count.
    m_blockCount = totalBlockCount;
    
    // Allocate an array large enough to have entries for every block.
    m_entryCount = getEntryCountForBlockCount(totalBlockCount);
    m_entries = new uint32_t[m_entryCount];
    assert(m_entries);
    
    // The entries start out all marked as used.
    markAll(kUsed);

    // Clear the dirty flag that was just set by markAll().
    clearDirty();
    
    return SUCCESS;
}
    
PhyMap::~PhyMap()
{
    if (m_entries)
    {
        delete [] m_entries;
        m_entries = NULL;
    }
}

void PhyMap::relinquishEntries()
{
    m_entries = NULL;
    clearDirty();
}

void PhyMap::markAll(bool isFree)
{
    // Used entries are marked 0, free are marked 1.
    memset(m_entries, isFree ? 0xff : 0, m_entryCount * kEntrySizeInBytes);
    
    // Set the map to be dirty.
    setDirty();
}

RtStatus_t PhyMap::markBlock(uint32_t absoluteBlock, bool isFree, bool doAutoErase)
{
    // Validate block address.
    assert(absoluteBlock < m_blockCount);
    assert(m_entries);
    
    // Find the array index where this phys block belongs
    uint32_t coarseIndex = absoluteBlock / kBlocksPerEntry;
    uint32_t fineIndex = absoluteBlock % kBlocksPerEntry;
    uint32_t entryValue = m_entries[coarseIndex];
    uint32_t blockMask = (1 << fineIndex);

    // Set the bit or clear it accordingly
    if (isFree)
    {
        // Mark the block as free by setting its bit in the entry.
        entryValue |= blockMask;

        // Ensure that the block is actually erased.
        Block block(absoluteBlock);
        if (doAutoErase && !block.isErased())
        {
            // As well as setting the bit, erase physical block
            RtStatus_t retCode = block.eraseAndMarkOnFailure();

            if (retCode)
            {
                // Mark the block bad and return success.
                //! \todo Must be able to add this new bad block to BBRC and update DBBT!
                entryValue &= ~blockMask;
                
                // Add this new bad block to the appropriate region.
                Region * region = g_nandMedia->getRegionForBlock(block);
                if (region)
                {
                    region->addNewBadBlock(block);
                }
            }
        }
    }
    else
    {
        // Mark the block as used by clearing its bit in the entry.
        entryValue &= ~blockMask;
    }

    // Update the map entry.
    m_entries[coarseIndex] = entryValue;
    
    // The phy map is now dirty.
    setDirty();
    
    return SUCCESS;
}

//! \todo Optimize this in the case where it is not auto erasing, so it can set entire
//!     entries at once (if the range is large enough).
RtStatus_t PhyMap::markRange(uint32_t absoluteStartBlock, uint32_t blockCount, bool isFree, bool doAutoErase)
{
    uint32_t theBlock;
    for (theBlock = absoluteStartBlock; theBlock < absoluteStartBlock + blockCount; ++theBlock)
    {
        RtStatus_t status = markBlock(theBlock, isFree, doAutoErase);
        if (status != SUCCESS)
        {
            return status;
        }
    }
    
    return SUCCESS;
}

bool PhyMap::isBlockFree(uint32_t absoluteBlock) const
{
    // Find the array index where this phys block belongs
    uint32_t coarseIndex = absoluteBlock / kBlocksPerEntry;
    uint32_t fineIndex = absoluteBlock % kBlocksPerEntry;
    uint32_t blockMask = (1 << fineIndex);

    // The block is free if the bit is nonzero.
    return (m_entries[coarseIndex] & blockMask) != 0;
}

//! This function counts the number of unused blocks in the map by counting
//! the number of bits which are set.
//!
//! \return Number of free blocks on all NANDs.
uint32_t PhyMap::getFreeCount() const
{
    unsigned i;
    uint32_t u32FreeCount = 0;

    for (i = 0; i < m_entryCount; i++)
    {
        u32FreeCount += count_ones_32(m_entries[i]);
    }

    return u32FreeCount;
}

void PhyMap::setDirty()
{
    bool oldDirty = m_isDirty;
    m_isDirty = true;
    
    // Invoke dirty callback.
    if (m_dirtyListener)
    {
        m_dirtyListener(this, oldDirty, m_isDirty, m_dirtyRefCon);
    }
}

void PhyMap::clearDirty()
{
    bool oldDirty = m_isDirty;
    m_isDirty = false;
    
    // Invoke dirty callback.
    if (m_dirtyListener)
    {
        m_dirtyListener(this, oldDirty, m_isDirty, m_dirtyRefCon);
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Searches a phy map entry for an empty block.
//!
//! \param entryBitField The 16-bit bit field to search, where a 1 bit is an
//!     available slot and 0 is occupied.
//! \param startIndex Starting bit to search from.
//! \param endIndex The bit number at which the search will be stopped. This
//!     is one position after the last bit examined.
//! \param[out] foundFreeBlock Pointer to a boolean which is set to true upon
//!     a positive match.
//! \param planeMask Mask on block number to isolate the plane number. If zero is passed
//!     then this parameter is effectively ignored.
//! \param planeNumber The required plane that the result block must belong to. Ignored
//!     if the \a planeMask is zero.
//!
//! \return The index of an available block is returned, or -1 is returned if
//!     the scanned range of the entry is completely occupied.
////////////////////////////////////////////////////////////////////////////////
int PhyMap::searchEntryBitField(uint32_t entryBitField, int startIndex, int endIndex, bool * foundFreeBlock, unsigned planeMask, unsigned planeNumber)
{
    int index = startIndex;
    
    // Check index ranges.
    assert(startIndex >= 0 && startIndex < kBlocksPerEntry);
    assert(endIndex >= 0 && endIndex <= kBlocksPerEntry);
    
    // Shift entry value to start index.
    if (startIndex)
    {
        entryBitField >>= startIndex;
    }
    
    // Scan each bit looking for a 1 that is in the correct plane.
    while (index < endIndex
        && ((entryBitField & 1) == 0
            || (index & planeMask) != planeNumber))
    {
        index++;
        entryBitField >>= 1;
    }
    
    *foundFreeBlock = index < endIndex;
    
    // Return a -1 if no available slot was found.
    return index < endIndex ? index : -1;
}

bool PhyMap::findFirstFreeBlock(uint32_t startBlock, uint32_t endBlock, uint32_t * freeBlock, unsigned planeMask, unsigned planeNumber)
{
    assert(freeBlock);
    
    uint32_t startCoarseIndex = startBlock / kBlocksPerEntry;
    uint32_t startFineIndex = startBlock % kBlocksPerEntry;
    uint32_t endCoarseIndex = endBlock / kBlocksPerEntry;
    uint32_t endFineIndex = endBlock % kBlocksPerEntry;
    uint32_t coarseIndex = startCoarseIndex;
    int fineIndex;
    bool didFindBlock = false;
    
    while (coarseIndex <= endCoarseIndex)
    {
        // Get this phy map entry.
        uint32_t bitField = m_entries[coarseIndex];
        
        // Don't bother with the entry if it is full.
        if (bitField != kFullEntry)
        {
            // Figure out where to start and stop seaching in this entry.
            int searchStart = (coarseIndex == startCoarseIndex) ? startFineIndex : 0;
            int searchEnd = (coarseIndex == endCoarseIndex) ? (endFineIndex + 1) : kBlocksPerEntry;
            
            // Search this entry.
            fineIndex = searchEntryBitField(bitField, searchStart, searchEnd, &didFindBlock, planeMask, planeNumber);
            
            // Exit the search loop if we've found an available block.
            if (didFindBlock)
            {
                // Make sure the fine index is within range.
                assert(fineIndex >= 0 && fineIndex < kBlocksPerEntry);

                // Compute and return the physical block address.
                *freeBlock = coarseIndex * kBlocksPerEntry + fineIndex;
                
                break;
            }
        }
        
        // Move to the next entry.
        ++coarseIndex;
    }
    
    return didFindBlock;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
