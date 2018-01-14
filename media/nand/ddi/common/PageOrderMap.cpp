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
//! \file PageOrderMap.cpp
//! \brief 
///////////////////////////////////////////////////////////////////////////////

#include "PageOrderMap.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include "drivers/media/sectordef.h"

using namespace nand;

///////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////

//! Number of bits in a word.
#define BITS_PER_WORD (32)

//! Computes the size of the occupied array.
#define OCCUPIED_SIZE(n) (ROUND_UP_DIV((n), BITS_PER_WORD) * sizeof(uint32_t))

extern uint32_t count_ones_32(uint32_t value);

///////////////////////////////////////////////////////////////////////////////
// Source
///////////////////////////////////////////////////////////////////////////////

PageOrderMap & PageOrderMap::operator = (const PageOrderMap & other)
{
    // Maps must match in entry count and size.
    assert(m_entryCount == other.m_entryCount);
    assert(m_entrySize == other.m_entrySize);
    
    // Copy map only if LSI Table is allocated
    if ( m_map != NULL )
    {
        memcpy(m_map, other.m_map, (m_entryCount * m_entrySize));
    }
    memcpy(m_occupied, other.m_occupied, OCCUPIED_SIZE(m_entryCount));
    
    return *this;
}

RtStatus_t PageOrderMap::init(unsigned entryCount, unsigned maxEntryValue, bool bAllocLSITable)
{
    // Cannot re-init without first cleaning up.
    assert(!m_occupied);
    
    // Make sure entries will fit within a byte value. If this assert hits, you
    // probably need to change to uint16_t entries.
    m_entryCount = entryCount;
    
    // Set the default max value to the entry count.
    m_entrySize = getEntrySize(entryCount,maxEntryValue);
    // Allocate block to be shared by the occupied bitmap and map array.
    if (bAllocLSITable == true)
    {
        m_occupied = (uint32_t *)malloc(OCCUPIED_SIZE(entryCount) + (entryCount * m_entrySize));
    }
    else
    {
        m_occupied = (uint32_t *)malloc(OCCUPIED_SIZE(entryCount));    
    }
    if (!m_occupied)
    {
        return ERROR_OUT_OF_MEMORY;
    }
    
    // Point map array in the allocated block, just after the bitmap.
    if (bAllocLSITable == true)
    {
        m_map = (uint8_t *)m_occupied + OCCUPIED_SIZE(entryCount);
    }
    else
    {
        m_map = (uint8_t *)NULL;
    }
    
    // Wipe map to entirely unoccupied.
    clear();
    
    return SUCCESS;
}

void PageOrderMap::cleanup()
{
    if (m_occupied)
    {
        // Free the one block we allocated.
        free(m_occupied);
        m_occupied = NULL;
        m_map = NULL;
    }
}

unsigned PageOrderMap::getEntry(unsigned logicalIndex) const
{
    assert(logicalIndex < m_entryCount);
    switch (m_entrySize)
    {
        case sizeof(uint8_t):
            return m_map[logicalIndex];
        case sizeof(uint16_t):
            return ((uint16_t *)m_map)[logicalIndex];
        case sizeof(uint32_t):
            return ((uint32_t *)m_map)[logicalIndex];
        default:
            assert(false);
            return 0;
    }
}

void PageOrderMap::setEntry(unsigned logicalIndex, unsigned physicalIndex)
{
    assert(logicalIndex < m_entryCount);
    switch (m_entrySize)
    {
        case sizeof(uint8_t):
            m_map[logicalIndex] = physicalIndex;
            break;
        case sizeof(uint16_t):
            ((uint16_t *)m_map)[logicalIndex] = physicalIndex;
            break;
        case sizeof(uint32_t):
            ((uint32_t *)m_map)[logicalIndex] = physicalIndex;
            break;
        default:
            assert(false);
    }
    
    setOccupied(logicalIndex);
}

