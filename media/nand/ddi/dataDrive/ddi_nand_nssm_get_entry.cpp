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
//! \brief This file contains the utilities needed to handle LBA ddi layer
//!        functions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include <string.h>
#include "Mapper.h"
#include "hw/core/vmemory.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "NssmManager.h"
#include "NonsequentialSectorsMap.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
//  Code
/////////////////////////////////////////////////////////////////////////////////

NonsequentialSectorsMap * NssmManager::getMapForIndex(unsigned index)
{
    return &m_mapsArray[index];
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Get the appropriate Non-Sequential Sector Map.
//!
//! This function will return the NonSequential Sector map entry for the given
//! LBA Block.  If the NSSM is not in the table, then build it from the data
//! in the NAND.
//!
//! \param[in]  blockNumber Virtual block number to search for.
//!
//! \return The map of the requested virtual block number. If NULL is returned,
//!     then an unrecoverable error occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NssmManager::getMapForVirtualBlock(uint32_t blockNumber, NonsequentialSectorsMap ** map)
{
    assert(map);
    RtStatus_t ret = SUCCESS;

    // Use the index to search for a matching map.
    NonsequentialSectorsMap * resultMap = static_cast<NonsequentialSectorsMap *>(m_index.find(blockNumber));
    if (resultMap)
    {
        // Update statistics.
        ++m_statistics.indexHits;
        
        // Reinsert the map in LRU order.
        resultMap->removeFromLRU();
        resultMap->insertToLRU();
        
        *map = resultMap;
        
        return ret;
    }
    
    // Update statistics.
    ++m_statistics.indexMisses;

    // If it wasn't found, we'll need to build it.
    ret = buildMap(blockNumber, &resultMap);
    if (ret != SUCCESS)
    {
        // Something bad happened... Reinsert the map in the LRU in case it was removed.
//        if (m_lru.containsNode(map))
//        {
//            map->removeFromLRU();
//        }
//        map->insertToLRU();
        
        return ret;
    }

    // Insert the newly built map into the LRU list.
    resultMap->insertToLRU();
    
    // Return map to caller.
    *map = resultMap;

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Lookup the virtual offset for a logical sector.
//!
//! This function gets the virtual sector in the remapped block corresponding
//! to a given linear sector offset.
//!
//! \param logicalSectorOffset Logical offset of the sector in the block.
//! \param [out] virtualSectorOffset Virtual sector offset that holds the most recent copy
//!     of the logical sector.
//! \param [out] isOccupied Whether the sector has been written previously.
//!
//! \retval SUCCESS The requested information was returned.
////////////////////////////////////////////////////////////////////////////////
void NonsequentialSectorsMap::getEntry(
    uint32_t logicalSectorOffset,
    uint32_t * virtualSectorOffset,
    bool * isOccupied,
    VirtualBlock ** whichVirtuaBlock)
{
    assert(virtualSectorOffset);
    assert(isOccupied);
    assert(whichVirtuaBlock);

    // The LBA was found above so now using the linear expected LBA sector,
    // grab the value in the NonSequential Map Sector.
    *virtualSectorOffset = m_map[logicalSectorOffset];
    *isOccupied = m_map.isOccupied(logicalSectorOffset);
    *whichVirtuaBlock = &m_virtualBlock;

    // If the logical sector has not been written to the primary block yet, see if we have
    // a backup block that contains it.
    if (!(*isOccupied) && hasBackup())
    {
        // We have a backup block, so return the sector info 
        //*virtualSectorOffset = m_backupMap[logicalSectorOffset];
        *isOccupied = m_backupMap.isOccupied(logicalSectorOffset);
        *whichVirtuaBlock = &m_backupBlock;
    }
}

void NonsequentialSectorsMap::insertToLRU()
{
//    assert(!m_manager->m_lru.containsNode(this));
    m_manager->m_lru.insert(this);
}

void NonsequentialSectorsMap::removeFromLRU()
{
//    assert(m_manager->m_lru.containsNode(this));
    m_manager->m_lru.remove(this);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////


