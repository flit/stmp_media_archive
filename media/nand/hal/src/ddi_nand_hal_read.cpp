////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal_internals
//! @{
//!
//  Copyright (c) 2005-2008 SigmaTel, Inc.
//!
//! \file    ddi_nand_hal_read.cpp
//! \brief   Common NAND HAL Read Functions
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "string.h"
#include "drivers/media/ddi_media.h"
#include "components/telemetry/tss_logtext.h"
#include "ddi_nand_hal_internal.h"
#include "hw/core/mmu.h"
#include "components/profile/cmp_profile.h"
#include "onfi_param_page.h"
#include "auto_free.h"
#include "os/dmi/os_dmi_api.h"
#include "ddi_nand_hal_tables.h"

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#if DEBUG
//! This global is used to insert read errors for testing purposes while
//! debugging. Set it to the error code you want to be returned from the
//! next HAL read function call. After that error is returned once, this global
//! will be reset to 0. As long as the value is zero, this global has no
//! effect whatsoever.
RtStatus_t g_nand_hal_insertReadError = 0;
#endif // DEBUG

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Read the ID of the NAND.
//!
//! Read a 6-byte ID from the chip.  Not all 6 bytes are supported by all
//! manufacturers, but we'll work with what we have.
//!
//! \param[in]  pReadIDCode Pointer to write the Read ID structure.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_DMA_TIMEOUT
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t CommonNandBase::readID(uint8_t * pReadIDCode)
{
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    NandDma::ReadId readIdDma(wChipNumber, eNandProgCmdReadID, 0, pReadIDCode);

    hw_core_invalidate_clean_DCache();

    return readIdDma.startAndWait(kNandResetTimeout);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Check if the NAND reports itself as an ONFI NAND.
//!
//! The Read ID command is used to read ID address 0x20 from the NAND. If the
//! NAND supports ONFI then the first 4 bytes returned will be "ONFI".
//!
//! \retval true The NAND is an ONFI NAND.
//! \retval false The NAND is not ONFI.
//!
//! \todo Read only 4 bytes instead of the 6 that the DMA class automatically
//!     uses (with no way to override).
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT bool CommonNandBase::checkOnfiID()
{
    // Lock the HAL.
    NandHalMutex mutexHolder;
    
    // Create the Read ID DMA for address 0x20.
    NandDma::ReadId readIdDma(wChipNumber, eNandProgCmdReadID, kOnfiReadIDAddress, g_nandHalResultBuffer);

    hw_core_invalidate_clean_DCache();

    RtStatus_t retCode = readIdDma.startAndWait(kNandResetTimeout);

    bool hasOnfiId = false;
    if (retCode == SUCCESS)
    {
        // Convert the result bytes into a word.
        uint32_t * idCode = (uint32_t *)&g_nandHalResultBuffer[0];
        hasOnfiId = (*idCode == kOnfiSignature);
    }

    return hasOnfiId;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Reads out the parameter page from an ONFI NAND.
//!
//! If the isOnfi() does not return true, then the results of this operation
//! are unpredictable.
//!
//! \param paramPage Pointer to the buffer where the parameter page will be
//!     written. Must be physically contiguous in memory, and must be at least
//!     256 bytes in size.
//! \retval SUCCESS The param page was read successfully and both the signature
//!     and CRC are correct.
//! \retval ERROR_DDI_NAND_HAL_INVALID_ONFI_PARAM_PAGE Either the signature
//!     or CRC of the param page is invalid.
//! \todo Read redundant copies until a valid copy of the parameter page is read,
//!     as described in the ONFI spec.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t CommonNandBase::readOnfiParameterPage(OnfiParamPage * paramPage)
{
    // Lock the HAL.
    NandHalMutex mutexHolder;
    
    // Create DMA descriptor components.
    NandDma::Component::CommandAddress commandDma;
    NandDma::Component::WaitForReady waitDma;
    NandDma::Component::ReceiveRawData readDma;
    NandDma::Component::Terminator terminatorDma;
    
    // This word aligned buffer contains both the command code to read the param page and
    // the address byte send with the command.
    uint8_t commandAddressBuffer[4] __attribute__((aligned(4))) = { eNandProgCmdReadONFIParamPage, 0x00 };
    
    // Init DMA components.
    commandDma.init(wChipNumber, commandAddressBuffer, 1); // One address byte
    waitDma.init(wChipNumber, &terminatorDma);
    readDma.init(wChipNumber, paramPage, sizeof(OnfiParamPage));
    terminatorDma.init();
    
    // Chain the DMA components together.
    commandDma >> waitDma >> readDma >> terminatorDma;
    
    // Wrap up the DMA sequence in an object. This same wrapper object is used for reading
    // additional copies of the param page by changing the DMA start descriptor to readDma.
    NandDma::WrappedSequence dma(wChipNumber, commandDma);
    
    unsigned i = 0;
    while (i++ < kMinOnfiParamPageCopies)
    {
        // Clear the CPU data cache, execute the DMA, and wait for it to complete.
        hw_core_invalidate_clean_DCache();
        RtStatus_t status = dma.startAndWait(kNandResetTimeout);
        
        if (status != SUCCESS)
        {
            // We got some DMA related error, so don't try to read other copies.
            return status;
        }

        // Check signature and param page CRC.
        if (paramPage->signature == kOnfiSignature && paramPage->computeCRC() == paramPage->crc)
        {
            return SUCCESS;
        }
        
        // Read the next copy of the param page. All we need to do is read out another
        // 256 bytes worth of data, so we can reuse the readDma object which is already
        // linked to the terminator.
        dma.setDmaStart(readDma);
    }
    
    return ERROR_DDI_NAND_HAL_INVALID_ONFI_PARAM_PAGE;
}

////////////////////////////////////////////////////////////////////////////////
//! \copydoc NandPhysicalMedia::getDeviceName()
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT char * CommonNandBase::getDeviceName()
{
    // We only support this feature for ONFI NANDs.
    if (pNANDParams->isONFI)
    {
        auto_free<OnfiParamPage> paramPage = os_dmi_malloc_phys_contiguous(sizeof(OnfiParamPage));
        if (paramPage && readOnfiParameterPage(paramPage) == SUCCESS)
        {
            char * nameString = reinterpret_cast<char *>(malloc(OnfiParamPage::kModelNameLength + 1));
            paramPage->copyModelName(nameString);
            return nameString;
        }
    }
    else if (g_nandHalContext.nameTable)
    {
        // The NAND is non-ONFI but a name table is available.
        NandDeviceNameTable table(g_nandHalContext.nameTable);
        return table.getNameForChipSelectCount(g_nandHalContext.chipSelectCount);
    }
    
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Send Reset command to the NAND.
//!
//! Send a RESET command to the specified NAND.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_RESET_FAILED
//!
//! \note Currently, all NAND use the same reset code.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t CommonNandBase::reset()
{
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    // This DMA descriptor chain is small, so there is no reason to not just put it on the stack.
    NandDma::Reset resetDma(wChipNumber, eNandProgCmdReset);

    // Flush cache and kick it off.
    hw_core_clean_DCache();
    RtStatus_t retCode = resetDma.startAndWait(kNandResetTimeout);

    if (retCode != SUCCESS)
    {
        retCode = ERROR_DDI_NAND_RESET_FAILED;
    }

    // Return.
    return(retCode);
}

////////////////////////////////////////////////////////////////////////////////
// See the type definition for NandPhysicalMedia::readPage() in ddi_nand_hal.h for
// details on this function. Don't tss print here due to known stack limits.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::readPage(
    uint32_t                            uSectorNum,
    SECTOR_BUFFER *                     pBuffer,
    SECTOR_BUFFER *                     pAuxiliary,
    NandEccCorrectionInfo_t *           pECC
    )
{
    RtStatus_t retval;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->pageDataSize);
    _verifyPhysicalContiguity(pAuxiliary, pNANDParams->pageMetadataSize);
    
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    // Update the DMA. By providing a valid aux buffer and aux read size, the DMA will use
    // two separate read DMA descriptors.
    g_nandHalContext.readDma.setChipSelect(wChipNumber);
    g_nandHalContext.readDma.setAddress(0, adjustPageAddress(uSectorNum));
    g_nandHalContext.readDma.setBuffers(pBuffer, pAuxiliary);
    
    {
        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationRead);
        
        // Flush the data cache to ensure that the DMA descriptor chain is in memory.
        hw_core_invalidate_clean_DCache();

        // Start the DMA and wait for it to finish.
        retval = g_nandHalContext.readDma.startAndWait(kNandReadPageTimeout);

        if (retval == SUCCESS)
        {
            // Check the ECC results.
            retval = correctEcc(pBuffer, pAuxiliary, pECC);
        }
    }

    // Return.

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

////////////////////////////////////////////////////////////////////////////////
// See the type definition for NandHalReadMetadataFunction_t in
// ddi_nand_hal.h for details on this function.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::readMetadata(
    uint32_t                            uSectorNum,
    SECTOR_BUFFER *                     pBuffer,
    NandEccCorrectionInfo_t *           pECC
    )
{
    RtStatus_t retval;
    uint32_t readOffset;
    uint32_t readSize;
    SECTOR_BUFFER *pAuxBuffer = pBuffer;
    SECTOR_BUFFER *pDataBuffer = pBuffer;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->pageMetadataSize);

    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;

    // Compute the offset and size of the metadata read.
    const EccTypeInfo_t * eccInfo = pNANDParams->eccDescriptor.getTypeInfo();
    assert(eccInfo);
    
    // Get the offset and length of the metadata for this page size and ECC type.
    retval = eccInfo->getMetadataInfo(pNANDParams->pageDataSize, &readOffset, &readSize);
    if (retval != SUCCESS)
    {
        return retval;
    }

