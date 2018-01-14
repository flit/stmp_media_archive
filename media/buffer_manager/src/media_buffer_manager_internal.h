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
//! \file buffer_manager_internal.h
//! \brief Internal definitions for the buffer manager.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "os/threadx/tx_api.h"
#include "components/telemetry/tss_logtext.h"
#include "os/dmi/os_dmi_api.h"
#include "hw/core/vmemory.h"
#include "drivers/media/sectordef.h"
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

// Set the default value of this macro.
#if !defined(RECORD_BUFFER_STATS)
    #define RECORD_BUFFER_STATS 0
#endif

//! \brief Maximum number of buffers that can be tracked at once.
#define MAX_BUFFER_COUNT (10)

//! \brief Used to indicate that there is no temporary buffer waiting to time out.
#define NO_NEXT_TIMEOUT (-1)

//! \brief Timeout in milliseconds for temporary buffers.
#define TEMPORARY_BUFFER_TIMEOUT_MS (200)

//! \brief Number of milliseconds to delay before attempting to post a DPC again.
#define TIMER_RETRY_DELAY_MS (50)

//! \brief Internal flags applied to buffers.
//!
//! These internal flags are flags that the buffer manager may apply
//! to a buffer to keep track of state, but that clients will never pass
//! in when requesting a buffer. All internal flags are within the bit
//! range of 16 to 31.
enum _media_buffer_internal_flags
{
    kMediaBufferFlag_InUse = (1 << 16),   //!< The buffer is in use.
    kMediaBufferFlag_Temporary = (1 << 17),     //!< The buffer should be disposed of when released.
    kMediaBufferFlag_Realigned = (1 << 18),     //!< The buffer has been realigned.
    
    kMediaBufferManager_InternalFlagsMask = (0xffff << 16)   //!< Mask for all internal flags.
};

//! \brief Buffer information.
//!
//! This structure holds information about each buffer being controlled by the
//! media buffer manager. The global context has an array of these structures,
//! where the first #MediaBufferManagerContext_t::slotCount elements are potentially
//! valid. For any given instance of one of these structs, it is valid if and
//! only if the #MediaBufferInfo_t::data field is non-zero.
//!
//! Temporary buffers, those that are dynamically allocated at runtime, are
//! retained for a certain length of time after they are released back to the
//! buffer manager. The #MediaBufferInfo_t::timeout field here will be set to the
//! system clock time in ticks when the temporary buffer should finally be freed.
//! Until that time, the buffer is available to match incoming requests, and if
//! it is used the timeout is reset.
//!
//! The #MediaBufferInfo_t::bufferType field is an optional field only present
//! when buffer statistics are enabled. Normally the #MediaBufferInfo_t::length
//! field is sufficient, but to track statistics by buffer type we need to know
//! the original type used when a buffer was created.
typedef struct _buffer_info {
    size_t length;          //!< Size of this buffer in bytes.
    SECTOR_BUFFER * data;   //!< Pointer to the buffer itself. This is always non-zero for valid buffer entries.
    uint32_t flags;         //!< Flags pertaining to this buffer.
    uint32_t refCount;      //!< Number of references to this buffer.
    uint32_t timeout;       //!< Absolute time in ticks when this buffer expires. Only applies to temporary buffers.
    SECTOR_BUFFER * originalBuffer; //!< If the buffer has been realigned, then this field points to the original
                                    //! result of the allocation; this is the pointer that should be passed to free().

#if RECORD_BUFFER_STATS
    //! \name Statistics
    //@{
    MediaBufferType_t bufferType;   //!< The type of this buffer.
    unsigned acquiredCount;         //!< Number of times the buffer has been acquired.
    uint64_t createdTimestamp;      //!< Microsecond timestamp when the buffer was created (added).
    uint64_t acquiredTimestamp;     //!< Microsecond timestamp when the buffer was last acquired.
    uint64_t releasedTimestamp;     //!< Microsecond timestamp for when the buffer was last released.
    uint64_t averageUsageAccumulator;   //!< Accumulator for computing the averageUsageTimespan.
    uint64_t averageUsageTimespan;  //!< Average number of microseconds the buffer is being held.
    //@}
#endif // RECORD_BUFFER_STATS
} MediaBufferInfo_t;

