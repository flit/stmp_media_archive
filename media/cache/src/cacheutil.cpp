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
//! \file cacheutil.cpp
//! \ingroup media_cache_internal
//! \brief Contains the implementation of media cache index utilities.
///////////////////////////////////////////////////////////////////////////////

#include "cacheutil.h"
#include <stdio.h>
#include <set>
#include "hw/core/vmemory.h"

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

void MediaCacheEntry::reset()
{
    isValid = 0;
    isDirty = 0;
    isWritePending = 0;
    isWriteThrough = 0;
    refcount = 0;
    drive = 0;
    sector = 0;
    weight = 0;
    
#if CACHE_STATISTICS
    timestamp = 0;
    creationTimestamp = 0;
    readCount = 0;
    writeCount = 0;
#endif // CACHE_STATISTICS
}

RtStatus_t MediaCacheEntry::read()
{
    // Read the requested sector into the selected cache entry.
    return DriveReadSector(drive, sector, (SECTOR_BUFFER *)buffer);
}

RtStatus_t MediaCacheEntry::write()
{
    assert(isValid);
    
    RtStatus_t status = DriveWriteSector(drive, sector, (SECTOR_BUFFER *)buffer);
    if (status == SUCCESS)
    {
        // Clear dirty flag on a successful write.
        isDirty = 0;
    }

    return status;
}

RtStatus_t MediaCacheEntry::flush()
{
    RtStatus_t status = SUCCESS;
    
    if (isValid && isDirty)
    {
        status = write();
    }
    return status;
}

void MediaCacheEntry::retain()
{
    // Disable interrupts while modifying the ref count.
    bool irqState = hw_core_EnableIrqInterrupt(false);
    
    // Increment the reference count.
    refcount++;
    
    hw_core_EnableIrqInterrupt(irqState);
}

void MediaCacheEntry::release()
{
    // Disable interrupts while modifying the ref count.
    bool irqState = hw_core_EnableIrqInterrupt(false);
    
    // Decrement the reference count of this cache entry.
    assert(refcount > 0);
    refcount--;
    
    hw_core_EnableIrqInterrupt(irqState);
}

RtStatus_t MediaCacheEntry::waitUntilWriteCompletes()
{
    uint64_t startTime = hw_profile_GetMicroseconds();
    uint64_t elapsed = 0;
    while (isWritePending && elapsed < CACHE_WAIT_TIMEOUT)
    {
        uint32_t u32OwnershipCount;

        u32OwnershipCount = release_cache_lock(); //cache_unlock();
        tx_thread_sleep(1);
        resume_cache_lock(u32OwnershipCount); //cache_lock();
        elapsed = hw_profile_GetMicroseconds() - startTime;
    }
    
    // Return an error if we timed out.
    if (elapsed >= CACHE_WAIT_TIMEOUT)
    {
        return ERROR_DDI_MEDIA_CACHE_TIMEOUT;
    }
    
    return SUCCESS;
}

RtStatus_t MediaCacheEntry::waitUntilRefcountReaches(unsigned targetCount)
{
    uint64_t startTime = hw_profile_GetMicroseconds();
    uint64_t elapsed = 0;
    while (refcount > targetCount && elapsed < CACHE_WAIT_TIMEOUT)
    {
        uint32_t u32OwnershipCount;

        // Unlock the cache while waiting so other threads can have a chance to release this entry.
        u32OwnershipCount = release_cache_lock(); //cache_unlock();
        tx_thread_sleep(1);
        resume_cache_lock(u32OwnershipCount); //cache_lock();
        elapsed = hw_profile_GetMicroseconds() - startTime;
    }
    
    // Return an error if we timed out.
    if (elapsed >= CACHE_WAIT_TIMEOUT)
    {
        return ERROR_DDI_MEDIA_CACHE_TIMEOUT;
    }
    
    return SUCCESS;
}

RedBlackTree::Key_t MediaCacheEntry::getKey() const
{
    return cache_BuildIndexKey(drive, sector);
}

bool MediaCacheEntry::isNodeValid() const
{
    return static_cast<bool>(isValid);
}

int MediaCacheEntry::getWeight() const
{
    return weight;
}

#if CACHE_VALIDATE
void MediaCacheEntry::validate() const
{
    if (!isValid)
    {
        return;
    }
    
    // Can't have a write pending without being owned.
    if (isWritePending && refcount == 0)
    {
        printf("Warning! Write pending on cache entry with 0 refcount: entry 0x%08x\n", this);
    }
    
    // Can't have write-through set without write-pending set.
    if (isWriteThrough && !isWritePending)
    {
        printf("Warning! Write through set on cache entry without write pending: entry 0x%08x\n", this);
    }
    
    // Check sector number if drive is valid.
    if (sector >= DriveGetInfoTyped<uint32_t>(drive, kDriveInfoSizeInNativeSectors))
    {
        printf("Warning! Invalid native sector number %d for cache entry 0x%08x\n", sector, this);
    }
}
#endif // CACHE_VALIDATE

