///////////////////////////////////////////////////////////////////////////////
// Copyright (c) SigmaTel, Inc. All rights reserved.
//
// SigmaTel, Inc.
// Proprietary & Confidential
//
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of SigmaTel, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup media_buf_mgr
//! @{
//! \file media_buffer_manager.c
//! \brief Implementation of the media buffer manager.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "media_buffer_manager_internal.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/ddi_media_errordefs.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "drivers/media/nand/hal/src/ddi_nand_hal_internal.h"
#include "os/thi/os_thi_api.h"
#include "os/dpc/os_dpc_api.h"
#include "os/vmi/os_vmi_api.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

#if !defined(INT32_MAX)
    #define INT32_MAX (0x7fffffffL)
#endif

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

//! \brief Global state information for the buffer manager.
MediaBufferManagerContext_t g_mediaBufferManagerContext;

#if RECORD_BUFFER_STATS

//! In debug builds, this global controls whether acquires and releases
//! of all buffers, both permanent and temporary, will be logged. The
//! logging of allocations is useful to see the sequence in which buffers
//! are acquired and released.
bool g_mediaBufferManagerLogAllocations = false;

#endif // RECORD_BUFFER_STATS

#pragma weak g_nandHalContext

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

static size_t media_buffer_get_type_size(MediaBufferType_t bufferType);
static RtStatus_t media_buffer_add_internal(MediaBufferType_t bufferType, uint32_t bufferFlags, uint32_t refCount, SECTOR_BUFFER * buffer, SECTOR_BUFFER * originalBuffer, unsigned * insertIndex);
static bool media_buffer_search(size_t length, bool exactLengthMatch, uint32_t flags, unsigned * result);
static SECTOR_BUFFER * media_buffer_allocate_internal(size_t length, uint32_t flags, bool physicallyContiguous);
static bool media_buffer_is_contiguous(SECTOR_BUFFER * buffer, size_t length);
static SECTOR_BUFFER * media_buffer_allocate(size_t length, uint32_t flags, uint32_t * resultFlags, SECTOR_BUFFER ** original);
static void media_buffer_shrink_slots(void);
static void media_buffer_setup_next_timeout(void);
static void media_buffer_dispose_temporary(uint32_t unused);

#if RECORD_BUFFER_STATS
static void media_buffer_update_stats(MediaBufferStatistics_t * stats, bool isAcquire);
#endif // RECORD_BUFFER_STATS

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

//! \brief Returns the size in bytes of a buffer type.
//!
//! This function will return the size of a class of buffer. It takes
//! knowledge available only at runtime into account, such as the page size of
//! the currently attached NAND device.
//!
//! \param bufferType The class of the buffer.
//!
//! \return A buffer length in bytes.
static size_t media_buffer_get_type_size(MediaBufferType_t bufferType)
{
    switch (bufferType)
    {
        case kMediaBufferType_Sector:
            return MediaGetMaximumSectorSize();
            
        case kMediaBufferType_Auxiliary:
            return REDUNDANT_AREA_BUFFER_ALLOCATION;
            
        case kMediaBufferType_NANDPage:
            // This is an ugly kludge that lets us avoid a dependency upon MediaGetInfo(), which
            // is very useful for keeping unit tests simple.
            // As above, use the maximum size possible if the media is not ready yet.
            if (&g_nandHalContext != NULL)
            {
                return g_nandHalContext.parameters.pageTotalSize;
            }
            else
            {
                return MAX_SECTOR_TOTAL_SIZE;
            }

        default:
            return 0;
    }
}

