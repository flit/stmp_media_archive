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
//! \file ddi_nand_hal_type16.cpp
//! \brief Functions for type 16 devices.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_hal_internal.h"
#include "hw/core/mmu.h"
#include "hw/core/vmemory.h"
#include <stdlib.h>
#include <string.h>
#include "drivers/media/include/ddi_media_timers.h"
#include "drivers/media/sectordef.h"
#include "os/dmi/os_dmi_api.h"

// Disabled to get 3710 live updater to build.
#if !defined(STMP37xx)

///////////////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////////////

//! \brief Shared buffer used to hold the resume read command value (0x00).
const uint8_t kResumeReadCommandBuffer[1] __ALIGN4__ = { eNandProgCmdRead1 };

//! \brief The unique value for byte 1 of the Read ID response for a 4GB 24nm SmartNAND.
const uint8_t k4GBReadIDByte1Value = 0xd7;

//! \brief Size in bytes of one ECC payload.
const int kEccPayloadSize = 512;

const uint8_t kMultireadReadCommandBuffer[] __ALIGN4__ = {
        eNandProgCmdRead1_2ndCycle  // 0x30
    };

//const uint8_t kMultireadRandomDataCommandBuffer[] __ALIGN4__ = {
//        eNandProgCmdRandomDataOut,  // 0x05
//        0,                  // col byte 0
//        0,                  // col byte 1
//    };

const uint8_t kMultireadFinishRandomDataCommandBuffer[] __ALIGN4__ = {
        eNandProgCmdRandomDataOut_2ndCycle  // 0xe0
    };

#if DEBUG

struct SmartNandMetrics {
    uint32_t singleReadCount;
    uint32_t singleMetaReadCount;
    uint32_t singleWriteCount;
    uint32_t singleEraseCount;
    uint32_t singleMoveCount;
    uint32_t multireadCount;
    uint32_t multireadFallbackCount;
    uint32_t multireadMetaCount;
    uint32_t multireadMetaFallbackCount;
    uint32_t multiwriteCount;
    uint32_t multiwriteFallbackCount;
    uint32_t multiEraseCount;
    uint32_t multiEraseFallbackCount;
} g_smartNandMetrics = {0};

#endif // DEBUG

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

//! This constructor is here simply to ensure that the sleep enabled flag is cleared upon
//! object creation.
Type16Nand::Type16Nand()
:   m_isSleepEnabled(false),
    m_isAsleep(false),
    m_isInFastReadMode(false)
{
}

//! We always use 2K firmware pages for Toshiba Type 16 PBA-NAND devices. The ROM doesn't
//! support disabling ECC, yet there is not enough redundant area in the pages to hold
//! even 2-bit BCH when applied to the full 8192 data bytes. So we have to use a
//! smaller data size that will fit in the 8224 byte page with ECC enabled.
//!
//! Toshiba PBA-NAND devices do not require bad block conversion because ECC
//! is not required (from our point of view, at least).
__STATIC_TEXT RtStatus_t Type16Nand::init()
{
    RtStatus_t result = Type11Nand::init();
    if (result != SUCCESS)
    {
        return result;
    }

    // Figure out which generation this chip is based on the read ID results.
    switch (g_nandHalContext.readIdResponse.data[5])
    {
        case kToshiba32nmPBANANDIDByte6:
            m_chipGeneration = k32nm;
            break;
        
        case kToshiba24nmPBANANDIDByte6:
            m_chipGeneration = k24nm;
            break;
        
        // Unknown ID byte value!
        default:
            return ERROR_GENERIC;
    }
    
    m_is4GB = (g_nandHalContext.readIdResponse.data[1] == k4GBReadIDByte1Value);
    
    if (wChipNumber == 0)
    {
        // Override NAND parameter flags.
        pNANDParams->hasSmallFirmwarePages = true;
        pNANDParams->requiresBadBlockConversion = false;
        pNANDParams->hasInternalECCEngine = true;
        pNANDParams->supportsDieInterleaving = false;
        pNANDParams->supportsMultiplaneWrite = true;
        pNANDParams->supportsMultiplaneErase = true;
        pNANDParams->supportsMultiplaneRead = true;
        pNANDParams->supportsCacheRead = true;
        pNANDParams->supportsCacheWrite = true;
        pNANDParams->supportsMultiplaneCacheRead = true;
        pNANDParams->supportsMultiplaneCacheWrite = true;
        pNANDParams->supportsCopyback = true;
        pNANDParams->supportsMultiplaneCopyback = true;
        
        // Set the firmware page size.
        pNANDParams->firmwarePageTotalSize = XL_SECTOR_TOTAL_SIZE;
        pNANDParams->firmwarePageDataSize = XL_SECTOR_DATA_SIZE;
        pNANDParams->firmwarePageMetadataSize = XL_SECTOR_REDUNDANT_SIZE;
        
        // PBA-NANDs have a maximum bad block percentage of 4%.
        pNANDParams->maxBadBlockPercentage = 4;
    }
    
#if PBA_USE_CACHE_WRITE
    // Allocate our temp page buffers. They must be physically contiguous.
    m_cacheWriteBuffer = reinterpret_cast<SECTOR_BUFFER *>(os_dmi_malloc_phys_contiguous(CACHED_BUFFER_SIZE(pNANDParams->pageDataSize)));
    m_cacheWriteAuxBuffer = reinterpret_cast<SECTOR_BUFFER *>(os_dmi_malloc_phys_contiguous(CACHED_BUFFER_SIZE(pNANDParams->pageMetadataSize)));
    
    // Align aux buffer to cache line. The page buffer is big enough that it will always be aligned
    // to 4K VM pages.
    if (((uint32_t)m_cacheWriteAuxBuffer & (BUFFER_CACHE_LINE_MULTIPLE - 1)) != 0)
    {
        free(m_cacheWriteAuxBuffer);
        m_actualCacheWriteAuxBuffer = reinterpret_cast<SECTOR_BUFFER *>(os_dmi_malloc_phys_contiguous(CACHED_BUFFER_SIZE(pNANDParams->pageMetadataSize + BUFFER_CACHE_LINE_MULTIPLE)));
        m_cacheWriteAuxBuffer = (SECTOR_BUFFER *)ROUND_UP((uint32_t)m_actualCacheWriteAuxBuffer, BUFFER_CACHE_LINE_MULTIPLE);
    }
    else
    {
        m_actualCacheWriteAuxBuffer = m_cacheWriteAuxBuffer;
    }
    
    m_isInCacheWrite = false;
    m_hasPageInCacheBuffer = false;
    m_cacheWriteBlock = 0;
    m_cacheWriteBufferedPageOffset = 0;
#endif

    // Initialize the DMA descriptor chain objects.
    buildPageReadWriteDma();
    buildFirmwareReadDma();
    buildMetadataReadDma();
    buildModeChangeDma();
    buildMultireadDma();
#if PBA_MOVE_PAGE
    buildMovePageDma();
#endif
    // Switch to fast read mode.
    m_isInFastReadMode = false;
    enableFastReadMode(true);
    
    // Put the device into sleep mode and turn on auto sleep. This is only necessary
    // for the older 32nm generation. The new 24nm generation does not require active
    // sleep mode management.
    if (m_chipGeneration == k32nm)
    {
        setSleepMode(true);
        m_isSleepEnabled = true;
    }

    return SUCCESS;
}

__STATIC_TEXT void Type16Nand::buildPageReadWriteDma()
{
    const unsigned addressByteCount = pNANDParams->wNumColumnBytes + pNANDParams->wNumRowBytes;
    
    // Prepare the page read DMA.
    m_pageReadDma.init(
        wChipNumber,            // chipSelect
        eNandProgCmdRead1,      // command1 (0x00)
        NULL,           // addressBytes
        addressByteCount,   // addressByteCount
        eNandProgCmdRead1_2ndCycle,  // command2 (0x30)
        NULL,                   // dataBuffer
        0,                      // dataReadSize
        NULL,                   // auxBuffer
        0);                     // auxReadSize

#if !PBA_USE_READ_MODE_2
    // Init the status read and DMA component that sends the read resume command (0x00).
    m_pageStatusReadDma.init(wChipNumber, eNandProgCmdReadStatus, g_nandHalResultBuffer);
    m_pageResumeReadDma.init(wChipNumber, kResumeReadCommandBuffer, 0);
#endif // !PBA_USE_READ_MODE_2
    
    // Prepare the metadata read DMA.
    m_pageWriteDma.init(
        wChipNumber,            // chipSelect
        eNandProgCmdSerialDataInput,      // command1 (0x80)
        NULL,           // addressBytes
        addressByteCount,   // addressByteCount
        eNandProgCmdPageProgram,  // command2 (0x10)
        NULL,                   // dataBuffer
        0,                      // dataReadSize
        NULL,                   // auxBuffer
        0);                     // auxReadSize
    
    // Prepare the status check DMA.
    m_statusReadDma.init(wChipNumber, eNandProgCmdReadStatus, g_nandHalResultBuffer);
    
    // Chain the status check onto the end of each of page and metadata read.
#if PBA_USE_READ_MODE_2
    m_pageReadDma >> m_statusReadDma;
#endif // PBA_USE_READ_MODE_2
    m_pageWriteDma >> m_statusReadDma;
}

