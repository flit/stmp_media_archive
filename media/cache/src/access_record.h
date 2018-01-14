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
//! \file access_record.h
//! \ingroup media_cache_internal
//! \brief Contains internal declarations of the cache access record types.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__access_record_h__)
#define __access_record_h__

extern "C" {
#include "types.h"
#include "hw/profile/hw_profile.h"
}

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

//! \addtogroup media_cache_internal
//@{

//! \def CACHE_ACCESS_RECORD
//!
//! Set this macro to 1 to enable media cache access recording. You still must
//! set the trigger globals at runtime to actual record data, though.
#if !defined(CACHE_ACCESS_RECORD)
    #define CACHE_ACCESS_RECORD 0
#endif

//! Maximum number of history records.
#define CACHE_MAX_HISTORY_COUNT (2000)

//@}

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

// Forward declaration of this structure.
struct MediaCacheEntry;

#if CACHE_ACCESS_RECORD

/*!
 * \brief
 * \ingroup media_cache_internal
 */
struct MediaCacheAccessInfo
{
    uint32_t sector;
    uint32_t readCount;
    uint32_t writeCount;
    uint64_t lastReadTimestamp;
    uint64_t lastWriteTimestamp;
    MediaCacheAccessInfo * next;
    
    MediaCacheAccessInfo(MediaCacheAccessInfo * theNext, uint32_t theSector)
    :   sector(theSector), readCount(0), writeCount(0), lastReadTimestamp(0), lastWriteTimestamp(0), next(theNext)
    {
    }
    
    inline void recordRead()
    {
        readCount++;
        lastReadTimestamp = hw_profile_GetMicroseconds();
    }
    
    inline void recordWrite()
    {
        writeCount++;
        lastWriteTimestamp = hw_profile_GetMicroseconds();
    }
};

/*!
 * \brief
 * \ingroup media_cache_internal
 */
struct MediaCacheOperationInfo
{
    typedef enum { kRead, kWrite, kFlush, kEvict, kInvalidate } OpType_t;
    
    uint8_t drive;
    uint32_t sector;
    uint32_t endSector;
    unsigned subsector;
    unsigned count;
    OpType_t op;
    bool didHit;
    bool didFlush;
    uint64_t timestamp;
    uint16_t chainIndex;
    uint16_t entryIndex;
    MediaCacheOperationInfo * next;
    
    MediaCacheOperationInfo(unsigned theDrive, uint32_t theSector, OpType_t theOp)
    :   drive(theDrive),
        sector(theSector),
        endSector(theSector),
        subsector(0),
        count(1),
        op(theOp),
        didHit(false),
        didFlush(false),
        timestamp(hw_profile_GetMicroseconds()),
        chainIndex(0),
        entryIndex(0)
    {
    }
    
    MediaCacheOperationInfo(unsigned theDrive, uint32_t theSector, bool isWrite, bool theDidHit, bool theDidFlush, unsigned theChainIndex, unsigned theEntryIndex)
    :   drive(theDrive),
        sector(theSector),
        endSector(theSector),
        subsector(0),
        count(1),
        op(isWrite ? kWrite : kRead),
        didHit(theDidHit),
        didFlush(theDidFlush),
        timestamp(hw_profile_GetMicroseconds()),
        chainIndex(theChainIndex),
        entryIndex(theEntryIndex)
    {
    }
    
    //! \brief Postfix increment operator.
    MediaCacheOperationInfo & operator ++ (int)
    {
        endSector++;
        timestamp = hw_profile_GetMicroseconds();
        return *this;
    }
};

/*!
 * \brief
 * \ingroup media_cache_internal
 */
struct MediaCacheOperationHistory
{
    MediaCacheOperationInfo * head;
    MediaCacheOperationInfo * tail;
    unsigned count;
    
    //! \brief Adds an operation to the history.
    void insert(MediaCacheOperationInfo * op);
};

#endif // CACHE_ACCESS_RECORD

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

//! \addtogroup media_cache_internal
//@{

//! \name Access record
//@{

#if CACHE_ACCESS_RECORD
    void cache_RecordAccess(const MediaCacheEntry * cache, bool isWrite, bool didHit, bool didFlush, uint16_t chainIndex);
    void cache_RecordFlush(const MediaCacheEntry * cache);
    void cache_RecordEvict(const MediaCacheEntry * cache, bool didFlush);
#else
    inline void cache_RecordAccess(const MediaCacheEntry * cache, bool isWrite, bool didHit, bool didFlush, uint16_t chainIndex) {}
    inline void cache_RecordFlush(const MediaCacheEntry * cache) {}
    inline void cache_RecordEvict(const MediaCacheEntry * cache, bool didFlush) {}
#endif // CACHE_ACCESS_RECORD

//@}

//@}

#endif // __access_record_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

