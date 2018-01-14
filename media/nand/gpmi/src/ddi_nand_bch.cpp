#if defined(STMP378x)
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
//! \addtogroup ddi_media_nand_hal_ecc_internal
//! @{
//! \file    ddi_nand_bch.cpp
//! \brief   Functions for managing the BCH ECC peripheral.
//!
//! This file contains the NAND HAL BCH ECC interface functions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_gpmi_internal.h"
#include "hw/profile/hw_profile.h"
#include "registers/regsgpmi.h"
#include "registers/regsbch.h"
#include "hw/digctl/hw_digctl.h"
#include "hw/icoll/hw_icoll.h"
#include "hw/core/vmemory.h"
#include "components/telemetry/tss_logtext.h"

// Define as 1 to print to tss on ecc timeout, or 0.
#define DEBUG_LOG_ECC_TIMEOUTS 0

#if DEBUG_LOG_ECC_TIMEOUTS
extern uint16_t g_ui16EccTimeoutEventCount = 0;
#endif

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#define ECC_CORRECTION_TIMEOUT      1000     //!< 1000 useconds (1 ms)

//! \brief Simple macro to convert a number of bits into bytes, rounded up
//!     to the nearest byte.
#define BITS_TO_BYTES(bits) ((bits + 7) / 8)

//! \name BCH ECC commands
//!
//! The ECC_CMD field of the HW_GPMI_ECCCTRL register specifies whether to encode or
//! decode. For ECC8 this field also selects between 4-bit and 8-bit ECC. When using BCH,
//! only bit 0 has any effect (0=decode, 1=encode). The values for both BCH and ECC8
//! commands are the same; whether to use ECC8 or BCH is determined by the BUFFER_MASK
//! field of the same register.
//@{
#define GPMI_ECCCTRL_ECC_CMD__DECODE_BCH   BV_GPMI_ECCCTRL_ECC_CMD__DECODE_4_BIT    // 0
#define GPMI_ECCCTRL_ECC_CMD__ENCODE_BCH   BV_GPMI_ECCCTRL_ECC_CMD__ENCODE_4_BIT    // 1
//@}

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

__INIT_TEXT BchEccType::BchEccType(NandEccType_t theEccType, uint32_t theThreshold)
{
    eccType = theEccType;
    decodeCommand = GPMI_ECCCTRL_ECC_CMD__DECODE_BCH;
    encodeCommand = GPMI_ECCCTRL_ECC_CMD__ENCODE_BCH;
    parityBytes = NAND_ECC_BYTES_BCH(ddi_bch_GetLevel(theEccType));
    metadataSize = NAND_METADATA_SIZE_BCH;
    threshold = theThreshold;
    readGeneratesInterrupt = true;
    writeGeneratesInterrupt = true;
}

RtStatus_t BchEccType::computePayloads(unsigned dataSize, unsigned * payloadCount) const
{
    assert(payloadCount);
    
    //! \todo This isn't really valid, but the computePayloads() function isn't used by BCH so
    //! it doesn't really matter.
    *payloadCount = dataSize / NAND_ECC_BLOCK_SIZE;

    return SUCCESS;
}