//! \return An entry in the cache for the given sector.
//! \retval NULL No entry exists in the cache for \a sectorNumber.
MediaCacheEntry * cache_index_LookupSectorEntry(unsigned driveNumber, unsigned sectorNumber)
{
#if CACHE_STATISTICS
    // Time the map search.
    SimpleTimer timer;
#endif // CACHE_STATISTICS
    
    // Make sure we have a valid tree.
    assert(g_mediaCacheContext.tree);
    
    // Look for the sector in the tree.
    MediaCacheEntry * entry = NULL;
    RedBlackTree::Key_t key = cache_BuildIndexKey(driveNumber, sectorNumber);
    entry = static_cast<MediaCacheEntry *>(g_mediaCacheContext.tree->find(key));
    assert(!entry || entry->getKey() == key);
    
#if CACHE_STATISTICS
    g_mediaCacheContext.indexSearchTime += timer.getElapsed();
#endif // CACHE_STATISTICS
    
    return entry;
}

void cache_index_RemoveSectorEntry(MediaCacheEntry * entry)
{
    assert(entry->isValid);
    
#if CACHE_STATISTICS
    // Time the map search.
    SimpleTimer timer;
#endif // CACHE_STATISTICS
    
    // Make sure we have a valid tree.
    assert(g_mediaCacheContext.tree);

    g_mediaCacheContext.tree->remove(entry);
    
#if CACHE_STATISTICS
    g_mediaCacheContext.indexRemoveTime += timer.getElapsed();
#endif // CACHE_STATISTICS
}

void cache_index_AddSectorEntry(MediaCacheEntry * entry)
{
    assert(entry->isValid);
    
#if CACHE_STATISTICS
    // Time the map search.
    SimpleTimer timer;
#endif // CACHE_STATISTICS
    
    // Make sure we have a valid tree.
    assert(g_mediaCacheContext.tree);

    g_mediaCacheContext.tree->insert(entry);
    
#if CACHE_STATISTICS
    g_mediaCacheContext.indexInsertTime += timer.getElapsed();
#endif // CACHE_STATISTICS
}

#if CACHE_VALIDATE
void cache_ValidateEntries()
{
    MediaCacheLock lockCache;
    
    std::set<RedBlackTreeKey_t> sectors;
    unsigned i;
    MediaCacheEntry * entry = g_mediaCacheContext.entries;
    
    for (i = 0; i < g_mediaCacheContext.entryCount; i++, entry++)
    {
        // Nothing to verify on invalid entries.
        if (!entry->isValid)
        {
            continue;
        }
        
        // Make sure there isn't a duplicate.
        RedBlackTreeKey_t key = cache_BuildIndexKey(entry->drive, entry->sector);
        std::set<RedBlackTreeKey_t>::iterator it = sectors.find(key);
        if (it == sectors.end())
        {
            sectors.insert(key);
        }
        else
        {
            printf("Warning! Duplicate cache sector key: 0x%08x\n", key);
        }
        
        // Let the entry validate itself.
        entry->validate();
        
        // Now check that the entry is in the correct sector index tree. We skip this
        // check if the parent is NULL because that means it has been removed from the
        // index tree.
        if (entry->getParent() != NULL)
        {
            RedBlackTreeNode * node = g_mediaCacheContext.tree->find(entry->getKey());
            
            if ((MediaCacheEntry *)node != entry)
            {
                printf("Warning! Tree for drive %d doesn't contain entry 0x%08x (sector %d)\n", entry->drive, entry, entry->sector);
            }
        }
    }
}
#endif // CACHE_VALIDATE

