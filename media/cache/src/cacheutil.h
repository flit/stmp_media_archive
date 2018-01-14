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
//! \file cacheutil.h
//! \ingroup media_cache_internal
//! \brief Contains internal declarations for media cache index utilities.
////////////////////////////////////////////////////////////////////////////////
#ifndef __cacheutil_h__
#define __cacheutil_h__

#include "drivers/media/cache/media_cache.h"
#include <map>
#include <string.h>
#include <error.h>
#include <stdio.h>
#include "RedBlackTree.h"
#include "wlru.h"
#include "access_record.h"
#include "cache_statistics.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "simple_mutex.h"

extern "C" {
#include "hw/profile/hw_profile.h"
#include "hw/core/hw_core.h"
#include "os/filesystem/os_filesystem_errordefs.h"
#include "os/threadx/tx_api.h"
#include "os/threadx/os_tx_errordefs.h"
#include "os/thi/os_thi_api.h"
}

/*!
 * \page mediacacheinternaldocs1 Media Cache Internal Documentation
 * \ingroup media_cache_internal
 *
 * \section LRU
 *
 * The least-recently-used index maintains a list of cache entries
 * sorted by how recently they were used. The oldest entry is the head
 * of the list, and the most recent is the tail. This makes it a O(1)
 * operation to select the oldest cache entry to reuse.
 *
 * Both valid and invalid entries are present in the list. Invalid entries
 * are always inserted at the head of the list (oldest) so that they will be reused as
 * soon as possible. However, only unused, or unowned, entries are ever allowed
 * to be in the list. As soon as an entry is retained, it is removed from the list.
 *
 * \section Notes
 *
 * The sector indices are not only used to improve search time, but also work as a sort of
 * software semaphore or valid list. That is, only valid entries are ever in the indices.
 * There are asserts in cache_index_AddSectorEntry() and cache_index_RemoveSectorEntry()
 * to verify that this is true.
 */

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

//! \addtogroup media_cache_internal
//@{

//! \def CACHE_VALIDATE
//!
//! Set this macro to 1 to turn on validation of entry chains.
#if !defined(CACHE_VALIDATE)
    #define CACHE_VALIDATE 0
#endif

//! Two second timeout in microseconds.
#define CACHE_WAIT_TIMEOUT (2000000)

//! Number of ticks to wait to obtain the cache mutex.
#define CACHE_WAIT_TICKS (OS_MSECS_TO_TICKS(CACHE_WAIT_TIMEOUT/1000))

//! Maximum number of chained cache entries. This value is limited by the number
//! of bits available for the chained count in the token, although it is
//! currently set much lower than that limit.
#define CACHE_MAX_CHAINED_ENTRIES (8)

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

//! \brief Masks for the cache token fields.
//!
//! The token value returned to the caller for read and pinned write calls is
//! a 32-bit value containing several fields. The top 8 bits are a static
//! signature that identifies a valid token. The next 8 bits hold the number
//! of entries chained onto the first one. If there was only a single entry
//! returned to the caller, then this value will be 0. Finally, the bottom
//! half word holds the array index for the first entry in the result chain.
enum cache_token_field_masks
{
    kMediaCacheTokenEntryIndexMask = 0xffffL,
    kMediaCacheTokenChainedEntriesMask = 0xff0000L,
    kMediaCacheTokenChainedEntriesPosition = 16,
    kMediaCacheTokenSignature = 0x5a000000L,
    kMediaCacheTokenSignatureMask = 0xff000000L
};

//@}

////////////////////////////////////////////////////////////////////////////////
// Classes
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Media cache entry.
 * 
 * Each cache entry represents a single native sector of a drive. Each entry may
 * contain multiple nominal sectors, depending on whether the nominal sector size
 * is smaller than the native sector size. Read and write operations are always
 * performed on an entire native sector at once.
 *
 * This structure is a subclass of the red black tree node class. The entries are
 * themselves the nodes in the tree.
 *
 * The \a refcount field is used to keep track of the number of users of the
 * cache entry. When this field has a value of 0, there are no users. There can be
 * any number of readers of a given entry at once. However, when an entry is being
 * written to, only that single writer may own it. The \a refcount field doesn't
 * distinguish between readers and writers. Instead, the \a isWritePending flag
 * is used to signify that the current owner is a writer.
 *
 * In practice, you may see an entry with multiple owners and the \a isWritePending
 * flag set. This is simply because a blocked reader will pre-retain the entry to
 * prevent it from being invalidated or evicted when the writer releases. The
 * reader still waits for the write to complete before allowing the caller access.
 *
 * \ingroup media_cache_internal
 */
