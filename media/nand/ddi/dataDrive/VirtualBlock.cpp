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
//! \file
//! \brief Implementation of VirtualBlock class.
////////////////////////////////////////////////////////////////////////////////

#include "VirtualBlock.h"
#include "Region.h"
#include "Mapper.h"
#include "PhyMap.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

unsigned VirtualBlock::s_planes = 0;
unsigned VirtualBlock::s_virtualPagesPerBlock = 0;
unsigned VirtualBlock::s_planeMask = 0;
unsigned VirtualBlock::s_planeShift = 0;
unsigned VirtualBlock::s_virtualPagesPerBlockMask = 0;
unsigned VirtualBlock::s_virtualPagesPerBlockShift = 0; 

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

VirtualBlock::VirtualBlock()
:   BlockAddress(),
    m_mapper(NULL)
{
    assert(s_planes != 0);
    
    // Clear validity flags for physical addresses.
    clearCachedPhysicalAddresses();
}

VirtualBlock::VirtualBlock(Mapper * theMapper)
:   BlockAddress(),
    m_mapper(theMapper)
{
    assert(s_planes != 0);
    
    // Clear validity flags for physical addresses.
    clearCachedPhysicalAddresses();
}

VirtualBlock::VirtualBlock(const VirtualBlock & other)
:   BlockAddress(other),
    m_mapper(other.m_mapper)
{
    // Copy physical block information.
    for (int i=0; i < s_planes; ++i)
    {
        m_physicalAddresses[i] = other.m_physicalAddresses[i];
    }
}

VirtualBlock::~VirtualBlock()
{
}

VirtualBlock & VirtualBlock::operator = (const VirtualBlock & other)
{
    // Copy attributes.
    m_address = other.m_address;
    m_mapper = other.m_mapper;
    
    // Copy physical block information.
    for (int i=0; i < s_planes; ++i)
    {
        m_physicalAddresses[i] = other.m_physicalAddresses[i];
    }
    
    return *this;
}

void VirtualBlock::determinePlanesToUse()
{
    if (!s_planes || !s_virtualPagesPerBlock)
    {
        // Determine planes to use.
        NandParameters_t & params = NandHal::getParameters();
        s_planes = params.planesPerDie;
        s_planeMask = s_planes - 1;
        s_virtualPagesPerBlock = s_planes * params.wPagesPerBlock;
        s_virtualPagesPerBlockMask  = s_virtualPagesPerBlock - 1;
        int iShift;
        for (iShift=0; iShift<31; iShift++)
        {
            if ( s_planes == (1<<iShift))
                break;
        }
        s_planeShift = iShift;
        s_virtualPagesPerBlockShift = s_planeShift + params.pageToBlockShift;
        
        // Make sure we can actually use the number of planes we decided on.
        assert(s_planes > 0 && s_planes <= kMaxPlanes);
    }
}

void VirtualBlock::setMapper(Mapper * theMapper)
{
    m_mapper = theMapper;
}

void VirtualBlock::set(const BlockAddress & address)
{
    BlockAddress::set(address);
    
    // Clear validity flags for physical addresses.
    clearCachedPhysicalAddresses();
}

unsigned VirtualBlock::set(DataRegion * region, uint32_t logicalSectorInRegion)
{
    uint32_t logicalBlockInRegion = logicalSectorInRegion >> s_virtualPagesPerBlockShift;
    uint32_t logicalOffset = logicalSectorInRegion & s_virtualPagesPerBlockMask;
    
    uint32_t virtualBlock = region->getStartBlock() + logicalBlockInRegion * s_planes;
    set(virtualBlock);
    
    return logicalOffset;
}

void VirtualBlock::clearCachedPhysicalAddresses()
{
    for (int i=0; i < s_planes; ++i)
    {
        PhysicalAddressInfo & info = m_physicalAddresses[i];
        info.m_address = 0;
        info.m_isCached = false;
        info.m_isUnallocated = false;
    }
}

unsigned VirtualBlock::getPlaneForVirtualOffset(unsigned offset) const
{
    assert(offset < s_virtualPagesPerBlock);
    return (offset & s_planeMask);
}

bool VirtualBlock::isFullyUnallocated()
{
    int plane;
    for (plane=0; plane < s_planes; ++plane)
    {
        // Exit early if the plane is allocated.
        if (isPlaneAllocated(plane))
        {
            return false;
        }
    }
    
    // All planes are unallocated.
    return true;
}

bool VirtualBlock::isFullyAllocated()
{
    int plane;
    for (plane=0; plane < s_planes; ++plane)
    {
        // Exit early if the plane is unallocated.
        if (!isPlaneAllocated(plane))
        {
            return false;
        }
    }
    
    // All planes are allocated.
    return true;
}

bool VirtualBlock::isPlaneAllocated(unsigned thePlane)
{
    assert(thePlane < s_planes);
    PhysicalAddressInfo & info = m_physicalAddresses[thePlane];
    
    // Make sure the physical info is cached.
    if (!info.m_isCached)
    {
        BlockAddress temp;
        getPhysicalBlockForPlane(thePlane, temp);
    }
    
    return !info.m_isUnallocated;
}

