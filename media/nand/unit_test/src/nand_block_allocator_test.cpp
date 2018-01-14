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
#include "drivers/media/common/media_unit_test_helpers.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "drivers/media/nand/ddi/systemDrive/ddi_nand_system_drive.h"
#include "drivers/media/nand/ddi/common/ddi_nand_ddi.h"
#include "drivers/media/nand/ddi/common/DeferredTask.h"
#include "drivers/media/nand/ddi/media/ddi_nand_media.h"
#include "drivers/media/nand/ddi/mapper/BlockAllocator.h"
#include "drivers/media/nand/ddi/mapper/Mapper.h"
#include "drivers/media/nand/ddi/mapper/PhyMap.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \brief Constants controlling the allocation test.
enum _alloc_constants
{
    //! Number of times to allocate blocks per configuration.
    kAllocIterations = 10000,
    
    //! How many allocations each '.' character represents on the printout.
    kAllocationsPerDot = 100,
    
    //! Maximum number of allocations for which the actual block number will be printed.
    kMaxBlockNumberPrintCutoff = 20,
    
    //! Whether to mark the allocated blocks as used in the phymap.
    kMarkAllocatedBlocksUsed = false
};

//! \brief Special error codes for this test.
enum _test_errors
{
    kBlockOutOfRangeError = 0x10000001,
    kBlockWrongPlaneError = 0x10000002,
    kBlockNotAllocatedError = 0x10000003
};

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

RtStatus_t test_alloc(nand::BlockAllocator & alloc, const char * msg);
RtStatus_t test_constraints(nand::BlockAllocator & alloc);
RtStatus_t test_core();
RtStatus_t run_test();

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

RtStatus_t test_alloc(nand::BlockAllocator & alloc, const char * msg)
{
    uint32_t block;
    bool result;
    int i;
    char buf[1024] = {0};
    char tmp[64];
    RtStatus_t status = SUCCESS;
    NandParameters_t & params = NandHal::getParameters();
    
    if (msg)
    {
        snprintf(buf, sizeof(buf), "%s: ", msg);
    }
    
    // Figure out the valid block range.
    const nand::BlockAllocator::Constraints & c = alloc.getConstraints();
    NandPhysicalMedia * nand;
    uint32_t startBlock = 0;
    uint32_t endBlock = NandHal::getTotalBlockCount();
    
    if (c.m_chip != nand::BlockAllocator::Constraints::kUnconstrained)
    {
        nand = NandHal::getNand(c.m_chip);
        startBlock = nand->baseAbsoluteBlock();
        endBlock = startBlock + nand->wTotalBlocks;

        if (c.m_die != nand::BlockAllocator::Constraints::kUnconstrained)
        {
            startBlock += nand->wBlocksPerDie * c.m_die;
            endBlock = startBlock + nand->wBlocksPerDie;
        }
    }
    
    for (i=0; i < kAllocIterations; ++i)
    {
        result = alloc.allocateBlock(block);
        
        if (result)
        {
            // Validate the result block range.
            if (!(block >= startBlock && block < endBlock))
            {
                snprintf(tmp, sizeof(tmp), "%u(out of range) ", block);
                strcat(buf, tmp);
                
                status = kBlockOutOfRangeError;
                break;
            }
            
            // Validate the result block plane.
            if (c.m_plane != nand::BlockAllocator::Constraints::kUnconstrained)
            {
                unsigned plane = block & (params.planesPerDie - 1);
                if (plane != c.m_plane)
                {
                    snprintf(tmp, sizeof(tmp), "%u(wrong plane) ", block);
                    strcat(buf, tmp);
                    
                    status = kBlockWrongPlaneError;
                    break;
                }
            }
            
            // Print something.
            if (kAllocIterations <= kMaxBlockNumberPrintCutoff)
            {
                snprintf(tmp, sizeof(tmp), "%u ", block);
                strcat(buf, tmp);
            }
            else if (i % kAllocationsPerDot == 0)
            {
                strcat(buf, ".");
            }
            
            // Mark block taken.
            if (kMarkAllocatedBlocksUsed)
            {
                alloc.getPhyMap()->markBlockUsed(block);
            }
        }
        else
        {
            strcat(buf, "fail ");
            
            status = kBlockNotAllocatedError;
            break;
        }
    }
    
    FASTPRINT("%s\n", buf);
    
    return status;
}