bool PageOrderMap::isOccupied(unsigned logicalIndex) const
{
    assert(logicalIndex < m_entryCount);
    unsigned coarse = logicalIndex / BITS_PER_WORD;
    unsigned fine = logicalIndex % BITS_PER_WORD;
    return ((m_occupied[coarse] >> fine) & 0x1) != 0;
}

void PageOrderMap::setOccupied(unsigned logicalIndex, bool isOccupied)
{
    assert(logicalIndex < m_entryCount);
    unsigned coarse = logicalIndex / BITS_PER_WORD;
    unsigned fine = logicalIndex % BITS_PER_WORD;
    uint32_t mask = 0x1 << fine;
    if (isOccupied)
    {
        m_occupied[coarse] |= mask;
    }
    else
    {
        m_occupied[coarse] &= ~mask;
    }
}

unsigned PageOrderMap::operator [] (unsigned logicalIndex) const
{
    return getEntry(logicalIndex);
}

bool PageOrderMap::isInSortedOrder(unsigned entriesToCheck) const
{
    int i;
    entriesToCheck = std::min(entriesToCheck, m_entryCount);
    for (i = 0; i < entriesToCheck; i++)
    {
        if (!isOccupied(i) || getEntry(i) != i)
        {
            return false;
        }
    }
    
    return true;
}

void PageOrderMap::setSortedOrder()
{
    setSortedOrder(0, m_entryCount, 0);
}

void PageOrderMap::setSortedOrder(unsigned startEntry, unsigned count, unsigned startValue)
{
    unsigned i;
    
    // Set each entry's physical index equal to the logical index.
    count = std::min(count, m_entryCount - startEntry);
    for (i=0; i < count; ++i, ++startEntry, ++startValue)
    {
        setEntry(startEntry, startValue);
    }
}

void PageOrderMap::clear(bool bClearLSITable)
{
    if (m_map != NULL && bClearLSITable)
    {
        memset(m_map, 0, (m_entryCount * m_entrySize));
    }
    memset(m_occupied, 0, OCCUPIED_SIZE(m_entryCount));
}

unsigned PageOrderMap::countDistinctEntries() const
{
    unsigned distinctCount = 0;
    unsigned i;
    for (i=0; i < ROUND_UP_DIV(m_entryCount, BITS_PER_WORD); i++)
    {
        // Count the ones in this occupied map word. This will only work properly when there
        // is a trailing edge if clear() was called at init time to set all bits to zero to
        // begin with. That is because we will count any bits set in the trailing edge as if
        // they were actual entries.
        distinctCount += count_ones_32(m_occupied[i]);
    }

    return distinctCount;
}

unsigned PageOrderMap::countEntriesNotInOtherMap(const PageOrderMap & other) const
{
    // Just exit if the other map has a different number of entries than me.
    if (m_entryCount != other.getEntryCount())
    {
        return 0;
    }
    
    unsigned entriesOnlyInMe = 0;
    unsigned i;
    for (i=0; i < m_entryCount; ++i)
    {
        if (isOccupied(i) && !other.isOccupied(i))
        {
            ++entriesOnlyInMe;
        }
    }
    
    return entriesOnlyInMe;
}

unsigned PageOrderMap::getEntrySize(unsigned entryCount, unsigned maxEntryValue)
{
    unsigned entrySize;
    
    // Set the default max value to the entry count.
    if (maxEntryValue == 0)
    {
        maxEntryValue = entryCount - 1;
    }
    
    // Determine the size of each entry in bytes.
    if (maxEntryValue <= 0xff)
    {
        entrySize = sizeof(uint8_t);
    }
    else if (maxEntryValue <= 0xffff)
    {
        entrySize = sizeof(uint16_t);
    }
    else
    {
        entrySize = sizeof(uint32_t);
    }
    return entrySize;
}
        
//! \breif Assign given pointer to m_map array pointer of PageOrderMap        
void PageOrderMap::setMapArray(uint8_t *pArray)
{
    assert(pArray);
    
    m_map = pArray;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
