////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2002-2006
//
//! \file FatUtils.c
//! \brief Utilities to work on the FAT.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "platform.h"
#include "chkdsk.h"
#include "fatutils.h"
#include "os/filesystem.h"
#include "string.h"
#include "fat_internal.h"

///////////////////////////////////////////////////////////////////////////////
//! \brief Loads NUM_CACHED_SECTORS sectors of FAT into the FAT buffer specified by
//!		stFat structure.
//!
//! \param[in] Sect Starting sector.
//!
//! \retval TRUE Operation successful.
//! \retval FALSE Impossible to read the sector.
///////////////////////////////////////////////////////////////////////////////
bool LoadFatSector(uint32_t Sect)
{
	uint32_t * pwTempDest;
	int i;
    int32_t * readBuffer;
    uint32_t cacheToken;

    // ensures that the sector is confined within the Primary FAT
    if ( Sect < g_checkdisk_context->stPartitionBootSector.wStartSectPrimaryFat || Sect > (g_checkdisk_context->stPartitionBootSector.wStartSectSecondaryFat - 1) )
    {
        return false;
    }

    // Needs to check if already loaded
    if ( Sect == g_checkdisk_context->stFat.FatSectorCached )
    {
        return true;
    }

    // Needs to check if current group dirty and save fat if so
    if ( g_checkdisk_context->stFat.Control != CLEAN )
    {
        if ( WriteFatSector(g_checkdisk_context->stFat.FatSectorCached) != true )
        {
            return false;
        }
    }

    // Read the sectors
	EnterNonReentrantSection();
	for (i = 0; i < NUM_CACHED_SECTORS; ++i)
	{
        if ((readBuffer = FSReadSector(g_checkdisk_context->stFat.Device, Sect + i, 0, &cacheToken)) == NULL)
		{
	        LeaveNonReentrantSection();
			return false;
		}

		pwTempDest = g_checkdisk_context->stFat.pwBuffer + g_checkdisk_context->cachedSectorSizeInWords * i;
        memcpy(pwTempDest, readBuffer, g_checkdisk_context->cachedSectorSize);
        
        FSReleaseSector(cacheToken);
	}
	LeaveNonReentrantSection();

    g_checkdisk_context->stFat.FatSectorCached = Sect;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Writes NUM_CACHED_SECTORS sectors of FAT. Data to write is specified by stFat structure
//!
//! \param[in] Sect Starting Sector.
//!
//! \retval TRUE Operation successful.
//! \retval FALSE Impossible to write the sector.
///////////////////////////////////////////////////////////////////////////////
bool WriteFatSector(uint32_t Sect)
{
    uint32_t * pwTempSrc;
	int i;
	
	for (i = 0; i < NUM_CACHED_SECTORS; ++i)
	{
		pwTempSrc  = g_checkdisk_context->stFat.pwBuffer + g_checkdisk_context->cachedSectorSizeInWords * i;

		// Write sector in Primary FAT
		if ( (Sect + i) < g_checkdisk_context->stPartitionBootSector.wStartSectSecondaryFat )
		{
            if (FSWriteSector(g_checkdisk_context->stFat.Device, Sect + i, 0, (uint8_t *)pwTempSrc, 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
			{
				return false;
			}
		}
		else
		{
			// we've crossed over into the secondary FAT, just mark as clean and bail
			g_checkdisk_context->stFat.Control = CLEAN;
			return false;
		}
	}

    g_checkdisk_context->stFat.Control = CLEAN;
	g_checkdisk_context->stFat.FatSectorCached = Sect;
    return true;

}

///////////////////////////////////////////////////////////////////////////////
//! \brief TBD
//!
//! \param[in] wCurCx
//!
//! \return Cluster number.
//! \retval -1 An error occurred.
///////////////////////////////////////////////////////////////////////////////
//int32_t GetNextCxFat12(uint32_t wCurCx)
//{
//    uint32_t wStartSect;
//    uint32_t wOffCx;
//    
//    // Calculates start sector of group of NUM_CACHED_SECTORS buffer sectors for FAT
//    wStartSect = wCurCx / FAT12_ENTRIES_PER_SECT_GROUP;
//
//    // Calculates the cluster offset in the Fat buffer
//    wOffCx = wCurCx - (wStartSect * FAT12_ENTRIES_PER_SECT_GROUP);
//
//    // Loads the fat sector group into fat buffer if needed
//    // Each Fat buffer group is NUM_CACHED_SECTORS sectors
//    if ( ((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != g_checkdisk_context->stFat.FatSectorCached )
//    {
//        if ( LoadFatSector((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != true )
//        {
//            return(-1);
//        }
//    }
//
//    return(FetchCxFat12(wOffCx));
//}

///////////////////////////////////////////////////////////////////////////////
//! \brief TBD
//!
//! \param[in] wCurCx
//!
//! \return Cluster number.
//! \retval -1 An error occurred.
///////////////////////////////////////////////////////////////////////////////
int32_t GetNextCxFat16(uint32_t wCurCx)
{
    uint32_t wStartSect;
    uint32_t wOffCx;
    uint32_t entriesPerSectorGroup = (g_checkdisk_context->cachedSectorSize * NUM_CACHED_SECTORS ) / sizeof(uint16_t);
    
    // Calculates start sector of group of NUM_CACHED_SECTORS buffer sectors for FAT
    wStartSect = wCurCx / entriesPerSectorGroup;

    // Calculates the cluster offset in the Fat buffer
    wOffCx = wCurCx - (wStartSect * entriesPerSectorGroup);

    // Loads the fat sector group into fat buffer if needed
    // Each Fat buffer group is NUM_CACHED_SECTORS sectors
    if ( ((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != g_checkdisk_context->stFat.FatSectorCached )
    {
        if ( LoadFatSector((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != true )
        {
            return(-1);
        }
    }

    return(FSGetWord((uint8_t *)g_checkdisk_context->stFat.pwBuffer, wOffCx * sizeof(uint16_t)));

}

///////////////////////////////////////////////////////////////////////////////
//! \brief TBD
//!
//! \param[in] wCurCx
//!
//! \return Cluster number.
//! \retval -1 An error occurred.
///////////////////////////////////////////////////////////////////////////////
int32_t GetNextCxFat32(uint32_t wCurCx)
{
    uint32_t wOffCx;
    uint32_t wStartSect;
    uint32_t entriesPerSectorGroup = (g_checkdisk_context->cachedSectorSize * NUM_CACHED_SECTORS ) / sizeof(uint32_t);

    // Calculates start sector of group of NUM_CACHED_SECTORS buffer sectors for FAT
    wStartSect = wCurCx / entriesPerSectorGroup;
    
    // Calculates the cluster offset in the Fat buffer
    wOffCx = wCurCx - (wStartSect * entriesPerSectorGroup);

    // Loads the fat sector group into fat buffer if needed
    // Each Fat buffer group is NUM_CACHED_SECTORS sectors
    if ( ((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != g_checkdisk_context->stFat.FatSectorCached )
    {
        if ( LoadFatSector((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != true )
        {
            return(-1);
        }
    }

    return FSGetDWord((uint8_t *)g_checkdisk_context->stFat.pwBuffer, wOffCx * sizeof(uint32_t));
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Fetches a FAT entry from the current FAT buffer
//!
//! The FAT entry wCurCx is referenced to the FAT sector
//! buffered. Use GetNextCxFat12() if FAT entry is an
//! absolute number.
//!
//! \param[in] wCurCx Fat Entry to fetch.
//!
//! \return Contents of FAT entry.
///////////////////////////////////////////////////////////////////////////////
//int32_t FetchCxFat12(uint32_t wCurCx)
//{
//    uint32_t wTemp = wCurCx / 2;
//
//    if ( wTemp*2 != wCurCx )
//    {
//        // Fecth MS 12-bit word from entry
//        return((int32_t)(*(g_checkdisk_context->stFat.pwBuffer + wTemp) >> 12));
//    }
//    else
//    {
//        // Fecth LS 12-bit word from entry
//        return((int32_t)(*(g_checkdisk_context->stFat.pwBuffer +  wTemp) & 0x00000fff));
//    }
//}

///////////////////////////////////////////////////////////////////////////////
//! \brief Frees the specified FAT entry.
//!
//! Depending of the file system FAT, this function calls FreeCxFat12() or
//! FreeCxFat16().
//!
//! \param[in] wFatEntry Fat entry to free up.
//!
//! \retval TRUE Operation successful.
//! \retval FALSE File System FAT not supported or FAT outside of boundaries
//!		or impossible to read a sector.
///////////////////////////////////////////////////////////////////////////////
bool FreeCxFat(uint32_t wFatEntry)
{
    int32_t wFatEntryValue;

    // Read content of Fat Entry
    wFatEntryValue = g_checkdisk_context->GetNextCxFromFat(wFatEntry);

    // If -1, the FAT is not supported => catastrophic
    if ( wFatEntryValue == BAD_CLUSTER )
    {
        return false;
    }

    // If 0, the FAT entry is already free
    if ( wFatEntryValue == 0x0 )
    {
        return true;
    }

    switch (g_checkdisk_context->stPartitionBootSector.TypeFileSystem)
    {
//        case FS_FAT12:
//            return FreeCxFat12(wFatEntry);
            
        case FS_FAT16:
            return FreeCxFat16(wFatEntry);
            
        case FS_FAT32:
            return FreeCxFat32(wFatEntry);
        
        default:
            return false;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Frees the specified FAT12 entry.
//!
//! \param[in] wCluster Fat entry to free up.
//!
//! \retval TRUE Operation successful.
//! \retval FALSE FAT outside of boundaries or impossible to read a sector.
///////////////////////////////////////////////////////////////////////////////
//bool FreeCxFat12(uint32_t wCluster)
//{
//    uint32_t wStartSect;
//    uint32_t wOffCx;
//    uint32_t wTemp;
//
//    // Calculates start sector of group of NUM_CACHED_SECTORS buffer sectors for FAT
//    wStartSect = wCluster / FAT12_ENTRIES_PER_SECT_GROUP;
//
//    // Calculates the cluster offset in the Fat buffer
//    wOffCx = wCluster - (wStartSect * FAT12_ENTRIES_PER_SECT_GROUP);
//
//    // Loads the fat sector group into fat buffer if needed
//    // Each Fat buffer group is NUM_CACHED_SECTORS sectors
//    if ( ((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != g_checkdisk_context->stFat.FatSectorCached )
//    {
//        if ( LoadFatSector((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != true )
//        {
//            return false;
//        }
//    }
//
//    wTemp = wOffCx / 2;
//
//    if ( wTemp * 2 != wOffCx )
//    {
//        // Cluster to free is in MS 12-bit word from entry
//        *(g_checkdisk_context->stFat.pwBuffer + wTemp) = *(g_checkdisk_context->stFat.pwBuffer + wTemp) & 0x00000fff;
//    }
//    else
//    {
//        // Cluster to free is in LS 12-bit word from entry
//        *(g_checkdisk_context->stFat.pwBuffer + wTemp) = *(g_checkdisk_context->stFat.pwBuffer + wTemp) & 0x00fff000;
//    }
//
//    // Mark the FAT sector group dirty
//    g_checkdisk_context->stFat.Control = DIRTY;
//
//    return true;
//}

///////////////////////////////////////////////////////////////////////////////
//! \brief Frees the specified FAT16 entry.
//!
//! \param[in] wCluster Fat entry to free up.
//!
//! \retval TRUE Operation successful.
//! \retval FALSE FAT outside of boundaries or impossible to read a sector.
///////////////////////////////////////////////////////////////////////////////
bool FreeCxFat16(uint32_t wCluster)
{
    uint32_t wStartSect;
    uint32_t wOffCx;
    uint32_t entriesPerSectorGroup = (g_checkdisk_context->cachedSectorSize * NUM_CACHED_SECTORS ) / sizeof(uint16_t);

    // Calculates start sector of group of NUM_CACHED_SECTORS buffer sectors for FAT
    wStartSect = wCluster / entriesPerSectorGroup;

    // Calculates the cluster offset in the Fat buffer
    wOffCx = wCluster - (wStartSect * entriesPerSectorGroup);

    // Loads the fat sector group into fat buffer if needed
    // Each Fat buffer group is NUM_CACHED_SECTORS sectors
    if ( ((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != g_checkdisk_context->stFat.FatSectorCached )
    {
        if ( LoadFatSector((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != true )
        {
            return false;
        }
    }

    PutWord((uint8_t *)g_checkdisk_context->stFat.pwBuffer, 0x0000, wOffCx * sizeof(uint16_t));

    // Mark the FAT sector group dirty
    g_checkdisk_context->stFat.Control = DIRTY;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Frees the specified FAT32 entry.
//!
//! \param[in] wCluster Fat entry to free up.
//!
//! \retval TRUE Operation successful.
//! \retval FALSE FAT outside of boundaries or impossible to read a sector.
///////////////////////////////////////////////////////////////////////////////
bool FreeCxFat32(uint32_t wCluster)
{
    uint32_t wStartSect;
    int wOffCx;
    uint32_t entriesPerSectorGroup = (g_checkdisk_context->cachedSectorSize * NUM_CACHED_SECTORS ) / sizeof(uint32_t);
    
    // Calculates start sector of group of NUM_CACHED_SECTORS buffer sectors for FAT
    wStartSect = wCluster / entriesPerSectorGroup;

    // Calculates the cluster offset in the Fat buffer
    wOffCx = wCluster - (wStartSect * entriesPerSectorGroup);

    // Loads the fat sector group into fat buffer if needed
    // Each Fat buffer group is NUM_CACHED_SECTORS sectors
    if ( ((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != g_checkdisk_context->stFat.FatSectorCached )
    {
        if ( LoadFatSector((wStartSect * NUM_CACHED_SECTORS) + g_checkdisk_context->stFat.FirstPrimaryFatSect) != true )
        {
            return false;
        }
    }

    PutDword((uint8_t *)g_checkdisk_context->stFat.pwBuffer, 0x00000000, wOffCx * sizeof(uint32_t));
    
    // Mark the FAT sector group dirty
    g_checkdisk_context->stFat.Control = DIRTY;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Checks if cluster is last of the chain.
//!
//! The chain ends with a cluster value of 0xfff (FAT12), 0xffff (FAT16),
//! or 0x0fffffff (FAT32).
//!
//! \param[in] wCluster The cluster.
//!
//! \retval TRUE If last cluster.
//! \retval FALSE Otherwise.
///////////////////////////////////////////////////////////////////////////////
bool IsLastCx(uint32_t wCluster)
{
    switch (g_checkdisk_context->stPartitionBootSector.TypeFileSystem)
    {
        case FS_FAT12:
            return (wCluster == 0x00000fff);

        case FS_FAT16:
            return (wCluster == 0x0000ffff);

        case FS_FAT32:
            return (wCluster == 0x0fffffff);
        
        default:
            return true;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! \brief TBD
//!
//! \param[in] DeviceNum TBD.
//! \param[in] sectorNumber TBD.
//! \param[in] wOffsetToWord TBD.
//! \param[in] wOffsetBit TBD.
//! \param[in] Bittype TBD.
//!
//! \return TBD.
///////////////////////////////////////////////////////////////////////////////
int FAT32_UpdateBit(int DeviceNum, int32_t sectorNumber, uint32_t wOffsetToWord, uint32_t wOffsetBit, uint32_t Bittype)
{
    int i = 0;
	int selection;
	int Counter;
	int index;
    uint32_t temp;
	uint32_t offsetMask = 1 << wOffsetBit;
    int32_t * readBuffer;
    uint32_t cacheToken;
    
    // First Search if the buffer is available in the cache
    index = SearchmatchingSector(sectorNumber,MAX_CACHES, &g_checkdisk_context->CacheDesc_chkdsk[0]);
    if ( index >= 0 )
    {
        switch ( Bittype )
        {
            case GET_BIT:
                g_checkdisk_context->CacheDesc_chkdsk[index].CacheCounter = READCOUNTER;
                return(g_checkdisk_context->CacheMem_chkdsk[index][wOffsetToWord] & offsetMask);
                
            case SET_BIT:
                g_checkdisk_context->CacheDesc_chkdsk[index].CacheCounter = WRITECOUNTER;
                g_checkdisk_context->CacheDesc_chkdsk[index].WriteAttribute = 1;
                temp = (g_checkdisk_context->CacheMem_chkdsk[index][wOffsetToWord] & offsetMask);
                g_checkdisk_context->CacheMem_chkdsk[index][wOffsetToWord] = g_checkdisk_context->CacheMem_chkdsk[index][wOffsetToWord] | offsetMask;
                return temp;
                
            case FREE_BIT:
                g_checkdisk_context->CacheDesc_chkdsk[index].CacheCounter = WRITECOUNTER;
                g_checkdisk_context->CacheDesc_chkdsk[index].WriteAttribute = 1;
                temp = 0xffffffff ^ offsetMask;
                g_checkdisk_context->CacheMem_chkdsk[index][wOffsetToWord] = g_checkdisk_context->CacheMem_chkdsk[index][wOffsetToWord] & temp;
                return 0;
        }
    }

    // Cache Miss, so must read.
    // Now find the Least recently used Buffer
    selection = 0;
    Counter = 0;
    for ( i = 0; i < MAX_CACHES; i++ )
    {
        if ( g_checkdisk_context->CacheDesc_chkdsk[i].CacheValid )
        {
            if ( g_checkdisk_context->CacheDesc_chkdsk[i].CacheCounter > Counter )
            {
                selection = i;
                Counter = g_checkdisk_context->CacheDesc_chkdsk[i].CacheCounter;
            }
        }
        else
        {
            selection = i;
            g_checkdisk_context->CacheDesc_chkdsk[selection].CacheValid = 1;
            break;
        }
    }
    
    // Flush the sector to the disk, if write attribute was set.
    if ( g_checkdisk_context->CacheDesc_chkdsk[selection].WriteAttribute == 1 )
    {
        if (FSWriteSector(DeviceNum, g_checkdisk_context->CacheDesc_chkdsk[selection].SectorNumber, 0, (uint8_t *)g_checkdisk_context->CacheMem_chkdsk[selection], 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
        {
            return -1;
        }
    }
    
    EnterNonReentrantSection();
    if ((readBuffer = FSReadSector(DeviceNum, sectorNumber, 0, &cacheToken)) == NULL)
	{
        LeaveNonReentrantSection();
        return -1;
	}
    memcpy((uint8_t *)g_checkdisk_context->CacheMem_chkdsk[selection], readBuffer, g_checkdisk_context->cachedSectorSize);
    FSReleaseSector(cacheToken);
    LeaveNonReentrantSection();

    g_checkdisk_context->CacheDesc_chkdsk[selection].SectorNumber = sectorNumber;
    IncrementCacheCounters_chkdsk();
    switch ( Bittype )
    {
        case GET_BIT:
            g_checkdisk_context->CacheDesc_chkdsk[selection].CacheCounter = READCOUNTER;
            return(g_checkdisk_context->CacheMem_chkdsk[selection][wOffsetToWord] & offsetMask);
            
        case SET_BIT:
            g_checkdisk_context->CacheDesc_chkdsk[selection].CacheCounter = WRITECOUNTER;
            g_checkdisk_context->CacheDesc_chkdsk[selection].WriteAttribute = 1;
            temp = (g_checkdisk_context->CacheMem_chkdsk[selection][wOffsetToWord] & offsetMask);
            g_checkdisk_context->CacheMem_chkdsk[selection][wOffsetToWord] = g_checkdisk_context->CacheMem_chkdsk[selection][wOffsetToWord] | offsetMask;
            return temp;
            
        case FREE_BIT:
            g_checkdisk_context->CacheDesc_chkdsk[selection].CacheCounter = WRITECOUNTER;
            g_checkdisk_context->CacheDesc_chkdsk[selection].WriteAttribute = 1;
            temp = 0xffffffff ^ offsetMask;
            g_checkdisk_context->CacheMem_chkdsk[selection][wOffsetToWord] = g_checkdisk_context->CacheMem_chkdsk[selection][wOffsetToWord] & temp;
            return 0;
    }
    
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief TBD
///////////////////////////////////////////////////////////////////////////////
void IncrementCacheCounters_chkdsk(void)
{
    int i;
    
    for ( i = 0; i< MAX_CACHES;i++ )
    {
        (g_checkdisk_context->CacheDesc_chkdsk[i].CacheCounter)++;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Searches the cache for a given sector number.
//!
//! This function will look into the Cache if the sector
//! specified by SectorNumber is there or not. If it is there
//! it returns index where does it find among all cache
//! (MAX_CACHES) else returns -1 indicating sector is not in the cache.
//!
//! \param[in] SectorNumber Current sector number.
//! \param[in] MaxCaches Number of caches.
//! \param[in] pstCacheDesc_chkdsk Pointer to structure containing cache for CheckDisk.
//!
//! \return Index of the sector.
//! \retval -1 The sector was not in the cache.
///////////////////////////////////////////////////////////////////////////////
uint32_t SearchmatchingSector(int32_t SectorNumber, int MaxCaches, tCACHEDESCR_checkdisk * pstCacheDesc_chkdsk)
{
	int iRetCount = 0,iTempCount;
	int iRetValue = -1;

	for(iTempCount = 0; iTempCount < MaxCaches; iTempCount++)
	{
		if(SectorNumber == pstCacheDesc_chkdsk->SectorNumber)
		{
			if(pstCacheDesc_chkdsk->CacheValid == 0)
			{
				iRetCount++;
			}
			else
			{
				iRetValue = iRetCount;
				break;
			}
		}
		else
		{
			iRetCount++;
		}
		pstCacheDesc_chkdsk++;
	}
	return iRetValue;
}



