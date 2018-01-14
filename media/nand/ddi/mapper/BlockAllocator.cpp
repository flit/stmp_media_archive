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
//! \file BlockAllocator.cpp
//! \brief Block allocation algorithms.
////////////////////////////////////////////////////////////////////////////////

#include "BlockAllocator.h"
#include <string.h>
#include <algorithm>
#include "registers/regsdigctl.h"
#include "PhyMap.h"
#include "ddi_nand_hal.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
// Code
/////////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark --BlockAllocator--
#endif

BlockAllocator::BlockAllocator(PhyMap * map)
:   m_phymap(map),
    m_start(0),
    m_end(0),
    m_constraints()
{
}

void BlockAllocator::setRange(uint32_t start, uint32_t end)
{
    m_start = start;
    m_end = end;
}

void BlockAllocator::setConstraints(const Constraints & newConstraints)
{
    m_constraints = newConstraints;
}

void BlockAllocator::clearConstraints()
{
    m_constraints.m_chip = Constraints::kUnconstrained;
    m_constraints.m_die = Constraints::kUnconstrained;
    m_constraints.m_plane = Constraints::kUnconstrained;
}

//! Only the chip and die constraints apply for limiting the range. The plane constraint
//! must be applied when searching for an available block. If a die is specified but the
//! chip is not, then the full range will be returned.
void BlockAllocator::getConstrainedRange(uint32_t & start, uint32_t & end)
{
    // If the chip is not constrained, then just return the whole range. This also
    // covers the case where the die is set but not the chip.
    if (m_constraints.m_chip == Constraints::kUnconstrained)
    {
        // Return the full range.
        start = m_start;
        end = m_end;
        return;
    }
    
    // Figure out the limits first, starting with the chip.
    NandPhysicalMedia * nand = NandHal::getNand(m_constraints.m_chip);
    assert(nand);
    
    uint32_t limitStart = nand->baseAbsoluteBlock();
    uint32_t limitEnd = nand->baseAbsoluteBlock() + nand->wTotalBlocks - 1;
    
    // Limit to the die, if specified.
    if (m_constraints.m_die != Constraints::kUnconstrained)
    {
        assert(m_constraints.m_die >= 0 && m_constraints.m_die < nand->wTotalInternalDice);
        
        limitStart += m_constraints.m_die * nand->wBlocksPerDie;
        limitEnd = limitStart + nand->wBlocksPerDie - 1;
    }
    
    // Make sure the range and limit overlap at least somewhat.
    if (m_start > limitEnd || m_end < limitStart)
    {
        // No overlap, so the constrained range is empty.
        start = 0;
        end = 0;
        return;
    }
    
    // Return the intersection of the range and limit.
    start = std::max(m_start, limitStart);
    end = std::min(m_end, limitEnd);
}

//! The plane constraint, if set, is honoured when searching for a free block.
//!
bool BlockAllocator::splitSearch(uint32_t start, uint32_t end, uint32_t position, uint32_t * result)
{
    assert(m_phymap);
    assert(position >= start && position <= end);

    // Prepare plane parameters for the search, if the plane is constrained.
    unsigned planeMask = 0;
    unsigned planeNumber = 0;
    if (m_constraints.m_plane != Constraints::kUnconstrained)
    {
        planeMask = NandHal::getParameters().planesPerDie - 1;
        planeNumber = m_constraints.m_plane;
    }
    
    // Search from the given position to the end of the range.
    bool foundOne = m_phymap->findFirstFreeBlock(position, end, result, planeMask, planeNumber);
    
    // If not starting from the very beginning of the range and the previous search didn't
    // find a free block, then search from the beginning to the given position.
    if (!foundOne && position > start)
    {
        foundOne = m_phymap->findFirstFreeBlock(start, position - 1, result, planeMask, planeNumber);
    }
    
    return foundOne;
}

#if !defined(__ghs__)
#pragma mark --RandomBlockAllocator--
#endif

RandomBlockAllocator::RandomBlockAllocator(PhyMap * map)
:   BlockAllocator(map),
    m_rng()
{
    // Seed PRNG.
    m_rng.setSeed(HW_DIGCTL_ENTROPY_RD() ^ HW_DIGCTL_MICROSECONDS_RD());
}

bool RandomBlockAllocator::allocateBlock(uint32_t & newBlockAddress)
{
    // Get the actual range we are going to use.
    uint32_t start;
    uint32_t end;
    getConstrainedRange(start, end);
    
    uint32_t rangeSize = end - start + 1; // End is inclusive so we need to add 1.
    uint32_t position = start + m_rng.next(rangeSize);
    uint32_t result;
    
    // Search from the random position, looping around if necessary.
    bool foundOne = splitSearch(start, end, position, &result);
    
    if (foundOne)
    {
        newBlockAddress = result;
    }
    
    return foundOne;
}

#if !defined(__ghs__)
#pragma mark --LinearBlockAllocator--
#endif

LinearBlockAllocator::LinearBlockAllocator(PhyMap * map)
:   BlockAllocator(map),
    m_currentPosition(0)
{
}

bool LinearBlockAllocator::allocateBlock(uint32_t & newBlockAddress)
{
    assert(m_phymap);
    
    // Get the actual range we are going to use.
    uint32_t start;
    uint32_t end;
    getConstrainedRange(start, end);
    
    uint32_t result;
    bool foundOne;
    
    // Ensure the current position is within the constrained range.
    if (m_currentPosition < start || m_currentPosition > end)
    {
        m_currentPosition = start;
    }
    
    // Search from the current position to the end of the range.
    foundOne = splitSearch(start, end, m_currentPosition, &result);
    
    if (foundOne)
    {
        newBlockAddress = result;
        
        // Save current position so we start searching from there next time.
        m_currentPosition = result;
        if (m_currentPosition >= end)
        {
            m_currentPosition = start;
        }
    }
    
    return foundOne;
}

void LinearBlockAllocator::setCurrentPosition(uint32_t position)
{
    // Update the position and make sure it's within the specified range.
    if (position < m_start)
    {
        m_currentPosition = m_start;
    }
    else if (position > m_end)
    {
        m_currentPosition = m_end;
    }
    else
    {
        m_currentPosition = position;
    }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