#if RECORD_BUFFER_STATS
//! \brief Statistics information about buffer usage.
typedef struct _media_buffer_statistics {
    unsigned totalAllocs;           //!< Total number of allocations.
    unsigned concurrentAllocs;      //!< Current number of buffers in use.
    unsigned maxConcurrentAllocs;   //!< Highest number of buffers in use at the same time.
    unsigned newAllocs;             //!< For temporary buffer, this is the number of new buffers that were allocated. Unused by permanent buffers.
    unsigned realignedAllocs;       //!< Number of buffer allocations that had to be realigned.
} MediaBufferStatistics_t;
#endif // RECORD_BUFFER_STATS

//! \brief Contains all global information for the buffer manager.
//!
//! The array of buffer structures, #MediaBufferManagerContext_t::buffers, holds
//! information about all of the buffers under the control of the media buffer
//! manager. The first #MediaBufferManagerContext_t::slotCount elements in the array
//! are the only ones that may be valid, but not all of them are required to be so.
//! If #MediaBufferManagerContext_t::bufferCount is less than the used slot count,
//! then invalid (unused) buffer array elements are present and must be skipped.
//! The requirements for a valid buffer structure are described in the documentation for
//! #MediaBufferInfo_t.
typedef struct _media_buffer_manager_context {
    //! \name General
    //@{
    bool isInited;      //!< Whether the buffer manager has been initialised.
    TX_MUTEX mutex;     //!< Mutex used to protect this context structure.
    //@}
    
    //! \name Buffer array
    //!
    //! Only bufferCount buffers out of the first slotCount elements of the buffer
    //! array are valid. And out of those, only freeCount buffers are available for
    //! use by clients of the buffer manager.
    //@{
    MediaBufferInfo_t buffers[MAX_BUFFER_COUNT]; //!< Array of buffers.
    unsigned slotCount;     //!< The number of buffer array entries that must be searched.
    unsigned bufferCount;   //!< The number of buffers of all types in the buffers array.
    unsigned freeCount;     //!< The number of unused buffers of all types in the buffers array.
    //@}
    
    //! \name Temporary timeouts
    //!
    //! These fields are used to time out temporary buffers after they have been returned
    //! to the buffer manager.
    //@{
    TX_TIMER timeoutTimer;  //!< ThreadX timer used to time out temporary buffers.
    int nextTimeout;        //!< Index of the temporary buffer that will next time out. #NO_NEXT_TIMEOUT if there is none.
    int bufferToDispose;    //!< Index of temporary buffer that has timed out and should be permanently disposed of. Will be set to #NO_NEXT_TIMEOUT if there is no buffer to dispose.
    //@}

#if RECORD_BUFFER_STATS
    //! \name Statistics
    //!
    //! In debug builds, these fields of the context are used to keep useful
    //! statistics about allocations. This data can be used to tune the number
    //! of buffers in an application to get the best match between memory
    //! and performance.
    //@{
    MediaBufferStatistics_t permStats;  //!< Statistics for permanent buffers.
    MediaBufferStatistics_t tempStats;   //!< Statistics for temporary buffers.
    MediaBufferStatistics_t permTypeStats[kMediaBufferType_Count]; //!< Statistics for permanent buffers by type.
    MediaBufferStatistics_t tempTypeStats[kMediaBufferType_Count]; //!< Statistics for temporary buffers by type.
    unsigned mismatchedSizeAllocs;  //!< Number of allocations where a buffer was selected that wasn't a perfect match in size.
    //@}
#endif // RECORD_BUFFER_STATS
} MediaBufferManagerContext_t;

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

extern MediaBufferManagerContext_t g_mediaBufferManagerContext;
extern const size_t kMediaBufferTypeSizes[];

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

void media_buffer_timeout(ULONG unused);

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

//! @}