RtStatus_t BchEccType::getMetadataInfo(unsigned dataSize, unsigned * metadataOffset, unsigned * metadataLength) const
{
    // Metadata is always in block 0 at offset 0.
    if (metadataOffset)
    {
        *metadataOffset = 0;
    }

    // Note: this is the raw nand metadata length, not the length used by BCH encoding.
    if (metadataLength)
    {
        *metadataLength = metadataSize;
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Read correction results from the ECC peripheral.
//!
//! Compute the ECC for the specified sector's data and verify the ECC
//! fields in its redundant area. If the check fails, try to correct the data.
//!
//! \param[in] info Pointer to the ECC info structure for the ECC type.
//! \param[out] correctionInfo Optional structure that is filled with data
//!     about corrected bit errors.
//!
//! \retval SUCCESS No errors detected.
//! \retval ERROR_DDI_NAND_HAL_ECC_FIXED Errors detected and fixed.
//! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED Uncorrectable errors detected.
//! \retval ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR Errors detected and
//!     fixed, but the number of bit errors for one or more payloads was above
//!     the threshold.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t BchEccType::correctEcc(SECTOR_BUFFER * pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo) const
{
    uint32_t  u32StartTime;
    RtStatus_t EccStatus = SUCCESS;
    uint32_t u32EccStatusRegister;

    u32StartTime = hw_digctl_GetCurrentTime();

    // Spin while no ECC Complete Interrupt has triggered
    //            and ECC_CORRECTION_TIMEOUT microsecs have not passed.
    // hw_digctl_CheckTimeOut takes care of
    // overflows.
    while((!(HW_BCH_CTRL_RD() & BM_BCH_CTRL_COMPLETE_IRQ)) &&
          (!hw_digctl_CheckTimeOut(u32StartTime, ECC_CORRECTION_TIMEOUT)));

    // Check for timeout occurring.
    if (!(HW_BCH_CTRL_RD() & BM_BCH_CTRL_COMPLETE_IRQ))
    {
#if DEBUG_LOG_ECC_TIMEOUTS
        g_ui16EccTimeoutEventCount++;
#endif
    }

    // Now read the ECC status.
    u32EccStatusRegister = HW_BCH_STATUS0_RD();

    if (u32EccStatusRegister & BM_BCH_STATUS0_UNCORRECTABLE)
    {
        EccStatus = ERROR_DDI_NAND_HAL_ECC_FIX_FAILED;

        // Fill in correction info if requested.
        if (correctionInfo)
        {
            readCorrectionStatus(NULL, NULL, pAuxBuffer, correctionInfo);
        }

        // Note: It is not necessary to reset the BCH block after an "uncorrectable" error.
        // In fact, due to a 378x chip bug it is not possible to reset the
        // BCH block after it has been used to transfer data.
    }
    else if (u32EccStatusRegister & BM_BCH_STATUS0_CORRECTED)
    {
        // If there were corrected bits then we want to get the maximum number of bit errors
        // and compare against a threshold to determine which error code to pass back.
        unsigned maxBitErrors;
        unsigned metadataBitErrors;
        
        // Read the payload status.
        readCorrectionStatus(&maxBitErrors, &metadataBitErrors, pAuxBuffer, correctionInfo);
        
        // Set the error to rewrite if the number of bit errors has met or exceeded
        // the threshold.
        if (maxBitErrors >= threshold || metadataBitErrors >= threshold)
        {
            EccStatus = ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR;
        }
        else
        {
            // Not enough errors to cause a rewrite.
            EccStatus = ERROR_DDI_NAND_HAL_ECC_FIXED;
        }

        EccStatus = ERROR_DDI_NAND_HAL_ECC_FIXED;
    }
    else if (u32EccStatusRegister) 
    {
        // Neither uncorrectable nor were there any corrections, but the caller still wants
        // us to fill in the correction info. This may include the case where one or more
        // payloads is all ones.
        readCorrectionStatus(NULL, NULL, pAuxBuffer, correctionInfo);
    }

    // Clear the ECC-completion flag and reenable the ISR in icoll.
    ddi_gpmi_clear_ecc_isr_enable();

    return EccStatus;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Reads the correction status for all payloads.
//!
//! \param[out] maxBitErrors
//! \param[out] metadataBitErrors
//! \param[in] pAuxBuffer Aux buffer containing status bytes to analyze.
//! \param[out] correctionInfo
////////////////////////////////////////////////////////////////////////////////
void BchEccType::readCorrectionStatus(unsigned * maxBitErrors, unsigned * metadataBitErrors, SECTOR_BUFFER *pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo) const
{
    unsigned i;
    unsigned validPayloadCount = 0;
    unsigned maxErrors = 0;
    int indexToAuxBuffer;
    uint32_t payload;
    uint8_t * p8AuxPointer = (uint8_t *)pAuxBuffer;
    unsigned payloadCount;
    unsigned metadataLength;
    
    // Get payload directly from flash layout register because
    // it could have been modified to force a 2k read.
    //info->computePayloads(info, 0, &payloadCount);
    payloadCount = HW_BCH_FLASH0LAYOUT0.B.NBLOCKS + 1;

    getMetadataInfo(0, NULL, &metadataLength);

    // Get the errors from auxillary pointer at offset after metadata bytes.

    // Get the status of Blocks. Each block's status is in a byte, starts at the beginning of a new word where metadata ends.
    indexToAuxBuffer = metadataLength + (metadataLength % 4);
    // Now get the max ecc corrections of data blocks including metadata ecc.
    for (i = 0; i < payloadCount; i++) 
    {
        payload = p8AuxPointer[indexToAuxBuffer + i];

        // Ignore uncorrectable and erased results while tracking the maximum number of errors for all payloads.
        if ((payload < BV_BCH_STATUS0_STATUS_BLK0__UNCORRECTABLE) && (payload > maxErrors))
        {
            maxErrors = payload;
        }
        // Fill in correction info structure.
        if (correctionInfo)
        {
            // Convert certain values to generic constants.
            switch (payload)
            {
                case BV_BCH_STATUS0_STATUS_BLK0__UNCORRECTABLE:
                    payload = NandEccCorrectionInfo::kUncorrectable;
                    break;
                    
                case BV_BCH_STATUS0_STATUS_BLK0__ERASED:
                    payload = NandEccCorrectionInfo::kAllOnes;
                    break;
            }
            
            correctionInfo->payloadCorrections[validPayloadCount] = payload;
        }
        
        validPayloadCount++;
    }
    
    // Fill in correction info.
    if (correctionInfo)
    {
        correctionInfo->payloadCount = validPayloadCount;
        // Metadata is included in block 0, so there is no independent metadata corrections count.
        correctionInfo->isMetadataValid = false;
        correctionInfo->metadataCorrections = 0;
        correctionInfo->maxCorrections = 0;

        // This loop sets maxCorrections to the highest number of corrections or
        // uncorrectable. Only if all payloads are all ones will the max be set
        // to all ones.
        for (i = 0; i < validPayloadCount; ++i)
        {
            unsigned thisPayloadCorrection = correctionInfo->payloadCorrections[i];
            if (thisPayloadCorrection != NandEccCorrectionInfo::kAllOnes)
            {
                if (correctionInfo->maxCorrections == NandEccCorrectionInfo::kAllOnes || thisPayloadCorrection > correctionInfo->maxCorrections)
                {
                    correctionInfo->maxCorrections = thisPayloadCorrection;
                }
            }
        }
    }
    
    // Return bit error counts.
    if (maxBitErrors)
    {
        *maxBitErrors = maxErrors;
    }
    if (metadataBitErrors)
    {
        // Metadata is included in block 0, so there is no independent metadata corrections count.
        *metadataBitErrors = 0;
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
uint32_t BchEccType::computeMask(uint32_t byteCount, uint32_t pageTotalSize, bool bIsWrite, bool readOnly2k, const NandEccDescriptor_t *pEccDescriptor, uint32_t * dataCount, uint32_t * auxCount) const
{
    if (bIsWrite)
    {
        // For write operations, metadata bytes are included in data count and mask is always PAGE.
        if (auxCount)
        {
            *auxCount = 0;
        }
        if (dataCount)
        {
            *dataCount = pageTotalSize;
        }

        return BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE;
    }
    else // Read operation
    {
        // Use the special aux-only read mode if the number of bytes is less than or
        // equal to the size of block 0 plus the metadata.
        if (byteCount <= pEccDescriptor->u32SizeBlock0 + pEccDescriptor->u32MetadataBytes)
        {
            // Aux count is number of parity bytes plus number of metadata bytes.
            if (auxCount)
            {
                // Formula for calculating number of parity bits for each block is (ecc_level * 13).
                unsigned uEccCount = ddi_bch_GetLevel(pEccDescriptor->eccTypeBlock0) * NAND_BCH_PARITY_SIZE_BITS;
                uEccCount = BITS_TO_BYTES(uEccCount);    // convert to bytes
                *auxCount = uEccCount + pEccDescriptor->u32MetadataBytes;
            }

            // Only want to read the metadata. But since the metadata is combined with block 0 data,
            // both are transferred.
            if (dataCount)
            {
                *dataCount = pEccDescriptor->u32SizeBlock0;
            }

            return BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_AUXONLY;
        }
        else // Full page read.
        {
            // We assume that we will get exactly 2KB of data when using a 2k read.
            assert((pEccDescriptor->u32SizeBlockN * NAND_BCH_2K_PAGE_BLOCKN_COUNT + pEccDescriptor->u32SizeBlock0) == 2048);
            unsigned actualBlockNCount = readOnly2k ? NAND_BCH_2K_PAGE_BLOCKN_COUNT : pEccDescriptor->u32NumEccBlocksN;
            
            // Aux count is number of parity bytes plus number of metadata bytes.
            if (auxCount)
            {
                // Formula for calculating number of parity bits for each block is (ecc_level * 13).
                unsigned uEccCount = (ddi_bch_GetLevel(pEccDescriptor->eccTypeBlock0) * NAND_BCH_PARITY_SIZE_BITS) +
                                     (actualBlockNCount *
                                     (ddi_bch_GetLevel(pEccDescriptor->eccType) * NAND_BCH_PARITY_SIZE_BITS));
                uEccCount = BITS_TO_BYTES(uEccCount);    // convert to bytes
                *auxCount = uEccCount + pEccDescriptor->u32MetadataBytes;
            }

            // Data count is the sum of the sizes of each block (block 0 may be a different size).
            if (dataCount)
            {
                *dataCount = (actualBlockNCount * pEccDescriptor->u32SizeBlockN) + pEccDescriptor->u32SizeBlock0;
            }

            return BV_GPMI_ECCCTRL_BUFFER_MASK__BCH_PAGE;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t BchEccType::preTransaction(uint32_t u32NandDeviceNumber, bool isWrite, const NandEccDescriptor_t * pEccDescriptor, bool bTransfer2k, uint32_t pageTotalSize) const
{
    ddi_gpmi_clear_ecc_complete_flag();

    ddi_bch_SetFlashLayout(u32NandDeviceNumber, pEccDescriptor, bTransfer2k, pageTotalSize);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t BchEccType::postTransaction(uint32_t u32NandDeviceNumber, bool isWrite) const
{
    // Clear the ECC-completion flag and reenable the ISR in icoll.
    ddi_gpmi_clear_ecc_isr_enable();
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t ddi_bch_init(void)
{
    
    // Note: Due to a 378x chip bug it is not possible to reset the
    // BCH block after it has been used to transfer data (for example
    // after booting from ROM). So rather than reset the block here we
    // just make sure it is enabled.
    // Here are some additional comments from the chip team:
    // The bug is that if the BCH is soft-reset after any transfers, then the AXI master
    // will be locked up until a hard reset.  Even if the AXI master is idle, the AXI state
    // machine will be locked up.  Soft resets after hard-reset should be safe, but once you
    // perform any BCH transfer, then any subsequent attempts to soft reset the BCH will almost
    // always lock up the BCH.  The only way to recover from a BCH lock-up is hard-reset.
    // There should not be any reason to soft reset the BCH.  The BCH should finish every transfer
    // properly and be ready for the next operation no matter the state of the page
    // (correctable, uncorrectable, erased, etc.)

    ddi_bch_enable();

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT void ddi_bch_enable(void)
{
    // Remove the clock gate.
    HW_BCH_CTRL_CLR( BM_BCH_CTRL_CLKGATE);

    // Poll until clock is in the NON-gated state.
    while (HW_BCH_CTRL.B.CLKGATE)
    {
        ; // busy wait
    }

    // Remove Soft Reset.
    HW_BCH_CTRL_CLR( BM_BCH_CTRL_SFTRST );

    // Poll until soft reset is clear.
    while (HW_BCH_CTRL.B.SFTRST)
    {
        ; // busy wait
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_bch_disable(void)
{
    // Gate the BCH block
    HW_BCH_CTRL_SET( BM_BCH_CTRL_CLKGATE );
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_bch_update_parameters(uint32_t u32NandDeviceNumber, const NandEccDescriptor_t *pEccDescriptor, uint32_t pageTotalSize)
{
    BW_GPMI_CTRL1_BCH_MODE(pEccDescriptor->isBCH() ? 1 : 0);

    if (pEccDescriptor->isBCH())
    {
        // Setup BCH registers
        BW_BCH_MODE_ERASE_THRESHOLD(pEccDescriptor->u32EraseThreshold);

        // All NAND chip selects currently use the same ECC, so we program only
        // flash layout register 0 and setup the layout select register to point
        // all the chips selects to layout register 0.
        HW_BCH_LAYOUTSELECT_WR(0);

        ddi_bch_SetFlashLayout(u32NandDeviceNumber, pEccDescriptor, kEccTransferFullPage, pageTotalSize);
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void  ddi_bch_SetFlashLayout(uint32_t u32NandDeviceNumber, const NandEccDescriptor_t * pEccDescriptor, bool bTransfer2k, uint32_t pageTotalSize)
{
    // BCH_MODE is forced to default (ECC8) after a gpmi soft reset, so we force it to BCH mode here.
    BW_GPMI_CTRL1_BCH_MODE(1);

    // Set flash0layout0 bch ecc register.
    if (bTransfer2k)
    {
        BW_BCH_FLASH0LAYOUT0_NBLOCKS(NAND_BCH_2K_PAGE_BLOCKN_COUNT);
    }
    else
    {
        BW_BCH_FLASH0LAYOUT0_NBLOCKS(pEccDescriptor->u32NumEccBlocksN);
    }
    BW_BCH_FLASH0LAYOUT0_META_SIZE(pEccDescriptor->u32MetadataBytes);
    BW_BCH_FLASH0LAYOUT0_ECC0(ddi_bch_GetLevel(pEccDescriptor->eccTypeBlock0)/2);
    BW_BCH_FLASH0LAYOUT0_DATA0_SIZE(pEccDescriptor->u32SizeBlock0);

    // Set flash0layout1 bch ecc register.

    // The setting of PAGE_SIZE is explained by the
    // the chip team as follows:
    // It depends whether you are reading or writing:
    // For writing, the BCH will write out to the end of the page as defined by the PAGE_SIZE field.
    // If you wanted to write a partial page, then you would definitely need to adjust down the PAGE_SIZE field before writing.
    // For reading, it doesn’t appear PAGE_SIZE is used.  BCH will start reading from the start of the page and stop when
    // it has enough data based upon all the other fields of the FLASHLAYOUT registers.
    BW_BCH_FLASH0LAYOUT1_PAGE_SIZE(pageTotalSize);
    BW_BCH_FLASH0LAYOUT1_ECCN(ddi_bch_GetLevel(pEccDescriptor->eccType)/2);
    BW_BCH_FLASH0LAYOUT1_DATAN_SIZE(pEccDescriptor->u32SizeBlockN);
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_bch_calculate_highest_level(uint32_t pageDataSize, uint32_t pageMetadataSize, NandEccDescriptor_t * resultEcc)
{
    uint32_t pageTotalSize = pageDataSize + pageMetadataSize;
    unsigned blockNCount = (pageDataSize / NAND_ECC_BLOCK_SIZE) - 1;
    unsigned block0Size = NAND_ECC_BLOCK_SIZE + NAND_METADATA_SIZE_BCH;
    unsigned bchLevel = NAND_MAX_BCH_ECC_LEVEL;
    
    while (bchLevel > 0)
    {
        uint32_t totalSize = BITS_TO_BYTES((bchLevel * NAND_BCH_PARITY_SIZE_BITS) + (blockNCount * bchLevel * NAND_BCH_PARITY_SIZE_BITS)) + block0Size + (blockNCount * NAND_ECC_BLOCK_SIZE);
        if (totalSize <= pageTotalSize)
        {
            break;
        }
        
        bchLevel -= 2;
    }
    
    // Return an error if nothing fits in the page.
    if (bchLevel == 0)
    {
        return ERROR_GENERIC;
    }
    
    // Fill in the resulting ECC descriptor.
    resultEcc->eccType = ddi_bch_GetType(bchLevel);
    resultEcc->eccTypeBlock0 = resultEcc->eccType;
    resultEcc->u32SizeBlockN = NAND_ECC_BLOCK_SIZE;
    resultEcc->u32SizeBlock0 = NAND_ECC_BLOCK_SIZE;
    resultEcc->u32NumEccBlocksN = blockNCount;
    resultEcc->u32MetadataBytes = NAND_METADATA_SIZE_BCH;
    resultEcc->u32EraseThreshold = 2;
    
    return SUCCESS;
}

//! @}
#endif //STMP378x
