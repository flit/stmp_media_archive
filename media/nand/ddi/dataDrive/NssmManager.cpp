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
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "NssmManager.h"
#include "NonsequentialSectorsMap.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include <string.h>
#include <stdlib.h>
#include "Mapper.h"
#include "hw/core/vmemory.h"
#include "ddi_nand_media.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/include/ddi_media_timers.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
// Definitions
/////////////////////////////////////////////////////////////////////////////////

#ifndef NDD_LBA_DEBUG_ENABLE
    //! Enable this define to turn on debug messages.
//    #define NDD_LBA_DEBUG_ENABLE
#endif

//! The number of pages per block that the NSSM count is defined in.
#define NSSM_BASE_PAGE_PER_BLOCK_COUNT (128)

/////////////////////////////////////////////////////////////////////////////////
//  Code
/////////////////////////////////////////////////////////////////////////////////

#pragma ghs section text=".init.text"
NssmManager::NssmManager(Media * nandMedia)
:   m_media(nandMedia),
    m_mapper(NULL),
    m_mapCount(0),
    m_uPOBlockSize(0),
    m_uPOUseIndex(0),
    m_PODataArray(0),
    m_mapsArray(NULL),
    m_index(),
    m_lru(0, 0, 0)
{
    // Clear all statistics values to 0.
    memset(&m_statistics, 0, sizeof(m_statistics));
    
    m_mapper = m_media->getMapper();
}
#pragma ghs section text=default

////////////////////////////////////////////////////////////////////////////////
//! \brief Partition Non Sequential Sectors Maps.
//!
//! This function dynamically allocates the NSSectorsMaps memory for the given
//! number of map entries, whose sizes depend on the quantity of sectors per block.
//! The actual quantity of entries in NSSectorsMaps memory may be adjusted up or
//! down from the requested value \a uMapsPerBaseNSSMs, depending on the quantity of sectors per block in the NAND.
//! \a uMapsPerBaseNSSMs is normalized to \a NSSM_BASE_PAGE_PER_BLOCK_COUNT pages per block.
//!
//! The new NSSMs are pushed onto the NSSM LRU. If the NSSM array has previously
//! been allocated and the requested \a uMapsPerBaseNSSMs is the same, then this function
//! does nothing and returns immediately.
//!
//! \param[in] uMapsPerBaseNSSMs    Number of maps to allocate, normalized to a NAND 
//!                                 with \a NSSM_BASE_PAGE_PER_BLOCK_COUNT pages per block.
//!                                 Must be greater than 0.
//!
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_NAND_DATA_DRIVE_CANT_ALLOCATE_USECTORS_MAPS
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NssmManager::allocate(unsigned uMapsPerBaseNSSMs)
{
    int32_t i32NumSectorsPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    unsigned mapsCount;
    
    if (uMapsPerBaseNSSMs == 0)
    {
        // Could not fit even one map...
        return ERROR_DDI_NAND_DATA_DRIVE_CANT_ALLOCATE_USECTORS_MAPS;
    }
    
    // Adjust the number of maps to allocate based on how many pages per block the NAND has.
    // The number of maps is defined in terms of NSSM_BASE_PAGE_PER_BLOCK_COUNT (nominally 128)
    // pages per block. So if there are fewer
    // pages per block, then the number of maps is increased. Vice versa for more pages per
    // block--the number of maps is decreased.
    if (i32NumSectorsPerBlock < NSSM_BASE_PAGE_PER_BLOCK_COUNT)
    {
        mapsCount = uMapsPerBaseNSSMs * (NSSM_BASE_PAGE_PER_BLOCK_COUNT / i32NumSectorsPerBlock);
    }
    else if (i32NumSectorsPerBlock > NSSM_BASE_PAGE_PER_BLOCK_COUNT)
    {
        mapsCount = uMapsPerBaseNSSMs / (i32NumSectorsPerBlock / NSSM_BASE_PAGE_PER_BLOCK_COUNT);
    }
    else
    {
        mapsCount = uMapsPerBaseNSSMs;
    }
    
    // Handle if there is already a NSSM array allocated. We either need to do nothing if
    // the array is already the size being requested, or dispose of the old array so we can
    // create a new one.
    if (m_mapsArray)
    {
        // No need to reallocate maps if they're already the desired size.
        if (m_mapCount == mapsCount)
        {
            return SUCCESS;
        }
        // Clean up any previous set of maps.
        else
        {
            // Evict and merge maps.
            flushAll();
            
            // Dispose of previously allocated maps.
            delete [] m_mapsArray;
            m_mapsArray = NULL;
            m_mapCount = 0;
        }
        if (m_PODataArray)
        {
            free(m_PODataArray);
            m_PODataArray = NULL;
        }
    }    

    // Compute size of single page order map internal array size
    m_uPOBlockSize = PageOrderMap::getEntrySize(i32NumSectorsPerBlock,0) * i32NumSectorsPerBlock;
    m_uPOUseIndex = 0;
    // Allocate array
    m_PODataArray = (uint8_t *)malloc(m_uPOBlockSize * mapsCount);
    // Validate allocation
    assert (m_PODataArray);

    // Now set up the pointers to the NSSM descriptor array and each of the NSSM maps.
    // The descriptors array goes at the beginning of the buffer, followed by the maps.
    // Here we also fill in the global NSSM information.
    m_mapsArray = new NonsequentialSectorsMap[mapsCount];
    m_mapCount = mapsCount;

    // Initialize each of the descriptor structs, and set the pointers to the two
    // maps to point into the appropriate place in the same buffer.
    int32_t iMap;
    
    for (iMap=0; iMap < mapsCount; iMap++)
    {
        m_mapsArray[iMap].init(this);
        m_mapsArray[iMap].insertToLRU();
    }
    
    return SUCCESS;
}

