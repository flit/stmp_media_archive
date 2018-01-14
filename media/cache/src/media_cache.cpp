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
//! \file media_cache.cpp
//! \brief Implementation of API to manage cache of media sectors.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "drivers/media/cache/media_cache.h"
#include "cacheutil.h"
#include <stdlib.h>
#include "drivers/media/sectordef.h"
#include "os/dmi/os_dmi_api.h"

extern "C" {
#include "hw/profile/hw_profile.h"
}

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

#define MAX_NUM_EXTENDED_CACHE_BUFFERS  8

//! Global context information for the media cache.
MediaCacheContext g_mediaCacheContext;

static unsigned g_mediaCacheCnt_bak = 0;
static uint8_t *g_mediaCacheBuffer_org = NULL;
static SECTOR_BUFFER *g_mediaCacheBuffer_extend[MAX_NUM_EXTENDED_CACHE_BUFFERS];
static SECTOR_BUFFER *g_mediaCacheBuffer_org_extend[MAX_NUM_EXTENDED_CACHE_BUFFERS];

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

// See media_cache.h for the documentation of this function.
RtStatus_t media_cache_init(uint8_t * cacheBuffer, uint32_t cacheBufferLength)
{
    // We only want to initialize once.
    if (g_mediaCacheContext.isInited)
    {
        return SUCCESS;
    }

    // Create the media cache mutex.
    if (tx_mutex_create(&g_mediaCacheContext.mutex, "mc", TX_NO_INHERIT) != TX_SUCCESS)
    {
        return ERROR_OS_KERNEL_TX_MUTEX_ERROR;
    }
    
	// Figure out the maximum sector size we'll have to cache by asking the LDL.
	uint32_t cacheSectorSize = MediaGetMaximumSectorSize();
    
    // Save this value in the context.
    g_mediaCacheContext.entryBufferSize = cacheSectorSize;

    // Set the number of cache entries we have based on the actual maximum sector size.
    g_mediaCacheContext.entryCount = cacheBufferLength / cacheSectorSize;
    
    // Can we use the given buffer? If the buffer is too small to hold even one
    // sector, or if it is not data cache aligned, then return an error.
    if (g_mediaCacheContext.entryCount == 0 || ((uint32_t)cacheBuffer & (BUFFER_CACHE_LINE_MULTIPLE - 1)) != 0)
    {
        return ERROR_DDI_MEDIA_CACHE_INVALID_BUFFER;
    }
	
    // Dynamically allocate the cache entry descriptors.
    unsigned cacheDescriptorsSize = g_mediaCacheContext.entryCount * sizeof(MediaCacheEntry);
    g_mediaCacheContext.entries =  (MediaCacheEntry *)malloc(cacheDescriptorsSize);
    if (g_mediaCacheContext.entries == NULL)
    {
        return ERROR_GENERIC;   //! \todo Better error!
    }
    memset(g_mediaCacheContext.entries, 0, cacheDescriptorsSize);
    
    // Create the drive tag to cache index tree.
    g_mediaCacheContext.tree = new RedBlackTree;
    assert(g_mediaCacheContext.tree);
    
    // Create the LRU with a window size of 0 in order to disable weighting.
    unsigned windowSize = 0; //std::min<unsigned>(16, g_mediaCacheContext.entryCount / 2);
    g_mediaCacheContext.lru = new WeightedLRUList(kMediaCacheWeight_Low, kMediaCacheWeight_High, windowSize);
    assert(g_mediaCacheContext.lru);

    // Init cache entries.
    int i = 0;
    int j = 0;
    MediaCacheEntry * entry = &g_mediaCacheContext.entries[0];
    
    for (; i < g_mediaCacheContext.entryCount; i++, entry++, j += cacheSectorSize)
    {
        // Use new in place operator to construct the media cache entry.
        new(entry) MediaCacheEntry(&cacheBuffer[j]);
        
        // Insert the entry in the LRU.
        g_mediaCacheContext.lru->insert(entry);
	}

    // Set the max chained entries so that it is never greater than half
    // the total number of entries. If the chain can encompass the all entries,
    // then we can end up in a deadlock in the SCSI code that overlaps two reads
    // or two pinned writes in a single thread. We subtract 1 from the value because
    // the chain always includes the base entry but the max entries count does not.
    g_mediaCacheContext.maxChainedEntries = std::min<unsigned>(CACHE_MAX_CHAINED_ENTRIES, g_mediaCacheContext.entryCount / 2);
    if (g_mediaCacheContext.maxChainedEntries > 0)
    {
        g_mediaCacheContext.maxChainedEntries--;
    }
    
    for(i=0; i<MAX_NUM_EXTENDED_CACHE_BUFFERS; i++)
    {
        g_mediaCacheBuffer_org_extend[i] = NULL;
        g_mediaCacheBuffer_extend[i] = NULL;
    }

    // We're now finished initing.
    g_mediaCacheContext.isInited = true;
    g_mediaCacheBuffer_org = cacheBuffer;
    
    return SUCCESS;
}

