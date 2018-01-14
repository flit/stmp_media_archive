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
#include "drivers/media/ddi_media_errordefs.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "drivers/media/nand/ddi/systemDrive/ddi_nand_system_drive.h"
#include "drivers/media/nand/ddi/common/ddi_nand_ddi.h"
#include "drivers/media/nand/ddi/common/DeferredTask.h"
#include "drivers/media/nand/ddi/media/ddi_nand_media.h"
#include "drivers/media/nand/ddi/media/Region.h"
#include "drivers/media/nand/ddi/mapper/BlockAllocator.h"
#include "drivers/media/nand/ddi/mapper/Mapper.h"
#include "drivers/media/nand/ddi/mapper/PhyMap.h"
#include "drivers/media/nand/ddi/dataDrive/ddi_nand_data_drive.h"
#include "drivers/media/nand/ddi/dataDrive/VirtualBlock.h"
#include "drivers/media/nand/ddi/dataDrive/NonsequentialSectorsMap.h"
#include "drivers/media/nand/ddi/dataDrive/NssmManager.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \brief Special error codes for this test.
enum _test_errors
{
    kBlockOutOfRangeError = 0x10000001,
    kBlockWrongPlaneError = 0x10000002,
    kBlockNotAllocatedError = 0x10000003
};

/*!
 * @brief Unit test for various components of the data drive.
 */
class NssmTest
{
public:
    NssmTest();
    ~NssmTest() {}
    
    RtStatus_t init();
    RtStatus_t erase_drive();
    RtStatus_t run();
    
    RtStatus_t test_virtual_block();
    RtStatus_t test_nssm();

protected:
    NandParameters_t & m_params;
    nand::Media * m_media;
    nand::Mapper * m_mapper;
    nand::PhyMap * m_phymap;
    nand::DataDrive * m_drive;
    nand::DataRegion * m_firstDataRegion;
    uint32_t m_sectorCount;
};

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

RtStatus_t run_test();

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

NssmTest::NssmTest()
:   m_params(NandHal::getParameters()),
    m_media(0),
    m_mapper(0),
    m_phymap(0),
    m_drive(0),
    m_firstDataRegion(0),
    m_sectorCount(0)
{
}

RtStatus_t NssmTest::init()
{
    // Get the NAND media object and related objects.
    m_media = static_cast<nand::Media *>(MediaGetMediaFromIndex(kInternalMedia));
    assert(m_media);
    m_mapper = m_media->getMapper();
    assert(m_mapper);
    m_phymap = m_mapper->getPhymap();
    assert(m_phymap);
    
    // Get the data drive object.
    m_drive = static_cast<nand::DataDrive *>(DriveGetDriveFromTag(DRIVE_TAG_DATA));
    if (!m_drive)
    {
        FASTPRINT("No data drive!\n");
        return ERROR_GENERIC;
    }
    
    // Find the first data region.
    nand::Region * region;
    nand::Region::Iterator it = m_media->createRegionIterator();
    while ((region = it.getNext()))
    {
        if (region->isDataRegion())
        {
            m_firstDataRegion = static_cast<nand::DataRegion *>(region);
            break;
        }
    }
    if (!m_firstDataRegion)
    {
        FASTPRINT("No data region\n");
        return ERROR_GENERIC;
    }
    
    // Get some properties of the data drive.
    m_drive->getInfo(kDriveInfoNativeSectorSizeInBytes, &g_actualBufferBytes);
    m_drive->getInfo(kDriveInfoSizeInNativeSectors, &m_sectorCount);
    
    return SUCCESS;
}

RtStatus_t NssmTest::erase_drive()
{
    RtStatus_t status;
    
    // Erase the drive.
    FASTPRINT("Erasing drive...\n");
    status = m_drive->erase();
    if (status != SUCCESS)
    {
        FASTPRINT("Failed to erase drive: 0x%08x (%s, line %d)\n", status, __PRETTY_FUNCTION__, __LINE__);
        return status;
    }
    FASTPRINT("done erasing\n");
    
    return SUCCESS;
}

RtStatus_t NssmTest::run()
{
    RtStatus_t status = SUCCESS;
    
    // Erase drive before first test.
    status = erase_drive();
    if (status != SUCCESS)
    {
        return status;
    }
    
    status = test_virtual_block();
    if (status != SUCCESS)
    {
        FASTPRINT("test_virtual_block failed: 0x%08x\n", status);
        return status;
    }
    
    // Erase drive again.
    status = erase_drive();
    if (status != SUCCESS)
    {
        return status;
    }
    
    status = test_nssm();
    if (status != SUCCESS)
    {
        FASTPRINT("test_nssm failed: 0x%08x\n", status);
        return status;
    }
    
    return status;
}