//! The purpose of this function is to convert a nominal sector number into
//! the corresponding native sector number and an offset into that native sector. Part
//! of this process is to also apply the offset to convert the partition-relative nominal
//! sector to the drive-relative native sector. Sector numbers in the cache are always
//! drive-relative.
//!
//! These two conversions are optional, however. They can individually be disabled through
//! the use of these flags:
//! - #kMediaCacheFlag_NoPartitionOffset: This flag prevents the partition offset from
//!     being added to the nominal sector before it is converted into a native sector
//!     number.
//! - #kMediaCacheFlag_UseNativeSectors: The \a nominalSector value passed in is returned
//!     as is in the \a nativeSector parameter, and the \a subsectorOffset parameter is
//!     set to zero.
//!
//! In addition, this function can compute the maximum number of sectors that can be
//! returned while remaining within the underlying native sector. This feature is used for
//! the read and pinned write operations that let the caller ask for more than one sector
//! at a time.
//!
//! \param pb A param block that is used to pass several parameters, including the flags,
//!     sector, and drive.
//! \param[out] nativeSector Upon return this points to the native sector number corresponding to
//!     the nominal sector value that was provided. If the #kMediaCacheFlag_UseNativeSectors flag
//!     is set, then this value will be equal to \a nominalSector.
//! \param[out] subsectorOffset Points to the offset in bytes of the nominal sector within the
//!     native sector.
//! \param[out] actualSectorCount When this parameter is not NULL, it will be set on return to
//!     the minimum of the number of nominal sectors from the subsector to the next native
//!     sector and the \a requestSectorCount field of the param block. Or if the
//!     #kMediaCacheFlag_UseNativeSectors flag is set, the value will always be 1.
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER
//! \retval ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS
RtStatus_t cache_AdjustAndConvertSector(MediaCacheParamBlock_t * pb, unsigned * nativeSector, unsigned * subsectorOffset, unsigned * actualSectorCount)
{
    assert(nativeSector);
    assert(subsectorOffset);
    
    LogicalDrive * driveDescriptor = DriveGetDriveFromTag(pb->drive);

    // Check the drive and return an error if it is bogus.
    if (!driveDescriptor)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }
    
    // Check either nominal or native sector bounds based on the flag.
    if (pb->flags & kMediaCacheFlag_UseNativeSectors)
    {
        if (pb->sector >= driveDescriptor->m_numberOfNativeSectors)
        {
            return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
        }
    }
    else
    {
        if (pb->sector >= driveDescriptor->m_u32NumberOfSectors)
        {
            return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
        }
    }
    
#if CACHE_VALIDATE
    // This is just a convenient place to validate all cache entries, since this function
    // is called from just about everywhere.
    cache_ValidateEntries();
#endif
    
    // Apply the partition offset to the sector that was passed in, if the drive is
    // one of the data drives (internal, external, Janus) and as long as the NoPartitionOffset
    // flag is not set.
    unsigned actualSector = pb->sector;
    if (!(pb->flags & kMediaCacheFlag_NoPartitionOffset))
    {
        actualSector += driveDescriptor->m_pbsStartSector;
    }
    
    // Convert nominal to native sectors.
    *subsectorOffset = 0;
    if (pb->flags & kMediaCacheFlag_UseNativeSectors)
    {
        *nativeSector = actualSector;
    }
    else
    {
        unsigned shift = driveDescriptor->m_nativeSectorShift;
        *nativeSector = actualSector >> shift;
        unsigned subsector = actualSector - (*nativeSector << shift);
        *subsectorOffset = subsector * driveDescriptor->m_u32SectorSizeInBytes;
    }

    // Allow up to as many nominal sectors fit in a native sector. If using native sectors,
    // however, only allow one. Unlike the other functions, we compute this early here because
    // we need the actual sector count to determine if we have to do a read back.
    if (actualSectorCount)
    {
        unsigned maxSectors = 1;
        if (!(pb->flags & kMediaCacheFlag_UseNativeSectors))
        {
            maxSectors = (driveDescriptor->m_nativeSectorSizeInBytes - *subsectorOffset) / driveDescriptor->m_u32SectorSizeInBytes;
        }
        *actualSectorCount = std::min<unsigned>(pb->requestSectorCount, maxSectors);
    }
    
    return SUCCESS;
}

void resume_cache_lock(uint32_t u32OwnershipCount)
{
    while(u32OwnershipCount > 0)
    {
        // Wait forever in the release build so we don't blow up due to colliding threads.
        tx_mutex_get(&g_mediaCacheContext.mutex, TX_WAIT_FOREVER);
        u32OwnershipCount--;
    }
}
    
uint32_t release_cache_lock()
{
    TX_THREAD   *pCurTx;
    uint32_t    u32Count;
    uint32_t    u32OwnershipCount = 0;
    
    pCurTx = tx_thread_identify();
    u32OwnershipCount = g_mediaCacheContext.mutex.tx_mutex_ownership_count;

    // check if the current thread is the owner of this mutex
    if(u32OwnershipCount > 0 && 
       pCurTx->tx_thread_id == g_mediaCacheContext.mutex.tx_mutex_owner->tx_thread_id)
    {    
        u32Count = u32OwnershipCount;
        while(u32Count > 0)
        {
            tx_mutex_put(&g_mediaCacheContext.mutex);            
            u32Count--;
        }
    }
    return u32OwnershipCount;
}



////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////