// See media_cache.h for the documentation of this function.
RtStatus_t media_cache_shutdown(void)
{
    // We lock the mutex and never unlock it before it is disposed of.
    tx_mutex_get(&g_mediaCacheContext.mutex, TX_WAIT_FOREVER);
    
    // Flush and invalidate everything before shutting down.
    MediaCacheParamBlock_t pb = {0};
    pb.flags = kMediaCacheFlag_FlushAllDrives | kMediaCacheFlag_Invalidate;
    media_cache_flush(&pb);
    
    // Dispose of the cache entry descriptors.
    free(g_mediaCacheContext.entries);
    g_mediaCacheContext.entries = NULL;

    // Dispose of the sector index tree.
    delete g_mediaCacheContext.tree;
    g_mediaCacheContext.tree = NULL;
    
    // Dispose of the LRU list.
    delete g_mediaCacheContext.lru;
    g_mediaCacheContext.lru = NULL;
    
    // Kill the cache mutex.
    tx_mutex_delete(&g_mediaCacheContext.mutex);
    
    // Done.
    g_mediaCacheContext.isInited = false;
    
    return SUCCESS;
}


RtStatus_t media_cache_increase(int cacheNumIncreased)
{
    RtStatus_t status = SUCCESS;
    int i,j;
        
    assert(g_mediaCacheContext.isInited);
    
    // already increased
    if(g_mediaCacheBuffer_org_extend[0] != NULL)
        return SUCCESS;

    if(cacheNumIncreased > MAX_NUM_EXTENDED_CACHE_BUFFERS)
        cacheNumIncreased = MAX_NUM_EXTENDED_CACHE_BUFFERS;

    for(i=0; i<cacheNumIncreased; i++)
    {
        g_mediaCacheBuffer_org_extend[i] = (SECTOR_BUFFER *)os_dmi_malloc_phys_contiguous(g_mediaCacheContext.entryBufferSize);
        if(g_mediaCacheBuffer_org_extend[i] == NULL)
        {
no_memory:
            // Not enough memory, stop here
            if(i == 0)  
            {   // not even 1 entry can be allocated, return error
                status = ERROR_OS_MEMORY_MANAGER_NOMEMORY;
                return ERROR_GENERIC;
            }

            // Update cacheNumIncreased with the number of entries allocated
            cacheNumIncreased = i;
            break;
        }

        // ensure the media cache buffer is 32 bytes aligned
        if( (((uint32_t)g_mediaCacheBuffer_org_extend[i])&(BUFFER_CACHE_LINE_MULTIPLE-1)) == 0)
        {
            g_mediaCacheBuffer_extend[i] = g_mediaCacheBuffer_org_extend[i];
        }
        else
        {
            // Not 32 bytes aligned, need to align the buffer manually
            free(g_mediaCacheBuffer_org_extend[i]);
            g_mediaCacheBuffer_org_extend[i] = (SECTOR_BUFFER *)os_dmi_malloc_phys_contiguous(g_mediaCacheContext.entryBufferSize+BUFFER_CACHE_LINE_MULTIPLE);
            if(g_mediaCacheBuffer_org_extend[i] == NULL)
            {
                goto no_memory;
            }
            
            g_mediaCacheBuffer_extend[i] = (SECTOR_BUFFER *)( (((uint32_t)g_mediaCacheBuffer_org_extend[i])+(BUFFER_CACHE_LINE_MULTIPLE-1)) & (~(BUFFER_CACHE_LINE_MULTIPLE-1)) );
        }
    }

    // Lock the cache.
    MediaCacheLock lockCache;

    MediaCacheParamBlock_t pb = {0};
    pb.flags = kMediaCacheFlag_FlushAllDrives | kMediaCacheFlag_Invalidate | kMediaCacheFlag_RemoveEntry;
    //flush all the cache first
    status =  media_cache_flush(&pb);            
    if(status!=SUCCESS)
        goto done;

    g_mediaCacheCnt_bak = g_mediaCacheContext.entryCount;
    
    free(g_mediaCacheContext.entries);
    g_mediaCacheContext.entries = NULL;

    delete g_mediaCacheContext.tree;
    g_mediaCacheContext.tree = NULL;
    
    // Dispose of the LRU list.
    delete g_mediaCacheContext.lru;
    g_mediaCacheContext.lru = NULL;


    g_mediaCacheContext.entryCount += cacheNumIncreased;
    
    // Dynamically allocate the cache entry descriptors.
    unsigned cacheDescriptorsSize = g_mediaCacheContext.entryCount * sizeof(MediaCacheEntry);
    g_mediaCacheContext.entries =  (MediaCacheEntry *)malloc(cacheDescriptorsSize);
    if (g_mediaCacheContext.entries == NULL)
    {
        status = ERROR_OS_MEMORY_MANAGER_NOMEMORY;
        goto done;
    }
    memset(g_mediaCacheContext.entries, 0, cacheDescriptorsSize);
    
    // Create the drive tag to cache index tree.
    g_mediaCacheContext.tree = new RedBlackTree;
    assert(g_mediaCacheContext.tree);
    

    // Create the LRU with a window size of 0 in order to disable weighting.
    unsigned windowSize = g_mediaCacheContext.entryCount / 2;
    g_mediaCacheContext.lru = new WeightedLRUList(kMediaCacheWeight_Low, kMediaCacheWeight_High, windowSize);
    assert(g_mediaCacheContext.lru);

    // Init cache entries.
    i = 0;
    j = 0;
    MediaCacheEntry * entry = &g_mediaCacheContext.entries[0];
    
    for (; i < g_mediaCacheCnt_bak; i++, entry++, j += g_mediaCacheContext.entryBufferSize)
    {
        // Use new in place operator to construct the media cache entry.
        new(entry) MediaCacheEntry(&g_mediaCacheBuffer_org[j]);
        
        // Insert the entry in the LRU.
        g_mediaCacheContext.lru->insert(entry);
	}
    j = 0;
    for (; i < g_mediaCacheContext.entryCount; i++, entry++, j++)
    {
        // Use new in place operator to construct the media cache entry.
        new(entry) MediaCacheEntry((uint8_t *)(g_mediaCacheBuffer_extend[j]));
        
        // Insert the entry in the LRU.
        g_mediaCacheContext.lru->insert(entry);
	}

    
    g_mediaCacheContext.maxChainedEntries = std::min<unsigned>(CACHE_MAX_CHAINED_ENTRIES, g_mediaCacheContext.entryCount / 2);
    if (g_mediaCacheContext.maxChainedEntries > 0)
    {
        g_mediaCacheContext.maxChainedEntries--;
    }

done:
    if(status!=SUCCESS)
    {
        for(i=0; i<MAX_NUM_EXTENDED_CACHE_BUFFERS; i++)
        {
            if(g_mediaCacheBuffer_org_extend[i] != NULL)
            {
                free(g_mediaCacheBuffer_org_extend[i]);
                g_mediaCacheBuffer_org_extend[i] = NULL;
                g_mediaCacheBuffer_extend[i] = NULL;
            }
        }
    }
         
    return status;
    
}

