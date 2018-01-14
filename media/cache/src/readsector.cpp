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
//! \file readsector.cpp
//! \brief Contains cache manager API to read sector.
////////////////////////////////////////////////////////////////////////////////

#include "cacheutil.h"
#include <stdio.h>
#include "auto_free.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"

////////////////////////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////////////////////////

const unsigned kMaxSupportedPlanes = 2;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

// See media_cache.h for the documentation of this function.
RtStatus_t media_cache_read(MediaCacheParamBlock_t * pb)
{
    RtStatus_t status;
    
    assert(g_mediaCacheContext.isInited);
    assert(pb->requestSectorCount > 0);
    
    // Clear return values until we know the read is successful.
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
    
    MediaTask task("media_cache_read");
    
    // Lock the cache.
    MediaCacheLock lockCache;
    
#if CACHE_STATISTICS
    g_mediaCacheContext.statistics[pb->drive].readCount++;
    g_mediaCacheContext.combinedStatistics.readCount++;
#endif // CACHE_STATISTICS

    // Find the cache entry for this device and sector. Cache entries
    // are always in terms of native sectors.
    MediaCacheEntry * cache = cache_index_LookupSectorEntry(pb->drive, nativeSector);

    bool didHit = cache != NULL;
    if (!cache)
    {
		// Evict a sector from the cache, and load the needed sector into the cache.
        // Upon successful load, this sector is cached but not yet tracked in the LRU list.
        status = cache_HandleCacheMiss(pb, nativeSector, true, &cache);
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
        
        // Remove this entry from the LRU list since it is now in use. This prevents any other
        // callers from trying to evict this entry until the read is complete.
        g_mediaCacheContext.lru->remove(cache);

        // Retain this entry. If a write is pending, we do this before unlocking to prevent
        // it from being flushed by another thread between when the write completes and we
        // subsequently relock the cache.
        cache->retain();
            
        // Make sure that no write is pending on this cache entry.
        status = cache->waitUntilWriteCompletes();
        if (status != SUCCESS)
        {
#if CACHE_STATISTICS
            g_mediaCacheContext.statistics[pb->drive].errors++;
            g_mediaCacheContext.combinedStatistics.errors++;
#endif // CACHE_STATISTICS

            cache->release();
            if (cache->isUnowned())
            {
                // Insert this entry at the MRU position of the LRU list, since we got a hit.
                g_mediaCacheContext.lru->insert(cache);
            }
            
            return status;
        }
    }
    
    // At this point, the required data is now in the sector cache.

#if CACHE_STATISTICS
    // Update statistics.
    cache->timestamp = hw_profile_GetMicroseconds();
    cache->readCount++;
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
        
    // Fill in response members of the param block.
    pb->buffer = cache->buffer + subsectorOffset;
    pb->token = kMediaCacheTokenSignature | cache->getArrayIndex(g_mediaCacheContext.entries);
    pb->actualSectorCount = actualSectorCount;
    
    cache_RecordAccess(cache, false, didHit, false, 0);
    
//     cache_ExtendResultChain(pb, cache, false);

#if CACHE_VALIDATE
    cache_ValidateChain(pb, cache, false);
#endif

    return SUCCESS;
}

// See media_cache.h for the documentation of this function.
//! \todo Need to release all entries in the chain even if there was an error returned
//!     from the cache_CompletePinnedWrite() function.
RtStatus_t media_cache_release(uint32_t token)
{
    // The token has to have a valid signature, or we ignore it.
    if ((token & kMediaCacheTokenSignatureMask) == kMediaCacheTokenSignature)
    {
        unsigned entryIndex = token & kMediaCacheTokenEntryIndexMask;
        MediaCacheEntry * cache = &g_mediaCacheContext.entries[entryIndex];
        unsigned chainedCount = (token & kMediaCacheTokenChainedEntriesMask) >> kMediaCacheTokenChainedEntriesPosition;
        
        MediaTask task("media_cache_release");

        // Have to lock the cache because we may modify the LRU list.
        MediaCacheLock lockCache;
        
        // This loop will finish pinned writes and release all cache entries in a chain.
        // If there are no chained entries, then only the primary entry will be handled.
        // Also, if there are no more owners after we release the entry in question, then
        // it will be placed into the LRU list.
        do {
            // Handle the end of a pinned write.
            if (cache->isWritePending)
            {
                RtStatus_t status = cache_CompletePinnedWrite(cache);
                if (status != SUCCESS)
                {
                    return status;
                }
            }

#if CACHE_STATISTICS            
            // Update the entry's timestamp.
            cache->timestamp = hw_profile_GetMicroseconds();
#endif // CACHE_STATISTICS
            
            // Release this entry.
            cache->release();
            
            // Only insert the entry in the LRU list if there are no other owners.
            if (cache->isUnowned())
            {
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
            }
            
            // Move to next entry in the chain.
            cache++;
        } while (chainedCount--);
    }
    
    return SUCCESS;
}

