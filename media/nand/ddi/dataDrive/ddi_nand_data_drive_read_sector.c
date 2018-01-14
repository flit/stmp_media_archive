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
//! \file
//! \brief Read routines for Nand Device Driver.
////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <stdio.h>
#include "types.h"
#include "components/telemetry/tss_logtext.h"
#include "hw/profile/hw_profile.h"
#include "Mapper.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "ddi_nand_media.h"
#include "VirtualBlock.h"
#include "MultiTransaction.h"
#include "NssmManager.h"
#include "NonsequentialSectorsMap.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// defs
////////////////////////////////////////////////////////////////////////////////

#ifndef REPORT_ECC_FAILURES
    #define REPORT_ECC_FAILURES 0
#endif

#if !defined(REPORT_ECC_REWRITES)
    #define REPORT_ECC_REWRITES 0
#endif

#ifndef DEBUG_DDI_NAND_READ_SECTOR
     #define DEBUG_DDI_NAND_READ_SECTOR
#endif

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#ifdef DEBUG_DDI_NAND_READ_SECTOR
bool g_nandEnableReadSectorDebug = false;
#endif

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! The block number conversion is very simple. This function scans all of the
//! regions associated with the drive. Each region
//! has a start physical block, a physical block count, and a logical block
//! count. The logical block count is simply the physical count minus any bad
//! blocks present in the region. When the region holding the logical block
//! is found, the virtual block is constructing by adding the logical offset
//! within the region to the absolute physical start block of the region.
//! The logical offset is simply the sum of logical blocks contained in all
//! prior regions subtracted from the logical block number.
//!
//! As you can see, there may be holes in the virtual block range for a given
//! drive. This is due to the bad blocks in a region not being counted in
//! the logical blocks for that region. The bad blocks are effectively being
//! combined together at the end of the region. There is no need to skip over
//! bad blocks in the virtual address range because data is not actually
//! written to the virtual blocks.
////////////////////////////////////////////////////////////////////////////////
DataRegion * DataDrive::getRegionForLogicalSector(uint32_t logicalSector, uint32_t & logicalSectorInRegion)
{
    uint32_t totalLogicalSectors = 0;
    DataRegion * scanRegion;
    Region::Iterator it(m_ppRegion, m_u32NumRegions);
    while ((scanRegion = (DataRegion *)it.getNext()))
    {
        uint32_t logicalPages = scanRegion->getNand()->blockToPage(scanRegion->getLogicalBlockCount());
        
        // Does our logical block sit in this region of the drive?
        if (logicalSector >= totalLogicalSectors && logicalSector < totalLogicalSectors + logicalPages)
        {
            // Return the physical sector to the caller.
            logicalSectorInRegion = logicalSector - totalLogicalSectors;
            
            // Return this region to the caller.
            return scanRegion;
        }
        
        // Add up logical blocks for all regions we've scanned so far.
        totalLogicalSectors += logicalPages;
    }
    
    return NULL;
}