struct MediaCacheEntry : public RedBlackTree::Node, public WeightedLRUList::Node
{
    //! \name Flags
    //@{
    
        uint8_t isValid:1;                  //!< Indicates whether the entry contains valid data (i.e. data has been read from storage into this cache entry).
        uint8_t isDirty:1;                  //!< Indicates that this cache entry has been modified and needs to be written to media.
        volatile uint8_t isWritePending:1;  //!< True when a pinned write is in progress.
        uint8_t isWriteThrough:1;           //!< Whether the pending write is a write-through.
        uint8_t bInsertToLRU:1;             //!< Indicates that this cache entry should be inserted on the LRU end instead of the (usual) MRU end.
        uint8_t _pad:3;                     //!< Unused pad field.
    
    //@}

    volatile uint8_t refcount; //!< Current number of owners of this entry. The entry has no owners when this value is zero.
	uint8_t weight;          //!< 
    DriveTag_t drive;         //!< Unique tag value for the logical drive.
	uint32_t sector;       //!< Native sector number. Always drive relative, not partition relative.

#if CACHE_STATISTICS
    //! \name Statistics
    //@{
    
        uint64_t timestamp;    //!< Time in microseconds when the cache entry was last accessed.
        uint64_t creationTimestamp;    //!< Timestamp when the entry was first created and loaded with data. Every time an entry is evicted and repurposed, this field gets reset to the current time.
        unsigned readCount;     //!< Number of read accesses.
        unsigned writeCount;    //!< Number of write accesses.
    
    //@}
#endif // CACHE_STATISTICS
    
    //! Pointer to the cache buffer. This is the last member of this structure simply because that
    //! makes it easier to see the other members in the MULTI debugger windows.
	uint8_t * buffer;

    //! \brief Constructor.
    MediaCacheEntry(uint8_t * theBuffer)
    :   RedBlackTree::Node(),
        WeightedLRUList::Node(),
        buffer(theBuffer)
    {
        // Clear the rest of the fields to zero.
        reset();
    }
    
    //! \name Utilities
    //@{
    
        //! \brief Clears and invalidates the entry.
        //! \pre The entry must have already been removed from any lists or indices.
        void reset();
        
        //! \brief Calculates the cache entry's index in an array.
        //! \param arrayStart The pointer to the start of the array of which the entry is a element.
        //! \return An integer index into \a arrayStart is calculated and returned.
        unsigned getArrayIndex(MediaCacheEntry * arrayStart) const
        {
            return ((uint32_t)this - (uint32_t)arrayStart) / sizeof(*this);
        }
        
#if CACHE_VALIDATE
        //! \brief Make sure all fields make sense.
        void validate() const;
#endif // CACHE_VALIDATE

    //@}

    //! \name Tree node methods
    //@{
    
        //! \brief Return the node's key value.
        virtual RedBlackTree::Key_t getKey() const;
    
    //@}
    
    //! \name LRU node methods
    //@{
    
        //! \brief Returns whether the cache entry is valid.
        virtual bool isNodeValid() const;
        
        //! \brief Returns the entry's weight value.
        virtual int getWeight() const;
    
    //@}
    
    //! \name Media operations
    //@{
    
        //! \brief Reads in the cache entry from media.
        //!
        //! All members except #buffer must be set before this method is called, otherwise
        //! results will be unexpected.
        //!
        //! \pre Fields must be set.
        RtStatus_t read();
        
        //! \brief Writes the cache entry to media.
        //!
        //! Be sure to fill in the members of the entry before calling this method! On a
        //! successful write, the #isDirty flag is cleared. Note that this method does not
        //! check the status of #isDirty before writing. Use flush() if you need that
        //! functionality. Also, the cache entry must be valid before calling this method.
        //!
        //! \pre Entry is valid and fields are set correctly.
        RtStatus_t write();
        
