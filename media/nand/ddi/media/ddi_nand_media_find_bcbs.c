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
//! \file ddi_nand_media_find_bcbs.c
//! \brief This file contains the functions for manipulating the Boot Control
//!        blocks including saving and retrieving the bad block table on
//!        the NAND.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_media.h"
#include "ddi_nand.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/nand/rom_support/ddi_nand_hamming_code_ecc.h"
#include "components/telemetry/tss_logtext.h"
#include <stdlib.h>

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Search the NAND for a boot block (NCB or LDLB) and read it from NAND if found.
//!
//! This function searches through the NAND looking for a matching boot block.
//! A block is considered matching if all three finger print values match
//! those in the \a pFingerPrintValues table.
//!
//! \param[in] u32NandDeviceNumber Physical NAND number to search.
//! \param[in] pFingerPrintValues Pointer to a table of finger prints.
//! \param[in,out] p32SearchSector On entering this function, this points to
//!     the number of the sector to start searching from. On exit, it's set
//!     to the sector at which the search stopped. If an error is returned,
//!     this value is not modified. This value is always in natural pages,
//!     not the 2K pages the ROM uses.
//! \param[in,out] pBuffer Pointer to memory to store the page reads. The buffer must
//!     be large enough to hold an entire page.  On SUCCESSful exit, contains the page that was read.
//! \param[in] u32NANDBootSearchLimit Number of sectors to search.
//! \param[in] bDecode Decode data before validating. Since only NCB is software encoded on the STMP378x, this parameter should be true for only NCB search.
//! \param[out] ppBCB the decoded bcb address will be returned in this parameter. A NULL should be passed for non NCB search and a valid address for NCB search.
//!
//! \retval    SUCCESS    No error has occurred.
//! \retval    ERROR_ROM_NAND_DRIVER_DMA_TIMEOUT  GPMI DMA timed out during read.
//! \retval    ERROR_ROM_NAND_DRIVER_NO_BCB  Couldn't find Boot Control Block.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::bootBlockSearch(
    uint32_t u32NandDeviceNumber,
    const FingerPrintValues_t *pFingerPrintValues,
    uint32_t * p32SearchSector,
    SECTOR_BUFFER * pBuffer,
    SECTOR_BUFFER * pAuxBuffer,
    bool bDecode,
    BootBlockStruct_t **ppBCB)
{
    bool bFoundBootBlock = false;
    uint32_t iBlockToSearch;
    RtStatus_t retStatus = SUCCESS;
    uint32_t iReadSector;
    NandPhysicalMedia * pNandPhysicalMediaDesc = NandHal::getNand(u32NandDeviceNumber);

    // find the boot block
    for(iBlockToSearch=0; ((iBlockToSearch<m_bootBlockSearchNumber) &&
             ((retStatus == SUCCESS) || (retStatus == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)) &&  // If ECC error, let's try again.
             !bFoundBootBlock); iBlockToSearch++)
    {
        // We skip g_NandBootBlockSearchStride pages between read attempts.
        iReadSector = (iBlockToSearch * kBootBlockSearchStride) + *p32SearchSector;

        #ifdef DEBUG_BOOT_BLOCK_SEARCH
        tss_logtext_Print(0,"Read Start\n");
        #endif

        // Perform the sector read. If bDecode is set, then we must use a non-ECC-corrected
        // read, since the error correction is done in software.
        if (bDecode)
        {
            retStatus = pNandPhysicalMediaDesc->readRawData(iReadSector, 0, m_params->pageTotalSize, pBuffer);
        }
        else
        {
            retStatus = pNandPhysicalMediaDesc->readFirmwarePage(iReadSector, pBuffer, pAuxBuffer, 0);
        }

        if (is_read_status_error_excluding_ecc(retStatus))
        {
            return retStatus;
        }

        #ifdef DEBUG_BOOT_BLOCK_SEARCH
        tss_logtext_Print(0,"Read Complete\n");
        #endif

        // If ECC was successful, we need to check the signature.
        if (is_read_status_success_or_ecc_fixed(retStatus))
        {
            // Force success status to cover the case where ECC_FIXED is returned from the read,
            // so we don't end up returning that status to the caller.
            retStatus = SUCCESS;
            
            if (bDecode)
            {
				// Try to find it using the new method.  This methode is currently
				// only being used on 3780 TA3, but since we don't know if the 
				// NAND in this system has been moved between 378x TA2 and TA3
				// we should check for both types of NCD
				retStatus = ddi_nand_media_decodeBCB_New((uint8_t *)pBuffer, ppBCB);

				// If the above fails then this is either using the old
				// NCB format or it's new NAND.  See if the old NCB
				// format is on the NAND.  We MUST find this out.  If
				// the updater does not find a NCB it assumes the NAND 
				// is new and will convert the factory bad block marks
				// to Freescall ECC version.  If this is done twice 
				// you will have lost all bad block information for
				// ever.
				if (retStatus != SUCCESS)
				{
					retStatus = ddi_nand_media_DecodeBCB((uint8_t *)pBuffer, ppBCB);
				}

                if (retStatus == SUCCESS)
                {
                    // Match signatures to determine if this was successful. ppBCB points to the good copy of BCB in pBuffer
                    bFoundBootBlock = ddi_nand_media_doFingerprintsMatch((BootBlockStruct_t*)*ppBCB,
                                pFingerPrintValues);
                }
            }
            else
            {
                // Match signatures to determine if this was successful.
                bFoundBootBlock = ddi_nand_media_doFingerprintsMatch((BootBlockStruct_t*)pBuffer,
                              pFingerPrintValues);
            }
        }
    }

    if (!bFoundBootBlock && retStatus==SUCCESS)
    {
        //didn't find a Boot block within the desired search area.
        retStatus = ERROR_DDI_NAND_DRIVER_NO_BCB;
    }

    // Remember where we stopped.
    *p32SearchSector = iReadSector;

    return retStatus;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief Searches for NCB by passing appropriate parameters to 
//! ddi_nand_media_BootBlockSearch function. 
//!
//! Two conditions are required to claim that the NCB has been found.  The software 
//! ECC must not exceed the limit and the fingerprints embedded in the page must
//! match the expected fingerprints.
//!
//! \param[in] u32CurrentNAND Number of the NAND to read from.
//! \param[in,out] On entering this function, this points to
//!     the number of the sector to start searching from. On exit, it's set
//!     to the sector at which the search stopped. If an error is returned,
//!     this value is not modified.
//! \param[in] pBuffer Pointer to buffer to use for sector reads. On SUCCESS, contains the NCB page that was read.
//! \param[in] pAuxBuffer Pointer to auxiliary buffer to use for reading from the device.
//!
//! \retval SUCCESS The NCB was found and read.
//! \retval ERROR_ROM_NAND_DRIVER_DMA_TIMEOUT  GPMI DMA timed out during read.
//! \retval ERROR_DDI_NAND_DRIVER_NCB_MEM_ALLOC_FAILED Failed to allocate memory.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::ncbSearch(uint32_t u32CurrentNAND, uint32_t * pReadSector, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer)
{
    RtStatus_t retStatus;

    // Search for NCB
#if defined(STMP37xx) || defined(STMP377x)
    retStatus = bootBlockSearch(u32CurrentNAND, &zNCBFingerPrints, pReadSector, pBuffer, pAuxBuffer, false, NULL);
#elif defined(STMP378x)

    BootBlockStruct_t * pNCB;

    // NCB is to be read raw and require the buffer size to be 2112 bytes, bBuffer is allocated less so can't use it. 
    // Allocating enough memory to read raw NCB data.
    MediaBuffer ncbBuffer;
    if ((retStatus = ncbBuffer.acquire(kMediaBufferType_NANDPage)) != SUCCESS)
    {
        return retStatus;
    }

    // For 378x chip, NCB is software encoded using hamming codes and triple redundancy check. So here we will direct 
    // ddi_nand_media_BootBlockSearch funtion to read the NCB then decode it and return the pointer to valid NCB data in last parameter
    retStatus = bootBlockSearch(u32CurrentNAND, &zNCBFingerPrints, pReadSector, ncbBuffer, pAuxBuffer, true, &pNCB);

    if (retStatus == SUCCESS)
    {
        // The NCB is returned in pNCB pointer, copy it to the output buffer
        memcpy((uint8_t*)pBuffer, (uint8_t*)pNCB, NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES);
    }
#else
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

    return retStatus;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Searches for the NCB and saves its contents if found.
//!
//! Two conditions are required to claim that the NCB has been found.  The ECC
//! must not exceed the limit and the fingerprints embedded in the page must
//! match the expected fingerprints.
//!
//! \param[in] u32CurrentNAND Number of the NAND to read from.
//! \param[in,out] On entering this function, this points to
//!     the number of the sector to start searching from. On exit, it's set
//!     to the sector at which the search stopped. If an error is returned,
//!     this value is not modified.
//! \param[in] pBuffer Pointer to buffer to use for sector reads. On SUCCESS, contains the NCB page that was read.
//! \param[in] pAuxBuffer Pointer to auxiliary buffer to use for reading from the device.
//! \param[in] loadParameters The timings and physical descriptor parameters
//!     from the NCB are loaded if this parameter is true. When set to false,
//!     the NCB is located but not processed.
//!
//! \retval SUCCESS The NCB was found and read.
//! \retval ERROR_ROM_NAND_DRIVER_DMA_TIMEOUT  GPMI DMA timed out during read.
//! \retval ERROR_ROM_NAND_DRIVER_NO_BCB The NCB was not found.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::findNCB(uint32_t u32CurrentNAND, uint32_t * pReadSector, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, bool loadParameters)
{
    RtStatus_t retStatus;
    NAND_Timing_t zNANDTiming;
    NAND_Timing2_struct_t NAND_Timing2_struct;
    NandPhysicalMedia * pNandPhysicalMediaDesc = NandHal::getNand(u32CurrentNAND);
    BootBlockStruct_t * pNCB;
    
    // search for NCB and read it from the NAND if found.
    retStatus = ncbSearch(u32CurrentNAND, pReadSector, pBuffer, pAuxBuffer);

    // If we were successful, now read the LDLB.
    if (retStatus != SUCCESS)
    {
        #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"NCB Search Result = 0x%X\n", retStatus);
        #endif

        return retStatus;
    }

    #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    tss_logtext_Print(0,"--->NCB found on NAND %d at sector %d.\n", u32CurrentNAND, *pReadSector);
    #endif

    if (loadParameters)
    {
        pNCB = (BootBlockStruct_t *)pBuffer;
        // load timing parameters
        zNANDTiming = pNCB->NCB_Block1.m_NANDTiming;

        // Speculatively load the bytes from the NAND_Timing2_struct area.
        // We don't know if this is actually initialized in this NCB, because
        // this could be an old NAND.
        NAND_Timing2_struct = pNCB->FirmwareBlock.NAND_Timing2_struct;

        // Find out if the NAND_Timing2_struct is initialized.
        // If so, the two timing structs will agree.
        if (zNANDTiming.NAND_Timing != NAND_Timing2_struct)
        {
            // They don't agree, so the NAND_Timing2_struct is not initialized.
            // Just use the more-limited information from zNANDTiming.
            // Copy using assignment operator.
            NAND_Timing2_struct = zNANDTiming.NAND_Timing;
        }

        // Don't set the timings from the NCB.
        // Explanation: See PHA-631.  Setting the timings from the NCB makes it
        // impossible to adjust the timings using a simple software update later.
        //
        // retStatus = ddi_gpmi_set_timings(&NAND_Timing2_struct, true /* bWriteToTheDevice */);

        // Load the parameters from the NAND page if some of these parameters haven't been filled in.
        // Note that the parameters in the NCB may not always be correct. They may have been modified
        // to help the ROM read only 2K from the full page if it can't reach subsequent 2K subpages.
        if (!pNandPhysicalMediaDesc->pNANDParams->wPagesPerBlock)
        {
            pNandPhysicalMediaDesc->pNANDParams->wPagesPerBlock = pNCB->NCB_Block1.m_u32SectorsPerBlock;
            pNandPhysicalMediaDesc->pNANDParams->pageDataSize = pNCB->NCB_Block1.m_u32DataPageSize;
            pNandPhysicalMediaDesc->pNANDParams->pageTotalSize = pNCB->NCB_Block1.m_u32TotalPageSize;
            // Load data from 2nd NCB bank.
            pNandPhysicalMediaDesc->pNANDParams->wNumRowBytes = pNCB->NCB_Block2.m_u32NumRowBytes;

            #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
            tss_logtext_Print(0,"Total Page Size = %d\n",  pNandPhysicalMediaDesc->pNANDParams->pageTotalSize);
            tss_logtext_Print(0,"Number of Row Bytes = %d\n",  pNandPhysicalMediaDesc->pNANDParams->wNumRowBytes);
            #endif
#if defined(STMP378x)
            // For 378x, ecc type is embedded in NCB and no longer derived from boot mode.
            pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.eccType = (NandEccType_t)pNCB->NCB_Block2.m_u32ECCType;
            // Check for BCH ECC type.
            if (pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.isBCH())
            {
                // We have to re-use the block N count from the current ECC descriptor, because
                // we force the one in the NCB to be 3 in order to have the ROM read only 2K
                // from each firmware page.
                pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.eccTypeBlock0     = (NandEccType_t)pNCB->NCB_Block2.m_u32EccBlock0EccLevel;
                pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.u32SizeBlockN     = pNCB->NCB_Block2.m_u32EccBlockNSize;
                pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.u32SizeBlock0     = pNCB->NCB_Block2.m_u32EccBlock0Size;
                pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.u32NumEccBlocksN  = pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.u32NumEccBlocksN;
                pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.u32MetadataBytes  = pNCB->NCB_Block2.m_u32MetadataBytes;
                pNandPhysicalMediaDesc->pNANDParams->eccDescriptor.u32EraseThreshold = pNCB->NCB_Block2.m_u32EraseThreshold;
            }
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif
        }
    }

    return retStatus;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief Searches for the LDLB on the NAND and reads its contents into NandMediaInfo.
//!
//! Two conditions are required to claim that the LDLB has been found.  The ECC
//! must not exceed the limit and the fingerprints embedded in the page must
//! match the expected fingerprints.
//! 
//! The contents that are read into NandMediaInfo include:
//!     + Firmware location
//!     + DBBTs locations
//! 
//! The block address at which the LDLB was found is also archived
//! in NandMediaInfo.
//!
//! \param[in] u32CurrentNAND Number of the NAND to read from.
//! \param[in,out] On entering this function, this points to
//!     the number of the sector to start searching from. On exit, it's set
//!     to the sector at which the search stopped. If an error is returned,
//!     this value is not modified.
//! \param[in] pBuffer Pointer to buffer to use for sector reads. On SUCCESS, contains the LDLB that was read.
//! \param[in] pAuxBuffer Pointer to auxiliary buffer to use for reading from the device.
//! \param[in] u32UseSecondaryBoot Boolean value set if we should use secondary
//!            boot.
//! \param[in] loadParameters If true, the addresses in the LDLB will be
//!     saved into #NandMediaInfo.
//!
//! \retval SUCCESS The LDLB was found and read.
//! \retval ERROR_ROM_NAND_DRIVER_DMA_TIMEOUT  GPMI DMA timed out during read.
//! \retval ERROR_ROM_NAND_DRIVER_NO_BCB The LDLB was not found.
//!
//! \note The global #NandMediaInfo will be modified if loadParameters is true.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::findLDLB(uint32_t u32CurrentNAND, uint32_t * pReadSector, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, bool b32UseSecondaryBoot, bool loadParameters)
{
    RtStatus_t retStatus;
    BootBlockStruct_t * pLDLB;
    uint32_t u32SectorsPerBlockShift = m_params->pageToBlockShift;

    // Now search for the LDLB and read it from the NAND if found.
    retStatus = bootBlockSearch(u32CurrentNAND, &zLDLBFingerPrints, pReadSector, pBuffer, pAuxBuffer, false, NULL);

    // if successful, we'll want to load the starting sector of boot image.
    if(retStatus != SUCCESS)//lets kick off the first one
    {
        #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"LDLB Search Result = 0x%X\n", retStatus);
        #endif

        return retStatus;
    }

    #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    tss_logtext_Print(0,"--->LDLB found on NAND %d at sector %d.\n", u32CurrentNAND, *pReadSector);
    #endif

    if (loadParameters)
    {
        unsigned pageToSector;

        // Now use the LDLB to load important variables for finding the boot image.
        pLDLB = (BootBlockStruct_t *)pBuffer;

        // Compute the multiplier to convert from natural NAND pages to 2K sectors.
        pageToSector = m_params->pageDataSize / NAND_PAGE_SIZE_2K;

        // Depending upon whether we're booting from the primary boot blocks or the secondary
        // boot blocks, we may need to start at different places.  The Primary boot block
        // will always be used unless an error occurs.
        if (!b32UseSecondaryBoot)
        {
            // We know the current firmware is valid because we just booted.
            m_bootBlocks.m_currentFirmware.b.bfNANDNumber = pLDLB->LDLB_Block2.m_u32Firmware_startingNAND;
            m_bootBlocks.m_currentFirmware.b.bfBlockAddress = (pLDLB->LDLB_Block2.m_u32Firmware_startingSector >> u32SectorsPerBlockShift) / pageToSector;
            m_bootBlocks.m_currentFirmware.b.bfBlockProblem = kNandBootBlockValid;

            // The LDLB is valid because we just read it.
            m_bootBlocks.m_ldlb1.b.bfNANDNumber = u32CurrentNAND;
            m_bootBlocks.m_ldlb1.b.bfBlockAddress = (*pReadSector >> u32SectorsPerBlockShift);
            m_bootBlocks.m_ldlb1.b.bfBlockProblem = kNandBootBlockValid;
        }
        else
        {
            // We know the current firmware is valid because we just booted.
            m_bootBlocks.m_currentFirmware.b.bfNANDNumber = pLDLB->LDLB_Block2.m_u32Firmware_startingNAND2;
            m_bootBlocks.m_currentFirmware.b.bfBlockAddress = (pLDLB->LDLB_Block2.m_u32Firmware_startingSector2 >> u32SectorsPerBlockShift) / pageToSector;
            m_bootBlocks.m_currentFirmware.b.bfBlockProblem = kNandBootBlockValid;

            // The LDLB is valid because we just read it.
            m_bootBlocks.m_ldlb2.b.bfNANDNumber = u32CurrentNAND;
            m_bootBlocks.m_ldlb2.b.bfBlockAddress = (*pReadSector >> u32SectorsPerBlockShift);
            m_bootBlocks.m_ldlb2.b.bfBlockProblem = kNandBootBlockValid;
        }

        // Fill in the primary and secondary DBBT addresses, but mark the block
        // state as unknown since we haven't actually tried to read either DBBT yet.
        // Whe FindBootControlBlocks continues, it will set the state value appropriately.
        m_bootBlocks.m_dbbt1.b.bfNANDNumber = pLDLB->LDLB_Block2.m_u32Firmware_startingNAND;
        m_bootBlocks.m_dbbt1.b.bfBlockAddress = (pLDLB->LDLB_Block2.m_u32DiscoveredBBTableSector >> u32SectorsPerBlockShift);
        m_bootBlocks.m_dbbt1.b.bfBlockProblem = kNandBootBlockUnknown;

        m_bootBlocks.m_dbbt2.b.bfNANDNumber = pLDLB->LDLB_Block2.m_u32Firmware_startingNAND2;
        m_bootBlocks.m_dbbt2.b.bfBlockAddress = (pLDLB->LDLB_Block2.m_u32DiscoveredBBTableSector2 >> u32SectorsPerBlockShift);
        m_bootBlocks.m_dbbt2.b.bfBlockProblem = kNandBootBlockUnknown;
    }

    return retStatus;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief Find Boot Control Blocks (NCB, LDLB, and DBBT) to initialize NAND layout structure.
//!
//! This function searches for the NAND Control Block (NCB) which contains the NAND
//! Timing and Control values.  It then scans for the Logical Device Layout
//! Block (LDLB) which contains information about where the Discovered Bad Block Table
//! and Primary Firmware can be found.  If an error occurs during the
//! search, this function will use the secondary NCB, LDLB and Firmware sectors
//! to continue loading.
//!
//! \param[in] pBuffer Use this buffer for reading sectors during discovery.
//! \param[in] pAuxBuffer Pointer to auxiliary buffer to use for reading from the device.
//! \param[in] allowRecovery If set to true and either the #RTC_NAND_SECONDARY_BOOT
//!     persistent bit is set or if secondary boot blocks were required then
//!     an attempt is made to recover the primary boot blocks.
//!
//! \retval    0 (SUCCESS)    If no error has occurred.
//! \retval    ERROR_ROM_NAND_DRIVER_DMA_TIMEOUT  GPMI DMA timed out during read.
//! \retval    ERROR_ROM_NAND_DRIVER_NO_BCB  Couldn't find Boot Control Block.
//!
//! \note The global structure #NandMediaInfo is modified with values read from
//!         the NCB and LDLB.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::findBootControlBlocks(SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, bool allowRecovery)
{
    RtStatus_t retStatus;
    uint32_t iReadSector = 0;
    uint32_t u32CurrentNAND = NAND0;                // Assume we'll boot from primary.
    bool bNowFindingSecondary;                      // Controls whether this search is looking for the primary or secondary boot blocks.
    bool bFailedToFindPrimary_WillUseSecondaryBCB;  // Indicates whether this search has failed to find the primary BCBs,
                                                    // and decided that the secondary BCBs must be used.

    // Always try to boot with the primary blocks because if there is
    // a problem we need to discover which block is bad.
    bFailedToFindPrimary_WillUseSecondaryBCB = false;

    #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    tss_logtext_Print(0,"Boot Search on Primary\n");
    #endif

    // We only need to search for the NCBs once since they never change location.
    if (!m_bootBlocks.m_isNCBAddressValid)
    {
        m_bootBlocks.m_ncb1.u = NAND_BOOT_BLOCK_UNKNOWN;
        m_bootBlocks.m_ncb2.u = NAND_BOOT_BLOCK_UNKNOWN;

        // Now we want to start the search for the NCB.

        bNowFindingSecondary = false;
        do {
            // If we need to use the secondary boot, set the start vector
            // The initial conditions work fine for the Primary boot -
            // always on NAND0 using sector 0
            if (bFailedToFindPrimary_WillUseSecondaryBCB || bNowFindingSecondary)
            {
                if (NandHal::getChipSelectCount() > 1)
                {
                    u32CurrentNAND = OTHER_NAND_FOR_SECONDARY_BCBS;
                    iReadSector = 0;
                }
                else // In single NAND case, secondary boot is in 2nd search area
                {   // Aren't we already here?
                    iReadSector = m_bootBlockSearchWindow;
                }
            }

            // search for NCBs on the NAND and read its contents if found.
            retStatus = findNCB(u32CurrentNAND, &iReadSector, pBuffer, pAuxBuffer, !bNowFindingSecondary);
            if (retStatus != SUCCESS)
            {
                // We failed to find the NCB.

                // If we failed while searching for the secondary boot but we can't
                // use the first boot block, we're out of luck. Return an error.
                if (bFailedToFindPrimary_WillUseSecondaryBCB)
                {
                    #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
                    tss_logtext_Print(0,"Failed to find NCB\n");
                    #endif
                    return ERROR_DDI_NAND_BCB_SEARCH_FAILED;
                }

                #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
                tss_logtext_Print(0,"..Searching for NCB2..\n");
                #endif

                // If we got to this point, we failed to find an NCB, and we did
                // not decide to use the secondary.

                if (!bNowFindingSecondary)
                {
                    // We were searching for the primary, but did not find it.
                    // Permanently switch to secondary.

                    // Record that the NCB primary Boot Block search failed on the given NAND.
                    m_bootBlocks.m_ncb1.b.bfNANDNumber = u32CurrentNAND;
                    m_bootBlocks.m_ncb1.b.bfBlockProblem = kNandBootBlockInvalid;

                    // The search for NCB failed, set a flag to indicate we need
                    // to search for NCB2 (secondary boot control blocks).
                    bFailedToFindPrimary_WillUseSecondaryBCB = true;
                }
                else
                { // true==bNowFindingSecondary
                    // We were searching for the secondary, but did not find it.

                    // If the secondary scan failed but the primary succeeded, just
                    // record that the NCB secondary Boot Block search failed on the given NAND.
                    m_bootBlocks.m_ncb2.b.bfNANDNumber = u32CurrentNAND;
                    m_bootBlocks.m_ncb2.b.bfBlockProblem = kNandBootBlockInvalid;

                    break;
                }
            } // if (did-not-find-an-NCB)
            else
            { // Found an NCB

                // Update NCB addresses.
                if (!(bFailedToFindPrimary_WillUseSecondaryBCB || bNowFindingSecondary))
                {
                    // Update primary addresses
                    m_bootBlocks.m_ncb1.b.bfNANDNumber = u32CurrentNAND;
                    m_bootBlocks.m_ncb1.b.bfBlockAddress = NandHal::getNand(u32CurrentNAND)->pageToBlock(iReadSector);
                    m_bootBlocks.m_ncb1.b.bfBlockProblem = kNandBootBlockValid;

                    // On the next loop, search for the secondary.
                    bNowFindingSecondary = true;
                }
                else
                {
                    // Update secondary addresses
                    m_bootBlocks.m_ncb2.b.bfNANDNumber = u32CurrentNAND;
                    m_bootBlocks.m_ncb2.b.bfBlockAddress = NandHal::getNand(u32CurrentNAND)->pageToBlock(iReadSector);
                    m_bootBlocks.m_ncb2.b.bfBlockProblem = kNandBootBlockValid;

                    // When find-secondary mode, switch the current NAND back to
                    // the first one before exiting the loop.
                    if (bNowFindingSecondary)
                    {
                        u32CurrentNAND = NAND0;
                    }

                    break;
                }
            } // Found an NCB

            // Continue while search fails.  This can only go 2 times max.
        } while (true);

        // The NCB is considered present as long as there is not a problem with NCB1.
        // NCB2 can have a problem and the addresses are still considered valid. Vice versa
        // is also true.
        if (!m_bootBlocks.m_ncb1.b.bfBlockProblem || !m_bootBlocks.m_ncb2.b.bfBlockProblem)
        {
            m_bootBlocks.m_isNCBAddressValid = true;
        }

    } // if (!NandMediaInfo.isNCBAddressValid)

    // Now we want to start the search for the LDLB.  The # of times to search depends
    // on whether we start with the primary or secondary boot ("bFailedToFindPrimary_WillUseSecondaryBCB").
    m_bootBlocks.m_ldlb1.u = NAND_BOOT_BLOCK_UNKNOWN;
    m_bootBlocks.m_ldlb2.u = NAND_BOOT_BLOCK_UNKNOWN;
    bNowFindingSecondary = false;
    do {
        // Set the start vector to begin the search.  First check to
        // see if we want to use the primary boot or the secondary boot.
        if (!(bFailedToFindPrimary_WillUseSecondaryBCB || bNowFindingSecondary))
        {
            // Use primary.

            // for the multi-NAND case, the LDLB is right after NCB.
            iReadSector = m_bootBlockSearchWindow;

            // In the single NAND case, the LDLB is after NCB2 which is after NCB.
            // only saying <1 in case this fuse hasn't been programmed.
            if (NandHal::getChipSelectCount() <= 1)
            {
                iReadSector *= 2;
            }

        }
        else // use secondary
        {
            // In the multi-NAND case, we must search on the second NAND for LDLB2,
            // which is after NCB2.
            if (NandHal::getChipSelectCount() > 1)
            {
                u32CurrentNAND = OTHER_NAND_FOR_SECONDARY_BCBS;
                iReadSector = m_bootBlockSearchWindow;
            }
            else
            {
                // Aren't we already here?
                iReadSector = 3 * m_bootBlockSearchWindow;
            }
        }

        // search for LDLBs on the NAND and read its contents if found.
        retStatus = findLDLB(u32CurrentNAND, &iReadSector, pBuffer, pAuxBuffer, bFailedToFindPrimary_WillUseSecondaryBCB, !bNowFindingSecondary);
        if (retStatus != SUCCESS)
        {
            // If we failed while searching for the Secondary boot, we're
            // out of luck.  Return an error.
            if (bFailedToFindPrimary_WillUseSecondaryBCB)
            {
                #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
                tss_logtext_Print(0,"Failed to find LDLB\n");
                #endif
                return ERROR_DDI_NAND_BCB_SEARCH_FAILED;
            }

            #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
            tss_logtext_Print(0,"..Searching for LDLB2..\n");
            #endif

            // If we got to this point, we failed to find an LDLB, and we did
            // not decide to use the secondary.

            if (!bNowFindingSecondary)
            {
                // We were searching for the primary, but did not find it.
                // Permanently switch to secondary.

                // Record that the LDLB primary Boot Block search failed on the given NAND.
                m_bootBlocks.m_ldlb1.b.bfNANDNumber = u32CurrentNAND;
                m_bootBlocks.m_ldlb1.b.bfBlockProblem = kNandBootBlockInvalid;

                // The search for LDLB failed, set a flag to indicate we need
                // to search for LDLB2 (secondary boot control blocks).
                // From now on, only the secondary boot blocks LDLB2, FW2, will
                // be used.
                bFailedToFindPrimary_WillUseSecondaryBCB = true;
            }
            else
            { // true==bNowFindingSecondary
                // We were searching for the secondary, but did not find it.

                // If the secondary failed but the primary succeeded,
                // just record that the LDLB secondary Boot Block search failed on the given NAND.
                m_bootBlocks.m_ldlb2.b.bfNANDNumber = u32CurrentNAND;
                m_bootBlocks.m_ldlb2.b.bfBlockProblem = kNandBootBlockInvalid;

                break;
            }
        } // if (did-not-find-an-LDLB)
        else // success
        { // Found an LDLB

            // We don't need to record the LDLB1 (or LDLB2 in useSecondaryBoot mode) address
            // here like we do with the NCB because ddi_nand_media_FindLDLB() does that job
            // for us. We only need to record the LDLB2 address in findSecondary mode.
            if (bNowFindingSecondary)
            {
                // Record secondary LDLB's location.
                m_bootBlocks.m_ldlb2.b.bfNANDNumber = u32CurrentNAND;
                m_bootBlocks.m_ldlb2.b.bfBlockAddress = NandHal::getNand(u32CurrentNAND)->pageToBlock(iReadSector);
                m_bootBlocks.m_ldlb2.b.bfBlockProblem = kNandBootBlockValid;

                // When find-secondary mode, switch the current NAND back to
                // the first one before exiting the loop.
                u32CurrentNAND = NAND0;
            }
            else
            {
                // We've found the primary LDLB, so go scan for the secondary now.
                // This is only to record its location since we've already loaded
                // info from the primary LDLB.
                bNowFindingSecondary = true;
                continue;
            }

            // Exit this loop the second time through.
            break;
        }  // Found an LDLB

        // Continue while search fails.  This can only go 2 times max.
    } while (true);

    // Now search for the Discovered Bad Block Table (DBBT).

    // Set the start vector to begin the search. The FindLDLB function called above has
    // filled in the addresses of the primary and secondary DBBT. We always start off looking
    // for the DBBT1, even if using secondary boot blocks. (For one, this works around the
    // fact that DBBT2 is never currently written by the SDK.) This is not a big deal,
    // since the LDLB contains pointers to both the primary and secondary DBBT.

    // Start the search where the LDLB tells us it should be.
    u32CurrentNAND = m_bootBlocks.m_dbbt1.b.bfNANDNumber;
    iReadSector = NandHal::getNand(u32CurrentNAND)->blockToPage(m_bootBlocks.m_dbbt1.b.bfBlockAddress);

    // Now search for the DBBT1 and read it from the NAND if found.
    retStatus = bootBlockSearch(u32CurrentNAND, &zDBBTFingerPrints, &iReadSector, pBuffer, pAuxBuffer, false, NULL);

    if (retStatus != SUCCESS)
    {
        // We were looking for zDBBT1BlockAddr, but failed.

        #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"Failed to find DBBT1\n");
        #endif

        // Record that the DBBT primary Boot Block was not found.
        m_bootBlocks.m_dbbt1.b.bfBlockProblem = kNandBootBlockInvalid;
    }
    else
    {
        // Primary DBBT is valid.
        m_bootBlocks.m_dbbt1.b.bfBlockProblem = kNandBootBlockValid;

        #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"--->DBBT found on NAND %d at sector %d.\n", u32CurrentNAND, iReadSector);
        #endif
    }

    // Now search for the other DBBT and read if from the NAND if found.
    // Start the search for other DBBT where the LDLB tells us it should be.
    u32CurrentNAND = m_bootBlocks.m_dbbt2.b.bfNANDNumber;
    iReadSector = NandHal::getNand(u32CurrentNAND)->blockToPage(m_bootBlocks.m_dbbt2.b.bfBlockAddress);

    retStatus = bootBlockSearch(u32CurrentNAND, &zDBBTFingerPrints, &iReadSector, pBuffer, pAuxBuffer, false, NULL);
    if (retStatus != SUCCESS)
    {
        // We were looking for zDBBT2BlockAddr, but failed.

        // Record that the DBBT secondary Boot Block was not found.
        m_bootBlocks.m_dbbt2.b.bfBlockProblem = kNandBootBlockInvalid;

        #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"Failed to find DBBT2\n");
        #endif

    } // if (did-not-find-DBBT)
    else
    {
        // Secondary DBBT is valid.
        m_bootBlocks.m_dbbt2.b.bfBlockProblem = kNandBootBlockValid;

        #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"--->DBBT found on NAND %d at sector %d.\n", u32CurrentNAND, iReadSector);
        #endif
    }

    if  ((m_bootBlocks.m_dbbt1.b.bfBlockProblem != kNandBootBlockValid) &&
         (m_bootBlocks.m_dbbt2.b.bfBlockProblem != kNandBootBlockValid))
    {
        // So.  No DBBT was found at all.

        #ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        tss_logtext_Print(0,"Failed to find any DBBT!\n");
        #endif
        // Some earlier versions of firmware did not write a DBBT
        // when there were no bad blocks on the NAND.
        // Therefore, if we did not find any DBBT, then we will assume that this is the case.
        // i.e. There are no bad blocks on the NAND.
        // Current firmware will write a DBBT later during NAND Discovery.
        m_iNumBadBlks = 0;
    }

    return retStatus;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}




