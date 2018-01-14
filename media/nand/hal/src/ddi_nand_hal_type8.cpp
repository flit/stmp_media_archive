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
//! \file    ddi_nand_hal_type8.cpp
//! \brief   Read functions for type8 Nand.
//!
//! This file contains read functions for type8 Nands.  Type8 Nands have
//! 4K pages but due to various hardware defects, firmware has
//! to fool 37xx hardware into thinking that it is actually reading from
//! a device with 2K-pages.
//!
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "drivers/media/ddi_media.h"
#include "ddi_nand_hal_internal.h"
#include "hw/core/mmu.h"

#if !defined (STMP378x)

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! Number of 2112 byte subpages in a 4224 byte page.
#define NUM_SUBPAGES_PER_4K_PAGE (2)

//! Constants for the \a readOnly2K argument of Type8_ReadSectorDataCommon().
enum
{
    //! Read the full 4K page.
    kNandType8Read4K = false,
    
    //! Read only the first 2K sector of the full 4K page. This mode is used to read
    //! system drive sectors, since they are presented as 2K only.
    kNandType8Read2K = true
};

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

//! We always use 2K firmware pages for Samsung Type 8 4K page NANDs that use 4-bit ECC.
//! Because of hardware limitations with the ECC8 engine and the resulting way in which
//! we store data in the page, the ROM cannot get to the second 2K within a page.
RtStatus_t Type8Nand::init()
{
    // Let the superclass init.
    RtStatus_t status = Type6Nand::init();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Set the firmware page size.
    pNANDParams->hasSmallFirmwarePages = true;
    pNANDParams->firmwarePageTotalSize = LARGE_SECTOR_TOTAL_SIZE;
    pNANDParams->firmwarePageDataSize = LARGE_SECTOR_DATA_SIZE;
    pNANDParams->firmwarePageMetadataSize = LARGE_SECTOR_REDUNDANT_SIZE;

    if (wChipNumber == 0)
    {
        // Rebuild the page read DMA to read only 2K at a time, so the ECC mask value is correct.
        uint32_t dataCount;
        uint32_t auxCount;
        uint32_t eccMask = pNANDParams->eccDescriptor.computeMask(
            LARGE_SECTOR_TOTAL_SIZE,   // readSize
            LARGE_SECTOR_TOTAL_SIZE, // pageTotalSize
            kEccOperationRead,
            kEccTransferFullPage,
            &dataCount,
            &auxCount);
        
        uint32_t addressByteCount = pNANDParams->wNumRowBytes + pNANDParams->wNumColumnBytes;
        
        g_nandHalContext.readDma.init(
            0,  // chip enable
            eNandProgCmdRead1,
            NULL, // addressBytes
            addressByteCount,  // addressByteCount
            eNandProgCmdRead1_2ndCycle,
            NULL,  //dataBuffer
            NULL,   //auxBuffer
            dataCount + auxCount,  //readSize
            pNANDParams->eccDescriptor,    //ecc
            eccMask);   //eccMask
            
        // Reinit firmware page read DMA. We can use the same ECC mask as above.
        g_nandHalContext.readFirmwareDma.init(
            0,  // chip enable
            eNandProgCmdRead1,
            NULL, // addressBytes
            addressByteCount,  // addressByteCount
            eNandProgCmdRead1_2ndCycle,
            NULL,  //dataBuffer
            NULL,   //auxBuffer
            dataCount + auxCount,  //readSize
            pNANDParams->eccDescriptor,    //ecc
            eccMask);   //eccMask
    
        // Reinit the write DMA for the same reason.
        eccMask = pNANDParams->eccDescriptor.computeMask(
            LARGE_SECTOR_TOTAL_SIZE,
            LARGE_SECTOR_TOTAL_SIZE,
            kEccOperationWrite,
            kEccTransferFullPage,
            &dataCount,
            &auxCount);
    
        g_nandHalContext.writeDma.init(
            0, // chipSelect,
            eNandProgCmdSerialDataInput, // command1,
            NULL, // addressBytes,
            addressByteCount, // addressByteCount,
            eNandProgCmdPageProgram, // command2,
            NULL, // dataBuffer,
            NULL, // auxBuffer,
            dataCount + auxCount, // sendSize,
            dataCount, // dataSize,
            auxCount, // leftoverSize,
            pNANDParams->eccDescriptor,
            eccMask);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Function which reads page from a Samsung 4K page Nand.
//!
//! This function reads the entire 4096-bytes worth of data from a page of
//! a Samsung 4K page NAND. On exit the \a pAuxiliary buffer will hold the
//! second copy of metadata on the NAND. This is because this same buffer is
//! reused for both 2K subpage reads.
//!
//! \param[in]  wSectorNum      Sector number relative to the chip enable.
//! \param[in]  pBuf            Buffer containing data, at least 4096 bytes large.
//! \param[in]  pAuxiliary      Buffer containing metadata.
//! \param[in]  pECC            Unused parameter.
//!
//! \retval SUCCESS The read was successful.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t Type8Nand::readPage(uint32_t u32SectorNum, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{    
    RtStatus_t status;
    RtStatus_t eccStatus1;
    RtStatus_t eccStatus2;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->pageDataSize);
    _verifyPhysicalContiguity(pAuxiliary, pNANDParams->pageMetadataSize);
    
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;

    // Update DMA descriptor.
    g_nandHalContext.readDma.setChipSelect(wChipNumber);
    g_nandHalContext.readDma.setAddress(0, adjustPageAddress(u32SectorNum));
    g_nandHalContext.readDma.setBuffers(pBuffer, pAuxiliary);
    
    // Read first 2K of data.
    {
        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationRead);
                                                        
        // Flush the data cache to ensure that the DMA descriptor chain is in memory.
        hw_core_invalidate_clean_DCache();
        
        // Start the DMA and wait for it to finish.
        status = g_nandHalContext.readDma.startAndWait(kNandReadPageTimeout);

        if (status == SUCCESS)
        {
            // Check the ECC results.
            eccStatus1 = correctEcc(pBuffer, pAuxiliary, pECC);
        }
    }

    // Read second 2K of data.
    if (status == SUCCESS)
    {
        // Update DMA to read the second 2112 bytes from the pages. The data is
        // stored to the second 2K of the data buffer, while the auxiliary buffer is reused.
        g_nandHalContext.readDma.setAddress(LARGE_SECTOR_TOTAL_SIZE, adjustPageAddress(u32SectorNum));
        g_nandHalContext.readDma.setBuffers((uint8_t *)pBuffer + LARGE_SECTOR_DATA_SIZE, pAuxiliary);

        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationRead);
                                                        
        // Flush the data cache to ensure that the DMA descriptor chain is in memory.
        hw_core_invalidate_clean_DCache();

        // Start the DMA and wait for it to finish.
        status = g_nandHalContext.readDma.startAndWait(kNandReadPageTimeout);

        if (status == SUCCESS)
        {
            // Check the ECC results.
            eccStatus2 = correctEcc(pBuffer, pAuxiliary, pECC);
        }
    }
        
    // If we get an ECC_FIXED/ECC_FIXED_REWRITE_SECTOR/ECC_FIX_FAILED error on either of
    // the two subpages, then make sure the worst error of the two is returned.
    if (status == SUCCESS && (eccStatus1 != SUCCESS || eccStatus2 != SUCCESS))
    {
        // This depends on the progressively worse errors having higher error codes.
        assert(ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR > ERROR_DDI_NAND_HAL_ECC_FIXED);
        assert(ERROR_DDI_NAND_HAL_ECC_FIX_FAILED > ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR);
        status = (eccStatus1 > eccStatus2) ? eccStatus1 : eccStatus2;
    }