//! \brief Gets and returns the least recently used cache entry.
//!
//! It waits forever for an LRU entry to become available.
static MediaCacheEntry *cache_miss_GetLRUEntry()
{
    MediaCacheEntry * cache;

    // Note that the select() action removes a node from the LRU end of the list.
    while (!(cache = static_cast<MediaCacheEntry *>(g_mediaCacheContext.lru->select())))
    {
        const uint32_t u32OwnershipCount = release_cache_lock();
        tx_thread_sleep(1);
        resume_cache_lock(u32OwnershipCount);
    }

    return cache;
}

//! \brief Remove, evict, and retain a cache entry.
static void cache_miss_RemoveAndRetainEntry(MediaCacheEntry * cache, uint8_t drive)
{
    // If this cache element contains data...
    if (cache->isValid)
    {
        // ...remove that data from sector storage in the cache.
        cache_index_RemoveSectorEntry(cache);
    }

    // Record that we're evicting this sector from the cache.
    cache_RecordEvict(cache, cache->isDirty);

    // Claim ownership of the cache entry early on.
    cache->retain();

#if CACHE_STATISTICS
    // Update statistics.
    g_mediaCacheContext.statistics[drive].miss();
    g_mediaCacheContext.combinedStatistics.miss();

    if (cache->isValid)
    {
        g_mediaCacheContext.statistics[cache->drive].evictionCount++;
        g_mediaCacheContext.combinedStatistics.evictionCount++;
        if (cache->isDirty)
        {
            g_mediaCacheContext.statistics[cache->drive].dirtyEvictionCount++;
            g_mediaCacheContext.combinedStatistics.dirtyEvictionCount++;
        }
    }
#endif // CACHE_STATISTICS
}