__STATIC_TEXT void Type16Nand::buildFirmwareReadDma()
{
    const unsigned addressByteCount = pNANDParams->wNumColumnBytes + pNANDParams->wNumRowBytes;
    
    // Prepare firmware page read DMA. Unlike standard NAND read command sequences, the PBA-NAND
    // wants you to perform a status read after the ready-to-busy wait in order to perform the
    // read reclaim check. You can actually do this status read in the same place for normal NANDs,
    // but usually there is no need.
    uint32_t dataCount;
    uint32_t auxCount;
    uint32_t eccMask = pNANDParams->eccDescriptor.computeMask(
        XL_SECTOR_TOTAL_SIZE,   // readSize
        pNANDParams->pageTotalSize, // pageTotalSize
        kEccOperationRead,
        false,  // readOnly2K
        &dataCount,
        &auxCount);

    m_firmwareReadDma.init(
        wChipNumber,            // chipSelect
        eNandProgCmdRead1,      // command1 (0x00)
        NULL,           // addressBytes
        addressByteCount,   // addressByteCount
        eNandProgCmdRead1_2ndCycle,  // command2 (0x30)
        NULL,                   // dataBuffer
        NULL,                   // auxBuffer
        dataCount + auxCount,                      // readSize
        pNANDParams->eccDescriptor,                      // eccDescriptor
        eccMask);                     // eccMask

    // Init the status read and DMA component that sends the read resume command (0x00).
    m_firmwareStatusReadDma.init(wChipNumber, eNandProgCmdReadStatus, g_nandHalResultBuffer);
    m_firmwareResumeReadDma.init(wChipNumber, kResumeReadCommandBuffer, 0);
    
    // Link in the status read and read resume after the wait period and before the data read.
    m_firmwareReadDma.m_wait >> m_firmwareStatusReadDma >> m_firmwareResumeReadDma >> m_firmwareReadDma.m_readData;
}

__STATIC_TEXT void Type16Nand::buildMetadataReadDma()
{
    const unsigned addressByteCount = pNANDParams->wNumColumnBytes + pNANDParams->wNumRowBytes;
    
    // Prepare the metadata read DMA.
    m_metadataReadDma.init(
        wChipNumber,            // chipSelect
        eNandProgCmdRead1,      // command1 (0x00)
        NULL,           // addressBytes
        addressByteCount,   // addressByteCount
        eNandProgCmdRead1_2ndCycle,  // command2 (0x30)
        NULL,                   // dataBuffer
        0,                      // dataReadSize
        NULL,                   // auxBuffer
        0);                     // auxReadSize

    // Init the status read and DMA component that sends the read resume command (0x00) for the
    // metadata read sequence.
    m_metadataStatusReadDma.init(wChipNumber, eNandProgCmdReadStatus, g_nandHalResultBuffer);
    m_metadataResumeReadDma.init(wChipNumber, kResumeReadCommandBuffer, 0);
    
    // Link up the read reclaim portion of the metadata read chain.
    m_metadataReadDma.m_wait >> m_metadataStatusReadDma >> m_metadataResumeReadDma >> m_metadataReadDma.m_readData;
}

__STATIC_TEXT void Type16Nand::buildModeChangeDma()
{
    // Prepare the mode change DMA chain.
    m_modeDma.init(
        wChipNumber,            // chipSelect
        eNandProgCmdRead1,      // command1 (0x00)
        NULL,           // addressBytes
        5,              // addressByteCount
        eNandProgCmdPBAModeChange,  // command2 (0x57)
        NULL,                   // dataBuffer
        0,                      // dataReadSize
        NULL,                   // auxBuffer
        0);                     // auxReadSize
}

void Type16Nand::buildMultireadDma()
{
    const unsigned addressByteCount = pNANDParams->wNumColumnBytes + pNANDParams->wNumRowBytes;

    // Prep command and address buffers with unchanging values.
    m_multiread.inputPage0Buffer[0] = eNandProgCmdAddressInput;   // 0x60
    m_multiread.inputPage1Buffer[0] = eNandProgCmdAddressInput;   // 0x60
    
    m_multiread.readColumnPage0Buffer[0] = eNandProgCmdRead1;  // 0x00
    m_multiread.readColumnPage0Buffer[1] = 0;                  // col byte 0
    m_multiread.readColumnPage0Buffer[2] = 0;                  // col byte 1
    
    m_multiread.readColumnPage1Buffer[0] = eNandProgCmdRead1;  // 0x00
    m_multiread.readColumnPage1Buffer[1] = 0;                  // col byte 0
    m_multiread.readColumnPage1Buffer[2] = 0;                  // col byte 1
    
    m_multiread.randomDataCommand0Buffer[0] = eNandProgCmdRandomDataOut;  // 0x05
    m_multiread.randomDataCommand0Buffer[1] = 0;                          // col byte 0
    m_multiread.randomDataCommand0Buffer[2] = 0;                          // col byte 1
    
    m_multiread.randomDataCommand1Buffer[0] = eNandProgCmdRandomDataOut;  // 0x05
    m_multiread.randomDataCommand1Buffer[1] = 0;                          // col byte 0
    m_multiread.randomDataCommand1Buffer[2] = 0;                          // col byte 1
    
    // Init components to submit the two page addresses, wait for the NAND to complete
    // the read internally, and then read status.
    m_multiread.inputPage0Dma.init(wChipNumber, m_multiread.inputPage0Buffer, pNANDParams->wNumRowBytes);
    m_multiread.inputPage1Dma.init(wChipNumber, m_multiread.inputPage1Buffer, pNANDParams->wNumRowBytes);
    m_multiread.readCommandDma.init(wChipNumber, kMultireadReadCommandBuffer, 0);
    m_multiread.terminationDma.init();
    m_multiread.waitDma.init(wChipNumber, &m_multiread.terminationDma);
    m_multiread.statusDma.init(
        wChipNumber,
        eNandProgCmdPBAStatusRead2,  // status command (0xf1)
        g_nandHalResultBuffer);      // where to put the resulting status byte

    // Init components to read the page 0 data.
    m_multiread.readColumnPage0Dma.init(wChipNumber, m_multiread.readColumnPage0Buffer, addressByteCount);
    m_multiread.randomDataCommand0Dma.init(wChipNumber, m_multiread.randomDataCommand0Buffer, pNANDParams->wNumColumnBytes);
    m_multiread.finishRandomDataCommand0Dma.init(wChipNumber, kMultireadFinishRandomDataCommandBuffer, 0);
    m_multiread.receivePageData0Dma.init(wChipNumber, NULL, pNANDParams->pageDataSize);
    m_multiread.receivePageMetadata0Dma.init(wChipNumber, NULL, pNANDParams->pageMetadataSize);

    // Init components to read the page 1 data.
    m_multiread.readColumnPage1Dma.init(wChipNumber, m_multiread.readColumnPage1Buffer, addressByteCount);
    m_multiread.randomDataCommand1Dma.init(wChipNumber, m_multiread.randomDataCommand1Buffer, pNANDParams->wNumColumnBytes);
    m_multiread.finishRandomDataCommand1Dma.init(wChipNumber, kMultireadFinishRandomDataCommandBuffer, 0);
    m_multiread.receivePageData1Dma.init(wChipNumber, NULL, pNANDParams->pageDataSize);
    m_multiread.receivePageMetadata1Dma.init(wChipNumber, NULL, pNANDParams->pageMetadataSize);
    
    // Build the full DMA descriptor chain.
    m_multiread.inputPage0Dma >> m_multiread.inputPage1Dma >> m_multiread.readCommandDma >> m_multiread.waitDma >> m_multiread.statusDma >> m_multiread.readColumnPage0Dma >> m_multiread.randomDataCommand0Dma >> m_multiread.finishRandomDataCommand0Dma >> m_multiread.receivePageData0Dma >> m_multiread.receivePageMetadata0Dma >> m_multiread.readColumnPage1Dma >> m_multiread.randomDataCommand1Dma >> m_multiread.finishRandomDataCommand1Dma >> m_multiread.receivePageData1Dma >> m_multiread.receivePageMetadata1Dma >> m_multiread.terminationDma;
    
    // Create wrapper object for this sequence.
    m_multiread.multiReadDma.init(wChipNumber);
    m_multiread.multiReadDma.setDmaStart(m_multiread.inputPage0Dma);
}

__STATIC_TEXT RtStatus_t Type16Nand::cleanup()
{
#if PBA_USE_CACHE_WRITE
    flushWriteCacheBuffer();
    free(m_cacheWriteBuffer);
    free(m_actualCacheWriteAuxBuffer);
    m_cacheWriteBuffer = NULL;
    m_cacheWriteAuxBuffer = NULL;
#endif

    // Wake up the device and restore it to normal read mode.
    setSleepMode(false);
    enableFastReadMode(false);
    reset();
    
    return Type11Nand::cleanup();
}

RtStatus_t Type16Nand::readRawData(uint32_t wSectorNum, uint32_t columnOffset, uint32_t readByteCount, SECTOR_BUFFER * pBuf)
{
    NandHalMutex mutexHolder;
    
    // Wrap sleep disable/enable commands around the read.
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
    return Type11Nand::readRawData(wSectorNum, columnOffset, readByteCount, pBuf);
}

RtStatus_t Type16Nand::writeRawData(uint32_t pageNumber, uint32_t columnOffset, uint32_t writeByteCount, const SECTOR_BUFFER * data)
{
    NandHalMutex mutexHolder;
    
    // Wrap sleep disable/enable commands around the read.
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
    return Type11Nand::writeRawData(pageNumber, columnOffset, writeByteCount, data);
}

void Type16Nand::clearEccInfo(NandEccCorrectionInfo_t * pECC)
{
    if (pECC)
    {
        pECC->maxCorrections = 0;
        pECC->payloadCount = pNANDParams->pageDataSize / kEccPayloadSize;
        pECC->isMetadataValid = true;
        pECC->metadataCorrections = 0;
        
        unsigned i;
        for (i = 0; i < pECC->payloadCount; ++i)
        {
            pECC->payloadCorrections[i] = 0;
        }
    }
}