#if defined(STMP378x)
    // Use our preallocated buffer to hold the first ECC chunk for BCH.
    if (pNANDParams->eccDescriptor.isBCH())
    {
        pDataBuffer = (SECTOR_BUFFER *)m_pMetadataBuffer;
    }
#endif

    // Update DMA descriptor.
    g_nandHalContext.readMetadataDma.setChipSelect(wChipNumber);
    g_nandHalContext.readMetadataDma.setAddress(readOffset, adjustPageAddress(uSectorNum));
    g_nandHalContext.readMetadataDma.setBuffers(pDataBuffer, pAuxBuffer);

    {
        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationRead);
        
        hw_core_invalidate_clean_DCache();
        retval = g_nandHalContext.readMetadataDma.startAndWait(kNandReadPageTimeout);

        if (retval == SUCCESS)
        {
            retval = correctEcc(pBuffer, pBuffer, pECC);
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

////////////////////////////////////////////////////////////////////////////////
//! \brief      Read data from a page without correcting ECC.
//!
//! This is the common function used to read any number of bytes from any
//! location on the NAND page. ECC correction is disabled.
//!
//! \param[in]  wSectorNum Sector number to read.
//! \param[in] columnOffset Offset of first byte to read from the page.
//! \param[in] readByteCount Number of bytes to read, starting from \a columnOffset.
//! \param[in]  pBuf Pointer to buffer to fill with result.
//!
//! \retval     SUCCESS
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::readRawData(uint32_t wSectorNum, uint32_t columnOffset, uint32_t readByteCount, SECTOR_BUFFER * pBuf)
{
    NandHalMutex mutexHolder;

    _verifyPhysicalContiguity(pBuf, readByteCount);
    
    // Create the DMA descriptor on the stack, since raw reads are pretty rare.
    NandDma::ReadRawData rawReadDma(
        wChipNumber, // chipSelect,
        eNandProgCmdRead1, // command1,
        NULL, // addressBytes,
        (pNANDParams->wNumRowBytes + pNANDParams->wNumColumnBytes), // addressByteCount,
        eNandProgCmdRead1_2ndCycle, // command2,
        NULL, //pBuf, // dataBuffer,
        0, //readByteCount, // dataReadSize,
        NULL, // auxBuffer,
        0); // auxReadSize);
    rawReadDma.setAddress(columnOffset, adjustPageAddress(wSectorNum));
    rawReadDma.setBuffers(pBuf, readByteCount, NULL, 0);
    
    // Flush the data cache to ensure that the DMA descriptor chain is in memory.
    hw_core_invalidate_clean_DCache();

    return rawReadDma.startAndWait(kNandReadPageTimeout);
}

RtStatus_t CommonNandBase::readFirmwarePage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    // Just use a normal page read if firmware pages are normal sized (mostly ECC8).
    if (!pNANDParams->hasSmallFirmwarePages)
    {
        return readPage(uSectorNumber, pBuffer, pAuxiliary, pECC);
    }

    _verifyPhysicalContiguity(pBuffer, pNANDParams->firmwarePageDataSize);
    _verifyPhysicalContiguity(pAuxiliary, pNANDParams->firmwarePageMetadataSize);
    
    // By default, the only "small" firmware page size we support is 2K, when using BCH.
    assert(pNANDParams->firmwarePageDataSize == 2048);
    
    // When using BCH, we store only 2K worth of data in firmware pages because the
    // ROM cannot always get to other 2K subpages. So, we want to read only 2k.
    RtStatus_t retval;
    
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;

    // Update the DMA. By providing a valid aux buffer and aux read size, the DMA will use
    // two separate read DMA descriptors.
    g_nandHalContext.readFirmwareDma.setChipSelect(wChipNumber);
    g_nandHalContext.readFirmwareDma.setAddress(0, adjustPageAddress(uSectorNumber));
    g_nandHalContext.readFirmwareDma.setBuffers(pBuffer, pAuxiliary);
    
    {
        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationRead,
                                                        kEccTransfer2kPage);
        
        // Flush the data cache to ensure that the DMA descriptor chain is in memory.
        hw_core_invalidate_clean_DCache();
        
        // Start the DMA and wait for it to finish.
        retval = g_nandHalContext.readFirmwareDma.startAndWait(kNandReadPageTimeout);

        if (retval == SUCCESS)
        {
            // Check the ECC results.
            retval = correctEcc(pBuffer, pAuxiliary, pECC);
        }
    }

    // Return.

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