RtStatus_t media_cache_resume(void)
{
    RtStatus_t status = SUCCESS;
    int i,j;
        
    assert(g_mediaCacheContext.isInited);
    
    // not increase
    if(g_mediaCacheBuffer_org_extend[0] == NULL)
        return SUCCESS;

    // Lock the cache.
    MediaCacheLock lockCache;
    
    MediaCacheParamBlock_t pb = {0};
    pb.flags = kMediaCacheFlag_FlushAllDrives | kMediaCacheFlag_Invalidate | kMediaCacheFlag_RemoveEntry;
    //flush all the cache first
    status =  media_cache_flush(&pb);            
    if(status!=SUCCESS)
        return status;

    free(g_mediaCacheContext.entries);
    g_mediaCacheContext.entries = NULL;

    delete g_mediaCacheContext.tree;
    g_mediaCacheContext.tree = NULL;

    delete g_mediaCacheContext.lru;
    g_mediaCacheContext.lru = NULL;


    g_mediaCacheContext.entryCount = g_mediaCacheCnt_bak;
    
	printf("Media Cache Entry Count = %d\r\n",g_mediaCacheContext.entryCount);
	
    // Dynamically allocate the cache entry descriptors.
    unsigned cacheDescriptorsSize = g_mediaCacheContext.entryCount * sizeof(MediaCacheEntry);
    g_mediaCacheContext.entries =  (MediaCacheEntry *)malloc(cacheDescriptorsSize);
    if (g_mediaCacheContext.entries == NULL)
    {
        return ERROR_OS_MEMORY_MANAGER_NOMEMORY;
    }
    memset(g_mediaCacheContext.entries, 0, cacheDescriptorsSize);
    
    // Create the drive tag to cache index tree.
    g_mediaCacheContext.tree = new RedBlackTree;
    assert(g_mediaCacheContext.tree);

    // Create the LRU with a window size of 0 in order to disable weighting.
    unsigned windowSize = 0; //std::min<unsigned>(16, g_mediaCacheContext.entryCount / 2);
    g_mediaCacheContext.lru = new WeightedLRUList(kMediaCacheWeight_Low, kMediaCacheWeight_High, windowSize);
    assert(g_mediaCacheContext.lru);

    // Init cache entries.
    i = 0;
    j = 0;
    MediaCacheEntry * entry = &g_mediaCacheContext.entries[0];
    
    for (; i < g_mediaCacheCnt_bak; i++, entry++, j += g_mediaCacheContext.entryBufferSize)
    {
        // Use new in place operator to construct the media cache entry.
        new(entry) MediaCacheEntry(&g_mediaCacheBuffer_org[j]);
        
        // Insert the entry in the LRU.
        g_mediaCacheContext.lru->insert(entry);
	}

    
    g_mediaCacheContext.maxChainedEntries = std::min<unsigned>(CACHE_MAX_CHAINED_ENTRIES, g_mediaCacheContext.entryCount / 2);
    if (g_mediaCacheContext.maxChainedEntries > 0)
    {
        g_mediaCacheContext.maxChainedEntries--;
    }
    
    for(i=0; i<MAX_NUM_EXTENDED_CACHE_BUFFERS; i++)
    {
        if(g_mediaCacheBuffer_org_extend[i] != NULL)
        {
            free(g_mediaCacheBuffer_org_extend[i]);
            g_mediaCacheBuffer_org_extend[i] = NULL;
            g_mediaCacheBuffer_extend[i] = NULL;
        }
    }
    
    return status;

}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}


