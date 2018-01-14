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
//! \addtogroup ddi_nand_data_drive
//! @{
//! \file ddi_nand_data_drive_init.c
//! \brief This file handles the intialization of the data drive.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/ddi_media.h"
#include "Mapper.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "hw/core/vmemory.h"
#include <stdlib.h>
#include <algorithm>
#include "MultiTransaction.h"
#include "NssmManager.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
// Code
/////////////////////////////////////////////////////////////////////////////////

DataDrive::DataDrive(Media * media, Region * region)
:   LogicalDrive(),
    m_media(media),
    m_u32NumRegions(0),
    m_ppRegion(NULL),
    m_transactionStorage(),
    m_transaction(NULL)
{
    // Init inherited members from LogicalDrive.
    m_bInitialized = false;
    m_bPresent = true;
    m_bErased = false;
    m_bWriteProtected = false;
    m_Type = region->m_eDriveType;
    m_u32Tag = region->m_wTag;
    m_logicalMedia = media;

    NandParameters_t & params = NandHal::getParameters();
    m_u32SectorSizeInBytes = params.pageDataSize;
    m_nativeSectorSizeInBytes = m_u32SectorSizeInBytes;
    m_nativeSectorShift = 0;

    m_u32EraseSizeInBytes = params.pageDataSize * params.wPagesPerBlock;
    
    addRegion(region);
}

DataDrive::~DataDrive()
{
}

