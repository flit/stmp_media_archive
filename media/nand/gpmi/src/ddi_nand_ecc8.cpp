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
//! \file    ddi_nand_ecc8.cpp
//! \brief   Functions for managing the ECC8 peripheral.
//!
//! This file contains the NAND HAL ECC8 interface functions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_gpmi_internal.h"
#include "hw/profile/hw_profile.h"
#include "registers/regsgpmi.h"
#include "hw/digctl/hw_digctl.h"
#include "hw/icoll/hw_icoll.h"
#include "hw/core/vmemory.h"
#include "components/telemetry/tss_logtext.h"

// Define as 1 to print to tss on ecc timeout, or 0.
#define DEBUG_LOG_ECC_TIMEOUTS 0

#if DEBUG_LOG_ECC_TIMEOUTS
uint16_t g_ui16EccTimeoutEventCount = 0;
#endif

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#define ECC_CORRECTION_TIMEOUT      1000     //!< 1000 useconds (1 ms)

// Unit: microseconds. Critical value.
#define DDI_NAND_HAL_RESET_ECC8_SFTRST_LATENCY  (2)

//! Number of payloads that the ECC8 block supports.
#define ECC8_PAYLOAD_COUNT (8)

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

__INIT_TEXT ReedSolomonEccType::ReedSolomonEccType(NandEccType_t theEccType, uint32_t theDecodeCommand, uint32_t theEncodeCommand, uint32_t theParityBytes, uint32_t theMetadataSize, uint32_t theThreshold)
{
    eccType = theEccType;
    decodeCommand = theDecodeCommand;
    encodeCommand = theEncodeCommand;
    parityBytes = theParityBytes;
    metadataSize = theMetadataSize;
    threshold = theThreshold;
    readGeneratesInterrupt = true;
    writeGeneratesInterrupt = false;
    
    // The RS4 ECC info object must be allocated at the same time as the RS8 info object
    // because it is used for reading bit corrections even for RS8. If it is left up to
    // chance when the RS4 object is allocated, it may happen inside VMI, thus causing an
    // abort when attempting to page in the memory allocation code.
    if (theEccType == kNandEccType_RS8)
    {
        ddi_gpmi_get_ecc_type_info(kNandEccType_RS4);
    }
}

//! For ECC8, payloads are always 512 bytes, regardless of the ECC level.
//!
RtStatus_t ReedSolomonEccType::computePayloads(unsigned dataSize, unsigned * payloadCount) const
{
    assert(payloadCount);
    
    *payloadCount = dataSize / NAND_ECC_BLOCK_SIZE;

    return SUCCESS;
}