RtStatus_t NssmTest::test_virtual_block()
{
    FASTPRINT("Testing VirtualBlock...\n");
    
    nand::BlockAddress firstVirtualBlock = m_firstDataRegion->getStartBlock();
    nand::BlockAddress tempAddress;
    nand::BlockAddress tempAddress2;
    nand::PageAddress tempPage;
    
    nand::VirtualBlock vblock(m_mapper);
    nand::VirtualBlock vblock2(m_mapper);
    unsigned planeCount = VirtualBlock::getPlaneCount();
    unsigned pagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    FASTPRINT("Planes = %u, virtual ppb = %u, first virtual block = %u\n", planeCount, pagesPerBlock, firstVirtualBlock.get());
    
    // Set address to first virtual block.
    vblock = firstVirtualBlock;
    
    // Test basic stuff.
    REQ_RESULT(vblock.getPlaneForVirtualOffset(0), 0);
    if (planeCount > 1)
    {
        REQ_RESULT(vblock.getPlaneForVirtualOffset(1), 1);
    }
    
    // The drive is erased, so no blocks should be allocated yet.
    REQ_TRUE(vblock.isFullyUnallocated());
    REQ_FALSE(vblock.isFullyAllocated());
    vblock.clearCachedPhysicalAddresses();
    
    REQ_STATUS(vblock.getPhysicalBlockForPlane(0, tempAddress), ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR);
    
    // Allocate planes and verify addresses.
    REQ_SUCCESS(vblock.allocateAllPlanes());
    REQ_TRUE(vblock.isFullyAllocated());
    REQ_FALSE(vblock.isFullyUnallocated());

    REQ_SUCCESS(vblock.getPhysicalBlockForPlane(0, tempAddress));
    
    REQ_SUCCESS(vblock.getPhysicalPageForVirtualOffset(0, tempPage));
    REQ_TRUE(tempPage.get() == (tempAddress.get() * m_params.wPagesPerBlock));
    
    // Clear cached addresses and run test again.
    vblock.clearCachedPhysicalAddresses();
 
    REQ_TRUE(vblock.isFullyAllocated());
    REQ_FALSE(vblock.isFullyUnallocated());

    REQ_SUCCESS(vblock.getPhysicalBlockForPlane(0, tempAddress));
    
    REQ_SUCCESS(vblock.getPhysicalPageForVirtualOffset(0, tempPage));
    REQ_TRUE(tempPage.get() == (tempAddress.get() * m_params.wPagesPerBlock));
    
    REQ_TRUE(vblock.getMapperKeyFromVirtualOffset(0) == vblock);
    
    // Free and erase everything.
    REQ_SUCCESS(vblock.freeAndEraseAllPlanes());
    REQ_TRUE(vblock.isFullyUnallocated());
    REQ_FALSE(vblock.isFullyAllocated());
    
    // Allocate again.
    REQ_SUCCESS(vblock.allocateBlockForPlane(0, tempAddress));
    REQ_TRUE(vblock.isPlaneAllocated(0));
    
    if (planeCount > 1)
    {
        REQ_FALSE(vblock.isFullyUnallocated());
        REQ_FALSE(vblock.isFullyAllocated());
        REQ_FALSE(vblock.isPlaneAllocated(1));
    }
    
    REQ_SUCCESS(vblock.getPhysicalBlockForPlane(0, tempAddress2));
    REQ_TRUE(tempAddress == tempAddress2);
    
    vblock.clearCachedPhysicalAddresses();
    REQ_SUCCESS(vblock.getPhysicalBlockForPlane(0, tempAddress2));
    REQ_TRUE(tempAddress == tempAddress2);

    REQ_SUCCESS(vblock.getPhysicalPageForVirtualOffset(0, tempPage));
    REQ_TRUE(tempPage.get() == (tempAddress2.get() * m_params.wPagesPerBlock));
    // Reallocate all planes.
    REQ_SUCCESS(vblock.freeAndEraseAllPlanes());
    REQ_SUCCESS(vblock.allocateAllPlanes());
    REQ_TRUE(vblock.isFullyAllocated());
    REQ_FALSE(vblock.isFullyUnallocated());
    REQ_SUCCESS(vblock.getPhysicalBlockForPlane(0, tempAddress));
    
    // Copy phy blocks.
    vblock2 = vblock;
    
    // Free and erase everything in first vblock.
    REQ_SUCCESS(vblock.freeAndEraseAllPlanes());
    REQ_TRUE(vblock.isFullyUnallocated());
    REQ_FALSE(vblock.isFullyAllocated());
    
    // Make sure second vblock still has original addresses.
    REQ_SUCCESS(vblock2.getPhysicalBlockForPlane(0, tempAddress2));
    REQ_TRUE(tempAddress == tempAddress2);
    
    REQ_SUCCESS(vblock.allocateAllPlanes());
    REQ_TRUE(vblock.isFullyAllocated());
    REQ_FALSE(vblock.isFullyUnallocated());

    REQ_SUCCESS(vblock2.getPhysicalBlockForPlane(0, tempAddress2));
    REQ_TRUE(tempAddress == tempAddress2);
   
    return SUCCESS;
}

