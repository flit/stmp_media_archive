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
//! \addtogroup ddi_media_nand_hal_internals
//! @{
//! \file ddi_nand_hal_bad_block.cpp
//! \brief Functions for handling bad blocks.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_hal_internal.h"
#include "hw/core/mmu.h"
#include <stdlib.h>
#include <string.h>
#include "drivers/media/include/ddi_media_timers.h"

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

//! \brief Scan pages within a block to determine if it is marked bad.
//!
//! Different manufacturers put bad block marks on different pages (for Toshiba, page #'s
//! required depend on the device as well). Checking the first few and the last few pages
//! in a block (where "few" is defined below) will catch bad block marks for all
//! the manufacturers and flash devices we support.
bool CommonNandBase::isBlockBad(uint32_t blockAddress, SECTOR_BUFFER * auxBuffer, bool checkFactoryMarkings, RtStatus_t * readStatus)
{
    // Lock the HAL during this call.
    NandHalMutex mutexHolder;
    
    const unsigned pagesPerBlock = pNANDParams->wPagesPerBlock;
    
    // These constants tell us how many pages we need to check for bad block
    // marks at the beginning and the end of the block. 
    const unsigned totalPagesToCheck = 5;
    const unsigned pagesToCheck[totalPagesToCheck] = { 0, 1, pagesPerBlock - 3, pagesPerBlock - 2, pagesPerBlock - 1 };

    // Local variables.
    RtStatus_t Status;
    bool bIsBad = true;  // Assume block is bad by default, until we manage to read one page without error.
    RtStatus_t LocalReadFailErrorVal = SUCCESS;
    uint32_t pageAddress = blockToPage(blockAddress);

    // Loop over the pages we're checking.
    unsigned int uPageCounter;
    for (uPageCounter = 0; uPageCounter < totalPagesToCheck; uPageCounter++)
    {
        // Compute the address of the current page.
        uint32_t uCurrentPageAddress = pageAddress + pagesToCheck[uPageCounter];

        // If control arrives here, we successfully read the current page's redundant area.
        // Check for a bad block marker. If there isn't one, continue to the next page.
        if (isOnePageMarkedBad(uCurrentPageAddress, checkFactoryMarkings, auxBuffer, &Status))
        {
            // Forget about any error statuses.
            LocalReadFailErrorVal = SUCCESS;
            
            // Note that the block is bad and exit the scan loop.
            bIsBad = true;
            break;
        }
        
        // If we have successfully read a page, then we switch the default to being a good block.
        // The case where a bad block marker is detected is handled above.
        if (Status == SUCCESS)
        {
            bIsBad = false;
        }
        
        // Record the first error seen...
        if ( SUCCESS                             == LocalReadFailErrorVal ||
                 // ...and allow any overwrites of a "fix failed" status.
                 // Thus, the "fix failed" status will only persist if
                 // no other kinds of errors are seen.
                 ERROR_DDI_NAND_HAL_ECC_FIX_FAILED   == LocalReadFailErrorVal )
        {
            LocalReadFailErrorVal = Status;
        }
    }

    // Return the error status.
    if (readStatus != NULL)
    {
        *readStatus = LocalReadFailErrorVal;
    }
    
#if DEBUG
    if ( bIsBad == true &&
         SUCCESS != LocalReadFailErrorVal &&
         ERROR_DDI_NAND_HAL_ECC_FIX_FAILED != LocalReadFailErrorVal )
    {
        tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1),
            "CommonNandBase::isBlockBad declared a BB on chip=%d block=%u due to error x%x\n",
            wChipNumber, blockAddress, LocalReadFailErrorVal);
    }
#endif

    return bIsBad;
}

//! \brief Check the bad block marker for one page.
//!
//! Depending on \a checkFactoryMarkings, the metadata of the specified page is read in one of
//! two ways. Then the bad block marker byte is checked, and the result returned.
//!
//! \param pageAddress The address of the page to read, relative to the NAND object.
//! \param checkFactoryMarkings Whether the factory bad block marker location should be checked.
//! \param[out] readStatus If this parameter is not NULL, it will be filled in with the
//!     result status of the metadata read operation on exit.
//! \retval true The block was marked bad on this page.
//! \retval false The block was not marked bad.
bool CommonNandBase::isOnePageMarkedBad(uint32_t pageAddress, bool checkFactoryMarkings, SECTOR_BUFFER * auxBuffer, RtStatus_t * readStatus)
{
    RtStatus_t Status;
    bool isBad = false;
    
    pageAddress = adjustPageAddress(pageAddress);
    
    if (checkFactoryMarkings)
    {
        // When checking factory markings, we need to read the standard
        // redundant area that follows the data area. On the 37xx the
        // redundant area position is different from the standard because
        // of the way the ECC engine works. So we have to do the read
        // with ECC turned off in order to get at the standard redundant
        // area' location.
        Status = readRawData(pageAddress, pNANDParams->pageDataSize, pNANDParams->pageMetadataSize, auxBuffer);
    }
    else
    {
        // Attempt to read the current page's redundant area.
        Status = readMetadata(pageAddress, auxBuffer, 0);
    }

    // ECC correction is acceptable and counts as a successful read.
    if (nand::is_read_status_success_or_ecc_fixed(Status))
    {
        Status = SUCCESS;
    }
    
    // Return the actual status of the read operation.
    if (readStatus != NULL)
    {
        *readStatus = Status;
    }

    // If the read was successful, then whether the block was bad is determined by the value
    // of the bad block marker byte in the page's metadata. For most NANDs, the block is bad
    // if the marker byte is any value other than 0xff.
    if (Status == SUCCESS)
    {
        uint8_t markerValue = ((uint8_t *)auxBuffer)[kBadBlockMarkerMetadataOffset];
        isBad = markerValue != kBadBlockMarkerValidValue;
    }
    
    return isBad;
}

//! \copydoc NandPhysicalMedia::markBlockBad()
//!
RtStatus_t CommonNandBase::markBlockBad(uint32_t blockAddress, SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer)
{
    NandHalMutex mutexHolder;

    // Presume that we will fail to mark the block as bad.
    RtStatus_t rtCode = ERROR_DDI_NAND_PROGRAM_FAILED;
        
    // Allocate a single buffer for the entire NAND page and clear it to all zeroes. The
    // buffer is allocated before we grab the HAL mutex to allow paging of the allocation code.
    memset(pageBuffer, 0, pNANDParams->pageTotalSize);

    // Lock the HAL during this call. This is put into its own block so the call to
    // isBlockBad() below is made without the mutex owned, allowing that function to
    // allocate its own buffer.
    
    uint32_t sectorsPerBlock = pNANDParams->wPagesPerBlock;
    uint32_t wBaseSectorAddr = blockToPage(blockAddress);
    
    // Erase the bad block before marking it. Ignore any errors, because we
    // already know that the block is bad.
    eraseBlock(blockAddress);

    // Write zeroes to all pages of the block.
    int i;
    for (i = 0; i < sectorsPerBlock; ++i)
    {
        writeRawData(wBaseSectorAddr + i, 0, pNANDParams->pageTotalSize, pageBuffer);
    }
    
    // Check that the block is identifiable as a bad block now.
    if (isBlockBad(blockAddress, auxBuffer))
    {
        rtCode = SUCCESS;
    }

    // The result will be SUCCESS if we were able to mark at least one sector bad.
    return rtCode;
}

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