//! \brief Find and evict a cache entry for each plane. Flush entries to storage if necessary.
static RtStatus_t cache_miss_FindAndEvictEntries(uint8_t drive, unsigned startSector, bool doRead, MediaCacheEntry ** cache, uint32_t planeCount, unsigned * resultCount)
{
    RtStatus_t status;
    int i;

    // Find the LRU entry.
    cache[0] = cache_miss_GetLRUEntry();
    assert(cache[0]);
    
    bool firstNeedsFlush = cache[0]->isValid && cache[0]->isDirty;

    // Now the LRU list contains no entry for this cache element.

    cache_miss_RemoveAndRetainEntry(cache[0], drive);

    uint32_t firstSectorToFlush = cache[0]->sector;
    unsigned sectorNumber = firstSectorToFlush + 1;
    unsigned numEntriesToFlush = 1;
    unsigned numEntriesToEvict = 1;
    bool isSequential = false;
    
    // If the first cache entry must be flushed, then try to find its successive sector so
    // we can do a multi transaction.
    if (firstNeedsFlush)
    {
        // Find and evict a cache entry for each additional plane.
        for (i = 1; i < planeCount; i++)
        {
            // Look for an entry that has been assigned to the next sequential sector.
            cache[i] = cache_index_LookupSectorEntry(drive, sectorNumber);
            
            // We don't want to flush an entry that isn't dirty, and we can't use an entry
            // that has a writer.
            if (!cache[i] || !cache[i]->isDirty || cache[i]->isWritePending)
            {
                // No more entries can be found.
                break;
            }

            // Only if we're reading do we actually want to evict these extra sectors. If we're
            // not reading, we want these sectors to stay in the cache unmodified. But when
            // reading, we are going to be replacing the contents of the entry with another
            // sector's contents so we must evict it.
            if (doRead)
            {
                cache_miss_RemoveAndRetainEntry(cache[i], drive);
                ++numEntriesToEvict;
                
                // Remove this entry from the LRU. We'll reinsert it later, below.
                g_mediaCacheContext.lru->remove(cache[i]);
            }

            ++numEntriesToFlush;
            ++sectorNumber;
            isSequential = true;
        }
    }
    // Otherwise if we're going to be reading, then we need to find additional cache entries to
    // evict so the read can be multiplane.
    else if (doRead)
    {
        for (i = 1; i < planeCount; i++)
        {
            // Note that we dont call cache_miss_GetLRUEntry(), because we don't want to
            // wait around if there are no entries. In that case, the read will just have to be
            // non-multi.
            cache[i] = static_cast<MediaCacheEntry *>(g_mediaCacheContext.lru->select());

            if (!cache[i])
            {
                // No more entries can be found.
                break;
            }
            
            cache_miss_RemoveAndRetainEntry(cache[i], drive);
            
            ++numEntriesToFlush;
            ++numEntriesToEvict;
        }
    }
    
    // We can use multi transactions when we are flushing more than one sector and all sectors
    // are in sequential order.
    const bool useMulti = numEntriesToFlush > 1 && isSequential;

    // Open a multi-plane write operation to support cache entry flushes.
    if (useMulti)
    {
        status = DriveOpenMultisectorTransaction(drive, firstSectorToFlush, numEntriesToFlush, false);
        if (status != SUCCESS)
        {
            return status;
        }
    }

    RtStatus_t flushStatus = SUCCESS;

    // Flush the previous contents of each cache entry to storage, if it is dirty.
    for (i = 0; i < numEntriesToFlush; i++)
    {
        {
            MediaTask task("cache_HandleCacheMiss:flush");
            flushStatus = cache[i]->flush();
        }

        if (flushStatus != SUCCESS)
        {
            break;
        }
    }

    // Commit the multi-plane write.
    if (useMulti)
    {
        status = DriveCommitMultisectorTransaction(drive);
        if (status != SUCCESS)
        {
            return status;
        }
    }

    if (flushStatus != SUCCESS)
    {
        return flushStatus;
    }

    // Initialize each cache entry's fields.
    sectorNumber = startSector;
    for (i = 0; i < numEntriesToEvict; i++)
    {
        cache[i]->isValid = 0;
        cache[i]->isDirty = 0;
        cache[i]->isWritePending = 0;
        cache[i]->isWriteThrough = 0;
        cache[i]->drive = drive;
        cache[i]->sector = sectorNumber++;

#if CACHE_STATISTICS
        // Record when the entry was created.
        cache[i]->creationTimestamp = hw_profile_GetMicroseconds();
        cache[i]->readCount = 0;
        cache[i]->writeCount = 0;
#endif // CACHE_STATISTICS
    }

    *resultCount = numEntriesToEvict;
    return SUCCESS;
}

//! \brief Read from storage into cache entries.
static RtStatus_t cache_miss_ReadEntries(uint8_t drive, unsigned startSector, MediaCacheEntry ** cache, unsigned numEntries)
{
    RtStatus_t status;

    // Open a multi-plane read operation to support cache entry reads.
    if (numEntries > 1)
    {
        status = DriveOpenMultisectorTransaction(drive, startSector, numEntries, true);
        if (status != SUCCESS)
        {
            return status;
        }
    }

    RtStatus_t readStatus;

    for (int i = 0; i < numEntries; i++)
    {
        // This is the actual read from the storage into the cache.
        readStatus = cache[i]->read();
        if (readStatus != SUCCESS)
        {
            break;
        }
    }

    // Commit the multi-plane read.
    if (numEntries > 1)
    {
        status = DriveCommitMultisectorTransaction(drive);
        if (status != SUCCESS)
        {
            return status;
        }
    }

    return readStatus;
}

