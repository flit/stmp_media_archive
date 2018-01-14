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
//! \file    ddi_nand_hal_write.cpp
//! \brief   NAND HAL Write Functions common to many NANDs.
////////////////////////////////////////////////////////////////////////////////

#ifdef DEBUG
#ifndef DNHW_DO_WRITE_TESTS
    // This macro activates test-code, nominally in the copyPages() function.
    // The test-code:
    //    * Checks continuity of the sector and aux buffers
    //    * Blank-tests the destination page.
    //    * Reads-back and compares the data written to the destination.
    // You should normally leave this deactivated, since it will impact I/O performance.
    #define DNHW_DO_WRITE_TESTS 0
#endif
#endif // DEBUG

#if DNHW_DO_WRITE_TESTS
#include <stdlib.h>
#include <string.h>
#endif // DNHW_DO_WRITE_TESTS
#include "hw/core/hw_core.h"
#include "hw/core/vmemory.h"
#include "hw/core/mmu.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_hal_internal.h"
#include "components/profile/cmp_profile.h"

//! \todo Put in header.
extern void ddi_gpmi_clear_ecc_isr_enable();
#if DNHW_DO_WRITE_TESTS
#include "components/telemetry/tss_logtext.h"   // logText Agent
#endif // DNHW_DO_WRITE_TESTS
#ifdef DEBUG_DMA_TOUT
#include "hw/profile/hw_profile.h"
#endif

//#define DEBUG_HAL_WRITES

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#ifdef DEBUG_HAL_WRITES
bool bEnableHalWriteDebug = false;
#endif

#if defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
volatile bool gbDNHW_debug_dma_tout = false;