//! \brief Internal add function that returns the new buffer's index.
//!
static RtStatus_t media_buffer_add_internal(MediaBufferType_t bufferType, uint32_t bufferFlags, uint32_t refCount, SECTOR_BUFFER * buffer, SECTOR_BUFFER * originalBuffer, unsigned * insertIndex)
{
    unsigned i;
    MediaBufferInfo_t * info;

    assert(buffer != NULL);
    assert(((uint32_t)buffer & 0x3) == 0); // Make sure the buffer is word aligned.
    assert(g_mediaBufferManagerContext.isInited);

    // Acquire mutex.
    tx_mutex_get(&g_mediaBufferManagerContext.mutex, TX_WAIT_FOREVER);

    // Can't add a buffer when there is no more room.
    if (g_mediaBufferManagerContext.bufferCount == MAX_BUFFER_COUNT)
    {
        tx_mutex_put(&g_mediaBufferManagerContext.mutex);
        return ERROR_DDI_MEDIABUFMGR_NO_ROOM;
    }

    // Find an empty slot to insert the new buffer into. If we don't find one,
    // then when the loop is finished i will point at the next unused slot.
    for (i=0; i < g_mediaBufferManagerContext.slotCount; ++i)
    {
        if (g_mediaBufferManagerContext.buffers[i].data == NULL)
        {
            break;
        }
    }

    assert(i <= MAX_BUFFER_COUNT);

    // Fill in new buffer information.
    info = &g_mediaBufferManagerContext.buffers[i];
    info->length = media_buffer_get_type_size(bufferType);
    info->data = buffer;
    info->flags = bufferFlags;
    info->refCount = refCount;
    info->timeout = 0;
    info->originalBuffer = originalBuffer;

#if RECORD_BUFFER_STATS
    // In debug builds we keep track of the buffer's type for statistics
    // generation purposes.
    info->bufferType = bufferType;
#endif // RECORD_BUFFER_STATS

    // Increment buffer count and free count.
    g_mediaBufferManagerContext.bufferCount++;
    g_mediaBufferManagerContext.freeCount++;

    // Increment the used count if we added to the end.
    if (i == g_mediaBufferManagerContext.slotCount)
    {
        g_mediaBufferManagerContext.slotCount++;
    }

    // Return the index of the newly added buffer.
    if (insertIndex)
    {
        *insertIndex = i;
    }

    // Release mutex.
    tx_mutex_put(&g_mediaBufferManagerContext.mutex);
    return SUCCESS;
}

// See media_buffer_manager.h for the documentation for this function.
RtStatus_t media_buffer_add(MediaBufferType_t bufferType, uint32_t bufferFlags, SECTOR_BUFFER * buffer)
{
    return media_buffer_add_internal(bufferType, bufferFlags, 0, buffer, NULL, NULL);
}

//! \brief Searches the buffer list for a free buffer with the desired attributes.
//!
//! \param[in] length Desired length in bytes of the buffer.
//! \param[in] exactLengthMatch Whether \a length must match the buffer's size
//!     exactly, or the buffer can be larger than the requested size.
//! \param[in] flags Flags that the buffer must have set.
//! \param[out] result On successful exit, this parameter is set to the index
//!     of the buffer that was found.
//!
//! \retval true A matching, free buffer was found. The \a result parameter holds
//!     the index of the matching buffer.
//! \retval false No buffer is available that matches the request.
//!
//! \todo Improve the search algorithm. Right now it is only a simple linear search.
static bool media_buffer_search(size_t length, bool exactLengthMatch, uint32_t flags, unsigned * result)
{
    unsigned i;

    assert(result != NULL);

    // Search for free buffer.
    for (i=0; i < g_mediaBufferManagerContext.slotCount; ++i)
    {
        MediaBufferInfo_t * info = &g_mediaBufferManagerContext.buffers[i];
        bool isMatch;

        // Skip the buffer if it's data pointer is null.
        if (info->data == NULL)
        {
            continue;
        }

        // A buffer matches the request if:
        // - it is not busy
        // - the request flags are set on the buffer
        // - exactLengthMatch is:
        //   - true: length matches exactly
        //   - false: buffer length is >= the request length
        isMatch = ((info->flags & kMediaBufferFlag_InUse) == 0)
            && (((info->flags & ~kMediaBufferManager_InternalFlagsMask) & flags) == flags);

        if (exactLengthMatch)
        {
            // Length must match exactly.
            isMatch = isMatch && (info->length == length);
        }
        else
        {
            // Match as long as the buffer's length is large enough for the request.
            isMatch = isMatch && (info->length >= length);
        }

        // Return the buffer index if we've hit a match.
        if (isMatch)
        {
            *result = i;
            return true;
        }
    }

    // No buffer is available that matches the request.
    return false;
}

