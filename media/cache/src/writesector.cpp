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
//! \file writesector.cpp
//! \brief Contains cache manager API to write sector.
////////////////////////////////////////////////////////////////////////////////

#include "cacheutil.h"
#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

// See media_cache.h for the documentation of this function.
RtStatus_t media_cache_write(MediaCacheParamBlock_t * pb)
{
    RtStatus_t status;

    assert(g_mediaCacheContext.isInited);
        if (pb->writeOffset + pb->writeByteCount > DriveGetInfoTyped<uint32_t>(pb->drive, kDriveInfoSectorSizeInBytes) )
    {
        return ERROR_DDI_MEDIA_CACHE_INVALID_MEDIA_ADDRESS;
    }
    
    // Adjust the sector that was passed in the param block and convert nominal to native sectors.
    unsigned nativeSector;
    unsigned subsectorOffset;
    status = cache_AdjustAndConvertSector(pb, &nativeSector, &subsectorOffset, NULL);
    if (status != SUCCESS)
    {
        return status;
    }
    
    MediaTask task("media_cache_write");
    
    // Lock the cache.
    MediaCacheLock lockCache;

#if CACHE_STATISTICS
    g_mediaCacheContext.statistics[pb->drive].writeCount++;
    g_mediaCacheContext.combinedStatistics.writeCount++;
#endif // CACHE_STATISTICS
    
    // Try to find a preexisting cache entry for this drive and sector. Cache entries are always
    // in native sectors.
    MediaCacheEntry * cache = cache_index_LookupSectorEntry(pb->drive, nativeSector);

    // Handle a cache miss.
    bool didHit = cache != NULL;
    if (!cache)
    {
        // Need to read back if we're not writing over the whole native sector.
        // Read back if any of:
        // - _Neither_ NoReadback flag is set or NOREADBACK write type is set
        // - write offset is not 0
        // - write count is less than the native sector size
        bool noReadback = (pb->flags & kMediaCacheFlag_NoReadback) || (pb->mode == WRITE_TYPE_NOREADBACK);
        bool isSequential = (pb->flags & kMediaCacheFlag_SequentialWrite);
        bool isWriteOffset = ((subsectorOffset + pb->writeOffset) != 0 || 
                              (subsectorOffset == 0 && (pb->flags & kMediaCacheFlag_BypassCache)));
        bool isPartialWrite = (pb->writeByteCount < DriveGetInfoTyped<uint32_t>(pb->drive, kDriveInfoNativeSectorSizeInBytes));
        bool doRead = (!noReadback || isWriteOffset || (isPartialWrite && !(noReadback && isSequential)));

		// Evict a sector from the cache, and if (TRUE==doRead) load the needed sector into the cache.
        // Upon successful load, this sector is cached but not yet tracked in the LRU list.
        status = cache_HandleCacheMiss(pb, nativeSector, doRead, &cache);
        if (status != SUCCESS)
        {
            return status;
        }
        assert(cache);
    }
    else
    {
#if CACHE_STATISTICS
        // Update statistics.
        g_mediaCacheContext.statistics[pb->drive].hit();
        g_mediaCacheContext.combinedStatistics.hit();
#endif // CACHE_STATISTICS
        
        // Remove this entry from the LRU list before we retain it.
        g_mediaCacheContext.lru->remove(cache);
        
        // Retain the cache entry until the write operation completes. We retain before
        // waiting so that the entry cannot be flushed and invalidated by another thread
        // during the time that we unlock the cache.
        cache->retain();
    
        // Before modifying the entry, we must make sure that there are no other owners.
        // This also ensures that there are no other writer operations pending.
        status = cache->waitUntilOneOwner();
        if (status != SUCCESS)
        {
#if CACHE_STATISTICS
            g_mediaCacheContext.statistics[pb->drive].errors++;
            g_mediaCacheContext.combinedStatistics.errors++;
#endif // CACHE_STATISTICS

            cache->release();
            if (cache->isUnowned())
            {
                // Re-insert this entry in list at the MRU position of the LRU list, since we got a hit.
                g_mediaCacheContext.lru->insert(cache);
            }
            
            return status;
        }
    }

    // The above sections left the entry retained, but we don't need that so release it.
    cache->release();
    
    // Update cache entry fields.
    cache->isDirty = 1;

#if CACHE_STATISTICS
    // Update statistics.
    cache->timestamp = hw_profile_GetMicroseconds();
    cache->writeCount++;
#endif // CACHE_STATISTICS
    
    // Set options and parameters for the cache entry.
    if (pb->flags & kMediaCacheFlag_ApplyWeight)
    {
        cache->weight = pb->weight;
    }
    else
    {
        cache->weight = kMediaCacheWeight_Low;
    }
    if (pb->flags & kMediaCacheFlag_BypassCache)
    {
        // Nominally, we are supposed to avoid using the cache at all.
        // Instead, we will use the cache, but always treat this entry 
        // as low-priority/LRU in the list of entries.
        // This should minimize the disruption that it causes to the
        // rest of the cache.
        cache->bInsertToLRU = TRUE;
    }
    else
    {
        cache->bInsertToLRU = FALSE;
    }

    // Write new data into cache entry.
    memcpy(cache->buffer + subsectorOffset + pb->writeOffset, pb->buffer, pb->writeByteCount);
    
    // Place this entry back in the LRU list...
    if (cache->bInsertToLRU)
    {
        // ...at the LRU position.
        g_mediaCacheContext.lru->deselect(cache);
    }
    else
    {
        // ...at the MRU position.
        g_mediaCacheContext.lru->insert(cache);
    }
    
    // Record the write access.
    cache_RecordAccess(cache, true, didHit, false, 0);
    
    // Handle the write-through option by committing the sector contents to media immediately.
    if (pb->flags & kMediaCacheFlag_WriteThrough)
    {
        // This clears the dirty flag.
        status = cache->write();
        if (status != SUCCESS)
        {
#if CACHE_STATISTICS
            g_mediaCacheContext.statistics[pb->drive].errors++;
            g_mediaCacheContext.combinedStatistics.errors++;
#endif // CACHE_STATISTICS

            return status;
        }
    }
    
    return SUCCESS;
}