#endif
#if DNHW_DO_WRITE_TESTS
#pragma alignvar(32) 
//! \brief Pre-allocated auxiliary buffers. 
SECTOR_BUFFER s_auxBuffers_readback[NOMINAL_AUXILIARY_SECTOR_ALLOC_SIZE]; 
#pragma alignvar(32) 
//! \brief Pre-allocated sector buffers. 
SECTOR_BUFFER s_sectorBuffers_readback[NOMINAL_DATA_SECTOR_ALLOC_SIZE]; 
#endif

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_hal.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::enableWrites()
{
    // Enable NAND writes.
    ddi_gpmi_enable_writes(TRUE);

    // Return.
    return(SUCCESS);
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_hal.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::disableWrites()
{
    // Disable NAND writes.
    ddi_gpmi_enable_writes(FALSE);

    // Return.
    return(SUCCESS);
}

#if DEBUG && NAND_HAL_VERIFY_PHYSICAL_CONTIGUITY
//! \brief Determines whether a given buffer is physically contiguous.
//!
//! This function first gets the physical page number of the first word in
//! the buffer. Then it advances a VM page at a time through the buffer,
//! comparing the physical page at each step to make sure they are all
//! physically sequential. Finally, this function checks the physical page
//! of the last word of the buffer to make sure it is sequential as well.
//!
//! \param buffer The virtual address of the buffer to examine.
//! \param len Number of bytes long that the buffer is.
void _verifyPhysicalContiguity(const void * buffer, uint32_t len)
{
    uint32_t physicalAddress;
    uint32_t testAddress;
    uint32_t currentPage;
    uint32_t testPage;
    uint32_t lastWordAddress = (uint32_t)buffer + len - sizeof(uint32_t);
    
    // Get physical address of the first word of the buffer.
    os_vmi_VirtToPhys((uint32_t)buffer, &physicalAddress);
    currentPage = physicalAddress / VMI_PAGE_SIZE;
    
    // Check each page of the buffer to make sure the whole thing is contiguous.
    testAddress = (uint32_t)buffer + VMI_PAGE_SIZE;
    while (testAddress < lastWordAddress)
    {
        // Get physical address of the test address.
        os_vmi_VirtToPhys(testAddress, &physicalAddress);
        testPage = physicalAddress / VMI_PAGE_SIZE;
        
        // The page containing the test address must physically follow the previous page.
        assert(testPage == currentPage + 1);
        
        // Advance the test address by a VMI page.
        testAddress += VMI_PAGE_SIZE;
        currentPage = testPage;
    }
    
    // Get physical address of the last word of the buffer.
    os_vmi_VirtToPhys(lastWordAddress, &physicalAddress);
    testPage = physicalAddress / VMI_PAGE_SIZE;
    
    // The buffer is contiguous if the current and end physical pages are the same, or
    // if the end page is the next page after the current one.
    assert((testPage == currentPage) || (testPage == currentPage + 1));
}
#endif // DEBUG && NAND_HAL_VERIFY_PHYSICAL_CONTIGUITY

////////////////////////////////////////////////////////////////////////////////
//! \brief Write a page to the NAND.
//!
//! Writes a page to the NAND.
//!
//! \param[in] wSectorNum Which page to write, relative to the chip select.
//! \param[in] pBuf Buffer pointer to data to write to NAND.
//!
//! \retval SUCCESS
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::writePage(uint32_t wSectorNum,
    const SECTOR_BUFFER * pBuf, const SECTOR_BUFFER * pAuxillary )
{
    RtStatus_t rtCode;
#if DNHW_DO_WRITE_TESTS
    RtStatus_t status;
    NandEccCorrectionInfo_t corrections;
#endif

    _verifyPhysicalContiguity(pBuf, pNANDParams->pageDataSize);
    _verifyPhysicalContiguity(pAuxillary, pNANDParams->pageMetadataSize);

    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    {
#if DNHW_DO_WRITE_TESTS
    // Test that the target page is blank.
    status = readPage(wSectorNum, s_sectorBuffers_readback, s_auxBuffers_readback, &corrections);
    if ( !nand::is_read_status_success_or_ecc_fixed( status ) )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writePage read-before-write error stat=x%x p=x%x\r\n", status, wSectorNum);          
        
    }
    {
        int i;
        for(i = 0; i< 16;i++)
        {
            if(s_sectorBuffers_readback[i]!=0xFFFFFFFF)
            {
                tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writePage read-before-write error, page is not erased\r\n");          
                break;
            }
        }
    }
#endif  // DNHW_DO_WRITE_TESTS
    }

    {
        // Enable writes to this NAND for this scope.
        EnableNandWrites enabler(this);

        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationWrite);

        // Update shared DMA descriptors.
        g_nandHalContext.writeDma.setChipSelect(wChipNumber);
        g_nandHalContext.statusDma.setChipSelect(wChipNumber);
        g_nandHalContext.writeDma.setAddress(0, adjustPageAddress(wSectorNum));
        g_nandHalContext.writeDma.setBuffers(pBuf, pAuxillary);

        // Flush data cache and run DMA.
        hw_core_clean_DCache();
        rtCode = g_nandHalContext.writeDma.startAndWait(kNandWritePageTimeout);

        // Check the write status result.
        if (rtCode == SUCCESS)
        {
            if (checkStatus(g_nandHalResultBuffer[0], kNandStatusPassMask, NULL) != SUCCESS)
            {
                rtCode = ERROR_DDI_NAND_HAL_WRITE_FAILED;
            }
        }
    }

    {
#if DNHW_DO_WRITE_TESTS
    // Read-back the data for verification.
    status = readPage(wSectorNum, s_sectorBuffers_readback, s_auxBuffers_readback, &corrections);
    if ( !nand::is_read_status_success_or_ecc_fixed( status ) )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writePage readback-1 error stat=x%x p=x%x\r\n",status, wSectorNum);          
    }
    else
    {
        if(memcmp((void *)s_sectorBuffers_readback,(void *)pBuf,4096))
        {
            tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writePage readback-1 compare error, wSectorNum=x%x \r\n", wSectorNum);          
        }
    }
    // The last read may have been from the page-buffer in the NAND.
    // Pre-read from another page to flush the page buffer.
    status = readPage(wSectorNum+1, s_sectorBuffers_readback, s_auxBuffers_readback, &corrections);

    // Read-back the data for verification.
    status = readPage(wSectorNum, s_sectorBuffers_readback, s_auxBuffers_readback, &corrections);
    if ( !nand::is_read_status_success_or_ecc_fixed( status ) )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writePage readback-2 error stat=x%x p=x%x\r\n",status, wSectorNum);          
    }
    else
    {
        if(memcmp((void *)s_sectorBuffers_readback,(void *)pBuf,4096))
        {
            tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writePage readback-2 compare error, wSectorNum=x%x \r\n", wSectorNum);          
        }
    }
#endif // DNHW_DO_WRITE_TESTS
    }

    // Return.
    return(rtCode);
}