//! \brief Allocate a buffer modified by the flags.
//!
static SECTOR_BUFFER * media_buffer_allocate_internal(size_t length, uint32_t flags, bool physicallyContiguous)
{
    SECTOR_BUFFER * resultBuffer;
    
    switch (flags & (kMediaBufferFlag_NCNB | kMediaBufferFlag_FastMemory))
    {
        // Both fast and NCNB memory required.
        case kMediaBufferFlag_NCNB | kMediaBufferFlag_FastMemory:
            resultBuffer = (SECTOR_BUFFER *)os_dmi_malloc_fastmem_ncnb(length);
            break;

        // Only NCNB memory required.
        case kMediaBufferFlag_NCNB:
            resultBuffer = (SECTOR_BUFFER *)os_dmi_malloc_ncnb(length);
            break;

        // Only fast memory required.
        case kMediaBufferFlag_FastMemory:
            if (physicallyContiguous)
            {
                resultBuffer = (SECTOR_BUFFER *)os_dmi_malloc_fastmem_phys_contiguous(length);
            }
            else
            {
                resultBuffer = (SECTOR_BUFFER *)os_dmi_malloc_fastmem(length);
            }
            break;

        // No special requirements for the memory type.
        default:
            if (physicallyContiguous)
            {
                resultBuffer = (SECTOR_BUFFER *)os_dmi_malloc_phys_contiguous(length);
            }
            else
            {
                resultBuffer = (SECTOR_BUFFER *)malloc(length);
            }
            break;
    }
    
    return resultBuffer;
}

//! \brief Determines whether a given buffer is physically contiguous.
//!
//! This function first gets the physical page number of the first word in
//! the buffer. Then it advances a VM page at a time through the buffer,
//! comparing the physical page at each step to make sure they are all
//! physically sequential. Finally, this function checks the physical page
//! of the last word of the buffer to make sure it is sequential as well.
//!
//! \param buffer The virtual address of the buffer to examine.
//! \param length Number of bytes long that the buffer is.
//! \retval true The buffer is contiguous in physical memory.
//! \retval false The buffer is not physically contiguous.
static bool media_buffer_is_contiguous(SECTOR_BUFFER * buffer, size_t length)
{
    uint32_t physicalAddress;
    uint32_t testAddress;
    uint32_t currentPage;
    uint32_t testPage;
    uint32_t lastWordAddress = (uint32_t)buffer + length - sizeof(uint32_t);
    
    // Get physical address of the first word of the buffer.
    os_vmi_VirtToPhys((uint32_t)buffer, &physicalAddress);
    currentPage = physicalAddress / VMI_PAGE_SIZE;
    
    // Check each page of the buffer to make sure the whole thing is contiguous.
    testAddress = (uint32_t)buffer + VMI_PAGE_SIZE;
    while (testAddress < lastWordAddress)
    {
        // Get physical address of the test address.
        os_vmi_VirtToPhys(testAddress, &physicalAddress);
        testPage = physicalAddress / VMI_PAGE_SIZE;
        
        // The page containing the test address must physically follow the previous page.
        if (testPage != currentPage + 1)
        {
            return false;
        }
        
        // Advance the test address by a VMI page.
        testAddress += VMI_PAGE_SIZE;
        currentPage = testPage;
    }
    
    // Get physical address of the last word of the buffer.
    os_vmi_VirtToPhys(lastWordAddress, &physicalAddress);
    testPage = physicalAddress / VMI_PAGE_SIZE;
    
    // The buffer is contiguous if the current and end physical pages are the same, or
    // if the end page is the next page after the current one.
    return (testPage == currentPage) || (testPage == currentPage + 1);
}