void DataDrive::addRegion(Region * region)
{
    m_u32NumberOfSectors += (region->m_iNumBlks - region->getBadBlockCount())    // Number of Good Blocks
        * (NandHal::getParameters().wPagesPerBlock);
    m_numberOfNativeSectors = m_u32NumberOfSectors;

    m_u64SizeInBytes = ((uint64_t)m_u32NumberOfSectors * m_u32SectorSizeInBytes);
    
    region->m_pLogicalDrive = this;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Initialize the appropriate Data Drive.
//!
//! This function will initialize the Data drive which includes the following:
//!  - Initialize the Mapper interface if available.
//!  - Fill in an array of Region structures for this Data Drive.
//!  - Reconstruct Physical start address for each region.
//!  - Allocate non-sequential sectors maps (NSSM) for the drive.
//!
//! NANDDataDriveInit() sets up data structures used by the Data Drive routines.
//!
//! Some data structures are expected to already be partly or wholey
//! set up by Media initialization routines (MediaInit(),
//! MediaDiscoverAllocation(), etc.).
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LDRIVE_MEDIA_NOT_ALLOCATED
//! \retval ERROR_DDI_LDL_LDRIVE_LOW_LEVEL_MEDIA_FORMAT_REQUIRED
//! \retval ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED
//! \retval ERROR_DDI_LDL_LDRIVE_HARDWARE_FAILURE
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::init()
{
    RtStatus_t ret;
    
    DdiNandLocker locker;
    
    if (!m_bPresent)
    {
        return ERROR_DDI_LDL_LDRIVE_MEDIA_NOT_ALLOCATED;
    }
    
    // If we've already been initialized, just return SUCCESS.
    if (m_bInitialized)
    {
        return SUCCESS;
    }
    
    // Init the virtual block info.
    VirtualBlock::determinePlanesToUse();
    
    // Create transaction ownership semaphore.
    tx_semaphore_create(&m_transactionSem, "nand:xn", 1);
    
    // Pre-allocate memory to hold the current transaction object. This buffer needs to be
    // as large as the largest object that we will be storing there.
    if (!m_transactionStorage)
    {
        m_transactionStorage = (char *)malloc(std::max(sizeof(ReadTransaction), sizeof(WriteTransaction)));
        if (!m_transactionStorage)
        {
            return ERROR_OUT_OF_MEMORY;
        }
    }
    
    // Partition NonSequential SectorsMaps memory.
    ret = m_media->getNssmManager()->allocate(NUM_OF_MAX_SIZE_NS_SECTORS_MAPS);
    if (ret != SUCCESS)
    {
        return ret;
    }

    // Build private list of Data Drive Regions.
    //! \todo We shouldn't build the list of regions again. We were already given the list of 
    //!     regions in the ctor and addRegion() calls!
    buildRegionsList();

    // The last thing we must do is initialize the mapper. This comes last because it uses
    // the region structures and the NSSM.
    ret = m_media->getMapper()->init();
    if (ret)
    {
        return ret;
    }
    
    m_bInitialized = true;
    
    return SUCCESS;
}

//! \brief Scans for regions belonging to this drive.
//!
//! The drive's type and tag must have already been filled in when this is called.
void DataDrive::processRegions(Region ** regionArray, unsigned * regionCount)
{
    unsigned dataRegionsCount = 0;
    Region::Iterator it = m_media->createRegionIterator();
    Region * theRegion;
    while ((theRegion = it.getNext()))
    {
        if (m_Type == theRegion->m_eDriveType && m_u32Tag == theRegion->m_wTag)
        {
            if (regionArray)
            {
                regionArray[dataRegionsCount] = theRegion;
            }

            dataRegionsCount++;
        }
    }
    
    if (regionCount)
    {
        *regionCount = dataRegionsCount;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Build the Data Regions List.
//!
//! This function will build the list of data regions based upon the
//! number of regions.
////////////////////////////////////////////////////////////////////////////////
void DataDrive::buildRegionsList()
{
    // Scan once to get the number of data regions for this drive.
    unsigned dataRegionsCount = 0;
    processRegions(NULL, &dataRegionsCount);
    
    // Allocate an array of region pointers large enough to hold all of our regions.
    m_u32NumRegions = dataRegionsCount;
    m_ppRegion = new Region*[dataRegionsCount];
    assert(m_ppRegion);
    
    // Scan again to fill in the region pointer array.
    processRegions(m_ppRegion, NULL);
    
    // Fill in the logical block count for each data region. Note that logical blocks here
    // do not take planes into count!
    DataRegion * pRegion;
    int iNumSectorsPerBlk = NandHal::getParameters().wPagesPerBlock;
    uint32_t u32TotalLogicalSectors = 0; // Logical "native" sectors
    Region::Iterator it(m_ppRegion, m_u32NumRegions);
    while ((pRegion = (DataRegion *)it.getNext()))
    {
        // As far as the mapper is concerned, all these blocks can be allocated
        // However, some of these blocks could go bad so...
        pRegion->setLogicalBlockCount(pRegion->getBlockCount() - pRegion->getBadBlockCount());
        
        u32TotalLogicalSectors += (pRegion->getLogicalBlockCount() * iNumSectorsPerBlk);
    }
    
    // Subtract out the reserved blocks but only for the Data Drive which is large.
    if (m_Type == kDriveTypeData)
    {
        u32TotalLogicalSectors -= m_media->getReservedBlockCount() * iNumSectorsPerBlk;
        
        // Also subtract out the number of blocks reserved for maps by the mapper.
        u32TotalLogicalSectors -= kNandMapperReservedBlockCount * iNumSectorsPerBlk;
        // In worst case, each NSSM can have backup block 
        // plus we need atleast one free virtual block for mergeBlockCore operation
        u32TotalLogicalSectors -= ( 
            (m_media->getNssmManager()->getBaseNssmCount() + 1) *
            NandHal::getParameters().planesPerDie 
            ) * iNumSectorsPerBlk;
    }
    
    // Update the native sector count and recompute the total drive size using the
    // total logical sector count.
    m_numberOfNativeSectors = u32TotalLogicalSectors;
    m_u64SizeInBytes = (uint64_t)u32TotalLogicalSectors * (uint64_t)m_nativeSectorSizeInBytes;
    
    // Convert native to nominal sectors.
    m_u32NumberOfSectors = m_numberOfNativeSectors << m_nativeSectorShift;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