//! \brief Examines the status byte after a read page command.
//!
//! This method looks at the result of a status read command issued after completing a page
//! read command. It checks for either the read reclaim bit or pass/fail bit being set. The
//! return value is the value that should be returned to the caller of the read page method
//! to indicate which, if any, status bits were set and the required action.
//!
//! \pre The result of a read status (0x70) command is expected to be in the first
//!     byte of #g_nandHalResultBuffer.
//!
//! \retval ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR The read reclaim bit was set.
//! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED The pass/fail status bit was set, indicating
//!     an uncorrectable ECC error.
//! \retval SUCCESS The page read completed with any exceptions.
RtStatus_t Type16Nand::getReadPageStatus()
{
    // Change the status if the read reclaim bit was set in the status byte.
    // Check read reclaim bit directly. We also check the pass/fail bit to see if
    // there were uncorrectable ECC errors.
    if (g_nandHalResultBuffer[0] & kType16StatusReadReclaimMask)
    {
        return ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR;
    }
    else if (g_nandHalResultBuffer[0] & kType16StatusPassMask)
    {
        return ERROR_DDI_NAND_HAL_ECC_FIX_FAILED;
    }
    
    return SUCCESS;
}

RtStatus_t Type16Nand::readPageWithEcc(const NandEccDescriptor_t * ecc, uint32_t pageNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    NandHalMutex mutexHolder;
    
    // Wrap sleep disable/enable commands around the read.
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
    return Type11Nand::readPageWithEcc(ecc, pageNumber, pBuffer, pAuxiliary, pECC);
}

__STATIC_TEXT RtStatus_t Type16Nand::readPage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    NandHalMutex mutexHolder;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->pageDataSize);
    _verifyPhysicalContiguity(pAuxiliary, pNANDParams->pageMetadataSize);

#if DEBUG
    ++g_smartNandMetrics.singleReadCount;
#endif
    
    // Fill in ECC corrections info if asked.
    clearEccInfo(pECC);
    
    uint32_t adjustedPageAddress = adjustPageAddress(uSectorNumber);
    
#if PBA_USE_CACHE_WRITE
    // If possible, we can simply return the contents of our page buffer instead having to flush
    // the buffer and then read it back from the NAND.
    uint32_t pageOffset;
    uint32_t blockAddress;
    pageToBlockAndOffset(adjustedPageAddress, &blockAddress, &pageOffset);
    if (m_hasPageInCacheBuffer && blockAddress == m_cacheWriteBlock && pageOffset == m_cacheWriteBufferedPageOffset)
    {
        memcpy(pBuffer, m_cacheWriteBuffer, pNANDParams->pageDataSize);
        memcpy(pAuxiliary, m_cacheWriteAuxBuffer, pNANDParams->pageMetadataSize);
        return SUCCESS;
    }
#endif // PBA_USE_CACHE_WRITE
    
    // Wrap sleep disable/enable commands around the read.
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
    // Make sure we're in fast read mode.
    enableFastReadMode(true);
    
    // Update the DMA. By providing a valid aux buffer and aux read size, the DMA will use
    // two separate read DMA descriptors.
    m_pageReadDma.setAddress(0, adjustedPageAddress);
    m_pageReadDma.setBuffers(pBuffer, pNANDParams->pageDataSize, pAuxiliary, pNANDParams->pageMetadataSize);

#if !PBA_USE_READ_MODE_2
    // Link in the status read and read resume after the wait period and before the data read.
    // We have to relink this every time we update the buffers, because that causes the default
    // chain to be relinked.
    m_pageReadDma.m_wait >> m_pageStatusReadDma >> m_pageResumeReadDma >> m_pageReadDma.m_readData;
#endif // !PBA_USE_READ_MODE_2
    
    // Flush the data cache to ensure that the DMA descriptor chain is in memory.
    hw_core_invalidate_clean_DCache();
    
    // Start the DMA and wait for it to finish.
    RtStatus_t status = m_pageReadDma.startAndWait(kNandReadPageTimeout);

    if (status == SUCCESS)
    {
        // Process the status read result and check for read reclaim and uncorrectable ECC.
        status = getReadPageStatus();
    }

#if DEBUG
    // Insert a false read error if requested.
   if (g_nand_hal_insertReadError)
   {
       status = g_nand_hal_insertReadError;
       g_nand_hal_insertReadError = 0;
   }
#endif
    
    return status;
}

//! The 32 bytes of metadata are read from offset 8192 in the page.
__STATIC_TEXT RtStatus_t Type16Nand::readMetadata(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC)
{
    NandHalMutex mutexHolder;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->pageMetadataSize);

#if DEBUG
    ++g_smartNandMetrics.singleMetaReadCount;
#endif
    
    // Fill in ECC corrections info if asked.
    clearEccInfo(pECC);
    
    uint32_t adjustedPageAddress = adjustPageAddress(uSectorNumber);
    
#if PBA_USE_CACHE_WRITE
    // If possible, we can simply return the contents of our page buffer instead having to flush
    // the buffer and then read it back from the NAND.
    uint32_t pageOffset;
    uint32_t blockAddress;
    pageToBlockAndOffset(adjustedPageAddress, &blockAddress, &pageOffset);
    if (m_hasPageInCacheBuffer && blockAddress == m_cacheWriteBlock && pageOffset == m_cacheWriteBufferedPageOffset)
    {
        memcpy(pBuffer, m_cacheWriteAuxBuffer, pNANDParams->pageMetadataSize);
        return SUCCESS;
    }
#endif // PBA_USE_CACHE_WRITE
    
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
    // Make sure we're in normal read mode. Fast read mode won't work because it requires a
    // zero column address.
    enableFastReadMode(false);
    
    // Update the DMA.
    m_metadataReadDma.setAddress(pNANDParams->pageDataSize, adjustedPageAddress);
    m_metadataReadDma.setBuffers(pBuffer, pNANDParams->pageMetadataSize, NULL, 0);

    // Link in the status read and read resume after the wait period and before the data read.
    // We have to relink this every time we update the buffers, because that causes the default
    // chain to be relinked.
    m_metadataReadDma.m_wait >> m_metadataStatusReadDma >> m_metadataResumeReadDma >> m_metadataReadDma.m_readData;
    
    // Flush the data cache to ensure that the DMA descriptor chain is in memory.
    hw_core_invalidate_clean_DCache();
    
    // Start the DMA and wait for it to finish.
    RtStatus_t status = m_metadataReadDma.startAndWait(kNandReadPageTimeout);

    if (status == SUCCESS)
    {
        // Process the status read result and check for read reclaim and uncorrectable ECC.
        status = getReadPageStatus();
    }

#if DEBUG
    // Insert a false read error if requested.
   if (g_nand_hal_insertReadError)
   {
       status = g_nand_hal_insertReadError;
       g_nand_hal_insertReadError = 0;
   }
#endif
    
    return status;
}

#if PBA_USE_CACHE_WRITE
__STATIC_TEXT void Type16Nand::flushWriteCacheBuffer()
{
    NandHalMutex mutexHolder;
    
    if (!m_hasPageInCacheBuffer)
    {
        return;
    }
    
    writeBufferedPage(eNandProgCmdPageProgram);
    m_isInCacheWrite = false;
}
#endif // PBA_USE_CACHE_WRITE

// eNandProgCmdPageProgram or eNandProgCmdCacheProgram
__STATIC_TEXT RtStatus_t Type16Nand::writePageFromBuffer(uint32_t address, uint8_t programCommand, const SECTOR_BUFFER * pageBuffer, const SECTOR_BUFFER * auxBuffer)
{
    RtStatus_t status = SUCCESS;

    _verifyPhysicalContiguity(pageBuffer, pNANDParams->pageDataSize);
    _verifyPhysicalContiguity(auxBuffer, pNANDParams->pageMetadataSize);

#if DEBUG
    ++g_smartNandMetrics.singleWriteCount;
#endif
    
    SleepController disableSleep(this);
    
    // Enable writes to this NAND for this scope.
    EnableNandWrites enabler(this);
    
    // Update the DMA.
    m_pageWriteDma.setCommands(eNandProgCmdSerialDataInput, programCommand);
    m_pageWriteDma.setAddress(0, address);
    m_pageWriteDma.setBuffers(pageBuffer, pNANDParams->pageDataSize, auxBuffer, pNANDParams->pageMetadataSize);

    // This operation inserts wait prior to reading write status
    m_pageWriteDma >> m_pageWriteDma.m_wait >> m_statusReadDma;
    
    // Flush the entire data cache before starting the write. Because our buffers are larger
    // than the cache line size, this is faster than walking the buffer a cache line at a time.
    // Also, note that we do not need to invalidate for writes.
    hw_core_clean_DCache();

    // Start and wait for the DMA.
    status = m_pageWriteDma.startAndWait(kNandWritePageTimeout);

    // Convert status read result to abstract status.
    if (status == SUCCESS)
    {
        if (checkStatus(g_nandHalResultBuffer[0], kNandStatusPassMask, NULL) != SUCCESS)
        {
            status = ERROR_DDI_NAND_HAL_WRITE_FAILED;
        }
    }

    return status;
}

#if PBA_USE_CACHE_WRITE
__STATIC_TEXT RtStatus_t Type16Nand::writeBufferedPage(uint8_t programCommand)
{
    RtStatus_t status = SUCCESS;

    uint32_t address = blockAndOffsetToPage(m_cacheWriteBlock, m_cacheWriteBufferedPageOffset);
    writePageFromBuffer(address, eNandProgCmdPageProgram, m_cacheWriteBuffer, m_cacheWriteAuxBuffer);
    m_hasPageInCacheBuffer = false;

    return status;
}
#endif // PBA_USE_CACHE_WRITE