//! \brief Allocate a buffer modified by the flags.
//!
//! The two flags that this function honours are:
//! - kBufferManager_NCNBFlag
//! - kBufferManager_FastMemoryFlag
//!
//! Thus, the memory allocated by this function can be any combination of
//! standard or fast memory, cached or non-cached. The returned buffer
//! should be deallocated with a call to free().
//!
//! When allocating CB memory, this routine will ensure that the returned buffer is aligned
//! to the start of a data cache line. Also, the buffer's size is rounded up to the next
//! cache line. All of this is to prevent the buffer from being modified due to a cache flush
//! after a partial cache line has been modified by other code. NCNB allocations do not need
//! to be aligned like this because they do not exist in the data cache (and NCNB regions are
//! always aligned at VM page boundaries, anyway).
//!
//! \param[in] length Number of bytes required in the buffer. Must be greater
//!     than zero.
//! \param[in] flags Flags that modify how the buffer is allocated. Any flags
//!     not supported by the function may be set and they will simply be
//!     ignored.
//! \param[out] resultFlags If the buffer had to be realigned, then #kMediaBufferFlag_Realigned
//!     will be returned through this parameter. Otherwise this is set to 0.
//! \param[out] original If the allocated buffer had to be realigned, indicated by the value
//!     of \a resultFlags upon return, then this parameter is set to the unaligned result of
//!     the allocation that must be passed to free().
//!
//! \return A pointer to a newly-allocated buffer is returned.
//! \retval NULL Returned if the memory could not be allocated for some
//!     reason, such as the system being out of memory.
static SECTOR_BUFFER * media_buffer_allocate(size_t length, uint32_t flags, uint32_t * resultFlags, SECTOR_BUFFER ** original)
{
    SECTOR_BUFFER * resultBuffer;
    size_t roundedLength;
    bool allocateContiguous = false;

    assert(length > 0);

    // Set default result flags.
    *resultFlags = 0;

    for (;;)
    {
        // Set allocation length to the request size rounded up to the next full cache line.
        roundedLength = CACHED_BUFFER_SIZE(length);
        
        // Allocate NCNB memory if that flag is set. Same for fast memory.
        // Otherwise normal memory will do.
        resultBuffer = media_buffer_allocate_internal(roundedLength, flags, allocateContiguous);
        
        // Catch an error before dealing with alignment.
        if (!resultBuffer)
        {
            return NULL;
        }

        // If the NCNB flag is not set then we need to make sure the resulting buffer
        // was aligned to the cache line size.
        if (!(flags & kMediaBufferFlag_NCNB) && ((uint32_t)resultBuffer & (BUFFER_CACHE_LINE_MULTIPLE - 1)) != 0)
        {
            // The buffer we got back doesn't have the alignment we need, so free it and
            // allocate a larger buffer that we can align within.
            free(resultBuffer);

            // Allocate enough extra room to align within.
            roundedLength += BUFFER_CACHE_LINE_MULTIPLE;

            // Allocate the larger buffer.
            resultBuffer = media_buffer_allocate_internal(roundedLength, flags, allocateContiguous);
            
            // Handle a failed allocation.
            if (!resultBuffer)
            {
                return NULL;
            }

            *resultFlags = kMediaBufferFlag_Realigned;
            *original = resultBuffer;

            // Align the buffer up to the next cache line.
            resultBuffer = (SECTOR_BUFFER *)(((uint32_t)resultBuffer & ~(BUFFER_CACHE_LINE_MULTIPLE - 1)) + BUFFER_CACHE_LINE_MULTIPLE);
        }
        
        // For non-NCNB allocations, we must make sure that the buffer is physically contiguous.
        // The first attempt at allocating is done with the non-contiguous malloc routines because
        // they are more memory-efficient and often return contiguous memory anyway.
        if (!(flags & kMediaBufferFlag_NCNB) && !media_buffer_is_contiguous(resultBuffer, length))
        {
            // If we have attempted to allocate a contiguous buffer but didn't get one back,
            // then something has gone wrong.
            assert(!allocateContiguous);
            
            // We need a contiguous buffer and the one we got is not. So we free the buffer
            // and loop around to allocate a contiguous one.
            if (*resultFlags & kMediaBufferFlag_Realigned)
            {
                free(*original);
            }
            else
            {
                free(resultBuffer);
            }
            allocateContiguous = true;
        }
        else
        {
            break;
        }
    }

    return resultBuffer;
}

#if RECORD_BUFFER_STATS
//! \brief Update statistics.
static void media_buffer_update_stats(MediaBufferStatistics_t * stats, bool isAcquire)
{
    if (isAcquire)
    {
        // Update stats for a buffer acquire.
        stats->totalAllocs++;
        stats->concurrentAllocs++;

        if (stats->concurrentAllocs > stats->maxConcurrentAllocs)
        {
            stats->maxConcurrentAllocs = stats->concurrentAllocs;
        }
    }
    else
    {
        // Update stats for a buffer release.
        stats->concurrentAllocs--;
    }
}
#endif // RECORD_BUFFER_STATS