RtStatus_t NssmTest::test_nssm()
{
    FASTPRINT("Testing NonsequentialSectorsMap...\n");
    int i;
    bool isOccupied;
    nand::PageAddress pageAddress;
    uint32_t virtualOffset;
    RtStatus_t status;
    
    nand::Page page;
    page.allocateBuffers(true, true);
    
    nand::BlockAddress firstVirtualBlock = m_firstDataRegion->getStartBlock();
    nand::VirtualBlock vblock(m_mapper);
    unsigned planeCount = VirtualBlock::getPlaneCount();
    unsigned pagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    
    // Create our map.
    nand::NonsequentialSectorsMap map;
    map.init(m_media->getNssmManager());
    nand::VirtualBlock & mapVblock = map.getVirtualBlock();
    
    REQ_SUCCESS(map.prepareForBlock(firstVirtualBlock));
    
    REQ_TRUE(map.isValid());
    REQ_FALSE(map.hasBackup());
    
    // Make sure all logical offsets are unoccupied.
    for (i=0; i < pagesPerBlock; ++i)
    {
        map.getEntry(i, &virtualOffset, &isOccupied);
        REQ_FALSE(isOccupied);
    }
    
    // Now write some pages in sequential order.
    for (i=0; i < pagesPerBlock - 1; ++i)
    {
        REQ_SUCCESS(map.getNextOffset(i, &virtualOffset));
        REQ_RESULT(virtualOffset, i);
        
        status = mapVblock.getPhysicalPageForVirtualOffset(virtualOffset, pageAddress);
        if (i < planeCount)
        {
            // Should have to alloc block the first time each plane.
            REQ_STATUS(status, ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR);
        }
        else
        {
            REQ_SUCCESS(status);
        }
        if (status == ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR)
        {
            nand::BlockAddress newBlock;
            unsigned thePlane = mapVblock.getPlaneForVirtualOffset(virtualOffset);
            REQ_SUCCESS(mapVblock.allocateBlockForPlane(thePlane, newBlock));
    
            // Get the physical page address again. There should be no error this time.
            REQ_SUCCESS(mapVblock.getPhysicalPageForVirtualOffset(virtualOffset, pageAddress));
        }
        
        // Prepare a unique page buffer.
        fill_data_buffer(page.getPageBuffer(), pageAddress);
        
        // Prepare the aux buffer.
        nand::Metadata & md = page.getMetadata();
        md.prepare(mapVblock.getMapperKeyFromVirtualOffset(virtualOffset), i);
        
        // Write the page.
        REQ_SUCCESS(page.write());
        
        // Update map.
        map.addEntry(i, virtualOffset);
    }
    
    // Go back and make sure everything is as expected.
    for (i=0; i < pagesPerBlock - 1; ++i)
    {
        map.getEntry(i, &virtualOffset, &isOccupied);
        REQ_TRUE(isOccupied);
        REQ_RESULT(virtualOffset, i);
    }
    
    // Reinit the map, forcing it to rebuild from metadata.
    REQ_SUCCESS(map.prepareForBlock(firstVirtualBlock));
    REQ_TRUE(map.isValid());
    REQ_FALSE(map.hasBackup());
    
    // Make sure everything is as expected after rebuilding from metadata.
    for (i=0; i < pagesPerBlock - 1; ++i)
    {
        map.getEntry(i, &virtualOffset, &isOccupied);
        REQ_TRUE(isOccupied);
        REQ_RESULT(virtualOffset, i);
    }
    
    
    return SUCCESS;
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
    
    NssmTest theTest;
    status = theTest.init();
    if (status != SUCCESS)
    {
        FASTPRINT("test init returned 0x%08x\n", status);
        return status;
    }
    status = theTest.run();
    if (status != SUCCESS)
    {
        FASTPRINT("test run returned 0x%08x\n", status);
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