__STATIC_TEXT RtStatus_t Type16Nand::writePage(uint32_t uSectorNumber, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary)
{
    NandHalMutex mutexHolder;
    
    uint32_t adjustedPageAddress = adjustPageAddress(uSectorNumber);
    
#if PBA_USE_CACHE_WRITE
    RtStatus_t status;
    uint32_t pageOffset;
    uint32_t blockAddress;
    pageToBlockAndOffset(adjustedPageAddress, &blockAddress, &pageOffset);
    bool isLastPageInBlock = (pageOffset == pNANDParams->wPagesPerBlock - 1);
    
    // If we're not in a write cache sequence, then just save off this page into our buffer.
    if (!m_isInCacheWrite && !m_hasPageInCacheBuffer)
    {
        // If writing to the last page in a block, then just go ahead and send it out since we
        // can't enter a write cache sequence for this page anyway.
        if (isLastPageInBlock)
        {
            return writePageFromBuffer(adjustedPageAddress, eNandProgCmdPageProgram, pBuffer, pAuxiliary);
        }
        
        // Just save off the page data for now.
        memcpy(m_cacheWriteBuffer, pBuffer, pNANDParams->pageDataSize);
        memcpy(m_cacheWriteAuxBuffer, pAuxiliary, pNANDParams->pageMetadataSize);
        
        m_cacheWriteBlock = blockAddress;
        m_cacheWriteBufferedPageOffset = pageOffset;
        m_hasPageInCacheBuffer = true;
        
        return SUCCESS;
    }
    
    // We're in a write cache sequence, even if we haven't actually sent the first write cache
    // command yet. So send out the page that is sitting in our buffer.
    if (m_hasPageInCacheBuffer)
    {
        bool isSameBlock = (blockAddress == m_cacheWriteBlock);
        bool isPageInSequence = (isSameBlock && pageOffset == m_cacheWriteBufferedPageOffset + 1);
    
        uint8_t programCommand;
        
        if (!isPageInSequence)
        {
            // Writing to a different block, so terminate the write cache sequence with the page
            // that is in our buffer.
            programCommand = eNandProgCmdPageProgram;
            m_isInCacheWrite = false;
        }
        else
        {
            // Writing to the same block as the page that is in our buffer, so the buffered page
            // can be written with the cache command.
            programCommand = eNandProgCmdCacheProgram;
            m_isInCacheWrite = true;
        }
        
        status = writeBufferedPage(programCommand);
        if (status != SUCCESS)
        {
            return status;
        }
    }
    
    // Check if the incoming page data is for the last page in the block, in which case
    // we have to close out the write cache sequence.
    if (isLastPageInBlock)
    {
        m_isInCacheWrite = false;
        return writePageFromBuffer(adjustedPageAddress, eNandProgCmdPageProgram, pBuffer, pAuxiliary);
    }
    else
    {
        // Simply save off this page data for now and update the page counters.
        memcpy(m_cacheWriteBuffer, pBuffer, pNANDParams->pageDataSize);
        memcpy(m_cacheWriteAuxBuffer, pAuxiliary, pNANDParams->pageMetadataSize);
        
        m_cacheWriteBlock = blockAddress;
        m_cacheWriteBufferedPageOffset = pageOffset;
        m_hasPageInCacheBuffer = true;
    }

    return SUCCESS;
    
#else // PBA_USE_CACHE_WRITE

    return writePageFromBuffer(adjustedPageAddress, eNandProgCmdPageProgram, pBuffer, pAuxiliary);

#endif // PBA_USE_CACHE_WRITE
}

//! \brief Read 4K with Reed-Solomon 8-bit ECC.
__STATIC_TEXT RtStatus_t Type16Nand::readFirmwarePage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    RtStatus_t retval = SUCCESS;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->firmwarePageDataSize);
    _verifyPhysicalContiguity(pAuxiliary, pNANDParams->firmwarePageMetadataSize);
    
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
    // Make sure we're in normal read mode.
    enableFastReadMode(false);
    
    {
        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        XL_SECTOR_TOTAL_SIZE,
                                                        kEccOperationRead);

        // Update the DMA. By providing a valid aux buffer and aux read size, the DMA will use
        // two separate read DMA descriptors.
        m_firmwareReadDma.setAddress(0, adjustPageAddress(uSectorNumber));
        m_firmwareReadDma.setBuffers(pBuffer, pAuxiliary);
        
        // Flush the data cache to ensure that the DMA descriptor chain is in memory.
        hw_core_invalidate_clean_DCache();
        
        // Start the DMA and wait for it to finish.
        retval = m_firmwareReadDma.startAndWait(kNandReadPageTimeout);
        
        // Read ECC status only if the caller asked us to fill in ECC correction info. Since the
        // PBA-NAND has its own internal ECC, we should never see bit errors on our side.
        if (retval == SUCCESS && pECC)
        {
            correctEcc(pBuffer, pAuxiliary, pECC);
        }

        if (retval == SUCCESS)
        {
            // Process the status read result and check for read reclaim and uncorrectable ECC.
            retval = getReadPageStatus();
        }
    }

#if DEBUG
    // Insert a false read error if requested.
   if (g_nand_hal_insertReadError)
   {
       retval = g_nand_hal_insertReadError;
       g_nand_hal_insertReadError = 0;
   }
#endif

    return retval;
}

//! \brief Write 4K with Reed-Solomon 8-bit ECC.
//!
//! This write command is a little special because we use a column change command in the middle
//! of the sequence so we can write the metadata at offset 8192 of the page. Thanks to the
//! chainable DMA descriptors, this can be done in a single DMA.
__STATIC_TEXT RtStatus_t Type16Nand::writeFirmwarePage(uint32_t uSectorNumber, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary)
{
    RtStatus_t rtCode;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->firmwarePageDataSize);
    _verifyPhysicalContiguity(pAuxiliary, pNANDParams->firmwarePageMetadataSize);
    
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    SleepController disableSleep(this);
    flushWriteCacheBuffer();

    uint32_t dataCount;
    uint32_t auxCount;
    uint32_t eccMask = pNANDParams->eccDescriptor.computeMask(
        XL_SECTOR_TOTAL_SIZE,
        XL_SECTOR_TOTAL_SIZE,
        kEccOperationWrite,
        kEccTransferFullPage,
        &dataCount,
        &auxCount);
    
    NandDma::WriteEccData writeDma(
        wChipNumber, // chipSelect,
        eNandProgCmdSerialDataInput, // command1,
        NULL, // addressBytes,
        (pNANDParams->wNumRowBytes + pNANDParams->wNumColumnBytes), // addressByteCount,
        eNandProgCmdPageProgram, // command2,
        pBuffer, // dataBuffer,
        pAuxiliary, // auxBuffer,
        dataCount + auxCount, // sendSize,
        dataCount, // dataSize,
        auxCount, // leftoverSize,
        pNANDParams->eccDescriptor,
        eccMask);
    
    NandDma::Component::CommandAddress columnChangeDma;
    NandDma::Component::SendRawData sendMetadataDma;

    // We assume the PBA-NAND uses two column bytes, as this should never change.
    assert(pNANDParams->wNumColumnBytes == 2);
    
#pragma alignvar(4)
    // Buffer contains command byte and two column address bytes.
    const uint8_t columnChangeBuffer[3] =
        {
            eNandProgCmdRandomDataIn,              // 0x85 column address change command
            (pNANDParams->pageDataSize) & 0xff,         // Block status byte offset LSB
            ((pNANDParams->pageDataSize) >> 8) & 0xff   // Block status byte offset MSB
        };

    // Construct DMAs to send the column address change command and the new column address bytes,
    // and then to send the one block status byte. A separate buffer has to be used for the
    // block status byte because each buffer must be word aligned.
    columnChangeDma.init(wChipNumber, columnChangeBuffer, pNANDParams->wNumColumnBytes);
    sendMetadataDma.init(wChipNumber, pAuxiliary, pNANDParams->pageMetadataSize);
    g_nandHalContext.statusDma.setChipSelect(wChipNumber);
    
    // Modify the DMA descriptor chain. The column address command and block status byte components
    // are inserted after the first 4k of ECC'd data is sent, but before the write-page command.
    // Afterwards comes the status read command.
    writeDma.m_writeData >> columnChangeDma >> sendMetadataDma >> writeDma.m_cle2;
    writeDma.m_wait >> g_nandHalContext.statusDma;
    
    // Set target page address.
    writeDma.setAddress(0, adjustPageAddress(uSectorNumber));
    
    {
        // Enable writes to this NAND for this scope.
        EnableNandWrites enabler(this);

        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        XL_SECTOR_TOTAL_SIZE,
                                                        kEccOperationWrite);



        // Flush data cache and run DMA.
        hw_core_clean_DCache();
        rtCode = writeDma.startAndWait(kNandWritePageTimeout);

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
#if 0
    // Read back the firmware page and verify its contents.
    if (nand::is_read_status_success_or_ecc_fixed(readFirmwarePage(uSectorNumber, tempPageBuffer, (SECTOR_BUFFER *)pAuxiliary, NULL)))
    {
        if (memcmp(tempPageBuffer, pBuffer, XL_SECTOR_DATA_SIZE) != 0)
        {
            tss_logtext_Print(~0, "Type16Nand: firmware page %u failed readback verification!\n", uSectorNumber);
        }
    }
#endif
    }

    // Return.
    return(rtCode);
}