// See media_buffer_manager.h for the documentation for this function.
RtStatus_t media_buffer_acquire(MediaBufferType_t bufferType, uint32_t requiredFlags, SECTOR_BUFFER ** buffer)
{
    size_t typeSize = media_buffer_get_type_size(bufferType);
    SECTOR_BUFFER * data;
    RtStatus_t status;

    assert(buffer != NULL);
    assert(g_mediaBufferManagerContext.isInited);

    // Acquire mutex.
    tx_mutex_get(&g_mediaBufferManagerContext.mutex, TX_WAIT_FOREVER);

    // Is there a free buffer available?
    if (g_mediaBufferManagerContext.freeCount > 0)
    {
        unsigned matchIndex;
        bool isMatch;

        // Try a first time to find a matching free buffer that has the exact
        // length being requested and matches all flags.
        isMatch = media_buffer_search(typeSize, true, requiredFlags, &matchIndex);

        // If that doesn't pan out, try again to find a buffer at least as large
        // as the requested size and has all flags set.
        if (!isMatch)
        {
            isMatch = media_buffer_search(typeSize, false, requiredFlags, &matchIndex);
        }

        // Handle when we've found a buffer the caller can use.
        if (isMatch)
        {
            // Mark the buffer as used and set the return value.
            MediaBufferInfo_t * info = &g_mediaBufferManagerContext.buffers[matchIndex];
            info->flags |= kMediaBufferFlag_InUse;
            info->refCount = 1;
            *buffer = info->data;

            g_mediaBufferManagerContext.freeCount--;

            // Deal with temporary buffers.
            if (info->flags & kMediaBufferFlag_Temporary)
            {
                // Clear the timeout.
                info->timeout = 0;

                // If this was the buffer that was next going to timeout, deactivate
                // the timer.
                if (g_mediaBufferManagerContext.nextTimeout == matchIndex)
                {
                    // Kill the timer, it will be reactivated below if needed.
                    tx_timer_deactivate(&g_mediaBufferManagerContext.timeoutTimer);

                    // See if there is a next buffer to timeout. This will update
                    // the nextTimeout field of the context.
                    media_buffer_setup_next_timeout();
                }

                // If this buffer was queued to be disposed, cancel that.
                if (g_mediaBufferManagerContext.bufferToDispose == matchIndex)
                {
                    g_mediaBufferManagerContext.bufferToDispose = NO_NEXT_TIMEOUT;
                }
            }

#if RECORD_BUFFER_STATS
            // Update statistics.
            if (info->flags & kMediaBufferFlag_Temporary)
            {
                media_buffer_update_stats(&g_mediaBufferManagerContext.tempStats, true);
                media_buffer_update_stats(&g_mediaBufferManagerContext.tempTypeStats[info->bufferType], true);
            }
            else
            {
                media_buffer_update_stats(&g_mediaBufferManagerContext.permStats, true);
                media_buffer_update_stats(&g_mediaBufferManagerContext.permTypeStats[info->bufferType], true);
            }

            if (info->bufferType != bufferType)
            {
                g_mediaBufferManagerContext.mismatchedSizeAllocs++;
            }

            // Log the permanent allocation.
            if (g_mediaBufferManagerLogAllocations)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    "bufmgr: allocated perm buffer %x [#%d, size=%d, flags=%x]\n", (uint32_t)info->data, matchIndex, typeSize, requiredFlags);
            }
#endif // RECORD_BUFFER_STATS

            tx_mutex_put(&g_mediaBufferManagerContext.mutex);
            return SUCCESS;
        }
    }

    {
        SECTOR_BUFFER * originalBuffer = NULL;
        uint32_t resultFlags;
        uint32_t bufferFlags;

        // There are no buffers available in the list, or no match was found,
        // so create a temporary one.
        data = media_buffer_allocate(typeSize, requiredFlags, &resultFlags, &originalBuffer);

        // Error out if the allocation failed.
        if (!data)
        {
            tx_mutex_put(&g_mediaBufferManagerContext.mutex);
            return ERROR_DDI_MEDIABUFMGR_ALLOC_FAILED;
        }

        // Build the combined flags that are set for this buffer when it is added below.
        // The buffer is marked as temporary and in use. Marking it as temporary
        // will cause it to be freed when the caller releases it.
        bufferFlags = requiredFlags | resultFlags | kMediaBufferFlag_Temporary | kMediaBufferFlag_InUse;

#if RECORD_BUFFER_STATS
        // Update statistics.
        media_buffer_update_stats(&g_mediaBufferManagerContext.tempStats, true);
        media_buffer_update_stats(&g_mediaBufferManagerContext.tempTypeStats[bufferType], true);

        // Increment the number of new temporary buffers allocated.
        g_mediaBufferManagerContext.tempStats.newAllocs++;
        g_mediaBufferManagerContext.tempTypeStats[bufferType].newAllocs++;

        // Increment realigned buffer count if appropriate.
        if (resultFlags & kMediaBufferFlag_Realigned)
        {
            g_mediaBufferManagerContext.tempStats.realignedAllocs++;
            g_mediaBufferManagerContext.tempTypeStats[bufferType].realignedAllocs++;
        }

        // Log the temporary allocation.
        if (g_mediaBufferManagerLogAllocations)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                "bufmgr: allocated temp buffer %x [size=%d, flags=%x]\n", (uint32_t)data, typeSize, bufferFlags);
        }