//! \brief Return entries to LRU (and optionally to the cache).
//!
//! Called if an error occurs during cache miss operations.
static void cache_miss_ReturnEntries(MediaCacheEntry ** cache, unsigned numEntries, bool addIndex)
{
    for (int i = 0; i < numEntries; i++)
    {
        cache[i]->release();

        // Put the entry back into the LRU.
        g_mediaCacheContext.lru->deselect(cache[i]);

        // If requested, also add back to cache.
        if (addIndex)
        {
            cache_index_AddSectorEntry(cache[i]);
        }
    }
}

//! This helper function is used by the read and write APIs to perform the common task of
//! loading a sector that is not already in the cache. First, an entry is selected for
//! eviction with a call to cache_lru_SelectOldest(). That entry is then flushed, being
//! written to media if it was dirty. Then, if the \a doRead parameter is true, the new
//! sector specified by the \a nativeSector parameter is read into the selected cache entry.
//! In all cases, the fields of the selected entry are filled in before the entry is
//! returned to the caller.
//!
//! The cache entry that is returned through the \a resultEntry parameter has the following
//! state on exit:
//!     - it is retained
//!     - it has been inserted into the sector cache
//!     - it is \em not in the LRU
//!     - it is marked as valid and not dirty
//!     - the timestamp is not updated yet
//!
//! If multi-plane operations are enabled, the LRU is searched for additional entries that
//! correspond to the next "n" sequential sectors where "n" is the plane count. On exit from
//! this function, these additional entries are left in the same state as the resultEntry.
//!
//! - If the sector being evicted is dirty
//!     - If its successor is in the cache, not owned by a writer, and dirty then
//!         - Flush both
//!         - If doRead, evict successor too and save 
//!
//! \param pb The parameter block passed to the media cache API.
//! \param nativeSector The native sector index to load.
//! \param doRead Pass true for this parameter if a read of the incoming sector is required.
//!     This would be false, for instance, if the sector data is going to be completely
//!     replaced, thereby making a read unnecessary.
//! \param[out] resultEntry The newly loaded cache entry is returned through this parameter.
//!     See the above for details about the state of this entry.
//!
//! \return Either SUCCESS or an error code is returned.
//!
//! \pre The cache must be locked.
//! \post The cache is always locked, even when an error is returned.
RtStatus_t cache_HandleCacheMiss(MediaCacheParamBlock_t * pb, unsigned nativeSector, bool doRead, MediaCacheEntry ** resultEntry)
{
    assert(pb);
    assert(resultEntry);

    const uint8_t drive = pb->drive;
    const bool isExternalDrive = (drive == DRIVE_TAG_DATA_EXTERNAL);

    // If this is the external drive, set a retry count.
    int retryCount = isExternalDrive ? 2 : 0;

    // Get the plane count.
    const uint32_t planeCount = DriveGetInfoTyped<uint32_t>(drive, kDriveInfoOptimalTransferSectorCount);
    assert(planeCount);

    // Allocate an array to hold a media cache entry for each plane.
    MediaCacheEntry * cache[kMaxSupportedPlanes] = {0};
    assert(planeCount <= kMaxSupportedPlanes);

    RtStatus_t status;
    unsigned numEntries = 0;

    do
    {
        // Find and evict as many cache entries as we can, up to the plane count.
        // Cache entries are retained.
        // Dirty cache entries are flushed to storage.
        status = cache_miss_FindAndEvictEntries(drive, nativeSector, doRead, cache, planeCount, &numEntries);
        if (status != SUCCESS)
        {
            // A flush failed, so put the entries back into both the LRU and index since we haven't
            // modified them yet.
            cache_miss_ReturnEntries(cache, numEntries, true);

            if (isExternalDrive)
            {
                // This is for when the external media disappears but there are still cache entries
                // for the external data drive.
                media_cache_DiscardDrive(drive);
            }
        }
        else
        {
            // Successfully found and evicted cache entries.
            break;
        }
    } while(retryCount-- > 0);

    if (status != SUCCESS)
    {
        // A flush failed. Entries have already been put back into the LRU list.
        return status;
    }

    assert(numEntries);

    // If there are 2 entries, check next sector of multi transaction in cache. 
    // If available, reduce entries to 1 and read only one sector.
    if(numEntries>1)
    {
         MediaCacheEntry * cache = cache_index_LookupSectorEntry(drive, nativeSector+1); 
         // Sector found in one of the cache.
         if(cache!=NULL)
         {
            // Reduce num entries by 1.
            numEntries--;
         }
    }         

    // If reading, load data from storage into cache entries.
    if (doRead)
    {
        status = cache_miss_ReadEntries(drive, nativeSector, cache, numEntries);
        if (status != SUCCESS)
        {
            // The read failed, so just put entries into the LRU so they
            // don't get lost. They are still marked invalid so they will be reused immediately.
            cache_miss_ReturnEntries(cache, numEntries, false);
            return status;
        }
    }

    // Do final preparations for the cache entries before we return.
    for (int i = 0; i < numEntries; i++)
    {
        MediaCacheEntry * entry = cache[i];
        
        // Entry is valid now that it contains data (if we did a read).
        entry->isValid = 1;

        // Insert this new sector into the sector index tree.
        cache_index_AddSectorEntry(entry);
        
        // Only the cache entry we return should be retained. Otherwise the other entries
        // will never be fulled released!
        if (i > 0)
        {
            // Release this cache entry.
            entry->release();
            
            // Put the entry back into the LRU.
            g_mediaCacheContext.lru->insert(entry);
        }
    }

    // Return a pointer to the first cache entry. This is the one the user requested.
    *resultEntry = cache[0u];

    return SUCCESS;
}

