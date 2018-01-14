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
//! \addtogroup ddi_nand_media
//! @{
//! \file DiscoveredBadBlockTable.cpp
//! \brief This file contains the implementation of the DBBT class.
////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include "DiscoveredBadBlockTable.h"
#include "Region.h"
#include "ddi_nand_media.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#ifdef ALLOW_BB_TABLE_READ_SKIP
uint32_t s_skipTableOnNAND = 0;
#endif

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark --DiscoveredBadBlockTable--
#endif

DiscoveredBadBlockTable::DiscoveredBadBlockTable(Media * nandMedia)
:   m_media(nandMedia),
    m_sectorBuffer(),
    m_auxBuffer()
{
    // Clear the layout info.
    memset(&m_layout, 0, sizeof(m_layout));
}

void DiscoveredBadBlockTable::setBuffers(SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer)
{
    // Retain the passed in buffers.
    m_sectorBuffer = sectorBuffer;
    m_auxBuffer = auxBuffer;
}

RtStatus_t DiscoveredBadBlockTable::allocateBuffers()
{
    RtStatus_t retCode = SUCCESS;
    
    if (!m_sectorBuffer.hasBuffer())
    {
        retCode = m_sectorBuffer.acquire();
        if (retCode != SUCCESS)
        {
            return retCode;
        }
    }

    if (!m_auxBuffer.hasBuffer())
    {
        retCode = m_auxBuffer.acquire();
        if (retCode != SUCCESS)
        {
            return retCode;
        }
    }
    
    return retCode;
}

