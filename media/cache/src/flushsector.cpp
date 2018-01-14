///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor. All rights reserved.
// 
// Freescale Semiconductor
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of Freescale Semiconductor.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup media_cache_internal
//! @{
//! \file flushsector.cpp
//! \brief Contains the implementation of media cache flush APIs.
///////////////////////////////////////////////////////////////////////////////

#include "cacheutil.h"

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

static RtStatus_t flush_sector(DriveTag_t drive, int32_t sectorNumber, int32_t ix, uint32_t flags);
static RtStatus_t flush_cache(uint32_t flags);
static RtStatus_t flush_drive_cache(DriveTag_t drive, uint32_t flags);

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//! \brief Flushes the given sector to disk.
//!
//! \pre Cache must be locked.
///////////////////////////////////////////////////////////////////////////////
static RtStatus_t flush_sector(DriveTag_t drive, int32_t sectorNumber, int32_t ix, uint32_t flags)
{
    RtStatus_t rslt = SUCCESS;
    MediaCacheEntry * cache;
    
    MediaTask task("flush_sector");

    // If a valid index was provided, use it instead of searching the sector index.
    if (ix != -1)
    {
        assert(ix >= 0 && ix < g_mediaCacheContext.entryCount);
        cache = &g_mediaCacheContext.entries[ix];
    }
    else
    {
        // We need a param block to pass some parameters into the conversion function.
        MediaCacheParamBlock_t pb;
        pb.flags = flags;
        pb.drive = drive;
        pb.sector = sectorNumber;
        
        // Adjust the sector that was passed to us and convert nominal to native sectors.
        unsigned nativeSector;
        unsigned subsectorOffset;
        rslt = cache_AdjustAndConvertSector(&pb, &nativeSector, &subsectorOffset, NULL);
        if (rslt != SUCCESS)
        {
            return rslt;
        }
        
        // Look up this sector in the cache index.
        cache = cache_index_LookupSectorEntry(drive, nativeSector);
    }
    
    // Only need to actually flush if the entry is dirty.
    if (cache)
    {
        cache_RecordFlush(cache);
        
        // If invalidating, we need to ensure that there are no owners at all. But
        // if we're just flushing, then readers are OK but a writer is not. During the
        // wait routines we unlock the cache so that a deadlock does not occur with the
        // flushing thread waiting for an entry to become unowned while another thread
        // that owns that entry is waiting for the lock.
        if (flags & kMediaCacheFlag_Invalidate)
        {
            // Make certain that there are no owners of this cache entry.
            rslt = cache->waitUntilUnowned();
        }
        else
        {
            // Wait until any incomplete write is finished.
            rslt = cache->waitUntilWriteCompletes();
        }
        
        // Check the result now.
        if (rslt != SUCCESS)
        {
            return rslt;
        }
        
        // Flush if dirty.
        rslt = cache->flush();  // We will ignore errors if coming from the function here
                                // so that we can process the invalidate flag check.  
                                // Flush status is returned at the function's end.

        // Invalidate the entry only if the caller requested.
        if (flags & kMediaCacheFlag_Invalidate)
        {
            // If this cache element contains data...
            if (cache->isValid)
            {
                // ...remove that data from sector storage in the cache.
                cache_index_RemoveSectorEntry(cache);
            }
    
            // Invalidate the entry and place it at the head/LRU of LRU list.
            g_mediaCacheContext.lru->remove(cache);
            cache->reset(); // Note that reset() clears the "valid" flag, which causes insert()
                            // to insert to the head/LRU of the list.
            g_mediaCacheContext.lru->insert(cache);
        }

        if(flags & kMediaCacheFlag_RemoveEntry)
        {
            g_mediaCacheContext.lru->remove(cache);            
        }
    }

    return rslt;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Flushes all the cache dirty buffers.
//!
///////////////////////////////////////////////////////////////////////////////
static RtStatus_t flush_cache(uint32_t flags)
{
    RtStatus_t rslt = SUCCESS;
    int ix;

    // Flush the entire cache.
    for (ix = 0; ix < g_mediaCacheContext.entryCount; ix++)
    {
        MediaCacheEntry * cache = &g_mediaCacheContext.entries[ix];
        if (cache->isValid)
        {
            rslt = flush_sector(cache->drive, cache->sector, ix, flags);
            if (SUCCESS != rslt)
            {
                break;
            }
        }
    }

    // Flush all drives.
    DriveIterator_t iter;
    DriveTag_t tag;
    if (DriveCreateIterator(&iter) == SUCCESS)
    {
        while (DriveIteratorNext(iter, &tag) == SUCCESS)
        {
            DriveFlush(tag);
        }
        DriveIteratorDispose(iter);
    }

    return rslt; 
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Flushes all the cache buffers for a given drive.
//!
///////////////////////////////////////////////////////////////////////////////
static RtStatus_t flush_drive_cache(DriveTag_t drive, uint32_t flags)
{
    RtStatus_t rslt = SUCCESS;
    int32_t ix;

    // Flush all the caches for this drive
    for (ix = 0; ix < g_mediaCacheContext.entryCount; ix++)
    {
        MediaCacheEntry * cache = &g_mediaCacheContext.entries[ix];
        if (cache->isValid && drive == cache->drive)
        {
            rslt = flush_sector(cache->drive, cache->sector, ix, flags);
            // If error is returned , we will continue the loop for external drive 
            // as we need to invalidte and clear cache for all entries when we remove external media.
            if (SUCCESS != rslt && drive!=DRIVE_TAG_DATA_EXTERNAL)
            {
                break;
            }
        }
    }

    // Flush the drive
    DriveFlush(drive);

    return rslt; 
}

// See media_cache.h for the documentation of this function.
RtStatus_t media_cache_flush(MediaCacheParamBlock_t * pb)
{
    assert(g_mediaCacheContext.isInited);
    
    // Lock the cache.
    MediaCacheLock lockCache;
    
    RtStatus_t status;
    
    // Depending on the flags that are set, flush a single sector, a single drive,
    // or the entire cache.
    if (pb->flags & kMediaCacheFlag_FlushAllDrives)
    {
        status = flush_cache(pb->flags);
    }
    else if (pb->flags & kMediaCacheFlag_FlushDrive)
    {
        status = flush_drive_cache(pb->drive, pb->flags);
    }
    else
    {
        status = flush_sector(pb->drive, pb->sector, 0, pb->flags);
    }
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
