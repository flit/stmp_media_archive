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
//! \file BadBlockTable.cpp
//! \brief Implementation of the nand::BadBlockTable class.
////////////////////////////////////////////////////////////////////////////////

#include "BadBlockTable.h"
#include "components/telemetry/tss_logtext.h"
#include <string.h>

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#pragma ghs section text=".static.text"

BadBlockTable::BadBlockTable()
:   m_entries(NULL),
    m_entryCount(0),
    m_badBlockCount(0)
{
}

BadBlockTable::~BadBlockTable()
{
    release();
}

RtStatus_t BadBlockTable::allocate(uint32_t entryCount)
{
    // Shouldn't try to reallocate with first calling release().
    assert(!m_entries);
    
    // Only actually allocate if the entry count is nonzero.
    if (entryCount)
    {
        // Allocate the entry array.
        m_entries = new BlockAddress[entryCount];
        if (!m_entries)
        {
            return ERROR_OUT_OF_MEMORY;
        }
    }
    
    m_entryCount = entryCount;
    clear();
    
    return SUCCESS;
}

void BadBlockTable::release()
{
    if (m_entries)
    {
        delete [] m_entries;
        m_entries = NULL;
        m_entryCount = 0;
    }
    
    clear();
}

void BadBlockTable::clear()
{
    // Reset count to zero.
    m_badBlockCount = 0;
}

RtStatus_t BadBlockTable::growTable()
{
    // Save off the old table while we reallocate.
    BlockAddress * oldEntries = m_entries;
    uint32_t saveCount = m_badBlockCount;
    m_entries = NULL;
    
    RtStatus_t status = allocate(m_entryCount + kAllocChunkSize);
    if (status != SUCCESS)
    {
        m_entries = oldEntries;
        return status;
    }
    
    // Copy from the old table to the new one.
    memcpy(m_entries, oldEntries, saveCount);
    m_badBlockCount = saveCount;
    
    // Free the old table.
    delete [] oldEntries;
    
    return SUCCESS;
}

bool BadBlockTable::insert(const BlockAddress & newBadBlock)
{
    // Check if there is room for a new bad block.
    if (m_badBlockCount >= m_entryCount)
    {
        // Add some more entries.
        if (growTable() != SUCCESS)
        {
            return false;
        }
    }
    
    assert(m_entries);
    
    // Find where to insert the new bad block in the table in sorted order.
    int i;
    for (i=0; i < m_badBlockCount; ++i)
    {
        if (m_entries[i] >= newBadBlock)
        {
            // Move down the entries to open a hole where we can insert the new block.
            memcpy(&m_entries[i + 1], &m_entries[i], (m_badBlockCount - i) * sizeof(BlockAddress));
            
            break;
        }
    }
    
    // Insert the new bad block and increment the count.
    m_entries[i] = newBadBlock;
    ++m_badBlockCount;
    
    return true;
}

void BadBlockTable::print() const
{
    int iBadBlock;

    // Loop over bad blocks.
    for (iBadBlock = 0; iBadBlock < m_badBlockCount; ++iBadBlock)
    {
        tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1), "    0x%04X", m_entries[iBadBlock].get());
        if ((iBadBlock % 4) == 3)
        {
            tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1), "\n");
        }
    }
    
    if (((iBadBlock - 1) % 4) != 3)
    {
        tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1), "\n");
    }
}

BlockAddress BadBlockTable::skipBadBlocks(const BlockAddress & absoluteBlockNumber) const
{
    BlockAddress i = absoluteBlockNumber;

    // Scan forward starting at the provided block number.
    for (;; ++i)
    {
        if (isBlockBad(i))
        {
            continue;
        }
        else
        {
            break;
        }
    }

    return i;
}

bool BadBlockTable::isBlockBad(const BlockAddress & theBlock) const
{
    if (!m_badBlockCount)
    {
        return false;
    }
    
    assert(m_entries);
    
    // Take advantage of known sorted order to do a binary search.
    int l = -1;
    int h = m_badBlockCount;
    int i;
    
    while (l < h - 1)
    {
        i = l + (h - l) / 2;

        if (theBlock == m_entries[i])
        {
            return true;
        }
        else if (theBlock < m_entries[i])
        {
            h = i;
        }
        else // theBlock > m_entries[i]
        {
            l = i;
        }
    }
    
    return false;
}

bool BadBlockTable::adjustForBadBlocksInRange(BlockAddress & startBlock, uint32_t & blockCount, GrowDirection_t whichDir) const
{
    uint32_t blocksToReplace = countBadBlocksInRange(startBlock, blockCount);
    
    while (blocksToReplace)
    {
        // Increment the range length.
        ++blockCount;
        
        BlockAddress testBlock;
        if (whichDir == kGrowUp)
        {
            // Compute the location of the last block in the range.
            testBlock = BlockAddress(startBlock + blockCount - 1);

            // Check for overrunning the end of the NANDs.
            if (testBlock >= NandHal::getTotalBlockCount())
            {
                break;
            }
        }
        else if (whichDir == kGrowDown)
        {
            // Make sure we haven't hit the first block.
            if (startBlock == 0)
            {
                break;
            }
            
            // Decrement range start.
            testBlock = --startBlock;
        }
        
        // If the test block is good, then we have one less block to replace.
        if (!isBlockBad(testBlock))
        {
            --blocksToReplace;
        }
    }
    
    // We succeeded if we were able to replace all bad blocks.
    return (blocksToReplace == 0);
}

uint32_t BadBlockTable::countBadBlocksInRange(const BlockAddress & startBlock, uint32_t blockCount) const
{
    BlockAddress i = startBlock;
    uint32_t badBlockCount = 0;
    for (; blockCount--; ++i)
    {
        if (isBlockBad(i))
        {
            ++badBlockCount;
        }
    }
    
    return badBlockCount;
}

#pragma ghs section text=default

////////////////////////////////////////////////////////////////////////////////
// EOF
////////////////////////////////////////////////////////////////////////////////