RtStatus_t CommonNandBase::writeRawData(uint32_t pageNumber, uint32_t columnOffset, uint32_t writeByteCount, const SECTOR_BUFFER * data)
{
    RtStatus_t rtCode;
#if DNHW_DO_WRITE_TESTS
    RtStatus_t status;
#endif

    _verifyPhysicalContiguity(data, writeByteCount);

    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    {
#if DNHW_DO_WRITE_TESTS
    // Test that the target page is blank.
    status = readRawData(pageNumber, columnOffset, writeByteCount, s_sectorBuffers_readback);
    if ( !nand::read_status_success_or_ecc_fixed( status ) )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writeRawData read-before-write error stat=x%x p=x%x\r\n",status,pageNumber);          
    }
    else
    {
        int i;
        int iMaxWord = (writeByteCount>>2);
        for(i = 0; i< iMaxWord;i++)
        {
            if(s_sectorBuffers_readback[i]!=0xFFFFFFFF)
            {
                tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writeRawData read-before-write error, page is not erased\r\n");          
                break;
            }
        }
    }
#endif  // DNHW_DO_WRITE_TESTS
    }

    // Enable writes to this NAND for this scope.
    EnableNandWrites enabler(this);

    // Construct the raw write DMA descriptor.
    NandDma::WriteRawData rawWriteDma(
        wChipNumber, // chipSelect,
        eNandProgCmdSerialDataInput, // command1,
        NULL, // addressBytes,
        (pNANDParams->wNumRowBytes + pNANDParams->wNumColumnBytes), // addressByteCount,
        eNandProgCmdPageProgram, // command2,
        NULL, //data, // dataBuffer,
        0, //writeByteCount, // dataSize,
        NULL, // auxBuffer,
        0); // auxSize);
    rawWriteDma.setAddress(columnOffset, adjustPageAddress(pageNumber));
    rawWriteDma.setBuffers(data, writeByteCount, NULL, 0);
    
    // Set the chip select for the global status read DMA.
    g_nandHalContext.statusDma.setChipSelect(wChipNumber);
     
    // Chain our global status read DMA onto this raw write DMA.
    rawWriteDma >> g_nandHalContext.statusDma;

    // Flush data cache and run DMA.
    hw_core_clean_DCache();
    rtCode = rawWriteDma.startAndWait(kNandWritePageTimeout);

    // Check the write status result.
    if (rtCode == SUCCESS)
    {
        if (checkStatus(g_nandHalResultBuffer[0], kNandStatusPassMask, NULL) != SUCCESS)
        {
            rtCode = ERROR_DDI_NAND_HAL_WRITE_FAILED;
        }
    }

    {
#if DNHW_DO_WRITE_TESTS
    // Read-back the data for verification.
    status = readRawData(pageNumber, columnOffset, writeByteCount, s_sectorBuffers_readback);
    if ( !nand::read_status_success_or_ecc_fixed( status ) )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writeRawData readback-1 error stat=x%x p=x%x\r\n",status,pageNumber);
    }
    else
    {
        if(memcmp((void *)s_sectorBuffers_readback,(void *)data,writeByteCount))
        {
            tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writeRawData readback-1 compare error, pageNumber=x%x \r\n", pageNumber);          
        }
    }
    // The last read may have been from the page-buffer in the NAND.
    // Pre-read from another page to flush the page buffer.
    status = readRawData(pageNumber+1, columnOffset, writeByteCount, s_sectorBuffers_readback);
    // Read-back the data for verification.
    status = readRawData(pageNumber, columnOffset, writeByteCount, s_sectorBuffers_readback);
    if ( !nand::read_status_success_or_ecc_fixed( status ) )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writeRawData readback-2 error stat=x%x p=x%x\r\n",status,pageNumber);
    }
    else
    {
        if(memcmp((void *)s_sectorBuffers_readback,(void *)data,writeByteCount))
        {
            tss_logtext_Print( LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_UIM_GROUP, "writeRawData readback-2 compare error, pageNumber=x%x \r\n", pageNumber);          
        }
    }
#endif // DNHW_DO_WRITE_TESTS
    }

    // Return.
    return rtCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Erase a block synchronously.