uint8_t * NssmManager::getPOBlock()
{
    // Validate allocator variables
    assert(m_PODataArray);
    assert(m_uPOUseIndex < m_mapCount);
    // Compute array pointer
    uint8_t *u8Array = (m_PODataArray + m_uPOUseIndex * m_uPOBlockSize);
    // Increment used count
    m_uPOUseIndex++;
    return u8Array;
}

NssmManager::~NssmManager()
{
    if (m_mapsArray)
    {
        delete [] m_mapsArray;
        m_mapsArray = NULL;
    }
    if (m_PODataArray)
    {
        free(m_PODataArray);
        m_PODataArray = NULL;
    }
    m_mapCount = 0;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Return a count Non Sequential Sectors Maps.
//!
//! This function provides a count of map entries that is normalized
//! to a NAND with \a NSSM_BASE_PAGE_PER_BLOCK_COUNT pages per block.
//! This normalization is the same as the units used for the argument to
//! allocate().
//!
//! \retval uMapsPerBaseNSSMs    Number of allocated maps, normalized to a NAND 
//!                                 with \a NSSM_BASE_PAGE_PER_BLOCK_COUNT pages per block.
//!
////////////////////////////////////////////////////////////////////////////////
unsigned NssmManager::getBaseNssmCount()
{
    int32_t i32NumSectorsPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    unsigned uMapsPerBaseNSSMs;
    unsigned mapsCount = m_mapCount;

    // Adjust the number of maps to allocate based on how many pages per block the NAND has.
    // The number of maps is defined in terms of NSSM_BASE_PAGE_PER_BLOCK_COUNT (nominally 128)
    // pages per block. So if there are fewer
    // pages per block, then the number of maps is increased. Vice versa for more pages per
    // block--the number of maps is decreased.
    if (i32NumSectorsPerBlock < NSSM_BASE_PAGE_PER_BLOCK_COUNT)
    {
        uMapsPerBaseNSSMs = mapsCount / (NSSM_BASE_PAGE_PER_BLOCK_COUNT / i32NumSectorsPerBlock);
    }
    else if (i32NumSectorsPerBlock > NSSM_BASE_PAGE_PER_BLOCK_COUNT)
    {
        uMapsPerBaseNSSMs = mapsCount * (i32NumSectorsPerBlock / NSSM_BASE_PAGE_PER_BLOCK_COUNT);
    }
    else
    {
        uMapsPerBaseNSSMs = mapsCount;
    }

    return ( uMapsPerBaseNSSMs );
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Flush Non Sequential Sector Map for all drives.
////////////////////////////////////////////////////////////////////////////////
void NssmManager::flushAll()
{
    uint32_t u32NS_SectorsMapIdx;

#ifdef NDD_LBA_DEBUG_ENABLE
    tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP,
        "\r\n FlushNSSectorMap, %d\r\n\r\n", pDriveDescriptor->u32Tag);
#endif

    // Look for the Non Sequential SectorMap in the active maps array
    for (u32NS_SectorsMapIdx=0; u32NS_SectorsMapIdx < m_mapCount; u32NS_SectorsMapIdx++)
    {
        NonsequentialSectorsMap * map = getMapForIndex(u32NS_SectorsMapIdx);
        map->flush();
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Invalidate all Sector map entries
////////////////////////////////////////////////////////////////////////////////
void NssmManager::invalidateAll()
{
    uint32_t iMap;
    
    // Reset LRU list.
    m_lru.clear();

    for (iMap=0; iMap < m_mapCount; iMap++)
    {
        NonsequentialSectorsMap * map = getMapForIndex(iMap);
        assert(map);
        map->invalidate();
        map->insertToLRU();
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Invalidate Sector map entries for a single drive.
////////////////////////////////////////////////////////////////////////////////
void NssmManager::invalidateDrive(LogicalDrive * pDriveDescriptor)
{
    uint32_t u32NS_SectorsMapIdx;

    // Look for the Non Sequential SectorMap in the active maps array
    for (u32NS_SectorsMapIdx=0; u32NS_SectorsMapIdx < m_mapCount; u32NS_SectorsMapIdx++)
    {
        NonsequentialSectorsMap * map = getMapForIndex(u32NS_SectorsMapIdx);
        assert(map);
        
        Region * region = map->getRegion();
        
        // See if the region containing this map's virtual block belongs to the
        // drive the caller passed in.
        if (region && region->m_pLogicalDrive == pDriveDescriptor)
        {
            // Remove from LRU list before invalidating, since the invalidate() method clears
            // the LRU list links.
            map->removeFromLRU();
            map->invalidate();
            
            // Reinsert the entry in the LRU list.
            map->insertToLRU();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Build the NonSequential Sector Map from the NAND's Redundant Area.
//!
//! This function will read the redundant areas for a LBA to rebuild the
//! NonSequential Sector Map.  The result is placed in one of the SectorMaps
//! in RAM. It will evict another NS Sectors Map if a blank one is not available,
//! using an LRU algorithm.
//!
//! \param[in]  u32LBABlkAddr Virtual block number to rebuild NS Sector Map for.
//! \param[out]  resultMap Pointer to nonsequential sector map for
//!              this Block.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_NAND_DATA_DRIVE_CANT_RECYCLE_USECTOR_MAP No maps are available.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NssmManager::buildMap(uint32_t u32LBABlkAddr, NonsequentialSectorsMap ** resultMap)
{
    // Clear the result map.
    if (resultMap)
    {
        *resultMap = NULL;
    }

    // Make sure we actually have some maps available!
    if (0 == m_mapCount)
    {   
        // There are no NS_SectorMaps to recycle.
        return ERROR_DDI_NAND_DATA_DRIVE_CANT_RECYCLE_USECTOR_MAP;
    }

    // Get the least recently used map.
    NonsequentialSectorsMap * map = static_cast<NonsequentialSectorsMap *>(m_lru.select());
    if (!map)
    {
        // Didn't find one we can recycle
        return ERROR_DDI_NAND_DATA_DRIVE_CANT_RECYCLE_USECTOR_MAP;
    }
        
    // If the entry we just evicted has a back-up block, merge them.
    RtStatus_t retCode = map->flush();
    if (retCode)
    {
        map->insertToLRU();
        return retCode;
    }

    // Return the chosen map.
    if (resultMap)
    {
        *resultMap = map;
    }

    // Reinitialize the NSSM with the new virtual block.
    retCode = map->prepareForBlock(u32LBABlkAddr);
    if (retCode)
    {
        map->insertToLRU();
    }
    
    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