RtStatus_t test_constraints(nand::BlockAllocator & alloc)
{
    nand::BlockAllocator::Constraints c;
    int chip;
    int die;
    int plane;
    NandParameters_t & params = NandHal::getParameters();
    char buf[64];
    RtStatus_t status;
    
    // Unconstrained.
    alloc.clearConstraints();
    status = test_alloc(alloc, "unconstrained");
    if (status)
    {
        return status;
    }
    
    for (chip=0; chip < NandHal::getChipSelectCount(); ++chip)
    {
        NandPhysicalMedia * nand = NandHal::getNand(chip);
        
        // Constrained by chip only.
        c.m_chip = chip;
        c.m_die = nand::BlockAllocator::Constraints::kUnconstrained;
        c.m_plane = nand::BlockAllocator::Constraints::kUnconstrained;
        alloc.setConstraints(c);
        snprintf(buf, sizeof(buf), "(chip=%d)", chip);
        
        status = test_alloc(alloc, buf);
        if (status)
        {
            return status;
        }
        
        for (plane=0; plane < params.planesPerDie; ++plane)
        {
            // Constrained by chip and plane.
            c.m_chip = chip;
            c.m_die = nand::BlockAllocator::Constraints::kUnconstrained;
            c.m_plane = plane;
            alloc.setConstraints(c);
            snprintf(buf, sizeof(buf), "(chip=%d, plane=%d)", chip, plane);
            
            status = test_alloc(alloc, buf);
            if (status)
            {
                return status;
            }
        }
        
        for (die=0; die < nand->wTotalInternalDice; ++die)
        {
            // Constrained by chip and die.
            c.m_chip = chip;
            c.m_die = die;
            c.m_plane = nand::BlockAllocator::Constraints::kUnconstrained;
            alloc.setConstraints(c);
            snprintf(buf, sizeof(buf), "(chip=%d, die=%d)", chip, die);
            
            status = test_alloc(alloc, buf);
            if (status)
            {
                return status;
            }
            
            for (plane=0; plane < params.planesPerDie; ++plane)
            {
                // Constrained by chip, die, and plane.
                c.m_chip = chip;
                c.m_die = die;
                c.m_plane = plane;
                alloc.setConstraints(c);
                snprintf(buf, sizeof(buf), "(chip=%d, die=%d, plane=%d)", chip, die, plane);
                
                status = test_alloc(alloc, buf);
                if (status)
                {
                    return status;
                }
            }
        }
    }
    
    return SUCCESS;
}
    
RtStatus_t test_core()
{
    RtStatus_t status = SUCCESS;
    nand::Media * media = static_cast<nand::Media *>(MediaGetMediaFromIndex(kInternalMedia));
    assert(media);
    nand::Mapper * mapper = media->getMapper();
    assert(mapper);
    nand::PhyMap * realPhymap = mapper->getPhymap();
    assert(realPhymap);
    
    // Make a copy of the real phymap so we can mess with it.
    auto_delete<nand::PhyMap> phymap = new nand::PhyMap;
    assert(phymap);
    status = phymap->init(realPhymap->getBlockCount());
    if (status)
    {
        FASTPRINT("Failed to init phymap 0x%08x\n", status);
        return status;
    }
    memcpy(phymap->getAllEntries(), realPhymap->getAllEntries(), nand::PhyMap::kEntrySizeInBytes * phymap->getEntryCount());
    
    FASTPRINT(">>>Random<<<\n");
    nand::RandomBlockAllocator random(phymap);
    random.setRange(0, phymap->getBlockCount() - 1);
    status = test_constraints(random);
    
    if (status == SUCCESS)
    {
        FASTPRINT(">>>Linear<<<\n");
        nand::LinearBlockAllocator linear(phymap);
        linear.setRange(0, phymap->getBlockCount() - 1);
        status = test_constraints(linear);
    }
    
    return status;
}

RtStatus_t run_test()
{
    RtStatus_t status;
    
    status = MediaInit(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media init returned 0x%08x\n", status);
        return status;
    }
    
    status = MediaDiscoverAllocation(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media discover returned 0x%08x\n", status);
        return status;
    }
    
    // We need the data drive inited so the mapper is available.
    status = DriveInit(DRIVE_TAG_DATA);
    if (status != SUCCESS)
    {
        FASTPRINT("Initing primary system drive returned 0x%08x\n", status);
        return status;
    }
    
    status = test_core();
    if (status != SUCCESS)
    {
//         FASTPRINT("Media discover returned 0x%08x\n", status);
        return status;
    }
    
    status = MediaShutdown(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media shutdown returned 0x%08x\n", status);
        return status;
    }
    
    tss_logtext_Flush(TX_WAIT_FOREVER);
    
    return SUCCESS;
}

RtStatus_t test_main(ULONG param)
{
    RtStatus_t status;
    
    // Initialize the Media
    status = SDKInitialization();

    if (status == SUCCESS)
    {
        status = run_test();
    }
    
    if (status == SUCCESS)
    {
        FASTPRINT("unit test passed!\n");
    }
    else
    {
        FASTPRINT("unit test failed: 0x%08x\n", status);
    }
    
    exit(status);
    return status;
}