//! Scan cache entries to see if we can return more sectors. This functionality is based on
//! the requirement that sequential elements of the cache entry array have physically contiguous
//! sector buffers. If the drive has a native sector size smaller than the size of the cache
//! entry buffers then the sector data will not be contiguous between entries. Thus, chaining
//! is disabled in such a case.
//!
//! \param pb The original caller's param block. The \a actualSectorCount and \a token fields are
//!     updated to reflect the additional sectors being returned.
//! \param cache This is the base cache entry.
//! \param isWrite Pass true for this parameter if the result chain is intended to be used
//!     for a pinned write operation. False means that the operation is a read.
//!
//! \pre The actual sector count field of the param block must already be set to the number of
//!     nominal sectors remaining in the base cache entry, starting with the requested sector.
void cache_ExtendResultChain(MediaCacheParamBlock_t * pb, MediaCacheEntry * cache, bool isWrite)
{
    LogicalDrive * drive = DriveGetDriveFromTag(pb->drive);
    assert(drive);
    
    // We can't do chaining on drives that have a sector size smaller than the size
    // of each cache entry buffer, because that means that sector data is not contiguous.
    if (drive->m_nativeSectorSizeInBytes < g_mediaCacheContext.entryBufferSize)
    {
        return;
    }
    
    unsigned nativeShift = drive->m_nativeSectorShift;
    if (pb->flags & kMediaCacheFlag_UseNativeSectors)
    {
        nativeShift = 0;
    }
    
    unsigned nominalPerNative = 1 << nativeShift;
    unsigned maxChainedSectors = g_mediaCacheContext.maxChainedEntries << nativeShift;
    unsigned remainingNominalSectors = pb->requestSectorCount - pb->actualSectorCount;
    
    // Limit the remaining sectors by the maximum.
    remainingNominalSectors = std::min(remainingNominalSectors, maxChainedSectors);

    // We have to round the returned sectors down to the nearest full cache entry's
    // worth of nominal sectors. This is because ownership granularity is at the
    // cache entry level rather than nominal sector level. You can call this a kludge
    // if you like.
    if (nominalPerNative > 1)
    {
        remainingNominalSectors = remainingNominalSectors / nominalPerNative * nominalPerNative;
    }
    
    // Exit early if there are no more sectors to return to the caller.
    if (remainingNominalSectors == 0)
    {
        return;
    }

    // Start looking at the next entry.
    unsigned entryIndex = cache->getArrayIndex(g_mediaCacheContext.entries) + 1;
    MediaCacheEntry * scanEntry = cache + 1;
    unsigned nativeSectorInSequence = cache->sector + 1;
    unsigned chainIndex = 1;
    
    // Scan until the end of the entry array.
    while (remainingNominalSectors && entryIndex < g_mediaCacheContext.entryCount)
    {
        assert(scanEntry->getArrayIndex(g_mediaCacheContext.entries) == entryIndex);
        
        // Make sure the next sector in sequence isn't already in the cache. However, it's
        // ok if the sector is in the cache but is the entry we're examining.
        MediaCacheEntry * match = cache_index_LookupSectorEntry(pb->drive, nativeSectorInSequence);
        if (match && match != scanEntry)
        {
            break;
        }
        
        // Stop looking if we run into an entry that has owners or is dirty.
        if (scanEntry->isValid && (!scanEntry->isUnowned() || scanEntry->isDirty))
        {
            break;
        }

        // Does this entry happen to already be the next one in sequence?
        bool isInSequence = scanEntry->isValid && (scanEntry->drive == cache->drive) && (scanEntry->sector == nativeSectorInSequence);

        // Load the next sector number in sequence into this unowned entry.
        if (!isInSequence)
        {
            // If this cache element contains data...
            if (scanEntry->isValid)
            {
                // ...remove that data from sector storage in the cache.
                cache_index_RemoveSectorEntry(scanEntry);
            }
            
            // Update the entry's fields.
            scanEntry->drive = cache->drive;
            scanEntry->sector = nativeSectorInSequence;
            scanEntry->isValid = 0;
        
#if CACHE_STATISTICS
            // Record when the entry was created and clear access counts.
            scanEntry->creationTimestamp = hw_profile_GetMicroseconds();
            scanEntry->writeCount = 0;
            scanEntry->readCount = 0;
#endif // CACHE_STATISTICS
            
            // Read the sector data into the entry.
           if (!isWrite || !(pb->flags & kMediaCacheFlag_NoReadback) || remainingNominalSectors < nominalPerNative)
           {
                MediaTask task("cache_ExtendResultChain");
                RtStatus_t status = scanEntry->read();
                if (status != SUCCESS)
                {
                    //! \todo Clean up properly after the error.
                    printf("Chain read error (sector %d): %x\n", nativeSectorInSequence, status);
                    break;
                }
           }
            
            // The cache entry is now valid.
            scanEntry->isValid = 1;
            
            // Insert this sector into the cache index. For writes we remove and update the LRU list.
            cache_index_AddSectorEntry(scanEntry);
        }
        
        // Update fields that are changed whether the entry was already in sequence or not.
        scanEntry->isDirty = isWrite;
        scanEntry->isWritePending = isWrite;
        scanEntry->isWriteThrough = isWrite && (pb->flags & kMediaCacheFlag_WriteThrough);
            
        // Set sector weight.
        if (pb->flags & kMediaCacheFlag_ApplyWeight)
        {
            scanEntry->weight = pb->weight;
        }
        else
        {
            scanEntry->weight = kMediaCacheWeight_Low;
        }
        
        // Retain the entry.
        scanEntry->retain();
        
        // Remove this entry from the LRU.
        g_mediaCacheContext.lru->remove(scanEntry);

#if CACHE_STATISTICS
        scanEntry->timestamp = hw_profile_GetMicroseconds();
        
        // Increment access count.
        if (isWrite)
        {
            scanEntry->writeCount++;
        }
        else
        {
            scanEntry->readCount++;
        }
#endif // CACHE_STATISTICS
        
        // Record the read or write.
        cache_RecordAccess(scanEntry, isWrite, isInSequence, false, chainIndex);
        
        // Add on up to the entry's full size in nominal sectors.
        unsigned additionalNominalSectors = 1;
        if (!(pb->flags & kMediaCacheFlag_UseNativeSectors))
        {
            additionalNominalSectors = std::min(remainingNominalSectors, nominalPerNative);
        }

        // Update the param block and token chain count.
        remainingNominalSectors -= additionalNominalSectors;
        pb->actualSectorCount += additionalNominalSectors;
        pb->token += 1 << kMediaCacheTokenChainedEntriesPosition;

        // Move to the next cache entry.
        entryIndex++;
        scanEntry++;
        nativeSectorInSequence++;
        chainIndex++;
    }
}