void VirtualBlock::setPlaneAllocated(unsigned thePlane, bool isAllocated)
{
    assert(thePlane < s_planes);
    PhysicalAddressInfo & info = m_physicalAddresses[thePlane];
    info.m_isUnallocated = !isAllocated;
    info.m_isCached = true;
}

RtStatus_t VirtualBlock::getPhysicalBlockForPlane(unsigned thePlane, BlockAddress & address)
{
    assert(thePlane < s_planes);
    assert(m_mapper);
    
    // Use the cached physical address if available. But if the block is unallocated then
    // we want to try looking up the physical block again, in case it has been allocated
    // since we last tried.
    if (m_physicalAddresses[thePlane].m_isCached)
    {
        if (m_physicalAddresses[thePlane].m_isUnallocated)
        {
            return ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR;
        }
        else
        {
            address = m_physicalAddresses[thePlane].m_address;
            return SUCCESS;
        }
    }
    
    // Ask the mapper to look up the physical block associated with this virtual block
    // and plane.
    uint32_t physicalAddress = 0;
    RtStatus_t status = m_mapper->getBlockInfo(m_address + thePlane, &physicalAddress);
    address = physicalAddress;
    
    // Cache the physical address.
    PhysicalAddressInfo & info = m_physicalAddresses[thePlane];
    if (status == SUCCESS)
    {
        info.m_address = physicalAddress;
        info.m_isCached = true;
        info.m_isUnallocated = m_mapper->isBlockUnallocated(physicalAddress);
        
        if (info.m_isUnallocated)
        {
            status = ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR;
        }
    }
    else
    {
        // Got an unexpected error.
        info.m_isCached = false;
    }
    
    return status;
}

void VirtualBlock::setPhysicalBlockForPlane(unsigned thePlane, const BlockAddress & address)
{
    assert(thePlane < s_planes);
    
    PhysicalAddressInfo & info = m_physicalAddresses[thePlane];
    info.m_address = address;
    info.m_isCached = true;
    info.m_isUnallocated = false;
}

RtStatus_t VirtualBlock::getPhysicalPageForVirtualOffset(uint32_t virtualOffset, PageAddress & address)
{
    unsigned thePlane = getPlaneForVirtualOffset(virtualOffset);
    BlockAddress block;
    RtStatus_t status = getPhysicalBlockForPlane(thePlane, block);
    if (status == SUCCESS)
    {
        // Convert virtual offset to physical.
        uint32_t physicalOffset = virtualOffset >> s_planeShift;
        address = PageAddress(block, physicalOffset);
    }
    
    return status;
}

RtStatus_t VirtualBlock::allocateBlockForPlane(unsigned thePlane, BlockAddress & newPhysicalBlock)
{
    assert(thePlane < s_planes);
    
    RtStatus_t status;
    
    // Set up constraints for allocating this plane. By default there are no constraints.
    Mapper::AllocationConstraints constraints;
    
    // Figure out chip and die for the first plane's block.
    PhysicalAddressInfo & vbinfo = m_physicalAddresses[kFirstPlane];
    // If allocating backup block, try to constraint it by same chip.
    
    if (vbinfo.m_isUnallocated == false)
    {
        BlockAddress & ba = vbinfo.m_address;
        NandPhysicalMedia * nand = ba.getNand();
        if (nand != NULL)
        {
            // Always constrain by chip.
            constraints.m_chip = nand->wChipNumber;
        }
    }

    // No constraints are necessary if there is only a single plane.
    if (s_planes > 1)
    {
        // All planes are constrained by plane, of course.
        constraints.m_plane = thePlane;
        
        // The first plane is otherwise unconstrained and can reside anywhere. Secondary planes must
        // reside on the chip and die containing the first plane.
        if (thePlane > kFirstPlane)
        {
            // If the first plane is not yet allocated then we need to allocate it.
            if (!isPlaneAllocated(kFirstPlane))
            {
                BlockAddress temp;
                status = allocateBlockForPlane(kFirstPlane, temp);
                if (status != SUCCESS)
                {
                    return status;
                }
            }
            
            // Figure out chip and die for the first plane's block.
            BlockAddress & firstPlaneBlock = m_physicalAddresses[kFirstPlane].m_address;
            NandPhysicalMedia * nand = firstPlaneBlock.getNand();
            
            // Always constrain by chip.
            constraints.m_chip = nand->wChipNumber;
            
            // We only have to constrain by die if the NAND does not support interleaving between
            // dice on the same chip.
            if (!NandHal::getParameters().supportsDieInterleaving)
            {
                constraints.m_die = nand->relativeBlockToDie(firstPlaneBlock.getRelativeBlock());
            }
        }
    }
    
    // Allocate a block from the mapper that matches our requirements for this plane.
    uint32_t newBlockNumber;
    status = m_mapper->getBlockAndAssign(m_address + thePlane, &newBlockNumber, kMapperBlockTypeNormal, &constraints);
    
    // If the constrained allocate failed, then try again without any constraints. Obviously,
    // this will prevent multiplane operations, but its better than failing completely.
    if (status == ERROR_DDR_NAND_MAPPER_PHYMAP_MAPFULL)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "%s: falling back to unconstrained block alloc\n", __FUNCTION__);
        
        status = m_mapper->getBlockAndAssign(m_address + thePlane, &newBlockNumber, kMapperBlockTypeNormal, NULL);
    }
    
    // Return any errors now.
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Save the block number.
    PhysicalAddressInfo & info = m_physicalAddresses[thePlane];
    info.m_address = newBlockNumber;
    info.m_isCached = true;
    info.m_isUnallocated = false;

    // Return the new block to the caller.
    newPhysicalBlock = newBlockNumber;
    
    return SUCCESS;
}