//! \brief Convert PBA-NAND status to abstract status.
//!
//! Toshiba PBA-NAND status bits (0x70):
//! - Bit 0: Chip Status 1 - #kType16StatusPassMask ~= #kNandStatusPassMask
//! - Bit 1: Chip Status 2 - #kType16StatusCachePassMass ~= #kNandStatusCachePreviousPassMask
//! - Bit 2: n/a
//! - Bit 3: n/a
//! - Bit 4: Read Reclaim - #kType16StatusReadReclaimMask = #kNandStatusReadDisturbanceMask
//! - Bit 5: Page Buffer Ready/Busy - #kType16StatusReadyMask = #kNandStatusTrueReadyMask
//! - Bit 6: Data Cache Ready/Busy - #kType16StatusCacheReadyMask = #kNandStatusCacheReadyMask
//! - Bit 7: n/a
//!
//! \param status Status value read from the NAND.
//! \return Abstract status value.
uint32_t Type16Nand::convertStatusToAbstract(uint32_t status)
{
    // Mask off the Status Code and return it.
    // Flip bit 1, Previous Cache Pass/Fail, move into bit 8,
    // flip bit 0,  Pass/Fail
    // Get bits 6 & 5 (Ready/Busy & Cache R/B)
    return (((status & kType16StatusCachePassMask) << 7) ^ kNandStatusCachePreviousPassMask)
        | ((status & kType16StatusPassMask) ^ kType16StatusPassMask)
        | (status & (kType16StatusReadyMask|kType16StatusCacheReadyMask))
        | ((status & kType16StatusReadReclaimMask) << 8);
}

//! The sequence for a multi page read without data cache command:
//!
//!     <60h>-(PgAddr0)-<60h>-(PgAddr1)-<30h>-B2R-<f1h>-[status]-...
//!     ...<00h>-(Col+PgAddr0)-<05h>-(ColAddr0)-<e0h>-[page0data]-...
//!     ...<00h>-(Col+PgAddr1)-<05h>-(ColAddr1)-<e0h>-[page1data]
//!
RtStatus_t Type16Nand::readMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount)
{
    uint32_t planeMask = pNANDParams->wPagesPerBlock;
    uint32_t pageMask = pNANDParams->pageInBlockMask;
    
    // We can only do two blocks at once. If there are not exactly two blocks, or if the
    // blocks are not in different planes, then fall back to the common implementation. Same
    // if they aren't the same page offset within the blocks.
    if (pageCount != 2
        || (pages[0].m_address & planeMask) == (pages[1].m_address & planeMask)
        || (pages[0].m_address & pageMask) != (pages[1].m_address & pageMask))
    {
#if DEBUG
        ++g_smartNandMetrics.multireadFallbackCount;
#endif

        return CommonNandBase::readMultiplePages(pages, pageCount);
    }
    
#if DEBUG
    ++g_smartNandMetrics.multireadCount;
#endif

    NandHalMutex mutexHolder;
    SleepController disableSleep(this);
    flushWriteCacheBuffer();
    
    RtStatus_t status;
    
    // Use the 1st page of the block to calculate the Row address, then adjust as/if necessary.
    uint32_t rowAddress0 = adjustPageAddress(pages[0].m_address);
    uint32_t rowAddress1 = adjustPageAddress(pages[1].m_address);

    // Update address buffers.
    m_multiread.inputPage0Buffer[1] = rowAddress0 & 0xff;
    m_multiread.inputPage0Buffer[2] = (rowAddress0 >> 8) & 0xff;
    m_multiread.inputPage0Buffer[3] = (rowAddress0 >> 16) & 0xff;
    
    m_multiread.inputPage1Buffer[1] = rowAddress1 & 0xff;
    m_multiread.inputPage1Buffer[2] = (rowAddress1 >> 8) & 0xff;
    m_multiread.inputPage1Buffer[3] = (rowAddress1 >> 16) & 0xff;
    
    m_multiread.readColumnPage0Buffer[1] = 0;                  // col byte 0
    m_multiread.readColumnPage0Buffer[2] = 0;                  // col byte 1
    m_multiread.readColumnPage0Buffer[3] = rowAddress0 & 0xff;
    m_multiread.readColumnPage0Buffer[4] = (rowAddress0 >> 8) & 0xff;
    m_multiread.readColumnPage0Buffer[5] = (rowAddress0 >> 16) & 0xff;
    
    m_multiread.readColumnPage1Buffer[1] = 0;                  // col byte 0
    m_multiread.readColumnPage1Buffer[2] = 0;                  // col byte 1
    m_multiread.readColumnPage1Buffer[3] = rowAddress1 & 0xff;
    m_multiread.readColumnPage1Buffer[4] = (rowAddress1 >> 8) & 0xff;
    m_multiread.readColumnPage1Buffer[5] = (rowAddress1 >> 16) & 0xff;

    m_multiread.randomDataCommand0Buffer[1] = 0;    // col byte 0
    m_multiread.randomDataCommand0Buffer[2] = 0;    // col byte 1
    
    m_multiread.randomDataCommand1Buffer[1] = 0;    // col byte 0
    m_multiread.randomDataCommand1Buffer[2] = 0;    // col byte 1

    // Update buffers for the first page.
    m_multiread.receivePageData0Dma.setBufferAndSize(pages[0].m_buffer, pNANDParams->pageDataSize);
    m_multiread.receivePageMetadata0Dma.setBufferAndSize(pages[0].m_auxiliaryBuffer, pNANDParams->pageMetadataSize);
    
    // Update buffers for the second page.
    m_multiread.receivePageData1Dma.setBufferAndSize(pages[1].m_buffer, pNANDParams->pageDataSize);
    m_multiread.receivePageMetadata1Dma.setBufferAndSize(pages[1].m_auxiliaryBuffer, pNANDParams->pageMetadataSize);
    
    // Relink the chain to include the read page data components. Metadata reads will modify
    // the chain to exclude the page data read, so we have to bring it back in.
    m_multiread.finishRandomDataCommand0Dma >> m_multiread.receivePageData0Dma;
    m_multiread.finishRandomDataCommand1Dma >> m_multiread.receivePageData1Dma;

    // Flush the entire data cache before starting the write. Because our buffers are larger
    // than the cache line size, this is faster than walking the buffer a cache line at a time.
    hw_core_invalidate_clean_DCache();

    // Start and wait for the DMA.
    status = m_multiread.multiReadDma.startAndWait(kNandReadPageTimeout);

    if (status == SUCCESS)
    {
        // Determine read status for each block.
        bool isItem0District0 = ((pages[0].m_address & planeMask) == 0);
        fillMultiplaneReadStatus(pages, isItem0District0);
    }
    
    return status;
}

//! The sequence for a multi page read without data cache command:
//!
//!     <60h>-(PgAddr0)-<60h>-(PgAddr1)-<30h>-B2R-<f1h>-[status]-...
//!     ...<00h>-(Col+PgAddr0)-<05h>-(ColAddr0)-<e0h>-[page0data]-...
//!     ...<00h>-(Col+PgAddr1)-<05h>-(ColAddr1)-<e0h>-[page1data]
//!
RtStatus_t Type16Nand::readMultipleMetadata(MultiplaneParamBlock * pages, unsigned pageCount)
{
    uint32_t planeMask = pNANDParams->wPagesPerBlock;
    uint32_t pageMask = pNANDParams->pageInBlockMask;
    
    // We can only do two blocks at once. If there are not exactly two blocks, or if the
    // blocks are not in different planes, then fall back to the common implementation. Same
    // if they aren't the same page offset within the blocks.
    if (pageCount != 2
        || (pages[0].m_address & planeMask) == (pages[1].m_address & planeMask)
        || (pages[0].m_address & pageMask) != (pages[1].m_address & pageMask))
    {
#if DEBUG
        ++g_smartNandMetrics.multireadMetaFallbackCount;
#endif

        return CommonNandBase::readMultipleMetadata(pages, pageCount);
    }
    
#if DEBUG
    ++g_smartNandMetrics.multireadMetaCount;
#endif

    NandHalMutex mutexHolder;
    SleepController disableSleep(this);
    flushWriteCacheBuffer();
    
    RtStatus_t status;
    
    // Use the 1st page of the block to calculate the Row address, then adjust as/if necessary.
    uint32_t rowAddress0 = adjustPageAddress(pages[0].m_address);
    uint32_t rowAddress1 = adjustPageAddress(pages[1].m_address);
    
    uint8_t colByte0 = pNANDParams->pageDataSize & 0xff;
    uint8_t colByte1 = (pNANDParams->pageDataSize >> 8) & 0xff;

    // Update address buffers.
    m_multiread.inputPage0Buffer[1] = rowAddress0 & 0xff;
    m_multiread.inputPage0Buffer[2] = (rowAddress0 >> 8) & 0xff;
    m_multiread.inputPage0Buffer[3] = (rowAddress0 >> 16) & 0xff;
    
    m_multiread.inputPage1Buffer[1] = rowAddress1 & 0xff;
    m_multiread.inputPage1Buffer[2] = (rowAddress1 >> 8) & 0xff;
    m_multiread.inputPage1Buffer[3] = (rowAddress1 >> 16) & 0xff;
    
    m_multiread.readColumnPage0Buffer[1] = colByte0; // col byte 0
    m_multiread.readColumnPage0Buffer[2] = colByte1; // col byte 1
    m_multiread.readColumnPage0Buffer[3] = rowAddress0 & 0xff;
    m_multiread.readColumnPage0Buffer[4] = (rowAddress0 >> 8) & 0xff;
    m_multiread.readColumnPage0Buffer[5] = (rowAddress0 >> 16) & 0xff;
    
    m_multiread.readColumnPage1Buffer[1] = colByte0; // col byte 0
    m_multiread.readColumnPage1Buffer[2] = colByte1; // col byte 1
    m_multiread.readColumnPage1Buffer[3] = rowAddress1 & 0xff;
    m_multiread.readColumnPage1Buffer[4] = (rowAddress1 >> 8) & 0xff;
    m_multiread.readColumnPage1Buffer[5] = (rowAddress1 >> 16) & 0xff;

    m_multiread.randomDataCommand0Buffer[1] = colByte0;    // col byte 0
    m_multiread.randomDataCommand0Buffer[2] = colByte1;    // col byte 1
    
    m_multiread.randomDataCommand1Buffer[1] = colByte0;    // col byte 0
    m_multiread.randomDataCommand1Buffer[2] = colByte1;    // col byte 1

    // Update buffers.
    m_multiread.receivePageMetadata0Dma.setBufferAndSize(pages[0].m_auxiliaryBuffer, pNANDParams->pageMetadataSize);
    m_multiread.receivePageMetadata1Dma.setBufferAndSize(pages[1].m_auxiliaryBuffer, pNANDParams->pageMetadataSize);
    
    // Relink the chain to skip over the read page data components. Regular page reads adjust
    // the chain to include the page data read.
    m_multiread.finishRandomDataCommand0Dma >> m_multiread.receivePageMetadata0Dma;
    m_multiread.finishRandomDataCommand1Dma >> m_multiread.receivePageMetadata1Dma;

    // Flush the entire data cache before starting the write. Because our buffers are larger
    // than the cache line size, this is faster than walking the buffer a cache line at a time.
    hw_core_invalidate_clean_DCache();

    // Start and wait for the DMA.
    status = m_multiread.multiReadDma.startAndWait(kNandReadPageTimeout);

    if (status == SUCCESS)
    {
        // Determine read status for each block.
        bool isItem0District0 = ((pages[0].m_address & planeMask) == 0);
        fillMultiplaneReadStatus(pages, isItem0District0);
    }
    
    return status;
}