//!
//! Erase a block on the NAND specified. This function will not return until
//! the block has been erased.
//!
//! \param[in]  wBlockNum Which block to erase.
//!
//! \retval     SUCCESS.
//!
//! \warning Be sure to call EnableWrites before and DisableWrites after this function.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::eraseBlock(uint32_t wBlockNum)
{
    uint32_t wRowAddr;
    RtStatus_t ret;

    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    // Enable writes to this NAND for this scope.
    EnableNandWrites enabler(this);
    
    // Use the 1st page of the block to calculate the Row address, then adjust as/if necessary.
    wRowAddr = adjustPageAddress(blockToPage(wBlockNum));

    // The resulting status byte is stored here on the stack, and must be word aligned.
    g_nandHalContext.eraseDma.init(
        wChipNumber,
        eNandProgCmdBlockErase,
        wRowAddr,
        pNANDParams->wNumRowBytes,
        eNandProgCmdBlockErase_2ndCycle);
    
    // Update chip select for status read.
    g_nandHalContext.statusDma.setChipSelect(wChipNumber);
    
    // Chain the status read DMA onto our erase DMA.
    g_nandHalContext.eraseDma >> g_nandHalContext.statusDma;

    // Flush cache.
    hw_core_invalidate_clean_DCache();

    // Initiate DMA and wait for completion.
    ret = g_nandHalContext.eraseDma.startAndWait(kNandEraseBlockTimeout);

    if (SUCCESS == ret)
    {
        if (checkStatus(g_nandHalResultBuffer[0], kNandStatusPassMask, NULL) != SUCCESS)
        {
            ret = ERROR_DDI_NAND_HAL_WRITE_FAILED;
        }
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
//! This common implementation of checkStatus() uses the convertStatusToAbstract()
//! method to convert a status value read from the NAND to an abstract form
//! shared by all NAND types. The abstract status is compared against a mask
//! value to determine the result code, and the abstract status can optionlly
//! be returned to the caller.
//!
//! \param[in] status Status value read from the NAND.
//! \param[in] Mask Bits in the abstract status that must be set for a successful result.
//! \param[out] abstractStatus Optional output status in abstract form.
//! 
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_HAL_CHECK_STATUS_FAILED
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::checkStatus(uint32_t status, uint32_t Mask, uint32_t * abstractStatus)
{
    RtStatus_t retValue;

    // Convert status to abstract.
    uint32_t localStatus = convertStatusToAbstract(status);
    if ((localStatus & Mask) == Mask)
    {
        retValue = SUCCESS;
    }
    else
    {
        retValue = ERROR_DDI_NAND_HAL_CHECK_STATUS_FAILED;
    }
    
    // Return the abstract status to the caller if requested.
    if (abstractStatus)
    {
        *abstractStatus = localStatus;
    }

    return retValue;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Check Status for Type 6 NANDs
//!
//! Given a Status word, invert and twiddle the bits to standardize it.
//!
//! Type 6 70h status bits:
//! -  Bit 0 - Total Pass(0)/Fail(1)
//! -  Bit 1 - Don't Care
//! -  Bit 2 - Don't Care
//! -  Bit 3 - Don't Care
//! -  Bit 4 - Don't Care
//! -  Bit 5 - Reserved (Must be Don't Care)
//! -  Bit 6 - Ready(1)/Busy(0)
//! -  Bit 7 - Write Protect (0=Protected)
//!
//! 71h status bits:
//! -  Bit 0 - Total Pass(0)/Fail(1)
//! -  Bit 1 - Plane 0 Pass(0)/Fail(1)
//! -  Bit 2 - Plane 1 Pass(0)/Fail(1)
//! -  Bit 3 - Plane 2 Pass(0)/Fail(1)
//! -  Bit 4 - Plane 3 Pass(0)/Fail(1)
//! -  Bit 5 - Reserved (Must be Don't Care)
//! -  Bit 6 - Ready(1)/Busy(0)
//! -  Bit 7 - Write Protect (0=Protected)
//!
//! \param status Status value read from the NAND.
//! \return Abstract status value.
//!
//! \note May be cached or normal program Get Status.
////////////////////////////////////////////////////////////////////////////////
uint32_t Type6Nand::convertStatusToAbstract(uint32_t status)
{
    // Mask off the Status Code and return it.
    // flip bits 7 and 0, Get bits 7, 6 and 0. Move 6 into 5
    // Since Cache is not supported on Type1 NAND, we'll dummy CacheReady and CachePass
    // since it is used by some routines (AsyncEraseCallback(), bNANDHalNandIsBusy()).
    return ((status & kType6StatusReadyMask) >> 1)
        | ((status & (kType6StatusPassMask|kType6StatusWriteProtectMask)) ^ (kType6StatusPassMask|kType6StatusWriteProtectMask))
        | kNandStatusCachePreviousPassMask  // dummy cache previous pass value
        | kNandStatusCacheReadyMask;        // dummy cache ready value
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Check Status for Type 2 NANDs
//!
//! Given a Status word, invert and twiddle the bits to standardize it.
//!
//! Type 2 70h status bits:
//! -  Bit 0 - Pass(0)/Fail(1)
//! -  Bit 1 - Cache Pass(0)/Fail(1)
//! -  Bit 2 - Don't Care
//! -  Bit 3 - Don't Care
//! -  Bit 4 - Don't Care
//! -  Bit 5 - True Ready(1)/Busy(0)
//! -  Bit 6 - Cache Ready(1)/Busy(0)
//! -  Bit 7 - Write Protect (0=Protected)
//!
//! \param status Status value read from the NAND.
//! \return Abstract status value.
//!
//! \note May be cached or normal program Get Status.
////////////////////////////////////////////////////////////////////////////////
uint32_t Type2Nand::convertStatusToAbstract(uint32_t status)
{
    // Mask off the Status Code and return it.
    // Flip Previous Cache Pass/Fail, move into bit 8,
    // flip bits 7 and 0, (WriteProtect & Pass/Fail)
    // Get bits 6 & 5 (Ready/Busy & True R/B)
    return (((status & kType2StatusCachePassMask) << 7) ^ kNandStatusCachePreviousPassMask)
        | ((status & (kType2StatusPassMask|kType2StatusWriteProtectMask)) ^ (kType2StatusPassMask|kType2StatusWriteProtectMask))
        | (status & (kType2StatusReadyMask|kType2StatusCacheReadyMask));
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_nand_hal.h for documentation.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::copyPages(
    NandPhysicalMedia * targetNand,
    uint32_t wSourceStartSectorNum,
    uint32_t wTargetStartSectorNum,
    uint32_t wNumSectors,
    SECTOR_BUFFER * sectorBuffer,
    SECTOR_BUFFER * auxBuffer,
    NandCopyPagesFilter * filter,
    uint32_t * successfulPages)
{
    RtStatus_t status;
    uint32_t copiedPages = 0;

    _verifyPhysicalContiguity(sectorBuffer, pNANDParams->pageDataSize);
    _verifyPhysicalContiguity(auxBuffer, pNANDParams->pageMetadataSize);
    
    // Note that we don't explicitly lock the HAL here. It will be locked by the read and
    // write page methods. If we were to add copyback support, then this would have to change.
    while (wNumSectors)
    {
        // Read in the source page.
        status = readPage(wSourceStartSectorNum, sectorBuffer, auxBuffer, NULL);

        // Detect unrecoverable ECC notices here.
        if ( !nand::is_read_status_success_or_ecc_fixed( status ) )
        {
            break;
        }
        
        if (filter)
        {
            // The modify flag is ignored for now since we don't use copyback.
            bool didModifyPage = false;
            status = filter->filter(this, targetNand, wSourceStartSectorNum, wTargetStartSectorNum, sectorBuffer, auxBuffer, &didModifyPage);
            if (status != SUCCESS)
            {
                break;
            }
        }
        
        // Write out the target page. Even if the source page was empty (erased), we have to
        // copy it to the target block, since you cannot skip writing pages within a block.
        if (targetNand->writePage(wTargetStartSectorNum, sectorBuffer, auxBuffer) != SUCCESS)
        {
            status = ERROR_DDI_NAND_HAL_WRITE_FAILED;
            break;
        }

        wNumSectors--;
        wSourceStartSectorNum++;
        wTargetStartSectorNum++;
        ++copiedPages;
    }

    // Convert benign ECC notices to SUCCESS here.
    if ( nand::is_read_status_success_or_ecc_fixed( status ) )
    {
        status = SUCCESS;
    }
    
    // Set number of successfully copied pages.
    if (successfulPages)
    {
        *successfulPages = copiedPages;
    }

    // Return.
    return status;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_nand_hal_types.h for documentation.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::writeFirmwarePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary)
{
    return writePage(uSectorNum, pBuffer, pAuxiliary);
}

///////////////////////////////////////////////////////////////////////////////
//! \copydoc NandPhysicalMedia::writeMultiplePages()
///////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::writeMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount)
{
    unsigned i;
    for (i=0; i < pageCount; ++i)
    {
        MultiplaneParamBlock & thisPage = pages[i];
        thisPage.m_resultStatus = writePage(thisPage.m_address,
                                            thisPage.m_buffer,
                                            thisPage.m_auxiliaryBuffer);
    }
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \copydoc NandPhysicalMedia::eraseMultipleBlocks()
///////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::eraseMultipleBlocks(MultiplaneParamBlock * blocks, unsigned blockCount)
{
    unsigned i;
    for (i=0; i < blockCount; ++i)
    {
        MultiplaneParamBlock & thisBlock = blocks[i];
        thisBlock.m_resultStatus = eraseBlock(thisBlock.m_address);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
