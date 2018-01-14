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
//! \file access_record.cpp
//! \ingroup media_cache_internal
//! \brief Contains the implementation of the cache access record.
///////////////////////////////////////////////////////////////////////////////

#include "cacheutil.h"
#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#if CACHE_ACCESS_RECORD

//! \addtogroup media_cache_internal
//@{

//! Sector access records will not be created unless this global is true.
bool g_cacheRecordAccessInfo = false;

//! Access history records are only inserted when this global is true.
bool g_cacheRecordHistory = false;

//! Whether to merge sequential read or write operations into a single history record.
bool g_cacheCoalesceSequentialOperations = true;

//@}

#endif // CACHE_ACCESS_RECORD

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if CACHE_ACCESS_RECORD

static MediaCacheAccessInfo * cache_FindAccessInfo(const MediaCacheEntry * cache)
{
    assert(cache->drive < MAX_LOGICAL_DRIVES);
    
    MediaCacheAccessInfo * record = g_mediaCacheContext.accessRecordList[cache->drive];
    
    while (record)
    {
        if (record->sector == cache->sector)
        {
            return record;
        }
        
        record = record->next;
    }
    
    // No match, so create a new record.
    record = new MediaCacheAccessInfo(g_mediaCacheContext.accessRecordList[cache->drive], cache->sector);
    if (record)
    {
        g_mediaCacheContext.accessRecordList[cache->drive] = record;
    }
    
    return record;
}
        
void cache_RecordAccess(const MediaCacheEntry * cache, bool isWrite, bool didHit, bool didFlush, uint16_t chainIndex)
{
    if (g_cacheRecordAccessInfo)
    {
        // Update access info.
        MediaCacheAccessInfo * record = cache_FindAccessInfo(cache);
        if (record)
        {
            if (isWrite)
            {
                record->recordWrite();
            }
            else
            {
                record->recordRead();
            }
        }
    }

    if (g_cacheRecordHistory)
    {
        // Update history.
        MediaCacheOperationInfo * history = g_mediaCacheContext.operationHistory.tail;
        if (history && history->drive == cache->drive && ((history->op == MediaCacheOperationInfo::kWrite && isWrite) || (history->op == MediaCacheOperationInfo::kRead && !isWrite)))
        {
            if (g_cacheCoalesceSequentialOperations && history->endSector == (cache->sector - 1) && history->count == 1)
            {
                (*history)++;
                return;
            }
            else if (history->sector == cache->sector && history->endSector == cache->sector)
            {
                history->count++;
                return;
            }
        }

        unsigned entryIndex = cache->getArrayIndex(g_mediaCacheContext.entries);
        history = new MediaCacheOperationInfo(cache->drive, cache->sector, isWrite, didHit, didFlush, chainIndex, entryIndex);
        if (history)
        {
            g_mediaCacheContext.operationHistory.insert(history);
        }
    }
}

void cache_RecordFlush(const MediaCacheEntry * cache)
{
    if (g_cacheRecordHistory)
    {
        // Update history.
        MediaCacheOperationInfo * history;
        unsigned entryIndex = cache->getArrayIndex(g_mediaCacheContext.entries);
        history = new MediaCacheOperationInfo(cache->drive, cache->sector, MediaCacheOperationInfo::kFlush);
        if (history)
        {
            history->didFlush = true;
            history->entryIndex = entryIndex;
            g_mediaCacheContext.operationHistory.insert(history);
        }
    }
}

void cache_RecordEvict(const MediaCacheEntry * cache, bool didFlush)
{
    if (g_cacheRecordHistory)
    {
        // Update history.
        MediaCacheOperationInfo * history;
        history = new MediaCacheOperationInfo(cache->drive, cache->sector, MediaCacheOperationInfo::kEvict);
        if (history)
        {
            history->didFlush = didFlush;
            history->entryIndex = cache->getArrayIndex(g_mediaCacheContext.entries);
            g_mediaCacheContext.operationHistory.insert(history);
        }
    }
}

void MediaCacheOperationHistory::insert(MediaCacheOperationInfo * op)
{
    // Remove the head if we're at the max.
    if (count >= CACHE_MAX_HISTORY_COUNT)
    {
        MediaCacheOperationInfo * temp = head;
        head = head->next;
        delete temp;
        count--;
    }
    
    op->next = NULL;
    if (tail)
    {
        tail->next = op;
    }
    tail = op;
    if (!head)
    {
        head = op;
    }
    
    count++;
}

#endif // CACHE_ACCESS_RECORD

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