//! \brief Fills in read result status for each plane's param block.
//!
//! The param blocks pointed to by \a will have their result status fields set based on
//! the value of the response to a read status command. 
//!
//! For multiplane erase and write commands, the 0xf1 status read command results are:
//!  - bit.0 = chip pass=0/fail=1
//!  - bit.1 = district 0 pass=0/fail=1
//!  - bit.2 = district 1 pass=0/fail=1
//!  - bit.3-5 = invalid
//!  - bit.6 = ready=1/busy=0
//!
//! Thus, bits 1 and 2 are used to determine whether the erase or write succeeded for
//! each district.
//!
//! \param pb Pointer to an array of parameter blocks whose m_resultStatus fields will
//!     be filled in. Exactly two entries are expected to be in the array.
//! \param isItem0District0 A boolean indicating whether the first element of \a pb belongs
//!     to district 0 (true) or district 1 (false). This is used to know which \a pb
//!     entry gets which result status.
//! \pre #g_nandHalResultBuffer is expected to hold the result of a multiplane read status
//!     command (i.e., 0xf1) in its first byte.
void Type16Nand::fillMultiplaneReadStatus(MultiplaneParamBlock * pb, bool isItem0District0)
{
    uint8_t statusByte = g_nandHalResultBuffer[0];
    
    // Determine read status for each plane.
    RtStatus_t district0Status = (statusByte & kType16StatusDistrict0PassMask)
        ? ERROR_DDI_NAND_HAL_ECC_FIX_FAILED
        : ((statusByte & kType16StatusDistrict0ReadReclaimMask)
            ? ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR
            : SUCCESS);
    RtStatus_t district1Status = (statusByte & kType16StatusDistrict1PassMask)
        ? ERROR_DDI_NAND_HAL_ECC_FIX_FAILED
        : ((statusByte & kType16StatusDistrict1ReadReclaimMask)
            ? ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR
            : SUCCESS);
    
    // Fill in results in the correct order, since the caller can provide the two blocks
    // in any order.
    if (isItem0District0)
    {
        pb[0].m_resultStatus = district0Status;
        pb[1].m_resultStatus = district1Status;
    }
    else
    {
        pb[1].m_resultStatus = district0Status;
        pb[0].m_resultStatus = district1Status;
    }
}

RtStatus_t Type16Nand::writeMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount)
{
    uint32_t planeMask = pNANDParams->wPagesPerBlock;
    uint32_t pageMask = pNANDParams->pageInBlockMask;
    
    // We can only do two blocks at once. If there are not exactly two blocks, or if the
    // blocks are not in different planes, then fall back to the common implementation. Same
    // if they aren't the same page offset within the blocks.
    if (pageCount != 2
        || (pages[0].m_address & planeMask) == (pages[1].m_address & planeMask)
        || (pages[0].m_address & pageMask) != (pages[1].m_address & pageMask))
    {
#if DEBUG
        ++g_smartNandMetrics.multiwriteFallbackCount;
#endif

        return CommonNandBase::writeMultiplePages(pages, pageCount);
    }
    
#if DEBUG
    ++g_smartNandMetrics.multiwriteCount;
#endif

    NandHalMutex mutexHolder;
    SleepController disableSleep(this);
    flushWriteCacheBuffer();
    
    RtStatus_t status;
    
    // Enable writes to this NAND for this scope.
    EnableNandWrites enabler(this);

    // Use the 1st page of the block to calculate the Row address, then adjust as/if necessary.
    uint32_t rowAddress1 = adjustPageAddress(pages[0].m_address);
    uint32_t rowAddress2 = adjustPageAddress(pages[1].m_address);

    unsigned addressByteCount = pNANDParams->wNumColumnBytes + pNANDParams->wNumRowBytes;
    
    // Construct multipage program DMA descriptor chain.
    NandDma::WriteRawData writeFirstPage(
        wChipNumber,
        eNandProgCmdSerialDataInput,    // 0x80
        NULL,
        addressByteCount,
        eNandProgCmdMultiPlaneWrite,    // 0x11
        pages[0].m_buffer,
        pNANDParams->pageDataSize,
        pages[0].m_auxiliaryBuffer,
        pNANDParams->pageMetadataSize);
    writeFirstPage.setAddress(0, rowAddress1);

    NandDma::WriteRawData writeSecondPage(
        wChipNumber,
        eNandProgCmdPBAMultiPlaneDataInput, // 0x81
        NULL,
        addressByteCount,
        eNandProgCmdPageProgram,            // 0x10
        pages[1].m_buffer,
        pNANDParams->pageDataSize,
        pages[1].m_auxiliaryBuffer,
        pNANDParams->pageMetadataSize);
    writeSecondPage.setAddress(0, rowAddress2);

    // Construct a status read DMA on the stack.
    NandDma::ReadStatus statusDma(
        wChipNumber,
        eNandProgCmdPBAStatusRead2,  // status command (0xf1)
        g_nandHalResultBuffer);      // where to put the resulting status byte
    
    // Build the full DMA descriptor chain.
    writeFirstPage >> writeSecondPage >> statusDma;

    // Flush the entire data cache before starting the write. Because our buffers are larger
    // than the cache line size, this is faster than walking the buffer a cache line at a time.
    // Also, note that we do not need to invalidate for writes.
    hw_core_clean_DCache();
    
    // Start and wait for the DMA.
    status = writeFirstPage.startAndWait(kNandWritePageTimeout);

    if (status == SUCCESS)
    {
        // Determine pass/fail status for each page.
        bool isItem0District0 = ((pages[0].m_address & planeMask) == 0);
        fillMultiplaneWriteStatus(pages, isItem0District0);
    }
    
    return status;
}

RtStatus_t Type16Nand::eraseMultipleBlocks(MultiplaneParamBlock * blocks, unsigned blockCount)
{
    RtStatus_t ret;

    // We can only do two blocks at once. If there are not exactly two blocks, or if the
    // blocks are not in different planes, then fall back to the common implementation.
    if (blockCount != 2 || (blocks[0].m_address & 1) == (blocks[1].m_address & 1))
    {
#if DEBUG
        ++g_smartNandMetrics.multiEraseFallbackCount;
#endif

        return CommonNandBase::eraseMultipleBlocks(blocks, blockCount);
    }

#if DEBUG
    ++g_smartNandMetrics.multiEraseCount;
#endif

    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    SleepController disableSleep(this);
    flushWriteCacheBuffer();
    
    // Enable writes to this NAND for this scope.
    EnableNandWrites enabler(this);

    // Use the 1st page of the block to calculate the Row address, then adjust as/if necessary.
    uint32_t wRowAddr = adjustPageAddress(blockToPage(blocks[0].m_address));
    uint32_t wRowAddr2 = adjustPageAddress(blockToPage(blocks[1].m_address));

    // Build the multi-erase DMA descriptor chain here on the stack.
    NandDma::MultiBlockErase eraseDma(
        wChipNumber,
        eNandProgCmdBlockErase,         // first and second command byte (0x60)
        wRowAddr,                       // first row address
        wRowAddr2,                      // second row address
        pNANDParams->wNumRowBytes,      // number of bytes to send for each row
        eNandProgCmdBlockErase_2ndCycle);    // post-address command (0xd0)

    // Construct a status read DMA on the stack.
    NandDma::ReadStatus statusDma(
        wChipNumber,
        eNandProgCmdPBAStatusRead2,  // status command (0xf1)
        g_nandHalResultBuffer);      // where to put the resulting status byte
    
    // Chain the status read DMA onto the multierase DMA.
    eraseDma >> statusDma;
    
    // Flush data cache.
    hw_core_clean_DCache();

    // Initiate DMA and wait for its completion.
    ret = eraseDma.startAndWait(kNandEraseBlockTimeout);

    if (ret == SUCCESS)
    {
        // Determine pass/fail status for each block.
        bool isItem0District0 = ((blocks[0].m_address & 1) == 0);
        fillMultiplaneWriteStatus(blocks, isItem0District0);
    }

    return ret;
}