void DiscoveredBadBlockTable::fillDbbtPageForChip(uint32_t iChip)
{
    BadBlockTableNand_t * pBadBlockTableForNand = (BadBlockTableNand_t *)m_sectorBuffer.getBuffer();

    // Set the buffer to erased state.
    m_sectorBuffer.fill(0xff);

    // Construct the DBBT structure in RAM for this chip.
    pBadBlockTableForNand->uNAND = iChip;
    pBadBlockTableForNand->uNumberBB = 0;

    uint32_t u32BadBlockOffset = 0;

    // Start filling in the Bad Block Table (for saving on the NAND).
    Region * region;
    Region::Iterator it = m_media->createRegionIterator();
    while ((region = it.getNext()))
    {
        // If this region doesn't reference the NAND we're interested in, continue.
        // Also skip this region if it doesn't have a bad block table.
        if (region->m_iChip != iChip || !region->usesBadBlockTable())
        {
            continue;
        }

        //! \todo Instead of just cutting off the bad block entries when the
        //! count hits the limit for one page, we need to write additional
        //! pages for the given NAND chip.
        BadBlockTable & table = *(region->getBadBlocks());
        int iCounter;
        int maxCount = std::min<int>(table.getCount(), NAND_DBBT_ENTRIES_PER_PAGE);
        for (iCounter = 0 ; iCounter < maxCount; iCounter++)
        {
            pBadBlockTableForNand->u32BadBlock[u32BadBlockOffset++] = table[iCounter].getRelativeBlock();
            pBadBlockTableForNand->uNumberBB++;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Write the Bad Block Table for each chip to the DBBT on the NAND.
//!
//! This function saves the Bad Block Table structure for all chips into the appropriate
//! pages on NAND.  The Bad Block Table structure is created in RAM by scanning
//! each region and if the region corresponds to the start and end pages
//! and the region is located on the chip, the Bad Block is added to the
//! structure in RAM.  The structure is then written from RAM to the NAND.
//!
//! \retval SUCCESS
//!
//! \note  The block descriptor is prepared using the global BBTable and
//!         scanning each region and filling in the corresponding Bad Block
//!         table in NAND.  The bad block tables will probably not be in
//!         sorted order.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DiscoveredBadBlockTable::writeChipsBBTable(BlockAddress & tableAddress)
{
    uint32_t u32PageOffset;
    int iChip;
    RtStatus_t retCode;

    // Make sure the bad block table is in the right mode.
    assert(m_media->getBadBlockTableMode() == kNandBadBlockTableDiscoveryMode);

    for (iChip = 0; iChip < NandHal::getChipSelectCount(); iChip++)
    {
        // Fill in the actual bad blocks for this chip from the region information.
        fillDbbtPageForChip(iChip);

        // The DBBT consists of different pages for different chips.
        // Compute the appropriate page for this chip.
        u32PageOffset = getDbbtPageOffset(iChip, kDBBT);
        
        // Write DBBT information BadBlockTableNand_t to the appropriate page on Chip 0.
        BootPage page(PageAddress(tableAddress, u32PageOffset));
        page.setBuffers(m_sectorBuffer, m_auxBuffer);
        page.getMetadata().prepare(0, 0);
        
        retCode = page.writeAndMarkOnFailure();
        if (retCode != SUCCESS)
        {
            // BootPage will mark the block bad, but we need to add the bad block to its region.
            if (retCode == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                Region * region = m_media->getRegionForBlock(tableAddress);
                if (region)
                {
                    region->addNewBadBlock(tableAddress);
                }
            }
            
            break;
        }
    }

    return retCode;
}

RtStatus_t DiscoveredBadBlockTable::writeBbrc(BlockAddress & tableAddress)
{
    uint32_t u32PageOffset;

    // Alias sectorBuffer as "pBootBlockStruct", for convenience.
    BootBlockStruct_t *pBootBlockStruct = (BootBlockStruct_t *)m_sectorBuffer.getBuffer();

    // Set the buffer to erased state.
    m_sectorBuffer.fill(0xff);

    // Add in the fingerprints.
    pBootBlockStruct->m_u32FingerPrint1 = BBRC_FINGERPRINT1;
    pBootBlockStruct->m_u32FingerPrint2 = BBRC_FINGERPRINT2;
    pBootBlockStruct->m_u32FingerPrint3 = BBRC_FINGERPRINT3;
    pBootBlockStruct->FirmwareBlock.m_u16Major = NCB_FIRMWAREBLOCK_VERSION_MAJOR;
    pBootBlockStruct->FirmwareBlock.m_u16Minor = NCB_FIRMWAREBLOCK_VERSION_MINOR;

    // Fill-in the bad-block counts in the stucture in RAM.
    Region * pNandRegionInfo;
    Region::Iterator it = m_media->createRegionIterator();
    int iRegion = 0;
    while ((pNandRegionInfo = it.getNext()))
    {
        pBootBlockStruct->FirmwareBlock.BadBlocksPerRegionCounts.u32NumBadBlksInRegion[iRegion] = pNandRegionInfo->getBadBlockCount();
        iRegion++;
    }
    
    pBootBlockStruct->FirmwareBlock.BadBlocksPerRegionCounts.u32Entries = iRegion;

    // Compute the appropriate page for the BBRC.
    u32PageOffset = getDbbtPageOffset(0 /*don't-care*/, DiscoveredBadBlockTable::kBBRC);

    // Write BBRC information BadBlocksPerRegionCounts_t to the appropriate page on Chip 0.
    BootPage page(PageAddress(tableAddress, u32PageOffset));
    page.setBuffers(m_sectorBuffer, m_auxBuffer);
    page.getMetadata().prepare(0, 0);
    
    RtStatus_t status = page.writeAndMarkOnFailure();
    if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
    {
        Region * region = m_media->getRegionForBlock(tableAddress);
        if (region)
        {
            region->addNewBadBlock(tableAddress);
        }
    }
        
    return status;
}

void DiscoveredBadBlockTable::fillInLayout()
{
    // Clear the layout.  Safer to do this than leave at 0xFFFFFFF
    memset(&m_layout, 0, sizeof(m_layout));

    // Now save the actual Bad Block information in the NAND.
    // Run through all the regions and keep track of how many blocks belong on each NAND.
    Region * region;
    Region::Iterator it = m_media->createRegionIterator();
    while ((region = it.getNext()))
    {
        m_layout.m_u32NumberBB_NAND[region->getChip()] += region->getBadBlockCount();
    }

    int i;
    for (i=0; i < MAX_NAND_DEVICES; ++i)
    {
        // We allocate NAND_MAX_DBBT_PAGES_PER_NAND many pages for a DBBT for each NAND.
        // A future option would be to make this quantity adaptable beyond one page,
        // according to the quantity of bad-blocks found.
        m_layout.m_u32Number2KPagesBB_NAND[i] = NAND_MAX_DBBT_PAGES_PER_NAND;

        // Bounds check the number of Bad Blocks and pages.
        m_layout.m_u32NumberBB_NAND[i] = std::min<uint32_t>(m_layout.m_u32NumberBB_NAND[i], NAND_DBBT_ENTRIES_PER_PAGE*NAND_MAX_DBBT_PAGES_PER_NAND);
    }
}

RtStatus_t DiscoveredBadBlockTable::writeOneBadBlockTable(BootBlockLocation_t & tableLocation)
{
    RtStatus_t retCode;
    
    // Loop as long as we get write/erase failures.
    do {
        // First Find a valid block within the DBBT1 reserved blocks.
        uint32_t relativeBlock = tableLocation.b.bfBlockAddress;
        retCode = m_media->findFirstGoodBlock(
            tableLocation.b.bfNANDNumber,
            &relativeBlock,
            m_media->getBootBlockSearchWindowInBlocks(),
            m_auxBuffer,
            kEraseFoundBlock);

        // Check to see if we found a valid block. If this fails, then there are no more
        // good blocks in the search area, so there is no point in trying again.
        if (retCode != SUCCESS)
        {
            return ERROR_DDI_NAND_CANT_ALLOCATE_DBBT_BLOCK;
        }
        
        // Record the DBBT address.
        BlockAddress tableAddress = BlockAddress(tableLocation.b.bfNANDNumber, relativeBlock);

        // First clear the page buffer.
        m_sectorBuffer.fill(0xff);

        // Format the layout page content, which includes the number of bad blocks on each NAND.
        fillInLayout();

        // Save the local copy into the page buffer, and add in the fingerprints.
        BootBlockStruct_t * pDBBT = (BootBlockStruct_t *)m_sectorBuffer.getBuffer();
        pDBBT->zDBBT1 = m_layout;
        pDBBT->m_u32FingerPrint1 = DBBT_FINGERPRINT1;
        pDBBT->m_u32FingerPrint2 = DBBT_FINGERPRINT2;
        pDBBT->m_u32FingerPrint3 = DBBT_FINGERPRINT3;

        // Write out the DBBT layout copies.
        BootPage page(tableAddress);
        page.setBuffers(m_sectorBuffer, m_auxBuffer);
        page.getMetadata().prepare(0, 0);

        // Write DBBT layout to first page of DBBT on Chip 0.
        // ddi_nand_media_WriteBootBlockPage() will mark the block bad if
        // there was an error.
        retCode = page.writeAndMarkOnFailure();
        if (retCode != SUCCESS)
        {
            if (retCode == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                Region * region = m_media->getRegionForBlock(tableAddress);
                if (region)
                {
                    region->addNewBadBlock(tableAddress);
                }
            }
            
            continue;
        }
        
        // Write empty pages between the layout page and the first bad block list page.
        retCode = writeEmptyPages(tableAddress, 1, getDbbtPageOffset(0, kDBBT));
        if (retCode != SUCCESS)
        {
            // If this write failed then set the flag and exit the for loop.
            continue;
        }

        // Save the Bad Block information for all chips into the NAND.
        // This function surveys pNANDMediaInfo and related structures to find
        // the needed information, and writes it to the NAND.
        retCode = writeChipsBBTable(tableAddress);
        if (retCode != SUCCESS)
        {
            continue;
        }

        // Write empty pages between the last active chip bad block page and the BBRC page.
        // This becomes a no-op unless there are fewer than the maximum number of chip selects.
        int offset = getDbbtPageOffset(NandHal::getChipSelectCount(), kDBBT);
        int endOffset = getDbbtPageOffset(0, kBBRC);
        retCode = writeEmptyPages(tableAddress, offset, endOffset);
        if (retCode != SUCCESS)
        {
            continue;
        }

        // Save the Bad Block counts for all regions into the NAND.
        retCode = writeBbrc(tableAddress);
        
        // If we got an erase/write failure then start over until we get it right. Other
        // errors that cannot be worked around cause the loop to exit immediately.
    } while (retCode == ERROR_DDI_NAND_HAL_WRITE_FAILED);
    
    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Write the Bad Block Table to the NAND.
//!
//! This function prepares the Discovered Bad Block Table for saving onto the first
//! chip by filling in the appropriate fields containing region allocation
//! information.  The DBBT is then written into the appropriate Block following
//! the Config Block.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_CANT_ALLOCATE_DBBT_BLOCK
//!
//! \note  The block descriptor is prepared using the global BBTable and
//!         scanning each region and filling in the corresponding Bad Block
//!         table in NAND.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DiscoveredBadBlockTable::writeBadBlockTables()
{
    RtStatus_t retCode;
    
    // Make sure we have valid buffers.
    retCode = allocateBuffers();
    if (retCode != SUCCESS)
    {
        return retCode;
    }

    BootBlocks & bootBlocks = m_media->getBootBlocks();
        
    // Write DBBT1.
    RtStatus_t dbbt1Status = writeOneBadBlockTable(bootBlocks.m_dbbt1);
    
    // Write DBBT2.
    RtStatus_t dbbt2Status = writeOneBadBlockTable(bootBlocks.m_dbbt2);
    
    // Return an error if either of the DBBT copies could not be written.
    if (dbbt1Status != SUCCESS || dbbt2Status != SUCCESS)
    {
        return ERROR_DDI_NAND_CANT_ALLOCATE_DBBT_BLOCK;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! This method is intended to help meet the requirement that NAND page writes
//! always be sequential within a given block.
//!
//! \param tableAddress Address of the block to write to.
//! \param startOffset The index of the first page within the block to write to.
//! \param endOffset The page index within the block \em after the last empty
//!     page to write. So if you only want to write a single empty page, then
//!     pass \a startOffset plus 1 for this argument.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DiscoveredBadBlockTable::writeEmptyPages(BlockAddress & tableAddress, int startOffset, int endOffset)
{
    RtStatus_t status = SUCCESS;
    
    if (endOffset > startOffset)
    {
        // Clear the two buffers.
        m_sectorBuffer.fill(0xff);
        m_auxBuffer.fill(0xff);
        
        // Setup the page object.
        Page emptyPage(PageAddress(tableAddress, startOffset));
        emptyPage.setBuffers(m_sectorBuffer, m_auxBuffer);

        // Write to each page in the given range.
        int offset;
        for (offset = startOffset; offset < endOffset; ++offset, ++emptyPage)
        {
            // If we get an error from the NAND, go ahead and mark the block bad.
            status = emptyPage.writeAndMarkOnFailure();
            if (status != SUCCESS)
            {
                if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    Region * region = m_media->getRegionForBlock(tableAddress);
                    if (region)
                    {
                        region->addNewBadBlock(tableAddress);
                    }
                }
                
                break;
            }
        }
    }
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Scan the first NAND for a valid Discovered Bad Block Table.
//!
//! This function will scan the NAND for a saved Discovered Bad Block Table
//! and return the resulting block number.
//!
//! \param[in] u32NAND Chip number on which to to look for the DBBT.
//! \param[in,out]  pu32DBBTPhysBlockAdd    Address to save resulting DBBT block
//!     location. Also, on input this is the initial block at which to look for
//!     the DBBT.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND No DBBT was found in
//!     the search area.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DiscoveredBadBlockTable::scan(uint32_t u32NAND, uint32_t *pu32DBBTPhysBlockAdd)
{
    NandPhysicalMedia * nand = NandHal::getNand(u32NAND);
    RtStatus_t Status;

#ifdef ALLOW_BB_TABLE_READ_SKIP
    if (s_skipTableOnNAND)
    {
#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"Skip reading BB Table from NAND.\r\n");
#endif
        return ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND;
    }
#endif

    // Make sure our buffers are allocated.
    Status = allocateBuffers();
    if (Status != SUCCESS)
    {
        return Status;
    }
    
    // Find the DBBT by searching the first blocks in the specific chip
    uint32_t iReadSector = nand->blockToPage(*pu32DBBTPhysBlockAdd);
    Status = m_media->bootBlockSearch(u32NAND, &zDBBTFingerPrints, &iReadSector, m_sectorBuffer, m_auxBuffer, false, NULL);
    
    if (Status == SUCCESS)
    {
        // Cast this pointer.
        BootBlockStruct_t * pDBBT = (BootBlockStruct_t *)m_sectorBuffer.getBuffer();

#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"Discovered Bad Block Table found at Block %d\r\n",iBlockNum);
#endif

        m_layout.m_u32Number2KPagesBB_NAND0 = pDBBT->zDBBT1.m_u32Number2KPagesBB_NAND0;
        m_layout.m_u32Number2KPagesBB_NAND1 = pDBBT->zDBBT1.m_u32Number2KPagesBB_NAND1;
        m_layout.m_u32Number2KPagesBB_NAND2 = pDBBT->zDBBT1.m_u32Number2KPagesBB_NAND2;
        m_layout.m_u32Number2KPagesBB_NAND3 = pDBBT->zDBBT1.m_u32Number2KPagesBB_NAND3;

        m_layout.m_u32NumberBB_NAND0 = pDBBT->zDBBT1.m_u32NumberBB_NAND0;
        m_layout.m_u32NumberBB_NAND1 = pDBBT->zDBBT1.m_u32NumberBB_NAND1;
        m_layout.m_u32NumberBB_NAND2 = pDBBT->zDBBT1.m_u32NumberBB_NAND2;
        m_layout.m_u32NumberBB_NAND3 = pDBBT->zDBBT1.m_u32NumberBB_NAND3;

        // Save off the Discovered Bad Block Address.
        *pu32DBBTPhysBlockAdd = nand->pageToBlock(iReadSector);
    }
    else
    {
        return ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Erase a valid Discovered Bad Block Table.
//!
//! This function will scan the NAND for a both copies of the Discovered Bad Block Table
//! and if found, they will be erased.
//!
//! \return Status of call or error.
//! \retval SUCCESS No error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DiscoveredBadBlockTable::erase()
{
    RtStatus_t retCode;
    uint32_t u32DBBTPhyBlockAdd;
    uint32_t u32Chip;
    BootBlocks & bootBlocks = m_media->getBootBlocks();
    Region * region;

    //find the first bad block table on the NAND.
    u32DBBTPhyBlockAdd = bootBlocks.m_dbbt1.b.bfBlockAddress;
    u32Chip = bootBlocks.m_dbbt1.b.bfNANDNumber;

    retCode = scan(u32Chip, &u32DBBTPhyBlockAdd);
    if (retCode == SUCCESS)
    {
        // Then erase DBBT1 block.
        Block dbbt1(BlockAddress(u32Chip, u32DBBTPhyBlockAdd));
        if (dbbt1.eraseAndMarkOnFailure() == ERROR_DDI_NAND_HAL_WRITE_FAILED)
        {
            region = m_media->getRegionForBlock(dbbt1);
            if (region)
            {
                region->addNewBadBlock(dbbt1);
            }
        }
    }
    else if (retCode != ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND)
    {
        return retCode;
    }

    u32DBBTPhyBlockAdd = bootBlocks.m_dbbt2.b.bfBlockAddress;
    u32Chip = bootBlocks.m_dbbt2.b.bfNANDNumber;

    retCode = scan(u32Chip, &u32DBBTPhyBlockAdd);
    if (retCode == SUCCESS)
    {
        // Then erase DBBT2 block.
        Block dbbt2(BlockAddress(u32Chip, u32DBBTPhyBlockAdd));
        if (dbbt2.eraseAndMarkOnFailure() == ERROR_DDI_NAND_HAL_WRITE_FAILED)
        {
            region = m_media->getRegionForBlock(dbbt2);
            if (region)
            {
                region->addNewBadBlock(dbbt2);
            }
        }
    }
    else if (retCode != ERROR_DDI_NAND_D_BAD_BLOCK_TABLE_BLOCK_NOT_FOUND)
    {
        return retCode;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Save a valid Discovered Bad Block Table.
//!
//! This function will scan the NAND for a saved Discovered Bad Block Table
//! and if it is found, it will erase that block.  It will then attempt
//! to write the Bad Block table.
//!
//! \return Status of call or error.
//! \retval SUCCESS No error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DiscoveredBadBlockTable::save()
{
#if defined(DEBUG) && defined(ALLOW_BB_TABLE_READ_SKIP)
    if (s_skipTableOnNAND)
    {
        tss_logtext_Print(0,"Skip saving BB Table to NAND.\r\n");
        return SUCCESS;
    }
#endif

#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    tss_logtext_Print(0,"Save BB Table to NAND.\r\n");
#endif

    // First we need to save off our Bad Block Table.  We need to find the
    // Bad Block Table on the NAND then erase it in preparation for saving.
    erase();

    // Either way, we need to save it off here.
    // Write the DBBT to the first NAND.
    return writeBadBlockTables();
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Compute a page-offset into the DBBT block.
//!
//! \fntype     Function
//!
//! The boot-block known as the "DBBT" contains separate tables for each
//! separate NAND chip, and can also contain more than just
//! locations of bad-blocks (i.e. the original "DBBT" information).
//! This function computes the page-offset into the DBBT block for
//! that page that matches the desired NAND chip and
//! DbbtContent_t.
//!
//! \param[in]  uChip           The index of the NAND chip whose DBBT page-offset is needed.
//!                             Range [0..3].  Values outside of this range are treated as zero.
//! \param[out] DbbtContent     One of enum DbbtContent_t.
//!
//! \retval A page-offset into the DBBT block.
//!
//! \pre    The DiscoveredBadBlockStruct_t must be known and provided.
////////////////////////////////////////////////////////////////////////////////
uint32_t DiscoveredBadBlockTable::getDbbtPageOffset(unsigned uChip, DbbtContent_t DbbtContent)
{
    uint32_t u32PageOffset = DBBT_DATA_START_PAGE_OFFSET;

    // What part of the "DBBT" does the caller want to access?
    if (kBBRC == DbbtContent)
    {
        // The caller wants the BBRC.
        // It is located right after the DBBT, and we can force the
        // desired address computation by setting uChip like this:
        uChip = 4;
    }
    else if (uChip > 3)
    {
        uChip = 0;
    }

    // Find the correct page index into the Discovered Bad Block Table for this chip.
    if (uChip > 0)
    {
        // Chip 1
        u32PageOffset += m_layout.m_u32Number2KPagesBB_NAND0;
    }
    if (uChip > 1)
    {
        // Chip 2
        u32PageOffset += m_layout.m_u32Number2KPagesBB_NAND1;
    }
    if (uChip > 2)
    {
        // Chip 3
        u32PageOffset += m_layout.m_u32Number2KPagesBB_NAND2;
    }

    if (uChip > 3)
    {
        // Find the index into the page containing the BadBlocksPerRegionCounts_t structure.
        // It is located right after the tables of discovered bad-blocks.
        u32PageOffset += m_layout.m_u32Number2KPagesBB_NAND3;
    }

    return (u32PageOffset);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Accessor function to get a pointer into a BadBlocksPerRegionCounts_t structure.
//!
//! \fntype     Function
//!
//! This function gets a pointer to a bad-blocks count embedded in a BadBlocksPerRegionCounts_t
//! structure.  This structure is itself embedded in a BootBlockStruct_t.
//!
//! \param[in]  pBootBlockStruct        Pointer to the BootBlockStruct_t in RAM
//!                                     that contains the BadBlocksPerRegionCounts_t structure.
//!                                     The contents at this pointer are not verified
//!                                     by this function.
//! \param[in]  uRegion                 Region number, [0..MAX_NAND_REGIONS).
//!
//! \return  Returned pointer to the bad-block count.
//!
//! \pre    The BootBlockStruct_t must exist at pBootBlockStruct, and must
//!         contain the FirmwareBlock.BadBlocksPerRegionCounts structure.
////////////////////////////////////////////////////////////////////////////////
uint32_t * DiscoveredBadBlockTable::getPointerToBbrcEntryForRegion( BootBlockStruct_t *pBootBlockStruct, unsigned uRegion)
{
    if ( uRegion < MAX_NAND_REGIONS &&
         uRegion < pBootBlockStruct->FirmwareBlock.BadBlocksPerRegionCounts.u32Entries )
    {
        return &(pBootBlockStruct->FirmwareBlock.BadBlocksPerRegionCounts.u32NumBadBlksInRegion[uRegion]);
    }
    else
    {
        return NULL;
    }
}

#if !defined(__ghs__)
#pragma mark --SaveDbbtTask--
#endif

SaveDbbtTask::SaveDbbtTask()
:   DeferredTask(kTaskPriority)
{
}

uint32_t SaveDbbtTask::getTaskTypeID() const
{
    return kTaskTypeID;
}

bool SaveDbbtTask::examineOne(DeferredTask * task)
{
    // Don't let this task be inserted if there is already another SaveDbbtTask in
    // the deferred queue.
    return (task->getTaskTypeID() == kTaskTypeID);
}

void SaveDbbtTask::task()
{
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: writing DBBT\n");
    
//    DdiNandLocker lock;
    DiscoveredBadBlockTable dbbt(g_nandMedia);
    dbbt.save();
    
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: done writing DBBT\n");
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
