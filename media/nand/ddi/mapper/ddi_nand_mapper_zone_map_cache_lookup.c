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
//! \addtogroup ddi_nand_mapper
//! @{
//! \file    ddi_nand_mapper_zone_map_cache_lookup.c
//! \brief   NAND mapper zone map cache implementation.
////////////////////////////////////////////////////////////////////////////////

#include "ZoneMapCache.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_media.h"
#include <string.h>
#include "hw/core/vmemory.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
//  Code
/////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief    Identifies the cache entry which is to be used with logical block.  
//!
//! This function either identifies the cache entry which contains zone-map
//! section containing u32Lba or identifies the cache entry which should be
//! loaded with zone-map section containing u32Lba.
//!
//! \param[in]    u32Lba                Logical Block Address.
//! \param[out]   pi32SelectedEntryNum  Entry for zone-map section containing u32Lba
//!
//! \return Status of call or error.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ZoneMapCache::lookupCacheEntry(uint32_t u32Lba, int32_t * pi32SelectedEntryNum)
{
    uint64_t u32MinTimeStamp;
    uint64_t u32SaveMinTimeStamp;
    int32_t  i;
    uint32_t u32StartingEntry;
    uint32_t u32NumEntries;
    uint32_t u32TimeStamp;
    uint32_t bCacheValid;
        
    *pi32SelectedEntryNum = 0; // was -1 causes mem corruption when counter flips. 0 ok.
    
    // first see if there exists a cache entry which already 
    // contains u32Lba.
    for (i=0; i < m_cacheSectionCount; ++i)
    {
        u32StartingEntry = m_descriptors[i].m_firstLBA;
        u32NumEntries    = m_descriptors[i].m_entryCount;
        bCacheValid      = m_descriptors[i].m_isValid;
    
        if ((bCacheValid) && 
            (u32Lba >= u32StartingEntry) && 
            (u32Lba < (u32StartingEntry + u32NumEntries)))
        {
            *pi32SelectedEntryNum = i;
            return SUCCESS;
        }
    }

    u32MinTimeStamp = hw_profile_GetMicroseconds();
    u32SaveMinTimeStamp = u32MinTimeStamp;
    
    // If there is an entry which has not been occupied yet,
    // used it.  Otherwise, pick the entry which has the 
    // earliest time-stamp (i.e LRU). 
    for (i=0; i < m_cacheSectionCount; ++i)
    {
        u32TimeStamp = m_descriptors[i].m_timestamp;
        bCacheValid = m_descriptors[i].m_isValid;
        
        // The entries have to be aged.  Otherwise, an early 
        // flurry of accesses will insure that a cache entry
        // will stay in Cache forever.
          
        if (!bCacheValid)
        {
            *pi32SelectedEntryNum = i;
            return SUCCESS;   
        }
        else if (u32TimeStamp < u32MinTimeStamp)
        {
            u32MinTimeStamp = u32TimeStamp;
            *pi32SelectedEntryNum = i;  
        }
    }

    // This happens once in a blue moon.  The microsecond timer has rolled over, and the "current" 
    // time is less than all of the time-stamps.  In this case, we'll just return with original
    // value of pi32SelectedEntryNum, which is 0.  But we have to fix the time-stamps since they're
    // likely to be greater than micro-second timer for the foreseeable future.  So, refresh all of
    // the time-stamps.
    if (u32MinTimeStamp == u32SaveMinTimeStamp)
    {        
        for (i=0; i < m_cacheSectionCount; ++i)
        {
            m_descriptors[i].m_timestamp = u32MinTimeStamp;
        }
            
    } 
    
    assert(!(((*pi32SelectedEntryNum) < 0) || ((*pi32SelectedEntryNum) >= m_cacheSectionCount)));
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief  Evicts existing zone-map section in cache entry and loads new section.
//!
//! This function evicts zone-map section currently contained in cache entry
//! i32SelectedEntry and loads new zone-map section which contains logical
//! block u32Lba
//!
//! \param[in]   u32Lba             Logical Block Address.
//! \param[in]   i32SelectedEntry   Entry for zone-map section.
//!
//! \return Status of call or error.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ZoneMapCache::evictAndLoad(uint32_t u32Lba, int32_t i32SelectedEntry)
{
    uint32_t bCacheValid;
    uint32_t u32StartingEntry;
    uint32_t u32NumEntries;
    RtStatus_t ret;
    CacheEntry & entry = m_descriptors[i32SelectedEntry];
            
    bCacheValid = entry.m_isValid;
    u32StartingEntry = entry.m_firstLBA;
    u32NumEntries = entry.m_entryCount;
    
    if (!bCacheValid)
    {
        ret = loadCacheEntry(u32Lba, i32SelectedEntry);
        if (ret)
        {
            return ret;
        }
    }
    else if ((u32Lba < u32StartingEntry) || (u32Lba >= (u32StartingEntry + u32NumEntries)))
    {
        // Logical address was not found in cache, we need to evict an entry
        // and read in the section which contains the logical address. 
        if (entry.m_isDirty)
        {
            ret = addSection(entry.m_entries, entry.m_firstLBA, entry.m_entryCount);
            if (ret)
            {
                return ret;
            }
        }
        
        ret = loadCacheEntry(u32Lba, i32SelectedEntry);
        if (ret)
        {
            return ret;
        }
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