RtStatus_t VirtualBlock::allocateAllPlanes()
{
    for (int i=0; i < s_planes; ++i)
    {
        BlockAddress temp;
        RtStatus_t status = allocateBlockForPlane(i, temp);
        if (status != SUCCESS)
        {
            return status;
        }
    }
    
    return SUCCESS;
}

RtStatus_t VirtualBlock::freeAndEraseAllPlanes()
{
    int i;
    bool doAutoErase = true;
    RtStatus_t status = SUCCESS;
    
    // Try to use multiplane erase if possible.
    if (s_planes > 1 && isFullyAllocated())
    {
        NandPhysicalMedia::MultiplaneParamBlock pb[kMaxPlanes] = {0};
        NandPhysicalMedia * nand = NULL;
        
        doAutoErase = false;
        
        // Fill in param block and verify that all blocks are on the same NAND.
        for (i=0; i < s_planes; ++i)
        {
            PhysicalAddressInfo & info = m_physicalAddresses[i];
            assert(info.m_isCached && !info.m_isUnallocated);
            
            if (!nand)
            {
                nand = info.m_address.getNand();
            }
            else if (nand != info.m_address.getNand())
            {
                // The physical blocks reside on different NANDs, so we can't use multiplane.
                doAutoErase = true;
                break;
            }
            
            // Fill in the block address relative to the NAND.
            pb[i].m_address = info.m_address.getRelativeBlock();
        }
        
        if (!doAutoErase)
        {
            // Do the erase.
            status = nand->eraseMultipleBlocks(pb, s_planes);
            if (status != SUCCESS)
            {
                return status;
            }
            
            // Review erase results.
            for (i=0; i < s_planes; ++i)
            {
                PhysicalAddressInfo & info = m_physicalAddresses[i];
                
                if (pb[i].m_resultStatus == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    // The erase of this block failed, so let the mapper deal with it for us.
                    m_mapper->handleNewBadBlock(info.m_address);
                }
                else if (pb[i].m_resultStatus != SUCCESS)
                {
                    // Some unexpected error, just save the status to return below.
                    status = pb[i].m_resultStatus;
                }
                else
                {
                    // Erase succeeded, mark the block free.
                    m_mapper->getPhymap()->markBlockFree(info.m_address);
                }
            }
        }
    }
    
    if (doAutoErase)
    {
        // Mark every allocated block free in the phymap.
        for (i=0; i < s_planes; ++i)
        {
            if (isPlaneAllocated(i))
            {
                PhysicalAddressInfo & info = m_physicalAddresses[i];
                assert(info.m_isCached && !info.m_isUnallocated);
                m_mapper->getPhymap()->markBlockFreeAndErase(info.m_address);
            }
        }
    }
    
    // Clear the cached addresses.
    clearCachedPhysicalAddresses();
    
    return status;
}

uint32_t VirtualBlock::getMapperKeyFromVirtualOffset(unsigned offset)
{
    return m_address + getPlaneForVirtualOffset(offset);
}

uint32_t VirtualBlock::getVirtualBlockFromMapperKey(uint32_t mapperKey)
{
    return mapperKey & ~(s_planeMask);
}

bool VirtualBlock::isFullyAllocatedOnOneNand()
{
    // Iterate over all the planes and compare NANDs.
    NandPhysicalMedia * firstNand = NULL;
    for (int i = 0; i < s_planes; ++i)
    {
        // Make sure this plane is allocated.
        if (!isPlaneAllocated(i))
        {
            return false;
        }
        
        PhysicalAddressInfo & info = m_physicalAddresses[i];
        assert(info.m_isCached && !info.m_isUnallocated);
        
        // Save the NAND from the first block.
        if (!firstNand)
        {
            firstNand = info.m_address.getNand();
        }
        // Compare the blocks for other planes with the first NAND.
        else if (firstNand != info.m_address.getNand())
        {
            // Different NAND!
            return false;
        }
    }
    
    // All planes are allocated to the same NAND.
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