#endif // RECORD_BUFFER_STATS

        // Add the new buffer to our list with the combined flags and a
        // reference count of 1.
        status = media_buffer_add_internal(bufferType, bufferFlags, 1, data, originalBuffer, NULL);
        if (status != SUCCESS)
        {
            if (resultFlags & kMediaBufferFlag_Realigned)
            {
                free(originalBuffer);
            }
            else
            {
                free(data);
            }

            tx_mutex_put(&g_mediaBufferManagerContext.mutex);
            return status;
        }

        // Return this new buffer to the caller.
        *buffer = data;

        // Decrement the free count. buffer_add() increments it so we need to
        // counter this.
        g_mediaBufferManagerContext.freeCount--;
    }

    // Release mutex.
    tx_mutex_put(&g_mediaBufferManagerContext.mutex);

    return SUCCESS;
}

//! \brief Determine if the slot count can be reduced.
//!
//! The array of buffers is examined starting at the end and moving towards
//! the beginning, stopping as soon as a valid entry is found. If there
//! were one or more contiguous invalid entries starting at the end, the
//! active slot count is reduced. This helps to speed searches for buffers
//! in media_buffer_acquire().
static void media_buffer_shrink_slots(void)
{
    int i;

    // Scan the buffer array backwards. When we exit the loop, i will
    // be set to the number of slots from the first to the last valid one.
    for (i = g_mediaBufferManagerContext.slotCount; i > 0; --i)
    {
        // Exit the loop when the slot prior to this one is valid.
        if (g_mediaBufferManagerContext.buffers[i - 1].data)
        {
            break;
        }
    }

    // Shrink it!
    g_mediaBufferManagerContext.slotCount = i;
}

// See media_buffer_manager.h for the documentation for this function.
RtStatus_t media_buffer_retain(SECTOR_BUFFER * buffer)
{
    unsigned i;
    RtStatus_t result = ERROR_DDI_MEDIABUFMGR_INVALID_BUFFER;

    assert(buffer != NULL);
    assert(g_mediaBufferManagerContext.isInited);

    // Acquire mutex.
    tx_mutex_get(&g_mediaBufferManagerContext.mutex, TX_WAIT_FOREVER);

    // Scan the buffer array looking for a matching data pointer.
    for (i=0; i < g_mediaBufferManagerContext.slotCount; ++i)
    {
        MediaBufferInfo_t * info = &g_mediaBufferManagerContext.buffers[i];

        // Is this the matching buffer?
        if (info->data == buffer)
        {
            // Add one reference.
            ++info->refCount;

            result = SUCCESS;
            break;
        }
    }

    // Put the mutex and return the status code.
    tx_mutex_put(&g_mediaBufferManagerContext.mutex);
    return result;
}

// See media_buffer_manager.h for the documentation for this function.
RtStatus_t media_buffer_release(SECTOR_BUFFER * buffer)
{
    unsigned i;
    RtStatus_t result = ERROR_DDI_MEDIABUFMGR_INVALID_BUFFER;

    assert(buffer != NULL);
    assert(g_mediaBufferManagerContext.isInited);

    // Acquire mutex.
    tx_mutex_get(&g_mediaBufferManagerContext.mutex, TX_WAIT_FOREVER);

    // Scan the buffer array looking for a matching data pointer.
    for (i=0; i < g_mediaBufferManagerContext.slotCount; ++i)
    {
        MediaBufferInfo_t * info = &g_mediaBufferManagerContext.buffers[i];

        if (info->data == buffer)
        {
            // Decrement the reference count.
            if (--info->refCount > 0)
            {
                // There are still references to the buffer, so don't actually
                // release it yet!
                result = SUCCESS;
                break;
            }
            
            // Different actions depending on whether the buffer is temporary.
            if (info->flags & kMediaBufferFlag_Temporary)
            {
                // Temporary buffer.

#if RECORD_BUFFER_STATS
                // Log the release.
                if (g_mediaBufferManagerLogAllocations)
                {
                    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                        "bufmgr: temp buffer %x will timeout in %d ms\n", (uint32_t)info->data, TEMPORARY_BUFFER_TIMEOUT_MS);
                }
#endif // RECORD_BUFFER_STATS

                // Reset the buffer's timeout.
                info->timeout = tx_time_get() + OS_MSECS_TO_TICKS(TEMPORARY_BUFFER_TIMEOUT_MS);

                // If there is not already a temp buffer waiting to timeout, set this one up.
                if (g_mediaBufferManagerContext.nextTimeout == NO_NEXT_TIMEOUT)
                {
                    g_mediaBufferManagerContext.nextTimeout = i;

                    tx_timer_change(&g_mediaBufferManagerContext.timeoutTimer, OS_MSECS_TO_TICKS(TEMPORARY_BUFFER_TIMEOUT_MS), 0);
                    tx_timer_activate(&g_mediaBufferManagerContext.timeoutTimer);
                }
            }
            else
            {
                // Permanent buffer.

#if RECORD_BUFFER_STATS
                // Update statistics.
                media_buffer_update_stats(&g_mediaBufferManagerContext.permStats, false);
                media_buffer_update_stats(&g_mediaBufferManagerContext.permTypeStats[info->bufferType], false);

                // Log the release.
                if (g_mediaBufferManagerLogAllocations)
                {
                    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                        "bufmgr: releasing perm buffer %x [#%d]\n", (uint32_t)info->data, i);
                }
#endif // RECORD_BUFFER_STATS
            }

            // Now make this buffer available for another caller to use.
            info->flags &= ~kMediaBufferFlag_InUse;

            // Increment the number of available buffers.
            g_mediaBufferManagerContext.freeCount++;

            // All good, so return success. We can exit the loop now.
            result = SUCCESS;
            break;
        }
    }

    // Put the mutex and return the status code.
    tx_mutex_put(&g_mediaBufferManagerContext.mutex);
    return result;
}