#if CACHE_VALIDATE
void cache_ValidateChain(MediaCacheParamBlock_t * pb, MediaCacheEntry * base, bool isWrite)
{
    SECTOR_BUFFER * compareBuffer;
    
    if (!isWrite)
    {
        if (media_buffer_acquire(kMediaBufferType_Sector, kMediaBufferFlag_None, &compareBuffer) != SUCCESS)
        {
            printf("cache_ValidateChain couldn't get buffer\n");
            return;
        }
    }
    
    unsigned sectorSize = DriveGetInfoTyped<uint32_t>(pb->drive, kDriveInfoNativeSectorSizeInBytes);
    MediaCacheEntry * cache = base;
    unsigned totalChained = 1 + (pb->token & kMediaCacheTokenChainedEntriesMask) >> kMediaCacheTokenChainedEntriesPosition;
    unsigned chainedCount = totalChained;
    unsigned chainedIndex = 1; // Base 1 number, so the "x/y" messages come out right.
    unsigned baseEntryIndex = base->getArrayIndex(g_mediaCacheContext.entries);
    
    // Make sure the chain doesn't run off the end of the entry array.
    if (baseEntryIndex + totalChained > g_mediaCacheContext.entryCount)
    {
        printf("Chain extends beyond cache entry array: %d->%d, max=%d\n", baseEntryIndex, totalChained - 1, g_mediaCacheContext.entryCount - 1);
        totalChained = g_mediaCacheContext.entryCount - baseEntryIndex;
        chainedCount = totalChained;
    }
    
    while (chainedCount--)
    {
        // Entry must be valid.
        if (!cache->isValid)
        {
            printf("Chained entry %d/%d is not valid\n", chainedIndex, totalChained);
        }
        
        // Check drive.
        if (cache->drive != base->drive)
        {
            printf("Mismatched drive: base=%d, chained entry %d/%d=%d\n", base->drive, chainedIndex, totalChained, cache->drive);
        }
        
        // Check sector.
        if (cache->sector != base->sector + chainedIndex - 1)
        {
            printf("Out of sequence sector: base=%d, chained entry %d/%d=%d\n", base->sector, chainedIndex, totalChained, cache->sector);
        }
        
        // Make sure the entry is owned.
        if (cache->isUnowned())
        {
            printf("Chained entry %d/%d has no owners\n", chainedIndex, totalChained);
        }
        
        // Check different things depending on read or write.
        if (isWrite)
        {
            if (!cache->isWritePending)
            {
                printf("Chained entry %d/%d for write does not have write bit set\n", chainedIndex, totalChained);
            }
        }
        else
        {
            if (cache->isWritePending)
            {
                printf("Chained entry %d/%d for read has write bit set\n", chainedIndex, totalChained);
            }
        
            // Can only compare clean cache entries with the contents on the media.
            if (!cache->isDirty)
            {
                // Read in the native sector to compare.
                if (DriveReadSector(cache->drive, cache->sector, compareBuffer) != SUCCESS)
                {
                    printf("DRS failure: sector=%d\n", cache->sector);
                    continue;
                }
                
                if (memcmp(compareBuffer, cache->buffer, sectorSize) != 0)
                {
                    printf("compare failure: base sector=%d, sector=%d, chained entry=%d/%d, entry index=%d\n", base->sector, cache->sector, chainedIndex, totalChained, baseEntryIndex + chainedIndex - 1);
                }
            }
        }
        
        cache++;
        chainedIndex++;
    }
    
    if (!isWrite)
    {
        media_buffer_release(compareBuffer);
    }
}
#endif // CACHE_VALIDATE

RtStatus_t media_cache_DiscardDrive (int iDrive)
{
    int ii;
    MediaCacheEntry *cache;

    // Lock the cache.
    MediaCacheLock lockCache;

    for(ii=0; ii<g_mediaCacheContext.entryCount; ii++)
    {
        cache = &g_mediaCacheContext.entries[ii];
        
        if(cache->drive == iDrive)
        {
            cache->waitUntilUnowned(); 

            if(cache->isValid)
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
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