RtStatus_t DataDrive::getSectorMapForLogicalSector(uint32_t logicalSector, uint32_t * logicalSectorInRegion, uint32_t * logicalOffset, NonsequentialSectorsMap ** map, DataRegion ** region)
{
    assert(logicalOffset);
    assert(map);
    
    // Look up the region and convert logical sector to be region relative.
    uint32_t localLogicalSectorInRegion;
    DataRegion * sectorRegion = getRegionForLogicalSector(logicalSector, localLogicalSectorInRegion);
    if (!sectorRegion)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }
    
    // We only use this virtual block object long enough to convert to the virtual address used
    // to find the NSSM. Once we have the NSSM, we use its virtual block instance instead.
    VirtualBlock vblock(m_media->getMapper());
    *logicalOffset = vblock.set(sectorRegion, localLogicalSectorInRegion);

    // Find the NSSM for this virtual block. If it isn't already in memory, the physical
    // block(s) will be scanned in order to build it.
    RtStatus_t status = m_media->getNssmManager()->getMapForVirtualBlock(vblock, map);
    if (status != SUCCESS)
    {
        return status;
    }
    assert(*map);
    
    // Return the logical sector relative to the region.
    if (logicalSectorInRegion)
    {
        *logicalSectorInRegion = localLogicalSectorInRegion;
    }
    
    // Return the region.
    if (region)
    {
        *region = sectorRegion;
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Read a page from the NAND.
//!
//! This function will read a page from the NAND.
//!
//! \param[in]  u32LogicalSectorNumber Logical Sector Number to be read.
//! \param[out]  pSectorData Pointer where sector data should be stored
//!              when reading.
//!
//! \return Status of call or error.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::readSector(uint32_t u32LogicalSectorNumber, SECTOR_BUFFER * pSectorData)
{
    RtStatus_t RetValue;

    RetValue = readSectorInternal(u32LogicalSectorNumber, pSectorData);

#ifdef DEBUG_DDI_NAND_READ_SECTOR
    if (RetValue != SUCCESS)
    {
        tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1), "NDDRS failure 0x%X, Drive %d, Lpage 0x%X\r\n",
            RetValue, m_u32Tag, u32LogicalSectorNumber);
    }