RtStatus_t ReedSolomonEccType::getMetadataInfo(unsigned dataSize, unsigned * metadataOffset, unsigned * metadataLength) const
{
    assert(metadataOffset);
    assert(metadataLength);
    
    unsigned payloadCount;
    computePayloads(dataSize, &payloadCount);
    *metadataOffset = dataSize + payloadCount * parityBytes;
    
    // RA always uses ECC4 regardless of page size when using the ECC8 block.
    *metadataLength = metadataSize + NAND_ECC_BYTES_4BIT;

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Read correction results from the ECC8 peripheral.
//!
//! Compute the ECC for the specified sector's data and verify the ECC
//! fields in its redundant area. If the check fails, try to correct the data.
//!
//! \param[in] info Pointer to the ECC info structure for the ECC type.
//! \param[in] pAuxBuffer Unused.
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
RtStatus_t ReedSolomonEccType::correctEcc(SECTOR_BUFFER *pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo) const
{
    uint32_t ui32_ECC8_STATUS0_FullRegisterContent;
    uint32_t  u32StartTime;
    RtStatus_t EccStatus = SUCCESS;

    u32StartTime = hw_digctl_GetCurrentTime();

    // Spin while no ECC Complete Interrupt has triggered
    //            and ECC_CORRECTION_TIMEOUT microsecs have not passed.
    // Previously we were using timer which was not
    // always running.  We're now using hardware microsecond
    // counter, and hw_digctl_CheckTimeOut takes care of
    // overflows.
    while((!(HW_ECC8_CTRL_RD() & BM_ECC8_CTRL_COMPLETE_IRQ)) &&
          (!hw_digctl_CheckTimeOut(u32StartTime, ECC_CORRECTION_TIMEOUT)));

    // Check for timeout occurring.
    if (!(HW_ECC8_CTRL_RD() & BM_ECC8_CTRL_COMPLETE_IRQ))
    {
#if DEBUG_LOG_ECC_TIMEOUTS
        //tss_logtext_Print(LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_3,"ECCtimeout\r\n");
         // tss is too expensive here (dpc stack oflow here so just increment a counter)
        g_ui16EccTimeoutEventCount++;
#endif
    }

    //
    // When we read from the NAND using GPMI with ECC,
    // there will be an ECC interrupt upon completion of the ECC correction.
    // Thereafter, these actions must happen in sequence:
    //     1.  ECC status must be read.
    //     2.  ECC ISR must be reenabled.
    //     3.  ECC-completion must be cleared, which frees the ECC
    //         block to process the next data.
    // The status must be read before the ECC-completion is cleared, or
    // the next ECC cycle will overwrite the status.  In the case of a
    // successful DMA and ECC, the code that reads the ECC status
    // also performs steps 2 and 3.
    // 

    // Now read the ECC status.
    ui32_ECC8_STATUS0_FullRegisterContent = HW_ECC8_STATUS0_RD();

    if (ui32_ECC8_STATUS0_FullRegisterContent & BM_ECC8_STATUS0_UNCORRECTABLE)
    {
        EccStatus = ERROR_DDI_NAND_HAL_ECC_FIX_FAILED;
        
        // Fill in correction info if requested.
        if (correctionInfo)
        {
            readCorrectionStatus(NULL, NULL, correctionInfo);
        }
        
        // The _UNCORRECTABLE status bit is sticky, and the only way to
        // clear it is to soft-reset the ECC circuit.
        // This status bit is also used to generate the "uncorrectable"
        // values in the correction-count fields of the status registers
        // so those values are also affected.
        //
        // Clear the _UNCORRECTABLE status bit by resetting the ECC circuit.
        ddi_ecc8_soft_reset();
    }
    else if (ui32_ECC8_STATUS0_FullRegisterContent & BM_ECC8_STATUS0_CORRECTED)
    {
        // If there were corrected bits then we want to get the maximum number of bit errors
        // and compare against a threshold to determine which error code to pass back.
        unsigned maxBitErrors;
        unsigned metadataBitErrors;
        unsigned metadataThreshold;
        
        // Lookup bit error threshold values.
        metadataThreshold = ddi_gpmi_get_ecc_type_info(kNandEccType_RS4)->threshold; // Metadata is always RS4
        
        // Read the payload status.
        readCorrectionStatus(&maxBitErrors, &metadataBitErrors, correctionInfo);
        
        // Set the error to rewrite if the number of bit errors has met or exceeded
        // the threshold.
        if (maxBitErrors >= threshold || metadataBitErrors >= metadataThreshold)
        {
            EccStatus = ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR;
        }
        else
        {
            // Not enough errors to cause a rewrite.
            EccStatus = ERROR_DDI_NAND_HAL_ECC_FIXED;
        }
    }
    else if (correctionInfo)
    {
        // Neither uncorrectable nor were there any corrections, but the caller still wants
        // us to fill in the correction info. This may include the case where one or more
        // payloads is all ones.
        readCorrectionStatus(NULL, NULL, correctionInfo);
    }

    // Clear the ECC-completion flag and reenable the ISR in icoll.
    ddi_gpmi_clear_ecc_isr_enable( );

    return EccStatus;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Reads the correction status for all payloads.
//!
//! \param[out] maxBitErrors
//! \param[out] metadataBitErrors
//! \param[out] correctionInfo
////////////////////////////////////////////////////////////////////////////////
void ReedSolomonEccType::readCorrectionStatus(unsigned * maxBitErrors, unsigned * metadataBitErrors, NandEccCorrectionInfo_t * correctionInfo) const
{
    uint32_t status0 = HW_ECC8_STATUS0_RD();
    uint32_t status1 = HW_ECC8_STATUS1_RD();
    unsigned i;
    unsigned validPayloadCount = 0;
    unsigned maxErrors = 0;
    unsigned metadataErrors = 0;
    unsigned metadataCorrections;
    bool isMetadataValid;
    
    // Iterate over all data payloads.
    for (i=0; i < ECC8_PAYLOAD_COUNT; ++i)
    {
        // Extract the status of payload i, 4 bits at a time.
        uint32_t payload = ((status1 >> (4 * i)) & 0xf);
        
        // Ignore this payload if it wasn't processed.
        if (payload == BV_ECC8_STATUS1_STATUS_PAYLOAD0__NOT_CHECKED)
        {
            continue;
        }
        
        // Ignore uncorrectable and all-ones results while tracking the maximum number of errors for all payloads.
        if (payload < BV_ECC8_STATUS1_STATUS_PAYLOAD0__NOT_CHECKED && payload > maxErrors)
        {
            maxErrors = payload;
        }
        
        // Fill in correction info structure.
        if (correctionInfo)
        {
            // Convert certain values to generic constants.
            switch (payload)
            {
                case BV_ECC8_STATUS1_STATUS_PAYLOAD0__UNCORRECTABLE:
                    payload = NandEccCorrectionInfo::kUncorrectable;
                    break;
                    
                case BV_ECC8_STATUS1_STATUS_PAYLOAD0__ALL_ONES:
                    payload = NandEccCorrectionInfo::kAllOnes;
                    break;
            }
            
            correctionInfo->payloadCorrections[validPayloadCount] = payload;
        }
        
        validPayloadCount++;
    }

    // Now check the bit errors for the metadata.
    metadataErrors = (status0 & BM_ECC8_STATUS0_STATUS_AUX) >> BP_ECC8_STATUS0_STATUS_AUX;
    switch (metadataErrors)
    {
        case BV_ECC8_STATUS0_STATUS_AUX__NOT_CHECKED:
            metadataErrors = 0;
            metadataCorrections = 0;
            isMetadataValid = false;
            break;
            
        case BV_ECC8_STATUS0_STATUS_AUX__UNCORRECTABLE:
            metadataErrors = 0;
            metadataCorrections = NandEccCorrectionInfo::kUncorrectable;
            isMetadataValid = true;
            break;
            
        case BV_ECC8_STATUS0_STATUS_AUX__ALL_ONES:
            metadataErrors = 0;
            metadataCorrections = NandEccCorrectionInfo::kAllOnes;
            isMetadataValid = true;
            break;
        
        default:
            metadataCorrections = metadataErrors;
            isMetadataValid = true;
            break;
    }
    
    // Fill in correction info.
    if (correctionInfo)
    {
        correctionInfo->payloadCount = validPayloadCount;
        correctionInfo->isMetadataValid = isMetadataValid;
        correctionInfo->metadataCorrections = metadataCorrections;
        
        // Start off with max being equal to metadata corrections.
        correctionInfo->maxCorrections = metadataCorrections;

        // This loop sets maxCorrections to the highest number of corrections or
        // uncorrectable. Only if all payloads are all ones will the max be set
        // to all ones.
        for (i=0; i < validPayloadCount; ++i)
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
        *metadataBitErrors = metadataErrors;
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ReedSolomonEccType::preTransaction(uint32_t u32NandDeviceNumber, bool isWrite, const NandEccDescriptor_t * pEccDescriptor, bool bTransfer2k, uint32_t pageTotalSize) const
{
    ddi_gpmi_clear_ecc_complete_flag();

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ReedSolomonEccType::postTransaction(uint32_t u32NandDeviceNumber, bool isWrite) const
{
    // Clear the ECC-completion flag and reenable the ISR in icoll.
    ddi_gpmi_clear_ecc_isr_enable();
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
uint32_t ReedSolomonEccType::computeMask(uint32_t byteCount, uint32_t pageTotalSize, bool bIsWrite, bool readOnly2k, const NandEccDescriptor_t *pEccDescriptor, uint32_t * dataCount, uint32_t * auxCount) const
{
    uint32_t mask;
    uint32_t temp;

    // Calculate the ECC Mask for this transaction.
    // Auxilliary = 0x100 set to request transfer to/from the Auxiliary buffer.
    // Buffer7 = 0x080 set to request transfer to/from buffer7.
    // Buffer6 = 0x040 set to request transfer to/from buffer6.
    // Buffer5 = 0x020 set to request transfer to/from buffer5.
    // Buffer4 = 0x010 set to request transfer to/from buffer4.
    // Buffer3 = 0x008 set to request transfer to/from buffer3.
    // Buffer2 = 0x004 set to request transfer to/from buffer2.
    // Buffer1 = 0x002 set to request transfer to/from buffer1.
    // Buffer0 = 0x001 set to request transfer to/from buffer0.
    // First calculate how many 512 byte buffers fit in here.
    temp = (byteCount / NAND_ECC_BLOCK_SIZE);
    mask = ((1 << temp) - 1);
    temp *= NAND_ECC_BLOCK_SIZE;

    // If there are any leftovers, assume they are redundant area.
    if (byteCount - temp)
    {
        mask |= BV_GPMI_ECCCTRL_BUFFER_MASK__AUXILIARY;
    }

    if (dataCount)
    {
        *dataCount = temp;
    }

    if (auxCount)
    {
        *auxCount = byteCount - temp;
    }

    return mask;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t ddi_ecc8_init(void)
{
    // Bring out of reset and disable Clk gate.
    // Soft Reset the ECC8 block
    ddi_ecc8_soft_reset();

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_ecc8_soft_reset(void)
{
    int64_t musecs;

    // Just for reliability, make sure AHBM soft-reset is not asserted.
    HW_ECC8_CTRL_CLR( BM_ECC8_CTRL_AHBM_SFTRST );

    // Reset the ECC8_CTRL block.
    // Prepare for soft-reset by making sure that SFTRST is not currently
    // asserted.
    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_SFTRST);

    // Wait at least a microsecond for SFTRST to deassert.
    musecs = hw_profile_GetMicroseconds();
    while (HW_ECC8_CTRL.B.SFTRST || (hw_profile_GetMicroseconds() - musecs < DDI_NAND_HAL_RESET_ECC8_SFTRST_LATENCY));

    // Also clear CLKGATE so we can wait for its assertion below.
    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_CLKGATE);

    // Now soft-reset the hardware.
    HW_ECC8_CTRL_SET(BM_ECC8_CTRL_SFTRST);

    // Poll until clock is in the gated state before subsequently
    // clearing soft reset and clock gate.
    while (!HW_ECC8_CTRL.B.CLKGATE)
    {
        ; // busy wait
    }

    // Deassert SFTRST.
    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_SFTRST);

    // Wait at least a microsecond for SFTRST to deassert. In actuality, we
    // need to wait 3 GPMI clocks, but this is much easier to implement.
    musecs = hw_profile_GetMicroseconds();
    while (HW_ECC8_CTRL.B.SFTRST || (hw_profile_GetMicroseconds() - musecs < DDI_NAND_HAL_RESET_ECC8_SFTRST_LATENCY));

    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_CLKGATE);

    // Poll until clock is in the NON-gated state before returning.
    while (HW_ECC8_CTRL.B.CLKGATE)
    {
        ; // busy wait
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_ecc8_enable(void)
{
    int64_t musecs;

    // Just for reliability, make sure AHBM soft-reset is not asserted.
    HW_ECC8_CTRL_CLR( BM_ECC8_CTRL_AHBM_SFTRST );

    // Deassert SFTRST.
    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_SFTRST);

    // Wait at least a microsecond for SFTRST to deassert.
    musecs = hw_profile_GetMicroseconds();
    while (HW_ECC8_CTRL.B.SFTRST || (hw_profile_GetMicroseconds() - musecs < DDI_NAND_HAL_RESET_ECC8_SFTRST_LATENCY));

    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_CLKGATE);

    // Poll until clock is in the NON-gated state before returning.
    while (HW_ECC8_CTRL.B.CLKGATE)
    {
        ; // busy wait
    }

}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_ecc8_disable(void)
{
    // Gate the ECC8 block
    HW_ECC8_CTRL_SET( BM_ECC8_CTRL_CLKGATE );
}

//! @}