//! \brief Look for the next temporary buffer to timeout and set up the timer.
//!
//! \pre The media buffer manager's mutex must have already been acquired.
//! \post g_mediaBufferManagerContext.nextTimeout is set to the index of the
//!     temporary buffer that will timeout next.
static void media_buffer_setup_next_timeout(void)
{
    unsigned i;
    int nextToTimeout = NO_NEXT_TIMEOUT;
    int timeoutTicks = INT32_MAX;
    int scanStartTime = tx_time_get();

    // Search for another inactive temporary buffer.
    for (i = 0; i < g_mediaBufferManagerContext.slotCount; ++i)
    {
        MediaBufferInfo_t * info = &g_mediaBufferManagerContext.buffers[i];

        // Is this a valid, inactive, temporary buffer?
        if (info->data && (info->flags & kMediaBufferFlag_Temporary) && !(info->flags & kMediaBufferFlag_InUse))
        {
            // Convert absolute timeout to relative time. The timeout time should hopefully
            // be bigger than the current time, although this is not guaranteed.
            int ticks = info->timeout - scanStartTime;

            // Make sure we never have a negative time.
            if (ticks < 0)
            {
                ticks = 0;
            }

            // Keep track of the buffer with the lowest interval to timeout.
            if (ticks < timeoutTicks)
            {
                timeoutTicks = ticks;
                nextToTimeout = i;
            }
        }
    }

    // Do we have another buffer waiting to timeout?
    if (nextToTimeout != NO_NEXT_TIMEOUT)
    {
        // Delay at least two ticks.
        if (timeoutTicks == 0)
        {
            timeoutTicks = 2;
        }

        tx_timer_change(&g_mediaBufferManagerContext.timeoutTimer, timeoutTicks, 0);
        tx_timer_activate(&g_mediaBufferManagerContext.timeoutTimer);
    }

    g_mediaBufferManagerContext.nextTimeout = nextToTimeout;
}

//! \brief Deferred procedure call to dispose of a temporary buffer.
//!
static void media_buffer_dispose_temporary(uint32_t unused)
{
    MediaBufferInfo_t * info;

    // Acquire mutex.
    tx_mutex_get(&g_mediaBufferManagerContext.mutex, TX_WAIT_FOREVER);

    // Check to make sure there is still a buffer to dispose and someone hasn't
    // come along and acquired it between when the timer fired and the DPC
    // actually started executing.
    if (g_mediaBufferManagerContext.bufferToDispose == NO_NEXT_TIMEOUT)
    {
        tx_mutex_put(&g_mediaBufferManagerContext.mutex);
        return;
    }

    assert(g_mediaBufferManagerContext.bufferToDispose >= 0 && g_mediaBufferManagerContext.bufferToDispose < g_mediaBufferManagerContext.slotCount);

    // Get the buffer info.
    info = &g_mediaBufferManagerContext.buffers[g_mediaBufferManagerContext.bufferToDispose];

#if RECORD_BUFFER_STATS
    // Update statistics.
    media_buffer_update_stats(&g_mediaBufferManagerContext.tempStats, false);
    media_buffer_update_stats(&g_mediaBufferManagerContext.tempTypeStats[info->bufferType], false);

    // Log the release.
    if (g_mediaBufferManagerLogAllocations)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
            "bufmgr: freeing temp buffer %x\n", (uint32_t)info->data);
    }