//! \brief Fills in write result status for each plane's param block.
//!
//! The param blocks pointed to by \a will have their result status fields set based on
//! the value of the response to a read status command. If the write or erase operation
//! succeeded for a district then its result status will be #SUCCESS, otherwise it
//! will be #ERROR_DDI_NAND_HAL_WRITE_FAILED.
//!
//! For multiplane erase and write commands, the 0xf1 status read command results are:
//!  - bit.0 = chip pass=0/fail=1
//!  - bit.1 = district 0 pass=0/fail=1
//!  - bit.2 = district 1 pass=0/fail=1
//!  - bit.3-5 = invalid
//!  - bit.6 = ready=1/busy=0
//!
//! Thus, bits 1 and 2 are used to determine whether the erase or write succeeded for
//! each district.
//!
//! \param pb Pointer to an array of parameter blocks whose m_resultStatus fields will
//!     be filled in. Exactly two entries are expected to be in the array.
//! \param isItem0District0 A boolean indicating whether the first element of \a pb belongs
//!     to district 0 (true) or district 1 (false). This is used to know which \a pb
//!     entry gets which result status.
//! \pre #g_nandHalResultBuffer is expected to hold the result of a multiplane read status
//!     command (i.e., 0xf1) in its first byte.
void Type16Nand::fillMultiplaneWriteStatus(MultiplaneParamBlock * pb, bool isItem0District0)
{
    // Get status value.
    uint8_t status = g_nandHalResultBuffer[0];
    
    // Determine pass/fail status for each block.
    RtStatus_t district0Status = (status & kType16StatusDistrict0PassMask) ? ERROR_DDI_NAND_HAL_WRITE_FAILED : SUCCESS;
    RtStatus_t district1Status = (status & kType16StatusDistrict1PassMask) ? ERROR_DDI_NAND_HAL_WRITE_FAILED : SUCCESS;
    
    // Fill in results in the correct order, since the caller can provide the two blocks
    // in any order.
    if (isItem0District0)
    {
        pb[0].m_resultStatus = district0Status;
        pb[1].m_resultStatus = district1Status;
    }
    else
    {
        pb[1].m_resultStatus = district0Status;
        pb[0].m_resultStatus = district1Status;
    }
}

RtStatus_t Type16Nand::reset()
{
    NandHalMutex mutexHolder;

#if PBA_USE_CACHE_WRITE
    // If we've already started a cache write sequence, then we should terminate it before
    // resetting. Otherwise we can just leave the page in our buffer.
    if (m_isInCacheWrite)
    {
        SleepController disableSleep(this);
        flushWriteCacheBuffer();
    }
#endif
    
    RtStatus_t status = Type11Nand::reset();
    
    // Return to sleep mode if it was enabled.
    if (m_isSleepEnabled)
    {
        setSleepMode(true);
    }
    
    return status;
}

RtStatus_t Type16Nand::readID(uint8_t * pReadIDCode)
{
    NandHalMutex mutexHolder;

    if (m_isSleepEnabled)
    {
        setSleepMode(false);
    }
    
    RtStatus_t status = Type11Nand::readID(pReadIDCode);

    if (m_isSleepEnabled)
    {
        setSleepMode(true);
    }
    
    return status;
}

RtStatus_t Type16Nand::eraseBlock(uint32_t uBlockNumber)
{
    NandHalMutex mutexHolder;
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
#if DEBUG
    ++g_smartNandMetrics.singleEraseCount;
#endif

    return Type11Nand::eraseBlock(uBlockNumber);
}

//! The standard bad block scanning implementation in CommonNandBase is used, but it is wrapped
//! so that the NAND is kept out of sleep mode for the whole scan. Also, we never need to enable
//! \a checkFactoryMarkings for PBA since the factory block status byte location is always used.
bool Type16Nand::isBlockBad(uint32_t blockAddress, SECTOR_BUFFER * auxBuffer, bool checkFactoryMarkings, RtStatus_t * readStatus)
{
    NandHal::SleepHelper disableSleep(false);
    
    flushWriteCacheBuffer();
    
    // Make sure we're in normal read mode.
    enableFastReadMode(false);
    
    // Force checkFactoryMarkings to false.
    return Type11Nand::isBlockBad(blockAddress, auxBuffer, false /*checkFactoryMarkings*/, readStatus);
}

//! Auto sleep is disabled while we copy pages from one block to another. No reason to be sending
//! a ton of sleep enable and disable commands when we know we're going to be doing a number of
//! reads and writes in sequence.
RtStatus_t Type16Nand::copyPages(NandPhysicalMedia * targetNand, uint32_t uSourceStartSectorNum, uint32_t uTargetStartSectorNum, uint32_t wNumSectors, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, NandCopyPagesFilter * filter, uint32_t * successfulPages)
{
    RtStatus_t status;

#if PBA_MOVE_PAGE
    uint32_t planeMask = pNANDParams->wPagesPerBlock;
    // Check if we are doing operation in same district or plane
    //          and pages are part of same NAND chip
    // For 2 plane nands following condition is sufficient    
    if ( (uSourceStartSectorNum & planeMask) == (uTargetStartSectorNum & planeMask) &&
        this->wChipNumber == targetNand->wChipNumber)    
    {
        // A more accurate condition for more than 2 plane nand is
        // planeMask = pNANDParams->planesPerDie;
        //if ( ( (uSourceStartSectorNum >> pNANDParams->pageToBlockShift ) & planeMask) 
        //  == ( (uTargetStartSectorNum  >> pNANDParams->pageToBlockShift ) & planeMask) && 
        //  planeMask != 0 )
    
        // Note prior to calling this API auxiliary data must be initialized otherwise 
        // buildMap will be corrupted and eventually datadrive will get corrupted.
        *successfulPages = 0;
        status = movePage(uSourceStartSectorNum, uTargetStartSectorNum,auxBuffer);
        if (status == SUCCESS)
            *successfulPages = 1;
        return status;
    }
#endif

    // We can't hold the HAL mutex while copying pages because we can call back into pageable
    // code when calling the filter object.
    NandHal::SleepHelper disableSleep(false);
    flushWriteCacheBuffer();

    return Type11Nand::copyPages(targetNand, uSourceStartSectorNum, uTargetStartSectorNum, wNumSectors, sectorBuffer, auxBuffer, filter, successfulPages);
}

RtStatus_t Type16Nand::enableSleep(bool isEnabled)
{
    // Sleep mode management is not required on the 24nm generation.
    if (m_chipGeneration == k24nm)
    {
        return SUCCESS;
    }
    
    if (m_isSleepEnabled == isEnabled)
    {
        return SUCCESS;
    }
    
    // Change sleep mode in all chips.
    unsigned i;
    for (i = 0; i < NandHal::getChipSelectCount(); ++i)
    {
        Type16Nand * nand = reinterpret_cast<Type16Nand *>(NandHal::getNand(i));
        
        // Set sleep mode to the default for the new state.
        if (m_isAsleep != isEnabled)
        {
            nand->setSleepMode(isEnabled);
        }
        
        // Turn on or off auto sleep mode.
        nand->m_isSleepEnabled = isEnabled;
    }
    
    return SUCCESS;
}

bool Type16Nand::isSleepEnabled()
{
    return m_isSleepEnabled;
}

RtStatus_t Type16Nand::setSleepMode(bool isEnabled)
{
    // Sleep mode management is not required on the 24nm generation.
    if (m_chipGeneration == k24nm)
    {
        return SUCCESS;
    }
    
    NandHalMutex mutexHolder;
    
    // Send the sleep enable or disable command.
    RtStatus_t status = changeMode(isEnabled ? eNandProgCmdPBAEnableSleepMode : eNandProgCmdPBADisableSleepMode);
    
    // Verify that the sleep mode was set as expected.
#if DEBUG && PBA_VERIFY_SLEEP_MODE
    if (status == SUCCESS)
    {
        uint8_t sleepModeState;
        status = getSleepModeState(&sleepModeState);
        
        if (status == SUCCESS)
        {
            if (sleepModeState != (uint8_t)isEnabled)
            {
                tss_logtext_Print(~0, "Type16Nand: sleep mode state did not change as expected (is=%u, expected=%u)\n", (unsigned)sleepModeState, (unsigned)isEnabled);
            }
        }
    }
#endif // DEBUG && PBA_VERIFY_SLEEP_MODE

    // Save the current sleep state.
    if (status == SUCCESS)
    {
        m_isAsleep = isEnabled;
    }
    
    return status;
}

RtStatus_t Type16Nand::getSleepModeState(uint8_t * isEnabled)
{
    // Set the command to read the sleep state.
    uint8_t addressBytes[5] = { eNandProgCmdPBACheckSleepModeState, 0 };
    
    // Build the DMA chain so that it reads back one single byte.
    NandDma::ReadRawData dma(
        wChipNumber,            // chipSelect
        eNandProgCmdRead1,      // command1 (0x00)
        addressBytes,           // addressBytes
        sizeof(addressBytes),   // addressByteCount
        eNandProgCmdPBAModeChange,  // command2 (0x57)
        g_nandHalResultBuffer,         // dataBuffer
        1,                      // dataReadSize
        NULL,                   // auxBuffer
        0);                     // auxReadSize

    // Flush the data cache to ensure that the DMA descriptor chain is in memory.
    hw_core_invalidate_clean_DCache();
    
    // Start the DMA and wait for it to complete.
    RtStatus_t status = dma.startAndWait(kNandReadPageTimeout);
    
    // Fill in return value.
    if (status == SUCCESS && isEnabled)
    {
        *isEnabled = g_nandHalResultBuffer[0];
    }
    
    return status;
}