////////////////////////////////////////////////////////////////////////////////
//! \brief Read correction information from the ECC driver.
//!
//! \param[in]  pBuffer     The buffer of interest. Currently ignored.
//! \param[in]  pAuxBuffer  The auxiliary buffer of interest. Used only for BCH ECC.
//! \param[in] correctionInfo Optional results of the correction.
//!
//! \retval SUCCESS                             There were no errors.
//! \retval ERROR_DDI_NAND_HAL_ECC_FIXED        Errors were detected and
//!                                             fixed.
//! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED   There were uncorrectable
//!                                                 errors.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::correctEcc(SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo)
{
    // Pass-through to the abstract ECC correction function.
    const EccTypeInfo_t * info = pNANDParams->eccDescriptor.getTypeInfo();
    if (!info)
    {
        return ERROR_GENERIC;
    }

    return info->correctEcc(pAuxBuffer, correctionInfo);
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_hal_types.h for documentation.
////////////////////////////////////////////////////////////////////////////////
uint32_t CommonNandBase::adjustPageAddress(uint32_t pageNumber)
{
    return pageNumber;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_hal.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::readPageWithEcc(const NandEccDescriptor_t * ecc, uint32_t pageNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    RtStatus_t retval;
    
    // Make sure the page has enough data to support the requested ECC type.
    // The 37xx 8-bit Reed-Solomon implementation requires a 4K page.
    if (ecc->eccType == kNandEccType_RS8 && pNANDParams->pageDataSize < XL_SECTOR_DATA_SIZE)
    {
        return ERROR_GENERIC;
    }
    
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
#if defined(STMP378x)
    bool overrideEcc = ecc->isBCH() && (*ecc != pNANDParams->eccDescriptor);
    
    // Update BCH params.
    if (overrideEcc)
    {
        ddi_bch_update_parameters((uint32_t)wChipNumber, ecc, pNANDParams->pageTotalSize);
    }
#endif
    
    uint32_t readSize = pNANDParams->pageTotalSize;
    
    // Handle 4-bit Reed-Solomon specially. Our ECC engine can only use RS4 with 2K pages.
    if (ecc->eccType == kNandEccType_RS4)
    {
        readSize = LARGE_SECTOR_TOTAL_SIZE;
    }

    // Get the ECC mask.
    uint32_t dataCount;
    uint32_t auxCount;
    uint32_t eccMask = ecc->computeMask(
        readSize,   // readSize
        pNANDParams->pageTotalSize, // pageTotalSize
        kEccOperationRead,
        kEccTransferFullPage,
        &dataCount,
        &auxCount);
    
    // Build a new DMA descriptor.
    NandDma::ReadEccData readDma(
        wChipNumber,  // chip enable
        eNandProgCmdRead1,
        NULL, // addressBytes
        (pNANDParams->wNumRowBytes + pNANDParams->wNumColumnBytes),  // addressByteCount
        eNandProgCmdRead1_2ndCycle,
        pBuffer,  //dataBuffer
        pAuxiliary,   //auxBuffer
        dataCount + auxCount,  //readSize
        *ecc,    //ecc
        eccMask);   //eccMask
    readDma.setAddress(0, adjustPageAddress(pageNumber));
    
    {
        EccTypeInfo::TransactionWrapper eccTransaction(
            *ecc,
            wChipNumber,
            pNANDParams->pageTotalSize,
            kEccOperationRead);
        
        
        // Flush the data cache to ensure that the DMA descriptor chain is in memory.
        hw_core_invalidate_clean_DCache();
        
        // Start the DMA and wait for it to finish.
        retval = readDma.startAndWait(kNandReadPageTimeout);

        if (retval == SUCCESS)
        {
            // Check the ECC results.
            retval = correctEcc(pBuffer, pAuxiliary, pECC);
        }
    }

#if defined(STMP378x)
    // Update BCH params again.
    if (overrideEcc)
    {
        ddi_bch_update_parameters((uint32_t)wChipNumber, &pNANDParams->eccDescriptor, pNANDParams->pageTotalSize);
    }
#endif

    return retval;
}

///////////////////////////////////////////////////////////////////////////////
//! \copydoc NandPhysicalMedia::readMultiplePages()
///////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::readMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount)
{
    unsigned i;
    for (i=0; i < pageCount; ++i)
    {
        MultiplaneParamBlock & thisPage = pages[i];
        thisPage.m_resultStatus = readPage(thisPage.m_address,
                                            thisPage.m_buffer,
                                            thisPage.m_auxiliaryBuffer,
                                            thisPage.m_eccInfo);
    }
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \copydoc NandPhysicalMedia::readMultipleMetadata()
///////////////////////////////////////////////////////////////////////////////
RtStatus_t CommonNandBase::readMultipleMetadata(MultiplaneParamBlock * pages, unsigned pageCount)
{
    unsigned i;
    for (i=0; i < pageCount; ++i)
    {
        MultiplaneParamBlock & thisPage = pages[i];
        thisPage.m_resultStatus = readMetadata(thisPage.m_address,
                                                thisPage.m_auxiliaryBuffer,
                                                thisPage.m_eccInfo);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