        //! \brief Writes the cache entry to media only if it is valid and dirty.
        RtStatus_t flush();
    
    //@}
    
    //! \name Reference counting
    //@{
    
        //! \brief Safely increments the entry's reference count by one.
        void retain();
        
        //! \brief Safely reduces the entry's reference count by one.
        void release();
        
        //! \brief Wait until there is no longer a pending write.
        //! \retval SUCCESS The \a isWritePending flag is no longer set.
        //! \retval ERROR_DDI_MEDIA_CACHE_TIMEOUT Timed out waiting for the write to complete.
        RtStatus_t waitUntilWriteCompletes();
        
        //! \brief Wait until the cache entry has no owners (a zero ref count).
        //! \note This method does not check for pending writes, because a writer
        //!     will also retain the cache entry until the write completes.
        //! \note The cache mutex is temporarily released while waiting.
        //! \retval SUCCESS There are no owners of the cache entry.
        //! \retval ERROR_DDI_MEDIA_CACHE_TIMEOUT Timed out waiting for the owner count to drop to zero.
        //! \pre The cache must be locked.
        inline RtStatus_t waitUntilUnowned() { return waitUntilRefcountReaches(0); }

        //! \brief Wait until there is a single owner of the entry.
        //!
        //! The caller can retain the entry and then use this method to wait until it
        //! is the sole owner.
        //!
        //! \note The cache mutex is released while waiting.
        //! \retval SUCCESS There is only one owner of the cache entry.
        //! \retval ERROR_DDI_MEDIA_CACHE_TIMEOUT Timed out waiting for the owner count to reach one.
        //! \pre The cache must be locked.
        inline RtStatus_t waitUntilOneOwner() { return waitUntilRefcountReaches(1); }
        
        //! \brief Common method for waiting on the refcount value.
        //! \note The cache mutex is temporarily released while waiting.
        //! \retval SUCCESS There are no owners of the cache entry.
        //! \retval ERROR_DDI_MEDIA_CACHE_TIMEOUT Timed out waiting for the owner count to reach \a targetCount.
        //! \pre The cache must be locked.
        RtStatus_t waitUntilRefcountReaches(unsigned targetCount);
        
        //! \brief Returns true if the entry has no owners.
        //!
        //! Invalid entries always have no owners.
        inline bool isUnowned() const { return !isValid || refcount == 0; }
    
    //@}
};

/*!
 * \brief Contains global media cache information.
 *
 * The statistics and access record members are only present when their respective
 * compile time option is enabled.
 *
 * The \a cacheIndexTree member is a red-black tree that maps from drive tag to the index
 * tree for that drive. The nodes of this tree have the drive tag as their key and hold
 * a pointer to the drive's cache index tree. The cache index tree then maps from 
 * drive-relative sector numbers to cache entries.
 *
 * The \a entries member is an array of fixed size containing all of the cache entry
 * descriptor structures. These descriptors are themselves both red-black tree nodes and
 * LRU list nodes, allowing them to be present in both a drive's cache index and the LRU
 * list as the same time. Because the cache entry descriptors are pre-allocated, there
 * is never a need to allocate a new tree or list node during runtime.
 *
 * \ingroup media_cache_internal
 */
struct MediaCacheContext
{
    bool isInited;  //!< True if the media cache has been initialized.
    TX_MUTEX mutex; //!< Mutex to protect access to the media cache.
    unsigned entryBufferSize;     //!< Size in bytes of the cache entry sector buffers. This is the maximum sector size for all drives.
    unsigned entryCount;    //!< Number of cache entries.
    MediaCacheEntry * entries;    //!< Pointer to the array of cache entries.
    unsigned maxChainedEntries;   //!< Maximum number of entries that may be chained for a read or pinned write.
    RedBlackTree * tree;    //!< Tree indexing all cached sectors.
    WeightedLRUList * lru;  //!< The LRU list.

#if CACHE_STATISTICS
    //! \name Statistics
    //@{
    
        MediaCacheDriveStatistics statistics[MAX_LOGICAL_DRIVES];   //!< Access statistics for all drives.
        MediaCacheDriveStatistics combinedStatistics; //!< Statistics for all drives together. 
        MediaCacheAverageTime indexSearchTime;  //!< Average microseconds spent searching the cache tree.
        MediaCacheAverageTime indexInsertTime;  //!< Average time to insert an entry into the cache tree.
        MediaCacheAverageTime indexRemoveTime;  //!< Average time to remove an entry from the cache tree.
        