//! The mode change type commands all use a structure similar to a read command. The first
//! command byte is 0x00, followed by five address bytes, and terminated with a final 0x57
//! command byte. The actual mode command is held in the first address byte. All other four
//! address bytes are ignored by the device.
//!
//! There is a prebuilt DMA chain for each chip enable, which enables the mode change command
//! to be sent as fast as possible.
RtStatus_t Type16Nand::changeMode(uint8_t modeByte)
{
    // Construct address byte array. All but the first address byte is ignored by the PBA-NAND.
    uint8_t addressBytes[5] = { modeByte, 0 };

    // Set the mode change subcommand value. For some strange reason, using an address byte array
    // that is a member variable doesn't work at all.
    m_modeDma.setAddress(addressBytes);
    
    // Flush the data cache to ensure that the DMA descriptor chain is in memory.
    hw_core_clean_DCache();
    
    // Start the DMA and wait for it to complete.
    return m_modeDma.startAndWait(kNandReadPageTimeout);
}

//! \post The member variable #m_isInFastReadMode is updated to track the current read mode
//!     of the NAND.
RtStatus_t Type16Nand::enableFastReadMode(bool isEnabled)
{
#if PBA_USE_READ_MODE_2
    if (isEnabled != m_isInFastReadMode)
    {
        // Switch to the new read mode.
        changeMode(isEnabled ? eNandProgCmdPBAReadMode2 : eNandProgCmdPBAReadMode1);
        
        m_isInFastReadMode = isEnabled;
    }
#endif // PBA_USE_READ_MODE_2
    
    return SUCCESS;
}

//! Toshiba PBA-NANDs have holes in their address spaces after each internal die. This
//! function converts a linear page address into an address that skips over the holes. We also skip
//! over the extended blocks since we don't use them; the NAND driver requires that
//! block and page counts are powers of 2.
//!
//! Actual address ranges for one chip enable with two dice (32nm):
//!  - 0x000000 - 0x07ffff : 4096 blocks
//!  - 0x080000 - 0x0819ff : 52 extended blocks
//!  - 0x081a00 - 0x0fffff : "chip gap"
//!  - 0x100000 - 0x17ffff : 4096 blocks
//!  - 0x180000 - 0x1819ff : 52 extended blocks
//!  - 0x181a00 - 0x1fffff : "chip gap"
//!
//! Actual address ranges for one chip enable with two dice (24nm):
//!  - 0x000000 - 0x7fffff : 4096 blocks
//!  - 0x100000 - 0x101bff : 28 extended blocks
//!  - 0x101c00 - 0x1fffff : "chip gap"
//!  - 0x200000 - 0x2fffff : 4096 blocks
//!  - 0x300000 - 0x301bff : 28 extended blocks
//!  - 0x301c00 - 0x3fffff : "chip gap"
__STATIC_TEXT uint32_t Type16Nand::adjustPageAddress(uint32_t pageAddress)
{
    // For the 32nm generation of PBA-NANDs, we use the same adjustment algorithm as for
    // Type 11 Toshiba NANDs. Same applies to 4GB 24nm device. Since the 24nm generation has
    //! 256 pages per block (except the 4GB), the adjustment differs somewhat.
    if (m_chipGeneration == k32nm || m_is4GB)
    {
        return Type11Nand::adjustPageAddress(pageAddress);
    }
    
    const uint32_t kOneDieLinearPageCount = 0x100000;    // 4096 blocks at 256 pages per block.
    const uint32_t kOneDieActualPageCount = 0x200000;   // Address range of each die per chip enable.
    
    uint32_t result = pageAddress;
    
    // Is this address beyond the first 4096 linear blocks?
    if (pageAddress >= kOneDieLinearPageCount)
    {
        // Page 0x80000 becomes page 0x80000.
        // Page 0x81000 becomes page 0x81000
        // Page 0xfffff becomes page 0xfffff.
        // Page 0x165000 becomes page 0x265000.
        unsigned internalDieNumber = pageAddress / kOneDieLinearPageCount;
        unsigned internalDiePageOffset = pageAddress % kOneDieLinearPageCount;
        result = kOneDieActualPageCount * internalDieNumber + internalDiePageOffset;
    }
    
    return result;
}


#if PBA_MOVE_PAGE
// This is a general copy back sequence specified on page 75 of datasheet.
RtStatus_t Type16Nand::movePage(
    uint32_t uSectorNumber, 
    uint32_t uTargetStartSectorNum, 
    SECTOR_BUFFER * auxBuffer)
{
    RtStatus_t status = SUCCESS;

    _verifyPhysicalContiguity(auxBuffer, pNANDParams->pageMetadataSize);

    NandHalMutex mutexHolder;

#if DEBUG
    ++g_smartNandMetrics.singleMoveCount;
#endif
    
    uint32_t adjustedPageAddress = adjustPageAddress(uSectorNumber);
    uint32_t adjustedTargetStartSectorNum = adjustPageAddress(uTargetStartSectorNum);
    
    // Wrap sleep disable/enable commands around the read.
    SleepController disableSleep(this);
    
    flushWriteCacheBuffer();
    
    // Make sure we're in fast read mode.
    enableFastReadMode(false);
    
    // Update the DMA. By providing a valid aux buffer and aux read size, the DMA will use
    // two separate read DMA descriptors.
    m_movePage.sourcePageReadDma.setAddress(0, adjustedPageAddress);
    //m_movePage.sourcePageReadDma.setBuffers(pageBuffer, 0, auxBuffer, 0);
    m_movePage.sourcePageReadDma.setBuffers(NULL, 0, NULL, 0);

    // Chain to fetch page into controller
    m_movePage.sourcePageReadDma.m_wait >> m_movePage.PageStatusDma;

    // Flush the entire data cache before starting the write. Because our buffers are larger
    // than the cache line size, this is faster than walking the buffer a cache line at a time.
    // Also, note that we do not need to invalidate for writes.
    hw_core_clean_DCache();

    // Start the DMA and wait for it to finish.
    status = m_movePage.sourcePageReadDma.startAndWait(kNandReadPageTimeout);

    if (status == SUCCESS)
    {
        // Process the status read result and check for read reclaim and uncorrectable ECC.
        status = getReadPageStatus();
    }
    if ( status != SUCCESS )
    {
        return status;
    }

    // Write the loaded page to other block in same die.
    EnableNandWrites enabler(this); 
    
    // Update the DMA.
    m_pageWriteDma.setCommands(eNandProgCmdCopyBackProgram, eNandProgCmdPageProgram);
    m_movePage.targetPageWriteDma.setAddress(pNANDParams->pageDataSize, adjustedTargetStartSectorNum);
    // If there is change in metadata update, otherwise just copy whole thing.
    if ( auxBuffer == NULL )
        m_movePage.targetPageWriteDma.setBuffers(NULL, 0, NULL, 0);
    else
        m_movePage.targetPageWriteDma.setBuffers(auxBuffer, pNANDParams->pageMetadataSize, NULL, 0);
    
    m_movePage.targetPageWriteDma >> 
    m_movePage.targetPageWriteDma.m_wait >>  
    m_movePage.PageStatusDma >> 
    m_movePage.targetPageWriteDma.m_done;

    hw_core_clean_DCache();

    // Start the DMA and wait for it to finish.
    status = m_movePage.targetPageWriteDma.startAndWait(kNandReadPageTimeout);

    if (status == SUCCESS)
    {
        if (checkStatus(g_nandHalResultBuffer[0], kNandStatusPassMask, NULL) != SUCCESS)
        {
            status = ERROR_DDI_NAND_HAL_WRITE_FAILED;
        }
    }
    return status;
}

void Type16Nand::buildMovePageDma()
{
    const unsigned addressByteCount = pNANDParams->wNumColumnBytes + pNANDParams->wNumRowBytes;

    // Initialize the DMA descriptor for read page operation
    m_movePage.sourcePageReadDma.init(
        wChipNumber,                    // chipSelect
        eNandProgCmdRead1,              // command1 (0x00)
        NULL,                           // addressBytes
        addressByteCount,               // addressByteCount
        eNandProgCmdReadForCopyBack_2ndCycle,  // command2 (0x35)
        NULL,                           // dataBuffer
        0,                              // dataReadSize
        NULL,                           // auxBuffer
        0);                             // auxReadSize
    // Initialize the status read sequence
    m_movePage.PageStatusDma.init(wChipNumber, eNandProgCmdReadStatus, g_nandHalResultBuffer);
    m_movePage.sourcePageReadDma >> m_movePage.PageStatusDma;

    m_movePage.targetPageWriteDma.init(
        wChipNumber,            // chipSelect
        eNandProgCmdCopyBackProgram,      // command1 (0x85)
        NULL,           // addressBytes
        addressByteCount,   // addressByteCount
        eNandProgCmdPageProgram,  // command2 (0x10)
        NULL,                   // dataBuffer
        0,                      // dataReadSize
        NULL,                   // auxBuffer
        0);                     // auxReadSize

    m_movePage.targetPageWriteDma >> 
    m_movePage.targetPageWriteDma.m_wait >>  
    m_movePage.PageStatusDma >> 
    m_movePage.targetPageWriteDma.m_done;

}
#endif 

#endif // !37xx

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