#endif

    return RetValue;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Internal implementation of readSector().
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::readSectorInternal(uint32_t u32LogicalSectorNumber, SECTOR_BUFFER * pSectorData)
{
    RtStatus_t status = SUCCESS;
    uint32_t u32LogicalSectorOffset;
    bool isOccupied;
    unsigned virtualSectorOffset;

    // Make sure we're initialized
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we won't go out of bounds
    if (u32LogicalSectorNumber >= m_u32NumberOfSectors)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    // Lock the NAND for our purposes.
    DdiNandLocker locker;

    // Disable auto sleep for the whole read process.
    NandHal::SleepHelper disableSleep(false);
    
    // Convert logical sector to be region relative. Then find the NSSM for this virtual block.
    // If it isn't already in memory, the physical block(s) will be scanned in order to build it.
    NonsequentialSectorsMap * sectorMap;
    status = getSectorMapForLogicalSector(u32LogicalSectorNumber, NULL, &u32LogicalSectorOffset, &sectorMap, NULL);
    if (status != SUCCESS)
    {
        return status;
    }
    assert(sectorMap);
    
    // Get the virtual block from the NSSM.
    VirtualBlock & vblock = sectorMap->getVirtualBlock();
    
    // Check if this is part of a transaction.
    bool isPartOfTransaction = (m_transaction
                                && m_transaction->isLive()
                                && !m_transaction->isWrite()
                                && vblock == m_transaction->getVirtualBlockAddress());

    // Get a buffer.
    AuxiliaryBuffer auxBuffer;
    if ((status = auxBuffer.acquire()) != SUCCESS)
    {
        return status;
    }

    if (!isPartOfTransaction)
    {
        // Look up the physical block containing the sector, to see if the block has been
        // allocated yet.
        PageAddress physicalPageAddress;
        status = sectorMap->getPhysicalPageForLogicalOffset(
            u32LogicalSectorOffset,
            physicalPageAddress,
            &isOccupied,
            &virtualSectorOffset);
        
        // Check if an attempt was made to read a sector which was never written to. If so, we can
        // avoid actually reading the page and just return all 0xffs.
        if (!isOccupied || status == ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR)
        {
             memset(pSectorData, 0xff, NandHal::getParameters().pageDataSize);
             return SUCCESS;
        }

#ifdef DEBUG_DDI_NAND_READ_SECTOR
        if (g_nandEnableReadSectorDebug)
        {
            tss_logtext_Print((LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1), "NDDRS Drive %x Lpage 0x%x VBlk 0x%x VOff 0x%x PBlk 0x%x\n", m_u32Tag, u32LogicalSectorNumber, vblock.get(), virtualSectorOffset, physicalPageAddress.getBlock().get());
        }
#endif

        // Read the sector.
        NandEccCorrectionInfo_t correctionInfo;
        status = physicalPageAddress.getNand()->readPage(physicalPageAddress.getRelativePage(), pSectorData, auxBuffer, &correctionInfo);

        if (is_read_status_error_excluding_ecc(status))
        {
            return status;
        }

        // Verify the ECC
        if (status != SUCCESS)
        {
#if REPORT_ECC_FAILURES
            log_ecc_failures(u32PhysicalBlockNumber, u32PhysicalSectorOffset, &correctionInfo);
#endif

            if (status == ERROR_DDI_NAND_HAL_ECC_FIXED)
            {
                // This error simply indicates that there were correctable bit errors.
            }
            else if (status == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
            {
                // There were uncorrectable bit errors in the data, so there's nothing we can do
                // except return an error.
                return status;
            }
            else if (status == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR)
            {
                // The ECC hit the threshold, so we must rewrite the block contents to a different
                // physical block, thus refreshing the data. Create a task to do it in the background.
                RelocateVirtualBlockTask * task = new RelocateVirtualBlockTask(m_media->getNssmManager(), sectorMap->getVirtualBlock());
                assert(task);
                if (task)
                {
                    m_media->getDeferredQueue()->post(task);
                }
           }
        }
    }
    else
    {
        // Save the address and buffers in the transaction object.
        m_transaction->pushSector(
            u32LogicalSectorNumber,
            u32LogicalSectorOffset,
            pSectorData,
            auxBuffer);
        
        // Clear the buffer object but don't actually free the buffer. We need it to stick around
        // until the transaction is complete.
        auxBuffer.relinquish();
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Prints a report about ECC failures.
//!
//! \param wPhysicalBlockNumber Physical block number on the NAND.
//! \param wSectorOffset Page number within the block that was read and found
//!     to be uncorrectable with ECC.
//! \param correctionInfo Pointer to the ECC correction result details structure.
////////////////////////////////////////////////////////////////////////////////
void log_ecc_failures(uint32_t wPhysicalBlockNumber, uint32_t wSectorOffset, NandEccCorrectionInfo_t * correctionInfo)
{
    int numEccs = correctionInfo->payloadCount;
    char buf[64];
    
    if (numEccs > 4)
    {
        // There are probably 8 payloads.
        sprintf(buf, "ECC[T%d B%d P%d %d %d %d %d %d %d %d %d M%d]\n",
            tx_time_get(),
            wPhysicalBlockNumber,
            wSectorOffset,
            correctionInfo->payloadCorrections[0],
            correctionInfo->payloadCorrections[1],
            correctionInfo->payloadCorrections[2],
            correctionInfo->payloadCorrections[3],
            correctionInfo->payloadCorrections[4],
            correctionInfo->payloadCorrections[5],
            correctionInfo->payloadCorrections[6],
            correctionInfo->payloadCorrections[7],
            correctionInfo->metadataCorrections);
    }
    else if (numEccs > 0)
    {
        // There are probably 4 payloads.
        sprintf(buf, "ECC[T%d B%d P%d %d %d %d %d M%d]\n",
            tx_time_get(),
            wPhysicalBlockNumber,
            wSectorOffset,
            correctionInfo->payloadCorrections[0],
            correctionInfo->payloadCorrections[1],
            correctionInfo->payloadCorrections[2],
            correctionInfo->payloadCorrections[3],
            correctionInfo->metadataCorrections);
    }
    else if (correctionInfo->isMetadataValid)
    {
        // Nothing but metadata.
        sprintf(buf, "ECC[T%d B%d P%d M%d]\n", 
            tx_time_get(),
            wPhysicalBlockNumber,
            wSectorOffset,
            correctionInfo->metadataCorrections);
    }
    
    // Print a single string with no substitutions so that TSS won't try to break up
    // the string into multiple packets, which can cause garbage output.
    tss_logtext_Print(LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1, buf);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