    //@}
#endif

#if CACHE_ACCESS_RECORD
    //! \name Access record
    //@{
    
        //! Unsorted linked lists of all sectors accessed with read and write counts and access times.
        MediaCacheAccessInfo * accessRecordList[MAX_LOGICAL_DRIVES];
        
        //! \brief Linked list of the #CACHE_MAX_HISTORY_COUNT most recent accesses, for all drives.
        //!
        //! The oldest record is at the head of the list, the newest at the tail.
        MediaCacheOperationHistory operationHistory;
    
    //@}
#endif // CACHE_ACCESS_RECORD
};

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

extern MediaCacheContext g_mediaCacheContext;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

// This comes down here because it needs g_mediaCacheContext to be declared.
/*!
 * \brief Helper class to automatically lock and unlock the media cache mutex.
 */
class MediaCacheLock : public SimpleMutex
{
public:
    //! \brief Constructor; locks the mutex.
    MediaCacheLock() : SimpleMutex(g_mediaCacheContext.mutex) {}
};

//! \addtogroup media_cache_internal
//@{

//! \name Utilities
//@{

    //! \brief Applies the partition offset and converts to a native sector and offset.
    RtStatus_t cache_AdjustAndConvertSector(MediaCacheParamBlock_t * pb, unsigned * nativeSector, unsigned * subsectorOffset, unsigned * actualSectorCount);

    //! \brief Creates a 64-bit index key from a drive and sector number pair.
    inline RedBlackTree::Key_t cache_BuildIndexKey(unsigned drive, unsigned sector)
    {
        return (static_cast<int64_t>(sector) | (static_cast<int64_t>(drive) << 32));
    }

    //! \brief Fully unlocks the cache mutex and returns its lock count.
    //! \return The lock count before it was unlocked.
    uint32_t release_cache_lock();
    
    //! \brief Restores the lock count for the media cache mutex.
    //! \param u32OwnershipCount The previous lock count for the mutex, as returned from release_cache_lock().
    void resume_cache_lock(uint32_t u32OwnershipCount);

//@}

//! \name Sector index
//!
//! These functions maintain an index of sector numbers to media sector cache entries.
//! Drive number is also considered. Using these functions to find a given cache entry is much
//! faster than using a linear search over all entries, especially as the number of entries
//! gets to be relatively large. A RedBlackTree is used to index the sectors for each drive.
//! Thus, the access times are O(log N) versus O(N) for linear operations.
//@{

    //! \brief Search for a matching sector in the sector cache.
    MediaCacheEntry * cache_index_LookupSectorEntry(unsigned driveNumber, unsigned sectorNumber);

    //! \brief Inserts the cache entry in the sector index.
    void cache_index_AddSectorEntry(MediaCacheEntry * entry);

    //! \brief Removes the cache entry from the sector index.
    void cache_index_RemoveSectorEntry(MediaCacheEntry * entry);

//@}

//! \name Read/write helpers
//@{

    //! \brief Evict an old entry and bring in a new one.
    RtStatus_t cache_HandleCacheMiss(MediaCacheParamBlock_t * pb, unsigned nativeSector, bool doRead, MediaCacheEntry ** resultEntry);

    //! \brief Tries to extend the result with as many contiguous sector buffers as possible.
    void cache_ExtendResultChain(MediaCacheParamBlock_t * pb, MediaCacheEntry * cache, bool isWrite);

    //! \brief Finish up a pinned write operation.
    RtStatus_t cache_CompletePinnedWrite(MediaCacheEntry * cache);

//@}

#if CACHE_VALIDATE
//! \name Validation
//@{

    //! \brief Examine all cache entries and verify their fields.
    void cache_ValidateEntries();

    //! \brief Verify that cache entry chain contents are as expected.
    void cache_ValidateChain(MediaCacheParamBlock_t * pb, MediaCacheEntry * base, bool isWrite);

//@}
#endif // CACHE_VALIDATE

//@}

#endif // __cacheutil_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