#if DEBUG
        // Insert a false read error if requested.
        if (g_nand_hal_insertReadError)
        {
            status = g_nand_hal_insertReadError;
            g_nand_hal_insertReadError = 0;
        }
#endif

    // Return.
    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Function which reads metadata from Samsung 4k-page Nand.
//!
//! This function reads metadata from 4k-page Samsung NAND.
//! Each 4k page is written as two 2k sectors each complete with its own copy
//! of the same metadata. This function reads only the second copy of the metadata.
//! The second copy is read instead of the first because a full data read of the
//! page using Type8_ReadPage() will result in the \a pAuxiliary buffer
//! containing the contents of the second copy of metadata
//!
//! \param[in] wSectorNum Page number.
//! \param[in] pBuf Buffer containing data.
//! \param[in] pECC Optional ECC correction status.
//!
//! \retval SUCCESS The read of the metadata was successful.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t Type8Nand::readMetadata(uint32_t u32SectorNum, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC)
{
    RtStatus_t status;
    uint32_t readOffset;
    uint32_t readSize;

    _verifyPhysicalContiguity(pBuffer, pNANDParams->pageMetadataSize);

    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;
    
    const EccTypeInfo_t * eccInfo = pNANDParams->eccDescriptor.getTypeInfo();
    assert(eccInfo);

    // Get the offset and length of the metadata.
    status = eccInfo->getMetadataInfo(LARGE_SECTOR_DATA_SIZE, &readOffset, &readSize);
    if (status != SUCCESS)
    {
        return status;
    }

    // We want to read the second copy of the metadata that sits at the end of the 4K page,
    // so skip the first 2112-byte subpage.
    readOffset += LARGE_SECTOR_TOTAL_SIZE;

    // Update DMA descriptor.
    g_nandHalContext.readMetadataDma.setChipSelect(wChipNumber);
    g_nandHalContext.readMetadataDma.setAddress(readOffset, adjustPageAddress(u32SectorNum));
    g_nandHalContext.readMetadataDma.setBuffers(pBuffer, pBuffer);

    {
        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationRead);
        
        // Flush the data cache to ensure that the DMA descriptor chain is in memory.
        hw_core_invalidate_clean_DCache();

        // Start the DMA and wait for it to finish.
        status = g_nandHalContext.readMetadataDma.startAndWait(kNandReadPageTimeout);

        if (status == SUCCESS)
        {
            // Check the ECC results.
            status = correctEcc(pBuffer, pBuffer, pECC);
        }
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Function which peforms two 2K writes back to back to same 4K sector.
//!
//! This function carries out command sequence (0x80-0x11-0x80-0x10).  The 
//! writes look like 2K sector writes. Fortunately, (0x80-0x11-0x80-0x10) command sequence only uses
//! one tProg time.  So, the amount of time used to program 4K sector by doing
//! two consecutive 2K writes is about the same as the time it would have taken
//! to program the 4K sector with a single 4K write.
//!
//! \param[in]  wSectorNum Sector number relative to the chip enable.
//! \param[in]  pBuf Buffer containing data for the 4KB page.
//! \param[in]  pAuxiliary Buffer containing metadata for the page.
//!
//! \retval SUCCESS The two 2K subpage writes were both successful.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t Type8Nand::writePage(uint32_t u32SectorNum, const SECTOR_BUFFER * pBuf, const SECTOR_BUFFER * pAuxiliary)
{
    RtStatus_t rtCode;

    _verifyPhysicalContiguity(pBuf, pNANDParams->pageDataSize);
    _verifyPhysicalContiguity(pAuxiliary, pNANDParams->pageMetadataSize);
     
    // This function is an official "port of entry" into the HAL, and all access
    // to the HAL is serialized.
    NandHalMutex mutexHolder;

    // Enable writes to this NAND for this scope.
    EnableNandWrites enabler(this);
    
    uint32_t dataCount;
    uint32_t auxCount;
    uint32_t eccMask = pNANDParams->eccDescriptor.computeMask(
        LARGE_SECTOR_TOTAL_SIZE,
        LARGE_SECTOR_TOTAL_SIZE,
        kEccOperationWrite,
        kEccTransferFullPage,
        &dataCount,
        &auxCount);

    // Update DMA descriptors.
    g_nandHalContext.writeDma.setChipSelect(wChipNumber);
    g_nandHalContext.writeDma.setAddress(0, adjustPageAddress(u32SectorNum));
    g_nandHalContext.writeDma.setBuffers(pBuf, pAuxiliary);
    g_nandHalContext.statusDma.setChipSelect(wChipNumber);

    NandDma::Component::SendEccData writeSecondData;
    writeSecondData.init(
        wChipNumber, // chipSelect,
        ((const uint8_t *)pBuf + LARGE_SECTOR_DATA_SIZE), // dataBuffer,
        pAuxiliary, // auxBuffer,
        (dataCount + auxCount), // sendSize,
        dataCount, // dataSize,
        auxCount, // leftoverSize,
        pNANDParams->eccDescriptor, // ecc,
        eccMask);
    
    // Link the second write data into the descriptor chain.
    g_nandHalContext.writeDma.m_writeData >> writeSecondData >> g_nandHalContext.writeDma.m_cle2;
    
    {
        EccTypeInfo::TransactionWrapper eccTransaction(pNANDParams->eccDescriptor,
                                                        wChipNumber,
                                                        pNANDParams->pageTotalSize,
                                                        kEccOperationWrite);


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

    return(rtCode);
}

#endif // STMP378x

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