// See media_cache.h for the documentation of this function.
RtStatus_t media_cache_pinned_write(MediaCacheParamBlock_t * pb)
{
    RtStatus_t status;

    assert(g_mediaCacheContext.isInited);
    assert(pb->requestSectorCount > 0);
    
    // Clear return values until we know the write is successful.
    pb->buffer = 0;
    pb->token = 0;
    pb->actualSectorCount = 0;
    
    // Adjust the sector that was passed in the param block and convert nominal to native sectors.
    unsigned nativeSector;
    unsigned subsectorOffset;
    unsigned actualSectorCount;
    status = cache_AdjustAndConvertSector(pb, &nativeSector, &subsectorOffset, &actualSectorCount);
    if (status != SUCCESS)
    {
        return status;
    }
    
    MediaTask task("media_cache_pinned_write");
    
    // Lock the cache.
    MediaCacheLock lockCache;

#if CACHE_STATISTICS
    g_mediaCacheContext.statistics[pb->drive].writeCount++;
    g_mediaCacheContext.combinedStatistics.writeCount++;
#endif // CACHE_STATISTICS
    
    // Try to find a preexisting cache entry for this drive and sector. Cache entries are always
    // in native sectors.
    MediaCacheEntry * cache = cache_index_LookupSectorEntry(pb->drive, nativeSector);

    // Handle a cache miss.
    bool didHit = cache != NULL;
    if (!cache)
    {
        // Read the current sector contents, unless the caller has indicated that this
        // is not necessary. However, if not all nominal sectors in the native sector
        // are going to be overwritten, we still have to do a readback, unless the
        // SequentialWrite flag is set and we're writing from the beginning of the native
        // sector.
        LogicalDrive * logicalDrive = DriveGetDriveFromTag(pb->drive);
        assert(logicalDrive);
        bool noReadback = (pb->flags & kMediaCacheFlag_NoReadback) || (pb->mode == WRITE_TYPE_NOREADBACK);
        bool isSequential = (pb->flags & kMediaCacheFlag_SequentialWrite);
        bool isWriteOffset = (subsectorOffset > 0);
        unsigned subsectorCount = (1 << logicalDrive->m_nativeSectorShift);
        bool isPartialWrite = (actualSectorCount < subsectorCount);
        bool doRead = (!noReadback || isWriteOffset || (isPartialWrite && !(noReadback && isSequential)));
            
		// Evict a sector from the cache, and if (TRUE==doRead) load the needed sector into the cache.
        // Upon successful load, this sector is cached but not yet tracked in the LRU list.
        status = cache_HandleCacheMiss(pb, nativeSector, doRead, &cache);
        if (status != SUCCESS)
        {
            return status;
        }
        assert(cache);
    }
    else
    {
#if CACHE_STATISTICS
        // Update statistics.
        g_mediaCacheContext.statistics[pb->drive].hit();
        g_mediaCacheContext.combinedStatistics.hit();
#endif // CACHE_STATISTICS
        
        // Now remove this entry from the LRU list. This prevents any other
        // callers from trying to evict this entry until the pinned write is complete.
        g_mediaCacheContext.lru->remove(cache);
        
        // Retain the cache entry until the write operation completes. We retain before
        // waiting so that the entry cannot be flushed and invalidated by another thread
        // during the time that we unlock the cache.
        cache->retain();
    
        // Before modifying the entry, we must make sure that there are no other owners.
        // This also ensures that there are no other writer operations pending.
        status = cache->waitUntilOneOwner();
        if (status != SUCCESS)
        {
#if CACHE_STATISTICS
            g_mediaCacheContext.statistics[pb->drive].errors++;
            g_mediaCacheContext.combinedStatistics.errors++;
#endif // CACHE_STATISTICS

            cache->release();
            if (cache->isUnowned())
            {
                g_mediaCacheContext.lru->insert(cache);
            }
            
            return status;
        }
    }
    
    // Update cache entry fields.
    cache->isDirty = 1;
    cache->isWritePending = 1;
    cache->isWriteThrough = (pb->flags & kMediaCacheFlag_WriteThrough) != 0;

#if CACHE_STATISTICS
    // Update statistics.
    cache->timestamp = hw_profile_GetMicroseconds();
    cache->writeCount++;
#endif // CACHE_STATISTICS
    
    // Set sector weight.
    if (pb->flags & kMediaCacheFlag_ApplyWeight)
    {
        cache->weight = pb->weight;
    }
    else
    {
        cache->weight = kMediaCacheWeight_Low;
    }
    
    // Set param block return values.
    pb->buffer = cache->buffer + subsectorOffset;
    pb->token = kMediaCacheTokenSignature | cache->getArrayIndex(g_mediaCacheContext.entries);
    pb->actualSectorCount = actualSectorCount;
    
    cache_RecordAccess(cache, true, didHit, false, 0);
    
//     cache_ExtendResultChain(pb, cache, true);
    
#if CACHE_VALIDATE
    cache_ValidateChain(pb, cache, true);
#endif
    
    return SUCCESS;
}

//! The cache is unlocked during the pinned write operation so that other cache operations
//! are not held off. It is only locked briefly again when completing the pinned write.
//! The danger here is that another read or write operation may request the same sector,
//! causing there to be two entries for the same sector. This is prevented by keeping the
//! pinned sector(s) in the cache index but retaining them and marking them as having a
//! write pending. Thus, another operation on the same sector will be held off until the
//! pinned write completes, but operations on other sectors may proceed unobstructed.
//!
//! \note It is the caller's responsibility to release the cache entry after this function
//!     returns.
RtStatus_t cache_CompletePinnedWrite(MediaCacheEntry * cache)
{
    // There is no longer a pending write.
    cache->isWritePending = 0;
    
    // Handle the write-through option.
    if (cache->isWriteThrough)
    {
        // Clear the write-through flag.
        cache->isWriteThrough = 0;
        
        // This clears the dirty flag.
        RtStatus_t status = cache->write();
        if (status != SUCCESS)
        {
#if CACHE_STATISTICS
            g_mediaCacheContext.statistics[cache->drive].errors++;
            g_mediaCacheContext.combinedStatistics.errors++;
#endif // CACHE_STATISTICS

            return status;
        }
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
