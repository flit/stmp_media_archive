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
//! \file ddi_nand_media_write_bcbs.c
//! \brief This file contains the functions for manipulating the Boot Control
//!        blocks including saving and retrieving the bad block table on
//!        the NAND.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_media.h"
#include "ddi_nand.h"
#include "DiscoveredBadBlockTable.h"
#include "ddi_nand_system_drive_recover.h"
#include "ddi_nand_ddi.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/nand/rom_support/ddi_nand_hamming_code_ecc.h"
#include "hw/digctl/hw_digctl.h"
#include "drivers/rtc/ddi_rtc.h"

// This layer violation is necessary so we can access the list of NAND commands.
#include "drivers/media/nand/hal/src/ddi_nand_hal_internal.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Write the NCB Boot Blocks to the NAND(s).
//!
//! This function will write out the NCB Boot Blocks to the NAND(s) at the
//! locations previously determined and stored in #NandMediaInfo by
//! ddi_nand_media_LayoutBootBlocks().
//! All NCBs are written here and now by this function. If the erase of an NCB's
//! block fails, that NCB will not be written. However, attempts are made to write
//! both NCBs regardless of whether the other one was written successfully. If 
//! either one fails, the appropriate error will be returned. But you can check
//! whether the bfBlockProblem field in the NCB's #BootBlockLocation_t struct in
//! #NandMediaInfo is set to #kNandBootBlockValid to tell if a given NCB was
//! actually written and which one failed.
//!
//! \param[in]  pNANDMediaInfo Pointer to the NAND media info descriptor
//! \param[in]  pNandTiming Pointer to the optimum NAND timings for this NAND.
//! \param[in]  pu8Page Pointer to the NAND buffer to use.
//! \param[in] auxBUffer Pointer to auxiliary buffer to use.
//!
//! \return Status of call or error.
//! \retval SUCCESS No error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::writeNCB(const NAND_Timing2_struct_t * pNandTiming, SECTOR_BUFFER * pu8Page, SECTOR_BUFFER * auxBuffer)
{
    RtStatus_t retCode = SUCCESS;
    BootBlockStruct_t * pBootControlBlock;
    unsigned pageToSector;
    unsigned pageToSectorShift;

#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
	tss_logtext_Print(0,"Writing NAND Control Block image...\n");
#endif

    // Prepare the redundant area
    nand::Metadata md(auxBuffer);
    md.prepare(BCB_SPACE_TAG);

    // NCB Initialization.
    // Clear out entire page .
    memset(pu8Page, 0, m_params->pageDataSize);

    // Overlay NAND Control Block structure on Page.
    pBootControlBlock = (BootBlockStruct_t *)pu8Page;

    // We have to handle Type 8 and Type 10 Samsung 4K page 128-byte RA NANDs specially, since the 37xx boot
    // ROM cannot use the shift and mask to get to the second 2K subpage of the 4K page. So we
    // basically make the NAND look like a 2K NAND to the ROM.
    // Devices using BCH also only provide 2K per firmware page for the ROM.
    if (m_params->hasSmallFirmwarePages)
    {
        pageToSector = m_params->firmwarePageDataSize / NAND_PAGE_SIZE_2K;
    }
    else
    {
        // Compute the multiplier to convert from natural NAND pages to 2K sectors.
        pageToSector = m_params->pageDataSize / NAND_PAGE_SIZE_2K;
    }

    // Convert the multiplier to a shift.
    pageToSectorShift = 0;
    if (pageToSector > 1)
    {
        while ((1 << pageToSectorShift) < pageToSector)
        {
            pageToSectorShift++;
        }
    }

    // Start building up the NAND Control Block.
    pBootControlBlock->m_u32FingerPrint1 = NCB_FINGERPRINT1;
    pBootControlBlock->m_u32FingerPrint2 = NCB_FINGERPRINT2;
    pBootControlBlock->m_u32FingerPrint3 = NCB_FINGERPRINT3;

    // Copy using assignment operator.
    pBootControlBlock->NCB_Block1.m_NANDTiming.NAND_Timing = *pNandTiming;

#if defined(STMP378x)
    // Check if we're using BCH. If so, the page total size must be calculated. We only write
    // 2K worth of data to the page, because that's all the ROM can get to. So the page data
    // size is fixed. But the page total size can vary, depending on the BCH layout settings.
    if (m_params->eccDescriptor.isBCH())
    {
        // Always fixed at 2048 bytes.
        pBootControlBlock->NCB_Block1.m_u32DataPageSize = LARGE_SECTOR_DATA_SIZE;
        
        // The calculations below will only work with block 0 and block N sizes of 512.
        assert(m_params->eccDescriptor.u32SizeBlockN == 512);
        assert(m_params->eccDescriptor.u32SizeBlock0 == 512);
        
        // Formula for calculating number of parity bits for each block is (ecc_level * 13).
        unsigned uEccCount = (ddi_bch_GetLevel(m_params->eccDescriptor.eccTypeBlock0) * NAND_BCH_PARITY_SIZE_BITS) + (NAND_BCH_2K_PAGE_BLOCKN_COUNT * (ddi_bch_GetLevel(m_params->eccDescriptor.eccType) * NAND_BCH_PARITY_SIZE_BITS));
        uEccCount = (uEccCount + (8-1)) / 8;    // convert to bytes
        unsigned auxCount = uEccCount + m_params->eccDescriptor.u32MetadataBytes;
        
        // Data count is the sum of the sizes of each block (block 0 may be a different size).
        unsigned dataCount = (NAND_BCH_2K_PAGE_BLOCKN_COUNT * m_params->eccDescriptor.u32SizeBlockN) + m_params->eccDescriptor.u32SizeBlock0;
                             
        pBootControlBlock->NCB_Block1.m_u32TotalPageSize = dataCount + auxCount;
    }
    else
    {
#endif // STMP378x

        // Using ECC8, or non-378x.
        
        // Set the page data size and page total size to the values the ROM expects
        // for Samsung 4K page NANDs (Types 8 and 10).
        if (m_params->hasSmallFirmwarePages)
        {
            // In this case, we already know for 378x that we're using ECC8 and not BCH,
            // so the same sizes apply as for 37xx.
            pBootControlBlock->NCB_Block1.m_u32DataPageSize = m_params->firmwarePageDataSize;
            pBootControlBlock->NCB_Block1.m_u32TotalPageSize = m_params->firmwarePageTotalSize;
        }
        else
        {
            // Specify the normal page data and total size for all other NAND types.
            pBootControlBlock->NCB_Block1.m_u32DataPageSize = m_params->pageDataSize;
            pBootControlBlock->NCB_Block1.m_u32TotalPageSize = m_params->pageTotalSize;
        }

#if defined(STMP378x)
    }
#endif

    // Sectors per block is in 2K sectors rather than pages.
    pBootControlBlock->NCB_Block1.m_u32SectorsPerBlock = m_params->wPagesPerBlock * pageToSector;
    pBootControlBlock->NCB_Block1.m_u32SectorInPageMask = pageToSector - 1;
    pBootControlBlock->NCB_Block1.m_u32SectorToPageShift = pageToSectorShift;
    pBootControlBlock->NCB_Block1.m_u32NumberOfNANDs = NandHal::getChipSelectCount();

    pBootControlBlock->NCB_Block2.m_u32TotalInternalDie = NandHal::getFirstNand()->wTotalInternalDice;
    pBootControlBlock->NCB_Block2.m_u32InternalPlanesPerDie = NandHal::getFirstNand()->wTotalInternalDice;
    pBootControlBlock->NCB_Block2.m_u32CellType = m_params->NandType;
    pBootControlBlock->NCB_Block2.m_u32NumRowBytes = m_params->wNumRowBytes;
    pBootControlBlock->NCB_Block2.m_u32NumColumnBytes = 2;

#if defined(STMP37xx) || defined(STMP377x)
    // The boot ROM expects the ECC type field to be set to a command for
    // the ECC8 engine, so we have to convert from our encoding.
    switch (m_params->eccDescriptor.eccType)
    {
        // Reed-Solomon 4-bit
        case kNandEccType_RS4:
            pBootControlBlock->NCB_Block2.m_u32ECCType = BV_GPMI_ECCCTRL_ECC_CMD__DECODE_4_BIT;
            break;

        // Reed-Solomon 8-bit
        case kNandEccType_RS8:
            pBootControlBlock->NCB_Block2.m_u32ECCType = BV_GPMI_ECCCTRL_ECC_CMD__DECODE_8_BIT;
            break;
    }
#elif defined(STMP378x)
    // for 378x, set ecc type in NCB
    pBootControlBlock->NCB_Block2.m_u32ECCType = (uint32_t)m_params->eccDescriptor.eccType;
    // Check for BCH ECC type.
    if (m_params->eccDescriptor.isBCH())
    {
        // We always set the number of N blocks to 3, to form a 2K page.
        pBootControlBlock->NCB_Block2.m_u32EccBlock0EccLevel   = (uint32_t)m_params->eccDescriptor.eccTypeBlock0;
        pBootControlBlock->NCB_Block2.m_u32EccBlockNSize       = m_params->eccDescriptor.u32SizeBlockN;
        pBootControlBlock->NCB_Block2.m_u32EccBlock0Size       = m_params->eccDescriptor.u32SizeBlock0;
        pBootControlBlock->NCB_Block2.m_u32NumEccBlocksPerPage = NAND_BCH_2K_PAGE_BLOCKN_COUNT;
        pBootControlBlock->NCB_Block2.m_u32MetadataBytes       = m_params->eccDescriptor.u32MetadataBytes;
        pBootControlBlock->NCB_Block2.m_u32EraseThreshold      = m_params->eccDescriptor.u32EraseThreshold;
        
        // Make sure that the ROM with get exactly 2K per read. It does not support
        // any block sizes that would produce more or less than 2K per read.
        assert(((pBootControlBlock->NCB_Block2.m_u32NumEccBlocksPerPage * pBootControlBlock->NCB_Block2.m_u32EccBlockNSize) + pBootControlBlock->NCB_Block2.m_u32EccBlock0Size) == 2048);
        
        // The ROM compares the page total size against 2112 and if larger will use the
        // subpage calculations, resulting in it dividing the N block count by 2. So we
        // have to counter that here.
        if (pBootControlBlock->NCB_Block1.m_u32TotalPageSize > LARGE_SECTOR_TOTAL_SIZE)
        {
            pBootControlBlock->NCB_Block2.m_u32NumEccBlocksPerPage *= 2;
        }
    }
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

    // Fill in read commands.
    pBootControlBlock->NCB_Block2.m_u32Read1stCode = eNandProgCmdRead1;
    pBootControlBlock->NCB_Block2.m_u32Read2ndCode = eNandProgCmdRead1_2ndCycle;

    // Fill in the firmware section of the boot block.
    pBootControlBlock->FirmwareBlock.m_u16Major = NCB_FIRMWAREBLOCK_VERSION_MAJOR;
    pBootControlBlock->FirmwareBlock.m_u16Minor = NCB_FIRMWAREBLOCK_VERSION_MINOR;

    memcpy(&(pBootControlBlock->FirmwareBlock.NAND_Timing2_struct), pNandTiming, sizeof(NAND_Timing2_struct_t) );

    SECTOR_BUFFER * actualSectorBuffer;
    SECTOR_BUFFER * actualAuxBuffer;
    bool doWriteRaw;

#if defined(STMP37xx) || defined(STMP377x)
    // Perform a normal ECC-encoded write.
    actualSectorBuffer = pu8Page;
    actualAuxBuffer = auxBuffer;
	doWriteRaw = false;
#elif defined(STMP378x)
    // Allocate enough temporary buffer for encoding NCB
    MediaBuffer fullPageBuffer;
    if ((retCode = fullPageBuffer.acquire(kMediaBufferType_NANDPage)) != SUCCESS)
    {
        return retCode;
    }
    fullPageBuffer.fill(0xff);
    actualSectorBuffer = fullPageBuffer;

	// Encode NCB using software ecc
    hw_digctl_ChipAndRevision chipRev = hw_digctl_GetChipRevision();
	if (chipRev == HW_3780_TA1 || chipRev == HW_3780_TA2)
	{
		EncodeHammingAndRedundancy((unsigned char *)pBootControlBlock, (uint8_t *)actualSectorBuffer);
	}
	else
	{
	    uint8_t * offsetDataCopy = (uint8_t *)actualSectorBuffer + NAND_HC_ECC_OFFSET_DATA_COPY;
	    uint8_t * offsetParityCopy = (uint8_t *)actualSectorBuffer + NAND_HC_ECC_OFFSET_PARITY_COPY;
		memcpy(offsetDataCopy, pBootControlBlock, NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES);
		CalculateHammingForNCB_New(offsetDataCopy, offsetParityCopy);
	}
	
	// Use a raw write of the software ECC encoded NCB.
	actualAuxBuffer = NULL;
	doWriteRaw = true;
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

    // Write both copies of the NCB.
    BootBlockLocation_t * whichNcb[2] = { &m_bootBlocks.m_ncb1, &m_bootBlocks.m_ncb2 };
    retCode = writeBootBlockPair(whichNcb, actualSectorBuffer, actualAuxBuffer, doWriteRaw);
    
    // If either NCB write succeeded, remember that we have a valid NCB.
    if (m_bootBlocks.m_ncb1.isValid() || m_bootBlocks.m_ncb2.isValid())
    {
        m_bootBlocks.m_isNCBAddressValid = true;
    }

    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Writes a pair of boot blocks.
//!
//! Attempts to write both copies of boot block whose location is specified
//! by a #BootBlockLocation_t struct passed into this function. If the erase of the boot
//! block fails, that block will not be written. However, attempts are made to write
//! both copies regardless of whether the other one was written successfully. If 
//! either one fails, the appropriate error will be returned. But you can check
//! whether the bfBlockProblem field in the passed in #BootBlockLocation_t structs
//! are set to #kNandBootBlockValid to tell if a given copy was
//! actually written and which one failed.
//!
//! \param bootBlocks Pointer to an array with two elements containing pointers to
//!     the two copies of #BootBlockLocation_t structs, one for each copy of the
//!     boot block.
//! \param pageBuffer Buffer containing the data for the boot block. This same buffer
//!     is written to both copies of the boot block.
//! \param auxBuffer Auxiliary buffer containing the metadata for the boot block copies.
//! \param doWriteRaw Whether to write using ECC or not. Pass false to use ECC. If
//!     this parameter is true then \a auxBuffer will be ignored.
//!
//! \retval SUCCESS Both copies of the boot block were written successfully.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::writeBootBlockPair(BootBlockLocation_t * bootBlocks[2], SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer, bool doWriteRaw)
{
    int i = 0;
    Region * region;
    RtStatus_t retCode = SUCCESS;
    
    // Write out both boot blocks.
    for (; i < 2; ++i)
    {
        BootBlockLocation_t & bootBlock = *bootBlocks[i];
        
        // Erase the boot block's block.
        Block thisBootBlock(BlockAddress(bootBlock.b.bfNANDNumber, bootBlock.b.bfBlockAddress));
        RtStatus_t thisStatus = thisBootBlock.eraseAndMarkOnFailure();
        
        if (thisStatus != SUCCESS && retCode == SUCCESS)
        {
            // Record this failure.
            retCode = thisStatus;
        }
    
        // Write out the boot block if the erase passed.
        if (thisStatus == SUCCESS)
        {
            BootPage page(PageAddress(bootBlock.b.bfNANDNumber, bootBlock.b.bfBlockAddress, 0));
            page.setRequiresRawWrite(doWriteRaw);
            page.setBuffers(pageBuffer, auxBuffer);
            
            thisStatus = page.writeAndMarkOnFailure();

            if (thisStatus != SUCCESS && retCode == SUCCESS)
            {
                retCode = thisStatus;
            }
        }
        
        // Mark the NCB as valid if both the erase and write succeeded.
        if (thisStatus == SUCCESS)
        {
            bootBlock.b.bfBlockProblem = kNandBootBlockValid;
        }
        else if (thisStatus == ERROR_DDI_NAND_HAL_WRITE_FAILED)
        {
            // Either the erase or write failed, so add this new bad block to the owning region.
            // The block was actually marked bad in the calls above.
            region = getRegionForBlock(thisBootBlock);
            if (region)
            {
                region->addNewBadBlock(thisBootBlock);
            }
        }
    }
    
    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Write the LDLB Boot Blocks to the NAND.
//!
//! This function will write out the LDLB boot blocks for the STMP3700 to
//! the NAND.
//! Both LDLB copies are written by this function. If the erase of an LDLB's
//! block fails, that LDLB will not be written. However, attempts are made to write
//! both LDLBs regardless of whether the other one was written successfully. If 
//! either one fails, the appropriate error will be returned. But you can check
//! whether the bfBlockProblem field in the LDLB's #BootBlockLocation_t struct in
//! #NandMediaInfo is set to #kNandBootBlockValid to tell if a given LDLB was
//! actually written and which one failed.
//!
//! \param[in]  pNANDMediaInfo Pointer to the NAND media info descriptor
//! \param[in]  u32SectorsInFirmware Number of sectors in this firmware load.
//! \param[in]  pu8Page Pointer to the NAND buffer to use.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::writeLDLB(uint32_t u32BlocksInFirmware, SECTOR_BUFFER * pu8Page, SECTOR_BUFFER * auxBuffer)
{
    RtStatus_t retCode = SUCCESS;
    BootBlockStruct_t * pBootControlBlock;
    unsigned pagesPerBlock;
    unsigned pageToSector;
    NandPhysicalMedia * nand = NandHal::getFirstNand();

#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
//    tss_logtext_Print(0,"Writing Logical Device Layout Block image...\n");
#endif

    // Prepare the redundant area
    nand::Metadata md(auxBuffer);
    md.prepare(BCB_SPACE_TAG);

    // LDLB Initialization.
    // Clear out entire page - data + RA + extra.
    memset(pu8Page, 0xff, m_params->pageDataSize);

    // Overlay NAND Control Block structure on Page.
    pBootControlBlock = (BootBlockStruct_t *)pu8Page;

    // Start building up the Logical Device Layout Block.
    pBootControlBlock->m_u32FingerPrint1 = LDLB_FINGERPRINT1;
    pBootControlBlock->m_u32FingerPrint2 = LDLB_FINGERPRINT2;
    pBootControlBlock->m_u32FingerPrint3 = LDLB_FINGERPRINT3;

    pBootControlBlock->LDLB_Block1.LDLB_Version.m_u16Major  = LDLB_VERSION_MAJOR;
    pBootControlBlock->LDLB_Block1.LDLB_Version.m_u16Minor  = LDLB_VERSION_MINOR;
    pBootControlBlock->LDLB_Block1.LDLB_Version.m_u16Sub    = LDLB_VERSION_SUB;

    pBootControlBlock->LDLB_Block2.FirmwareVersion.m_u16Major  = LDLB_VERSION_MAJOR;
    pBootControlBlock->LDLB_Block2.FirmwareVersion.m_u16Minor  = LDLB_VERSION_MINOR;
    pBootControlBlock->LDLB_Block2.FirmwareVersion.m_u16Sub    = LDLB_VERSION_SUB;

    // Fill in the NAND bitmap field, even though the ROM doesn't currently use it.
    {
        uint32_t bitmap = NAND_1_BITMAP; // There is always at least one chip.
        if (NandHal::getChipSelectCount() > 1)
        {
            bitmap |= NAND_2_BITMAP;
        }
        if (NandHal::getChipSelectCount() == 4)
        {
            bitmap |= NAND_3_BITMAP | NAND_4_BITMAP;
        }

        pBootControlBlock->LDLB_Block1.m_u32NANDBitmap = bitmap;
    }

    pagesPerBlock = nand->pNANDParams->wPagesPerBlock;

    // Compute the multiplier to convert from natural NAND pages to 2K sectors. For Samsung
    // Type 8 and Type 10 NANDs we always use 2K sectors even though the page is 4K, so the multiplier
    // is always 1. Also true when using BCH
    if (nand->pNANDParams->hasSmallFirmwarePages)
    {
        pageToSector = nand->pNANDParams->firmwarePageDataSize / NAND_PAGE_SIZE_2K;
    }
    else
    {
        pageToSector = nand->pNANDParams->pageDataSize / NAND_PAGE_SIZE_2K;
    }

    // The DBBT starting sector offset is in natural NAND pages, not 2K sectors.
    pBootControlBlock->LDLB_Block2.m_u32DiscoveredBBTableSector = (m_bootBlocks.m_dbbt1.b.bfBlockAddress * pagesPerBlock);
	pBootControlBlock->LDLB_Block2.m_u32DiscoveredBBTableSector2 = (m_bootBlocks.m_dbbt2.b.bfBlockAddress * pagesPerBlock);

    // Firmware starting sector offset is in 2K sectors, not natural pages.
    // Thus, page 2 of 4K page NAND will have a sector offset of 4 because
    // there are 4 2K pages before it.
    pBootControlBlock->LDLB_Block2.m_u32Firmware_sectorStride = 0;
    pBootControlBlock->LDLB_Block2.m_u32Firmware_startingNAND = m_bootBlocks.m_primaryFirmware.b.bfNANDNumber;
    pBootControlBlock->LDLB_Block2.m_u32Firmware_startingSector = (m_bootBlocks.m_primaryFirmware.b.bfBlockAddress * pagesPerBlock * pageToSector);

    pBootControlBlock->LDLB_Block2.m_u32Firmware_sectorStride2 = 0;
    pBootControlBlock->LDLB_Block2.m_u32Firmware_startingNAND2 = m_bootBlocks.m_secondaryFirmware.b.bfNANDNumber;
    pBootControlBlock->LDLB_Block2.m_u32Firmware_startingSector2 = (m_bootBlocks.m_secondaryFirmware.b.bfBlockAddress * pagesPerBlock * pageToSector);

    // The firmware sector count is also in 2K pages.
    pBootControlBlock->LDLB_Block2.m_uSectorsInFirmware = u32BlocksInFirmware * pagesPerBlock * pageToSector;
    pBootControlBlock->LDLB_Block2.m_uSectorsInFirmware2 = u32BlocksInFirmware * pagesPerBlock * pageToSector;

    // Now write both copies of the LDLB to the previously determined locations.
    BootBlockLocation_t * whichLdlb[2] = { &m_bootBlocks.m_ldlb1, &m_bootBlocks.m_ldlb2 };
    retCode = writeBootBlockPair(whichLdlb, pu8Page, auxBuffer, false);

    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Find n good blocks and return the last good block in p32NthBlock param.
//!
//! This function will search from u32BlockStart, finding good blocks until 
//! number of good blocks found are equal to nNumBlocks. It will return the last
//! good block number in p32NthBlock paramter
//!
//! \param[in]  pNANDMediaInfo Pointer to the NAND media info descriptor
//! \param[in]  nNand Nand number
//! \param[in]  nBlocks Number of good blocks to find
//! \param[in]  u32BlockStart Starting block number to look for good blocks
//! \param[out]  p32NthBlock Pointer to return the last good block.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//! \retval ERROR_DDI_NAND_MEDIA_FINDING_NEXT_VALID_BLOCK  if can't be found.
//!
//! \internal
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::findNGoodBlocks(int nNand, int nBlocks, uint32_t u32BlockStart, uint32_t * p32NthBlock)
{
    uint32_t u32RemainingBlocks;
    int i;

    for(i=0; i<nBlocks; i++)
    {
        // Compute number of blocks remaining unallocated on chip.
        u32RemainingBlocks = NandHal::getNand(nNand)->wTotalBlocks - u32BlockStart;
        if ( u32RemainingBlocks == 0)
        {
            // Ran out of good blocks.
            return ERROR_DDI_NAND_MEDIA_FINDING_NEXT_VALID_BLOCK;
        }

        // Find the next good block
        if (findFirstGoodBlock(nNand, &u32BlockStart, u32RemainingBlocks, NULL, kDontEraseFoundBlock) != SUCCESS)
        {
            return ERROR_DDI_NAND_MEDIA_FINDING_NEXT_VALID_BLOCK;
        }
        u32BlockStart = u32BlockStart + 1;
    }
    *p32NthBlock = u32BlockStart - 1;
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Determine the locations of the Boot Blocks on the NAND.
//!
//! This function will determine where the partitions for the NAND should go.
//! There will be expectations of where the Boot Control Blocks should be
//! and the searches will begin here.  Therefore this information must be
//! used when creating the Boot Block layout.  The NCB and LDLB are
//! searched for so they must be located on certain block boundaries.  The
//! others are found by using pointers that are stored in the LDLB so they
//! can be found immediately after the LDLB is found.
//! We're keeping track of the number of boot blocks used so that we know where
//! the firmware can be placed.
//!
//! \param[in]  pNANDMediaInfo Pointer to the NAND media info descriptor
//! \param[in]  iNumFirmwareBlocks Number of firmware blocks to reserve.
//! \param[out]  p32NumBlocksUsedForBootBlocks Pointer to record the number
//!              of blocks used for Boot Block area.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
//! \todo If the NCBs already exist, reuse their actual location instead of
//!     searching for a new location for them.
//! \internal
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::layoutBootBlocks(int iNumFirmwareBlocks, uint32_t * p32NumBlocksUsedForBootBlocks)
{
	uint32_t u32NextBlockPosition;
    uint32_t u32AllocatedBlock;
    uint32_t remainingBlocks;
    uint32_t u32DualNand_PriFWBlockSearch;
    uint32_t u32DualNand_SecFWBlockSearch;

    // Set this value to zero in case we return an error.
    *p32NumBlocksUsedForBootBlocks = 0;

    // Calculate the search window in blocks.
	u32NextBlockPosition = getBootBlockSearchWindowInBlocks();
	if (u32NextBlockPosition < 1)
	{
	    // If the search window is less than a single block in size, then we
	    // have a big problem. This code can only allocate and write boot blocks
	    // that are at least one block apart, but the ROM will not be able to
	    // find the blocks because it uses the actual search window of less than
	    // a block in pages.
	    tss_logtext_Print(0, "***\nWarning! OTP boot block search count is set too low!\nSearch window is less than a block, so boot blocks cannot be allocated properly.\n***\n");

        // Halt in debug builds.
#if DEBUG
	    SystemHalt();
#endif

	    // Set it to 1 so we can continue in release builds, even though we won't
	    // actually be able to boot because the ROM will be using a search window
	    // that is less than a block.
	    u32NextBlockPosition = 1;
	}

    //
    // Primary NCB
    //

    // Find Primary NCB
    // Always first good block on NAND0
    m_bootBlocks.m_ncb1.b.bfNANDNumber = NAND0;

    u32AllocatedBlock = 0;
    // Find the next good block at or after u32AllocatedBlock.
    if (findFirstGoodBlock(m_bootBlocks.m_ncb1.b.bfNANDNumber, &u32AllocatedBlock, u32NextBlockPosition, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_ncb1.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_ncb1.b.bfBlockProblem = kNandBootBlockEmpty;

    //
    // Secondary NCB
    //

    // Now allocate space for the 2nd NCB
    // In a Single NAND configuration, NCB2 will follow NCB1
    // In a multiNAND configuration, NCB2 will be the first good block on OTHER_NAND_FOR_SECONDARY_BCBS.
    // See design document for more details.
    if (NandHal::getChipSelectCount() > 1)
    {
        // MultiNAND configuration will have NCB2 in one of the first sectors on
        // NAND 1.
        m_bootBlocks.m_ncb2.b.bfNANDNumber = OTHER_NAND_FOR_SECONDARY_BCBS;
        u32AllocatedBlock = 0;
    }
    else
    {
        // Single NAND case has NCB2 directly following NCB but at a Search
        // Stride distance away.
        m_bootBlocks.m_ncb2.b.bfNANDNumber = m_bootBlocks.m_ncb1.b.bfNANDNumber;
        u32AllocatedBlock = u32NextBlockPosition;
    }

    // Find the next good block at or after u32AllocatedBlock.
    if (findFirstGoodBlock(m_bootBlocks.m_ncb2.b.bfNANDNumber,
                       &u32AllocatedBlock, u32NextBlockPosition, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_ncb2.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_ncb2.b.bfBlockProblem = kNandBootBlockEmpty;

    //
    // Primary LDLB
    //

    // Allocate space for the primary LDLB
    // In a Single NAND configuration, LDLB1 will follow NCB2
    // In a multiNAND configuration, LDLB1 will follow NCB1 on NAND0.
    // See design document for more details.
    if (NandHal::getChipSelectCount() > 1)
    {
        u32AllocatedBlock = u32NextBlockPosition;
    }
    else
    {
        // Single NAND case has LDLB1 the next search stride following NCB2
        u32AllocatedBlock = 2 * u32NextBlockPosition;
    }

    // Now allocate space for the primary LDLB
    m_bootBlocks.m_ldlb1.b.bfNANDNumber = NAND0;

    // Find the next good block at or after u32AllocatedBlock.
    if (findFirstGoodBlock(m_bootBlocks.m_ldlb1.b.bfNANDNumber,
                            &u32AllocatedBlock, u32NextBlockPosition, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_ldlb1.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_ldlb1.b.bfBlockProblem = kNandBootBlockEmpty;

    //
    // Secondary LDLB
    //

    // Now allocate space for the 2nd LDLB
    // In a Single NAND configuration, LDLB2 will follow LDLB1 + search stride
    // In a multiNAND configuration, LDLB2 will follow NCB2 on OTHER_NAND_FOR_SECONDARY_BCBS.
    // See design document for more details.
    if (NandHal::getChipSelectCount() > 1)
    {
        // MultiNAND configuration will have LDLB2 in one of the first sectors on
        // NAND 1, the next search stride following NCB2.
        u32AllocatedBlock = u32NextBlockPosition;
    }
    else
    {
        // Single NAND case has LDLB2 the next search stride following LDLB1
        u32AllocatedBlock = 3 * u32NextBlockPosition;
    }

    m_bootBlocks.m_ldlb2.b.bfNANDNumber = m_bootBlocks.m_ncb2.b.bfNANDNumber;

    // Find the next good block at or after u32AllocatedBlock.
    if (findFirstGoodBlock(m_bootBlocks.m_ldlb2.b.bfNANDNumber,
                       &u32AllocatedBlock, u32NextBlockPosition, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_ldlb2.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_ldlb2.b.bfBlockProblem = kNandBootBlockEmpty;

    //
    // Primary DBBT
    //

    // Now allocate space for the DBBT
    // In a Single NAND configuration, DBBT1 will follow LDLB2 + extra
    // In a multiNAND configuration, DBBT1 will follow LDLB1 + extra on NAND0.
    // See design document for more details.
    if (NandHal::getChipSelectCount() > 1)
    {
        u32AllocatedBlock = 2 * u32NextBlockPosition;
    }
    else
    {
        // Single NAND case has DBBT at the next search stride following LDLB2
        u32AllocatedBlock = 4 * u32NextBlockPosition;
    }

    m_bootBlocks.m_dbbt1.b.bfNANDNumber = NAND0;

    // Compute number of blocks remaining unallocated on that chip.
    remainingBlocks = NandHal::getNand(m_bootBlocks.m_dbbt1.b.bfNANDNumber)->wTotalBlocks - u32AllocatedBlock;
    if (remainingBlocks == 0)
    {
        // Ran out of good blocks.
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    // Find the next good block at or after u32AllocatedBlock. DBBTs don't use a ROM
    // search window. It should be at the next good block
    if (findFirstGoodBlock(m_bootBlocks.m_dbbt1.b.bfNANDNumber,
                       &u32AllocatedBlock, remainingBlocks, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_dbbt1.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_dbbt1.b.bfBlockProblem = kNandBootBlockEmpty;
    
    // Have a search window's worth of good blocks as spare after DBBT1
    if (findNGoodBlocks(m_bootBlocks.m_dbbt1.b.bfNANDNumber, u32NextBlockPosition, u32AllocatedBlock+1, &u32AllocatedBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    //
    // Secondary DBBT
    //

    // Now allocate space for the 2nd DBBT
    // In a Single NAND configuration, DBBT2 will follow DBBT1 + search area
    // In a multiNAND configuration, DBBT2 will follow LDLB2 + search stride on OTHER_NAND_FOR_SECONDARY_BCBS.
    // See design document for more details.
    if (NandHal::getChipSelectCount() > 1)
    {
        // For dual nand case, save position for primaryfwblock for future use
        u32DualNand_PriFWBlockSearch = u32AllocatedBlock + 1; 
        u32AllocatedBlock = (m_bootBlocks.m_ldlb2.b.bfBlockAddress + u32NextBlockPosition);
    }
    else
    {
        // Single NAND case has DBBT2 directly following DBBT + spare blocks 
        u32AllocatedBlock = u32AllocatedBlock + 1;
    }
    m_bootBlocks.m_dbbt2.b.bfNANDNumber = m_bootBlocks.m_ncb2.b.bfNANDNumber;

    // Compute number of blocks remaining unallocated on that chip.
    remainingBlocks = NandHal::getNand(m_bootBlocks.m_dbbt2.b.bfNANDNumber)->wTotalBlocks - u32AllocatedBlock;
    if (remainingBlocks == 0)
    {
        // Ran out of good blocks.
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    // Find the next good block at or after u32AllocatedBlock. DBBTs don't use a ROM
    // search window but should be at next good block.
    if (findFirstGoodBlock(m_bootBlocks.m_dbbt2.b.bfNANDNumber,
                       &u32AllocatedBlock, remainingBlocks, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_dbbt2.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_dbbt2.b.bfBlockProblem = kNandBootBlockEmpty;

    // Have a search window's worth of good blocks as spare after DBBT2
    if (findNGoodBlocks(m_bootBlocks.m_dbbt2.b.bfNANDNumber, u32NextBlockPosition, u32AllocatedBlock+1, &u32AllocatedBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    //
    // Primary boot image
    //

    // Now figure out where boot image starts
    // In a Single NAND configuration, BootImage will follow DBBT2 + extra
    // In a multiNAND configuration, BootImage will follow DBBT1 + extra on NAND0.
    // See design document for more details.
    if (NandHal::getChipSelectCount() > 1)
    {
        // Save secondary fw block address position for dual nand case
        u32DualNand_SecFWBlockSearch = u32AllocatedBlock + 1;
        // Use the saved off value for Primary FW block search
        u32AllocatedBlock = u32DualNand_PriFWBlockSearch;
    }
    else
    {
        // Single NAND case has FW image directly following DBBT.
        u32AllocatedBlock = u32AllocatedBlock + 1;
    }

    // Primary firmware always resides on chip 0.
    m_bootBlocks.m_primaryFirmware.b.bfNANDNumber = NAND0;

    // Compute number of blocks remaining unallocated on that chip.
    remainingBlocks = NandHal::getNand(m_bootBlocks.m_primaryFirmware.b.bfNANDNumber)->wTotalBlocks - u32AllocatedBlock;
    if (remainingBlocks == 0)
    {
        // Ran out of good blocks.
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    // Find the next good block at or after u32AllocatedBlock. Since the firmware images
    // can actually go anywhere, we let the search run for any many blocks as the firmware
    // image is large.
    if (findFirstGoodBlock(m_bootBlocks.m_primaryFirmware.b.bfNANDNumber, &u32AllocatedBlock, remainingBlocks, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_primaryFirmware.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_primaryFirmware.b.bfBlockProblem = kNandBootBlockEmpty;

    //
    // Secondary boot image
    //

    // Now figure out where secondary boot image starts
    // In a Single NAND configuration, BootImage2 will follow primary BootImage
    // In a multiNAND configuration, BootImage2 will follow DBBT2 + extra on OTHER_NAND_FOR_SECONDARY_BCBS.
    // See design document for more details.
    if (NandHal::getChipSelectCount() > 1)
    {
        // Use the saved off value for secondary fw block search
        u32AllocatedBlock = u32DualNand_SecFWBlockSearch;
    }
    else
    {
        // Single NAND case has FW image 2 directly following FW Image 1.  Add 1 buffer block.
        // Note that this does not take into account any bad blocks that might be located
        // within the first firmware image. But this is not an issue, because the
        // NANDMediaAllocate() takes care of that, later.
        u32AllocatedBlock = (m_bootBlocks.m_primaryFirmware.b.bfBlockAddress + iNumFirmwareBlocks + 1);
    }

    // Secondary firmware is placed on the same chip as NCB2. That is, chip 0 in
    // a single-chip system and chip 1 in a multi-chip system.
    m_bootBlocks.m_secondaryFirmware.b.bfNANDNumber = m_bootBlocks.m_ncb2.b.bfNANDNumber;

    // Compute number of blocks remaining unallocated on the chip.
    remainingBlocks = NandHal::getNand(m_bootBlocks.m_ncb2.b.bfNANDNumber)->wTotalBlocks - u32AllocatedBlock;
    if (remainingBlocks == 0)
    {
        // Ran out of good blocks.
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    // Find the next good block at or after u32AllocatedBlock. Since the firmware images
    // can actually go anywhere, we let the search run for any many blocks as the firmware
    // image is large.
    if (findFirstGoodBlock(m_bootBlocks.m_ncb2.b.bfNANDNumber, &u32AllocatedBlock, remainingBlocks, NULL, kDontEraseFoundBlock) != SUCCESS)
    {
        return ERROR_DDI_NAND_MEDIA_CANT_ALLOCATE_BCB_BLOCK;
    }

    m_bootBlocks.m_secondaryFirmware.b.bfBlockAddress = u32AllocatedBlock;
    m_bootBlocks.m_secondaryFirmware.b.bfBlockProblem = kNandBootBlockEmpty;

    //
    // Config block
    //

    // Now the config block will reside in the LDLB block so record this now.
    m_ConfigBlkAddr[NAND0] = m_bootBlocks.m_ldlb1.b.bfBlockAddress;

    if (NandHal::getChipSelectCount() > 1)
    {
        m_ConfigBlkAddr[OTHER_NAND_FOR_SECONDARY_BCBS] = m_bootBlocks.m_ldlb2.b.bfBlockAddress;
    }

#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    tss_logtext_Print((LOGTEXT_EVENT_FILESYSTEM | LOGTEXT_VERBOSITY_4),
            "\nAllocation is as follows\n");
    tss_logtext_Print(0,"\nAllocation is as follows\n");
    tss_logtext_Print(0,"\tNCB1 on NAND%d, Block %d\n",
           m_bootBlocks.m_ncb1.b.bfNANDNumber,
           m_bootBlocks.m_ncb1.b.bfBlockAddress);
    tss_logtext_Print(0,"\tNCB2 on NAND%d, Block %d\n",
           m_bootBlocks.m_ncb2.b.bfNANDNumber,
           m_bootBlocks.m_ncb2.b.bfBlockAddress);

    tss_logtext_Print(0,"\tLDLB1 on NAND%d, Block %d\n",
           m_bootBlocks.m_ldlb1.b.bfNANDNumber,
           m_bootBlocks.m_ldlb1.b.bfBlockAddress);
    tss_logtext_Print(0,"\tLDLB2 on NAND%d, Block %d\n",
           m_bootBlocks.m_ldlb2.b.bfNANDNumber,
           m_bootBlocks.m_ldlb2.b.bfBlockAddress);

    tss_logtext_Print(0,"\tDBBT1 on NAND%d, Block %d\n",
           m_bootBlocks.m_dbbt1.b.bfNANDNumber,
           m_bootBlocks.m_dbbt1.b.bfBlockAddress);
    tss_logtext_Print(0,"\tDBBT2 on NAND%d, Block %d\n",
           m_bootBlocks.m_dbbt2.b.bfNANDNumber,
           m_bootBlocks.m_dbbt2.b.bfBlockAddress);

    tss_logtext_Print(0,"\tPrimary Firmware on NAND%d, Block %d\n",
           m_bootBlocks.m_primaryFirmware.b.bfNANDNumber,
           m_bootBlocks.m_primaryFirmware.b.bfBlockAddress);
    tss_logtext_Print(0,"\tSecondary Firmware on NAND%d, Block %d\n",
           m_bootBlocks.m_secondaryFirmware.b.bfNANDNumber,
           m_bootBlocks.m_secondaryFirmware.b.bfBlockAddress);
#endif

    // Set the current firmware address to the new primary address.
    m_bootBlocks.m_currentFirmware = m_bootBlocks.m_primaryFirmware;

    *p32NumBlocksUsedForBootBlocks = m_bootBlocks.m_currentFirmware.b.bfBlockAddress;

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Rewrite invalid primary NCB, LDLB, and DBBT boot blocks.
//!
//! The assumption is that the secondary block was successfully found and
//! the chip # and block address filled in.  We make use of these below.
//! We need to perform a search of where we expect the primary boot image to
//! be and we copy from the secondary block (location known from a previous scan)
//! to the primary block (found in the below search).
//!
//! \param[in] force Set this to true to force a rewrite of the LDLB and DBBT,
//!     but not the NCB. When this is false, only those boot blocks that are
//!     recorded as invalid will be rewritten. Regardless of this value, the NCB
//!     is only rewritten when it is damaged.
//! \param[in] pBuffer Pointer to page buffer to use for reading from the device.
//! \param[in] pAuxBuffer Pointer to auxiliary buffer to use for reading from the device.
//!
//! \retval SUCCESS Boot blocks were recovered successfully.
//!
//! \pre The secondary boot blocks must have been successfully located.
//! \post Primary boot blocks, NCB1, LDLB1, and DBBT1, will have been rewritten to
//!     the media if they were corrupted or missing.
//!
//! \todo Refresh secondary boot blocks as well as primary.
//! \todo Manage the return code better. All recovery steps should be attempted
//!     even if one fails, but if any fail then an error should be returned.
//! \todo Consider using the regular WriteNCB and WriteLDLB functions instead
//!     of copying from the secondary boot blocks, which is potentially
//!     dangerous since the secondary blocks are not necessarily good.
//! \todo Is there a way to make sure the in-memory bad block tables are valid
//!     before writing the DBBT?
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::recoverBootControlBlocks(bool force, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer)
{
    RtStatus_t recoverStatus;
    NandPhysicalMedia * nandMedia;
    Region * region;

#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    // If the search did not yield anything, record the error.
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,"!!Refresh the boot blocks!!\n");
#endif

    // Recover NCB1 if there was a problem with the NCB1 Block. Forcing has no effect.
    if (m_bootBlocks.m_ncb1.b.bfBlockProblem == kNandBootBlockInvalid)
    {
        //NCB block is always first or 2nd block.
        uint32_t iBlockToRecover = 0;

#if DEBUG
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Refreshing NCB1 from NCB2!\n");
#endif

        // Find where the NCB1 should be.  This call also erases the block
        // in preparation for the copy below.
        recoverStatus = findFirstGoodBlock(0, &iBlockToRecover,
                                              getBootBlockSearchWindowInBlocks(), pAuxBuffer, kEraseFoundBlock);

        // Check to see if we exceeded the # of blocks allocated for the NCB.
        nandMedia = NandHal::getNand(m_bootBlocks.m_ncb2.b.bfNANDNumber);
        if (nandMedia->blockToPage(iBlockToRecover) < m_bootBlockSearchWindow)
        {
            // Perform the sector read from the secondary block.
            recoverStatus = nandMedia->readPage(nandMedia->blockToPage(m_bootBlocks.m_ncb2.b.bfBlockAddress),
                    pBuffer, pAuxBuffer, 0);

            if (is_read_status_success_or_ecc_fixed(recoverStatus))
            {
                recoverStatus = SUCCESS;
            }

            if (recoverStatus == SUCCESS)
            {
                // Now write the page to the block. Don't check return status.
                // Just hope it takes.
                BootPage page(PageAddress(0, iBlockToRecover, 0));
                page.setBuffers(pBuffer, pAuxBuffer);
                if (page.writeAndMarkOnFailure() == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    region = getRegionForBlock(iBlockToRecover);
                    if (region)
                    {
                        region->addNewBadBlock(iBlockToRecover);
                    }
                }
            }
        }

        m_bootBlocks.m_ncb1.b.bfBlockProblem = kNandBootBlockValid;

        // Update the Address.
        m_bootBlocks.m_ncb1.b.bfBlockAddress = iBlockToRecover;

        // We know the NCB address now.
        m_bootBlocks.m_isNCBAddressValid = true;
    }

    // Recover LDLB1 if there was a problem with the LDLB1 Block or if forced.
    if (force || m_bootBlocks.m_ldlb1.b.bfBlockProblem == kNandBootBlockInvalid)
    {
        uint32_t iBlockToRecover;
        uint32_t iReadSector;

#if DEBUG
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Refreshing LDLB1 from LDLB2!\n");
#endif

        // for the MultiNAND case, the LDLB is right after NCB.
        iReadSector = m_bootBlockSearchWindow;

        // In the single NAND case, the LDLB is after NCB2 which is after NCB.
        // only saying <1 in case this fuse hasn't been programmed.
        if (NandHal::getChipSelectCount() <= 1)
        {
            iReadSector *= 2;
        }

        nandMedia = NandHal::getNand(m_bootBlocks.m_ldlb2.b.bfNANDNumber);
            
        // Convert the page to a block.
        iBlockToRecover = nandMedia->pageToBlock(iReadSector);

        // Find where the LDLB should be.  This call also erases the block
        // in preparation for the copy below.
        recoverStatus = findFirstGoodBlock(0, &iBlockToRecover,
                                              getBootBlockSearchWindowInBlocks(), pAuxBuffer, kEraseFoundBlock);

        // Check to see if we exceeded the # of blocks allocated for
        // the LDLB.  Also make sure the search was successful.
        if ((nandMedia->blockToPage(iBlockToRecover) < (iReadSector + m_bootBlockSearchWindow))
              && (SUCCESS == recoverStatus))
        {
            // Perform the sector read from the secondary LDLB.
            recoverStatus = nandMedia->readPage(nandMedia->blockToPage(m_bootBlocks.m_ldlb2.b.bfBlockAddress),
                    pBuffer, pAuxBuffer, 0);

            if (is_read_status_success_or_ecc_fixed(recoverStatus))
            {
                // Now write the page to the block. Don't check return status.
                // Just hope it takes.
                BootPage page(PageAddress(0, iBlockToRecover, 0));
                page.setBuffers(pBuffer, pAuxBuffer);
                if (page.writeAndMarkOnFailure() == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    region = getRegionForBlock(iBlockToRecover);
                    if (region)
                    {
                        region->addNewBadBlock(iBlockToRecover);
                    }
                }
            }

            // The second page of the LDLB block is the config block so copy it as well.
            // Perform the sector read from the secondary block.
            recoverStatus = nandMedia->readPage(nandMedia->blockAndOffsetToPage(m_bootBlocks.m_ldlb2.b.bfBlockAddress,  CONFIG_BLOCK_SECTOR_OFFSET),
                    pBuffer, pAuxBuffer, 0);

            if (is_read_status_success_or_ecc_fixed(recoverStatus))
            {
                // Now write the page to the block. Don't check return status.
                // Just hope it takes.
                BootPage page(PageAddress(0, iBlockToRecover, CONFIG_BLOCK_SECTOR_OFFSET));
                page.setBuffers(pBuffer, pAuxBuffer);
                if (page.writeAndMarkOnFailure() == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    region = getRegionForBlock(iBlockToRecover);
                    if (region)
                    {
                        region->addNewBadBlock(iBlockToRecover);
                    }
                }
            }

            m_bootBlocks.m_ldlb1.b.bfBlockProblem = kNandBootBlockValid;

            // Update the Address.
            m_bootBlocks.m_ldlb1.b.bfBlockAddress = iBlockToRecover;

        }   // If block search was successful and within range.
    }

    // Recover DBBT1 if there was a problem with it or if forced.
    if (force || m_bootBlocks.m_dbbt1.b.bfBlockProblem == kNandBootBlockInvalid)
    {
        RtStatus_t dbbtStatus;

#if DEBUG
        tss_logtext_Print(0, "Rewriting DBBT!\n");
#endif
        
        DiscoveredBadBlockTable dbbt(this);
        dbbt.setBuffers(pBuffer, pAuxBuffer);
        dbbtStatus = dbbt.save();

        if (dbbtStatus != SUCCESS)
        {
            recoverStatus = dbbtStatus;
        }
    }

    return recoverStatus;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand.h for the documentation of this function.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_repair_boot_media(void)
{
    RtStatus_t status;
    uint32_t secondaryBoot;
    uint32_t needsRepair;

    // Read the persistent bit that the ROM sets when it failed to read
    // one of the boot blocks or part of the boot firmware.
    status = ddi_rtc_ReadPersistentField(RTC_NAND_SECONDARY_BOOT, &secondaryBoot);

    if (status == SUCCESS)
    {
        // This second persistent bit is set by the ROM when it encounters a page
        // with ECC bit errors at the threshold before they become uncorrectable.
        status = ddi_rtc_ReadPersistentField(RTC_NAND_SDK_BLOCK_REWRITE, &needsRepair);
    }

#if DEBUG
    // If either of these persistent bits are set, print a message in debug builds.
    if (secondaryBoot)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "RTC_NAND_SECONDARY_BOOT is set!\n");
    }
    if (needsRepair)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "RTC_NAND_SDK_BLOCK_REWRITE is set!\n");
    }
#endif

    // We don't need to do anything unless one of the persistent bits is set.
    if (status == SUCCESS && (needsRepair || secondaryBoot))
    {
        {
            // Allocate temporary buffers for the repair.
            SectorBuffer sectorBuffer;
            if ((status = sectorBuffer.acquire()) != SUCCESS)
            {
                return status;
            }

            AuxiliaryBuffer auxBuffer;
            if ((status = auxBuffer.acquire()) != SUCCESS)
            {
                return status;
            }
    
            // Repair broken boot blocks. If the NAND_SDK_BLOCK_REWRITE persistent bit
            // was set then we force a rewrite of all boot blocks, otherwise only those
            // that are known to be bad are repaired.
            status = g_nandMedia->recoverBootControlBlocks(needsRepair, sectorBuffer, auxBuffer);
        }

#if DEBUG
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Refreshing primary firmware because of persistent bits!\n");
#endif

        // Initiate a refresh of the primary firmware drive. The whole drive must be
        // refreshed because we don't know where an error, if any, is located on the drive.
        // Regardless of the status of refreshing the boot blocks, we want to do this.
        RtStatus_t refreshStatus = g_nandMedia->getRecoveryManager()->startRecovery(g_nandMedia->getRecoveryManager()->getPrimaryDrive());
        if (refreshStatus != SUCCESS)
        {
            status = refreshStatus;
        }

        // Clear the block rewrite persistent bit now that we've handled it. The secondary
        // boot bit will be cleared only after the firmware has been successfully refreshed.
        if (needsRepair)
        {
            ddi_rtc_WritePersistentField(RTC_NAND_SDK_BLOCK_REWRITE, 0);
        }
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