#endif // RECORD_BUFFER_STATS

    // Dispose of this temporary buffer. If we realigned the buffer then we have to
    // pass the actual pointer back and not the aligned one.
    if (info->flags & kMediaBufferFlag_Realigned)
    {
        free(info->originalBuffer);
    }
    else
    {
        free(info->data);
    }
    memset(info, 0, sizeof(*info));

    // Decrement the number of buffers in the array.
    g_mediaBufferManagerContext.bufferCount--;
    g_mediaBufferManagerContext.freeCount--;

    // Decrement the slot count if possible.
    media_buffer_shrink_slots();

    // Search for another buffer to timeout.
    media_buffer_setup_next_timeout();

    // No buffer pending disposal.
    g_mediaBufferManagerContext.bufferToDispose = NO_NEXT_TIMEOUT;

    // Release the mutex.
    tx_mutex_put(&g_mediaBufferManagerContext.mutex);
}

//! \brief Timer expiration function to time out temporary buffers.
//!
//! All this timer function does is queue up a DPC to do the actual work. This
//! is necessary because application timers have severe limits on the ThreadX
//! APIs that can be called.
 __STATIC_TEXT void media_buffer_timeout(ULONG unused)
{
    RtStatus_t status;

    // We want to dispose of the buffer that just timed out.
    g_mediaBufferManagerContext.bufferToDispose = g_mediaBufferManagerContext.nextTimeout;

    // Post DPC to do the dirty work.
    status = os_dpc_Send(OS_DPC_HIGH_LEVEL_DPC, media_buffer_dispose_temporary, 0, TX_NO_WAIT);

    // If we can't queue the DPC, set the timer up to fire again in a little bit.
    if (status != SUCCESS)
    {
        tx_timer_change(&g_mediaBufferManagerContext.timeoutTimer, OS_MSECS_TO_TICKS(TIMER_RETRY_DELAY_MS), 0);
        tx_timer_activate(&g_mediaBufferManagerContext.timeoutTimer);
    }
}

__INIT_TEXT RtStatus_t media_buffer_get_property(SECTOR_BUFFER * buffer, uint32_t whichProperty, void * value)
{
    unsigned i;
    RtStatus_t result = ERROR_DDI_MEDIABUFMGR_INVALID_BUFFER;
    MediaBufferInfo_t * info = NULL;

    assert(buffer != NULL);
    assert(value != NULL);
    assert(g_mediaBufferManagerContext.isInited);

    // Acquire mutex.
    tx_mutex_get(&g_mediaBufferManagerContext.mutex, TX_WAIT_FOREVER);

    // Scan the buffer array looking for a matching data pointer.
    for (i=0; i < g_mediaBufferManagerContext.slotCount; ++i)
    {
        info = &g_mediaBufferManagerContext.buffers[i];

        if (info->data == buffer)
        {
            // Found our buffer, so exit the scan loop!
            break;
        }
    }
    
    if (info)
    {
        switch (whichProperty)
        {
            case kMediaBufferProperty_Size:
                *(uint32_t *)value = info->length;
                break;
                
#if RECORD_BUFFER_STATS
            case kMediaBufferProperty_Type:
                *(MediaBufferType_t *)value = info->bufferType;
                break;
#endif // RECORD_BUFFER_STATS
                
            case kMediaBufferProperty_Flags:
                *(uint32_t *)value = info->flags;
                break;
                
            case kMediaBufferProperty_IsTemporary:
                *(bool *)value = (info->flags & kMediaBufferFlag_Temporary) != 0;
                break;
                
            case kMediaBufferProperty_IsInUse:
                *(bool *)value = (info->flags & kMediaBufferFlag_InUse) != 0;
                break;
            
            case kMediaBufferProperty_ReferenceCount:
                *(uint32_t *)value = info->refCount;
                break;
                
            case kMediaBufferProperty_Timeout:
                if (!(info->flags & kMediaBufferFlag_Temporary))
                {
                    // Set the result for permanent buffers to -1.
                    *(uint32_t *)value = (uint32_t)-1;
                }
                else if (info->flags & kMediaBufferFlag_InUse)
                {
                    // Temp buffers that are currently in use don't have a timeout, yet.
                    *(uint32_t *)value = 0;
                }
                else
                {
                    *(uint32_t *)value = info->timeout;
                }
                break;
                
            default:
                result = ERROR_DDI_MEDIABUFMGR_INVALID_PROPERTY;
        }
    }
    
    // Put the mutex and return the status code.
    tx_mutex_put(&g_mediaBufferManagerContext.mutex);
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

//! @}




