////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2002-2006
//
//! \file chkdsk.c
//! \brief Check Disk utility main program.
//!
//! Limitations:
//!  - Watch for stack overflows because of the recursivity for CountSubDirs().
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <os\fsapi.h>
#include <os\os_dmi_api.h>
#include "types.h"
#include "platform.h"
#include "chkdsk.h"
#include "bitio.h"
#include "fatutils.h"
#include "os/filesystem.h"
#ifdef STMP_BUILD_MSC
    #include "misc.h"
#endif
#include "fstypes.h"
#include "fat_internal.h"
#include "error.h"
#include <string.h>

#define MAX_FAT_ENTRY_WORDS (2048)

#define MAX_NESTING_LEVEL (16)

// Each buffer is sized based on sector size (CACHEBUFSIZE is a define used
// by the filesystem annd it holds the size of a sector in bytes.)

#define MINIMUM_BPS 2112

#define XSCRATCHSPACE_SIZE(bps)   (((bps)<MINIMUM_BPS) ? (MAX_CACHES * MINIMUM_BPS)         : (MAX_CACHES * (bps)))
#define DIRRECORDBUFFER_SIZE(bps) (((bps)<MINIMUM_BPS) ? (MINIMUM_BPS)                      : (bps))
#define FATBUFFER_SIZE(bps)       (((bps)<MINIMUM_BPS) ? (NUM_CACHED_SECTORS * MINIMUM_BPS) : (NUM_CACHED_SECTORS * (bps)))

//! Source of buffers used by CheckDisk, in DMI terms.
#define CHECKDISK_BUFFER_SOURCE (DMI_MEM_SOURCE_DONTCARE)

//! \brief Holds fast file handles for files that CheckDisk() deletes.
uint64_t *g_CheckDiskFastFileHandles = 0;

//! \brief Holds number of fast file handles for files that CheckDisk() deletes.
uint8_t g_CheckDiskNumFastFileHandles;

//! \brief Set to 1 if CheckDisk() deleted any directories.
uint8_t g_CheckDiskAnyDirectoriesDeleted;

//! Global context for CheckDisk. Only allocated while CheckDisk is actually
//! running. At all other times this will be set to NULL.
checkdisk_context_t * g_checkdisk_context = NULL;

static uint32_t g_nestinglevel = 0;
///////////////////////////////////////////////////////////////////////////////
//! \brief Allocates memory for buffers used by CheckDisk().
//!
//! \fntype Non-reentrant function
//!
//! \retval SUCCESS The memory was allocated.
//! \retval ERROR_OS_MEMORY_MANAGER_PARAM    If the input parameters are wrong.
//! \retval ERROR_OS_MEMORY_MANAGER_SYSTEM   If a system error occurs.
//! \retval ERROR_OS_MEMORY_MANAGER_NOMEMORY If the memory manager is out of memory.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t AllocateCheckDiskMemory(uint8_t bDiskNum)
{
    RtStatus_t status;
    uint32_t bytesPerSector = MediaTable[bDiskNum].BytesPerSector;
    uint32_t u32ScratchSpaceSize;
    uint32_t u32DirRecordBufferSize;
    uint32_t u32FatBufferSize;
    
    u32ScratchSpaceSize    = XSCRATCHSPACE_SIZE(bytesPerSector);
    u32DirRecordBufferSize = DIRRECORDBUFFER_SIZE(bytesPerSector);
    u32FatBufferSize       = FATBUFFER_SIZE(bytesPerSector);


	// SDK-3640
	// Allocate FastFileHandles buffer from heap rather
	// than globally, if its not already allocated
	// SDK-3640
	//
	if(g_CheckDiskFastFileHandles== 0)
	{
		status = os_dmi_MemAlloc((void **)&g_CheckDiskFastFileHandles, 
							sizeof(uint64_t)*CHECKDISK_MAX_FAST_FILE_HANDLES, 
							true, CHECKDISK_BUFFER_SOURCE);
		
		if (status != SUCCESS)
		{
			return status;
		}
	}
    status = os_dmi_MemAlloc((void **)&g_checkdisk_context, sizeof(checkdisk_context_t), true, CHECKDISK_BUFFER_SOURCE);
    if (status != SUCCESS)
    {
        return status;
    }
    
    status = os_dmi_MemAlloc((void **)&g_checkdisk_context->XScratchSpace, u32ScratchSpaceSize, true, CHECKDISK_BUFFER_SOURCE);
    if (status != SUCCESS)
    {
        return status;
    }
    
    status = os_dmi_MemAlloc((void **)&g_checkdisk_context->DirRecordBuffer, u32DirRecordBufferSize, true, CHECKDISK_BUFFER_SOURCE);
    if (status != SUCCESS)
    {
        return status;
    }
    
    status = os_dmi_MemAlloc((void **)&g_checkdisk_context->FATBuffer, u32FatBufferSize, true, CHECKDISK_BUFFER_SOURCE);
    if (status != SUCCESS)
    {
        return status;
    }
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Frees memory previously allocated for CheckDisk() buffers.
//!
//! \fntype Non-reentrant function
///////////////////////////////////////////////////////////////////////////////
void DeallocateCheckDiskMemory(void)
{
    if (!g_checkdisk_context)
    {
        return;
    }
    
    if (g_checkdisk_context->XScratchSpace)
    {
        os_dmi_MemFree(g_checkdisk_context->XScratchSpace);
    }
    
    if (g_checkdisk_context->DirRecordBuffer)
    {
        os_dmi_MemFree(g_checkdisk_context->DirRecordBuffer);
    }
    
    if (g_checkdisk_context->FATBuffer)
    {
        os_dmi_MemFree(g_checkdisk_context->FATBuffer);
    }
    
    os_dmi_MemFree(g_checkdisk_context);
    g_checkdisk_context = NULL;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Checks a disk for errors and performs repairs.
//!
//! \fntype Non-reentrant function
//!
//! \param[in] bDiskNum Index of disk to check.
//! \retval SUCCESS The disk check proceeded without error.
//! \retval ERROR_OS_FILESYSTEM_GENERAL An error occurred while checking the
//!     disk. The disk is invalid and cannot (should not) be used.
//! \retval ERROR_OS_FILESYSTEM_UNSUPPORTED_FS_TYPE Returned for filesystems
//!     that are not supported, such as FAT12.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t CheckDisk(uint8_t bDiskNum)
{
    DIR_CTRL_BLK stDirCtrlBlk;
    uint32_t j;
    uint32_t sectorno;
    uint32_t i;
    uint32_t TotalFatsectors = 0;
    RtStatus_t status;
    
    // allocate memory used by checkdisk
    status = AllocateCheckDiskMemory(bDiskNum);
    if (status != SUCCESS)
    {
        return status;
    }
    
    g_CheckDiskNumFastFileHandles = 0;
    g_CheckDiskAnyDirectoriesDeleted = 0;
    
    g_checkdisk_context->glb_wFileCorrupted = 0;
    g_checkdisk_context->glb_bFailReadSect = false;
    g_checkdisk_context->FlagNeedReadSector = 0;
    
    //FATEntryStatus is used to save cluster usage status in FAT16
    g_checkdisk_context->FATEntryStatus = (uint32_t *)g_checkdisk_context->XScratchSpace;
    g_checkdisk_context->FATEntryStatusLength = XSCRATCHSPACE_SIZE(MediaTable[bDiskNum].BytesPerSector) / sizeof(uint32_t);

    // CacheMem_chkdsk is used to cache the status of cluster usage in FAT32. 1 bit for each cluster. 
    // If CacheMem_chkdsk is not big enough to hold all the status of cluster 
    // the secondary FAT is used to temporary save this information.
    // So CacheMem_chkdsk is defined as the number of Sectors.  Here it is MAX_CACHES sectors.
    // Each sector can hold the status of cluster BytesPerSector * 8;
    // For FAT16, this information is saved in buffer FATEntryStatus
    
    g_checkdisk_context->cachedSectorSize = MediaTable[bDiskNum].BytesPerSector;
    g_checkdisk_context->cachedSectorSizeInWords = g_checkdisk_context->cachedSectorSize / sizeof(uint32_t);
    g_checkdisk_context->cachedClusterEntryPerSectorShift = MediaTable[bDiskNum].SectorShift + BITS_SHIFT_FOR_UINT8;
    g_checkdisk_context->cachedClusterEntryPerSectorMask = (1<<g_checkdisk_context->cachedClusterEntryPerSectorShift) -1;
    g_checkdisk_context->cachedDirRecordsPerSector = g_checkdisk_context->cachedSectorSize / BYTES_PER_DIR_RECORD;

    for ( j=0, i=0; i < MAX_CACHES; j += g_checkdisk_context->cachedSectorSize, i++ )
    {
        g_checkdisk_context->CacheDesc_chkdsk[i].CacheValid = 0;
        g_checkdisk_context->CacheDesc_chkdsk[i].WriteAttribute = 0;
        g_checkdisk_context->CacheMem_chkdsk[i] = (uint32_t *)&g_checkdisk_context->XScratchSpace[j];
    }

    // Initialize the Partition Boot Sector for current Logical Device
    if ( InitPartitionBootSectorStruct(bDiskNum) != true )
    {
        DeallocateCheckDiskMemory();
        return ERROR_OS_FILESYSTEM_GENERAL;
    }
    
    // Quit if FAT is not supported. FAT12 is technically not supported anymore (by the SDK)
    // so we exit in that case too.
    if ( g_checkdisk_context->stPartitionBootSector.TypeFileSystem == FATUNSUPPORTED || g_checkdisk_context->stPartitionBootSector.TypeFileSystem == FS_FAT12 )
    {
        DeallocateCheckDiskMemory();
        return ERROR_OS_FILESYSTEM_UNSUPPORTED_FS_TYPE;
    }
    
    // Initialize the Root Directory Control Block Structure
    stDirCtrlBlk.StartSectCurDir = g_checkdisk_context->stPartitionBootSector.wStartSectRootDir;
    stDirCtrlBlk.wStartCxCurDir = g_checkdisk_context->stPartitionBootSector.Rootdirstartcx;
    stDirCtrlBlk.CurSect = 0;
    stDirCtrlBlk.NumberFiles = 0;
    stDirCtrlBlk.Device = bDiskNum;
    stDirCtrlBlk.pwBuffer = g_checkdisk_context->DirRecordBuffer;
    stDirCtrlBlk.Control = CLEAN;

    // Initializes part of the FAT structure
    g_checkdisk_context->stFat.FirstPrimaryFatSect = g_checkdisk_context->stPartitionBootSector.wStartSectPrimaryFat;
    g_checkdisk_context->stFat.FirstSecondaryFatSect = g_checkdisk_context->stPartitionBootSector.wStartSectSecondaryFat;
    g_checkdisk_context->stFat.Device = bDiskNum;
    g_checkdisk_context->stFat.Control = CLEAN;
    g_checkdisk_context->stFat.FatSectorCached = 0;
    g_checkdisk_context->stFat.pwBuffer = g_checkdisk_context->FATBuffer;

    if ( g_checkdisk_context->stPartitionBootSector.TypeFileSystem == FS_FAT32 )
    {
        // clear out the secondary FAT
        sectorno = g_checkdisk_context->stFat.FirstSecondaryFatSect;
        TotalFatsectors = (g_checkdisk_context->stPartitionBootSector.dwTotalclusters) >> (g_checkdisk_context->cachedClusterEntryPerSectorShift);
        TotalFatsectors++;
        g_checkdisk_context->CacheMem_chkdsk[0][0] = 0x03;             // 2 first clusters are always reserved

        for ( i = 1; i < g_checkdisk_context->cachedSectorSizeInWords; i++ )
        {
            g_checkdisk_context->CacheMem_chkdsk[0][i] = 0;
        }

        if (FSWriteSector(bDiskNum, sectorno, 0, (uint8_t *)&(g_checkdisk_context->CacheMem_chkdsk[0][0]), 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
        {
            DeallocateCheckDiskMemory();
            return ERROR_OS_FILESYSTEM_GENERAL;
        }
        sectorno++;

        // Cleared for erasing rest of the sectors.
        g_checkdisk_context->CacheMem_chkdsk[0][0] = 0;
        
        // first sector is already written
        for ( i=0; i < (TotalFatsectors - 1); i++ )
        {
            if (FSWriteSector(bDiskNum, sectorno, 0, (uint8_t *)&(g_checkdisk_context->CacheMem_chkdsk[0][0]), 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
            {
                DeallocateCheckDiskMemory();
                return ERROR_OS_FILESYSTEM_GENERAL;
            }
            sectorno++;
        }
    }
    else
    {
        // check that FATEntryStatus has enough bits to represent all of the clusters (not for FAT32)
        assert(g_checkdisk_context->FATEntryStatusLength * BITS_PER_WORD >= g_checkdisk_context->stPartitionBootSector.dwTotalclusters);

        for ( j=0 ; j < MAX_FAT_ENTRY_WORDS ; j++ )
        {
            g_checkdisk_context->FATEntryStatus[j] = 0x0;
        }
        
        // 2 first clusters are always reserved
        g_checkdisk_context->FATEntryStatus[0] = 0x03;
    }

    // select function to get next cluster based on the filesystem type
    switch (g_checkdisk_context->stPartitionBootSector.TypeFileSystem)
    {
//        case FS_FAT12:
//            g_checkdisk_context->GetNextCxFromFat = &GetNextCxFat12;
//            break;

        case FS_FAT16:
            g_checkdisk_context->GetNextCxFromFat = &GetNextCxFat16;
            break;

        case FS_FAT32:
            g_checkdisk_context->GetNextCxFromFat = &GetNextCxFat32;
            break;

        default:
            DeallocateCheckDiskMemory();
            return ERROR_OS_FILESYSTEM_GENERAL;
    }

    // Load the 1st 3 sectors of primary FAT
    LoadFatSector(g_checkdisk_context->stPartitionBootSector.wStartSectPrimaryFat);

    // Reserve the clusters occupied by Rootdirectory
    ReserveCluster(g_checkdisk_context->stPartitionBootSector.Rootdirstartcx, bDiskNum);

    g_nestinglevel = 0;
    // Scan Files and sub dirs in root directory
    if ( ScanFilesAndSubDirs(&stDirCtrlBlk) != true )
    {
        // Bad Root Directory ....
        HandleFailReadSector();
        DeallocateCheckDiskMemory();
        return ERROR_OS_FILESYSTEM_GENERAL;
    }
    
    // Checking disk done
    // If a corrupted file has been found, we need to do the second pass
    // on scandisk. This second pass will find all used clusters for all
    // good files and sub directories and then delete all FAT entries
    // not used by the good files and subdirectories.
    if ( (g_checkdisk_context->glb_wFileCorrupted != 0) && (g_checkdisk_context->glb_bFailReadSect == false) )
    {
        // Flush last sector if needed
        if ( stDirCtrlBlk.Control == DIRTY )
        {
            if (FSWriteSector(stDirCtrlBlk.Device, stDirCtrlBlk.CurSect, 0, (uint8_t *)stDirCtrlBlk.pwBuffer, 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
            {
                HandleFailReadSector();
                DeallocateCheckDiskMemory();
                return ERROR_OS_FILESYSTEM_GENERAL;
            }
        }

        ScanAndUpdateFat(bDiskNum, g_checkdisk_context->stPartitionBootSector.TypeFileSystem);
        if ( g_checkdisk_context->stFat.Control != CLEAN )
        {
            if ( WriteFatSector(g_checkdisk_context->stFat.FatSectorCached) != true )
            {
                HandleFailReadSector();
                DeallocateCheckDiskMemory();
                return ERROR_OS_FILESYSTEM_GENERAL;
            }
        }
    }

    // copy primary FAT table to secondary FAT Table
    if (!CopyPrimaryFatToSecondary(bDiskNum, TotalFatsectors))
    {
        DeallocateCheckDiskMemory();
        return ERROR_OS_FILESYSTEM_GENERAL;
    }
    
    // now flush the entire cache for this device
    if (FSFlushDriveCache(bDiskNum) != SUCCESS)
    {
        DeallocateCheckDiskMemory();
        return ERROR_OS_FILESYSTEM_GENERAL;
    }
    
    // deallocate buffers and return success
    DeallocateCheckDiskMemory();
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Copies the primary FAT onto the secondary one.
//!
//! \param[in] TotalFatsectors Number of sectors that were used for bit
//!     buffering.
//!
//! \retval true The copy was performed without error.
//! \retval false An error occurred reading or writing
//!     a sector of the FAT.
///////////////////////////////////////////////////////////////////////////////
bool CopyPrimaryFatToSecondary(uint8_t bDiskNum, uint32_t TotalFatsectors)
{
    uint32_t j;
    uint32_t sectorno;
    uint32_t sector_1st_FAT;
    uint32_t SectorsToWrite;
    uint32_t count;
    int32_t * readBuffer;

    sector_1st_FAT = g_checkdisk_context->stFat.FirstPrimaryFatSect;
    sectorno = g_checkdisk_context->stFat.FirstSecondaryFatSect;

    if ( (g_checkdisk_context->glb_wFileCorrupted != 0) && (g_checkdisk_context->glb_bFailReadSect == false) )
    {
        // When FAT is corrupted.
        count = g_checkdisk_context->stPartitionBootSector.wNumberFatSectors;
    }
    else
    {
        // When FAT is not corrupted, update only the sectors we used for bit buffering.
        count = TotalFatsectors;
    }

    while ( count > 0 )
    {
        if ( count > MAX_CACHES )
        {
            SectorsToWrite = MAX_CACHES;
        }
        else
        {
            SectorsToWrite = count;
        }
        count -= SectorsToWrite;
        
        // Read a group of sectors from Primary FAT
        EnterNonReentrantSection();
        for ( j = 0; j < SectorsToWrite; j++ )
        {
            uint32_t cacheToken;
            if ((readBuffer = FSReadSector(bDiskNum, sector_1st_FAT, 0, &cacheToken)) == NULL)
            {
                LeaveNonReentrantSection();
                return false;
            }
            
            memcpy((uint8_t *)&(g_checkdisk_context->CacheMem_chkdsk[j][0]), readBuffer, g_checkdisk_context->cachedSectorSize);
            
            sector_1st_FAT++;
            
            FSReleaseSector(cacheToken);
        }
        LeaveNonReentrantSection();

        // Write a group of sectors to Secondary FAT
        for ( j = 0; j < SectorsToWrite; j++ )
        {
            if (FSWriteSector(bDiskNum, sectorno, 0, (uint8_t *)&(g_checkdisk_context->CacheMem_chkdsk[j][0]), 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
            {
                return false;
            }
            sectorno++;
        }
    }
    
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Scans FATEntryStatus array and frees up all FAT entries not used.
//!
//! \retval true Successful.
//! \retval false Unsuccessful, at least a media sector could not be read/write.
//!     This is a major failure.
///////////////////////////////////////////////////////////////////////////////
bool ScanAndUpdateFat(uint8_t bDiskNum, FAT_TYPE TypeFileSystem)
{
    uint32_t wCurrentCluster;

    for ( wCurrentCluster = 2 ; wCurrentCluster < g_checkdisk_context->stPartitionBootSector.dwTotalclusters ; wCurrentCluster++ )
    {
        if ( UpdateBit(wCurrentCluster, g_checkdisk_context->FATEntryStatus, g_checkdisk_context->FATEntryStatusLength, bDiskNum, g_checkdisk_context->stPartitionBootSector.TypeFileSystem, GET_BIT) == 0 )
        {
            if ( FreeCxFat(wCurrentCluster) != true )
            {
                return false;
            }
        }
    }

    return true;

}

#if 0
///////////////////////////////////////////////////////////////////////////////
//! \brief Checks if a file is a partial file.
//!
//! A file fragment where a cluster chain is broken due to host power failure or device 
//! disconnect events i.e., the file is not completely transferred.
//!
//! \param[in] file_blk Pointer to a FILE_CTRL_BLK
//! \param[in] Device Device Id
//! \param[in] file_cluster_cnt Total number of used clusters for the file
//!
//! \return FALSE if EOF marker was found and if the file is not a partial file
//!           TRUE if the file is a partial file
///////////////////////////////////////////////////////////////////////////////
bool CheckPartialFile(uint8_t Device, FILE_CTRL_BLK *file_blk, uint32_t file_cluster_cnt)
{
    int32_t nextCluster, startCluster = file_blk->StartCluster;
    bool ret = TRUE;
    int32_t total_clusters = file_cluster_cnt;
    int32_t fsize = 0;

    while ( total_clusters >= 0 )
    {
        //This loop is to verify if there is an EOF marker
        nextCluster = g_checkdisk_context->GetNextCxFromFat(startCluster);

        if ( nextCluster == BAD_CLUSTER ) 
        {
            // Found EOF marker
            ret = FALSE;
            break;
        }
        startCluster = nextCluster;
        total_clusters--;
    }

    // Check if this is really a partial file, eventhough we may have found EOF marker
    if (file_blk)
    {
        fsize = file_cluster_cnt*MediaTable[ Device].SectorsPerCluster*MediaTable[ Device].BytesPerSector;
        if ( fsize < file_blk->Size )
        {
            // Looks like this is a partial file
            return TRUE;
        }
    }
    return ret;
}
#endif

///////////////////////////////////////////////////////////////////////////////
//! \brief Checks if a file is cross linked with another previously
//!     checked file.
//!
//! \pre When calling this function the caller should check if the file cluster
//!     chain terminates, otherwise we could be trapped in the loop.
//!
//! \param[in] wStartCluster Start Cluster for file to check.
//!
//! \return Cluster count. Any value greater than or equal to 0 is a valid
//!     cluster count and indicates that the file is not cross-linked.
//! \retval -1 An error occurred. This includes when the file is cross-linked.
///////////////////////////////////////////////////////////////////////////////
int CheckCrossLinkFile(uint32_t wStartCluster, uint8_t bLogDevNumber)
{
    uint32_t wCluster = wStartCluster;
    uint32_t wClusterCount = 1;
    uint32_t i;
    uint32_t * FATEntryStatus = g_checkdisk_context->FATEntryStatus;
    uint32_t FATEntryStatusLength = g_checkdisk_context->FATEntryStatusLength;
    FAT_TYPE TypeFileSystem = g_checkdisk_context->stPartitionBootSector.TypeFileSystem;

    // This handles case of a 0 byte file
    if ( wCluster == 0 )
    {
        return 0;
    }

    // make sure the start cluster is within the valid range
    if (wCluster > g_checkdisk_context->stPartitionBootSector.dwTotalclusters)
    {
        return -1;
    }

    // bail from the loop if the cluster count ever hits the total number of
    // available clusters
    do {
        if ( UpdateBit(wCluster, FATEntryStatus, FATEntryStatusLength, bLogDevNumber, TypeFileSystem, SET_BIT) )
        {
            // Cluster is occupied already, conflict happens. Clean up this cluster chain
            wCluster = wStartCluster;
            for ( i = 0; i < (wClusterCount - 1); i++ )
            {
                UpdateBit(wCluster, FATEntryStatus, FATEntryStatusLength, bLogDevNumber, TypeFileSystem, FREE_BIT);
                wCluster = g_checkdisk_context->GetNextCxFromFat(wCluster);
                if (wCluster == BAD_CLUSTER)
                {
                    break;
                }
            }
            return -1;
        }

        wCluster = g_checkdisk_context->GetNextCxFromFat(wCluster);
        if (wCluster == BAD_CLUSTER)
        {
            break;
        }

        if ( wCluster <= 1 )
        {
            // Encounter invalid cluster( the first 2 clusters are always reserved). Clean up this cluster chain
            wCluster = wStartCluster;
            for ( i = 0; i < wClusterCount ; i++ )
            {
                UpdateBit(wCluster, FATEntryStatus, FATEntryStatusLength, bLogDevNumber, TypeFileSystem, FREE_BIT);
                wCluster = g_checkdisk_context->GetNextCxFromFat(wCluster);
                if (wCluster == BAD_CLUSTER)
                {
                    break;
                }
            }
            return -1;
        }

        if ( IsLastCx(wCluster) )
        {
            return(wClusterCount);
        }

        wClusterCount++;
        
    } while ( wClusterCount < g_checkdisk_context->stPartitionBootSector.dwTotalclusters );

    // It is not correct to use file-size in above loop
    // because file-size may itself be corrupted.
    return(wClusterCount);
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Scans a directory.
//!
//! Check if sub directory is legitimate. A legitimate sub directory has:
//!  - First record = parent directory
//!  - Second record = this directory
//! Also performs a number of other checks and validations of the directory.
//!
//! \param[in] pstDirCtrlBlk Pointer to directory control block.
//!
//! \retval true Successful.
//! \retval false The directory is in default.
///////////////////////////////////////////////////////////////////////////////
bool ScanDirectory(DIR_CTRL_BLK * pstDirCtrlBlk)
{
    uint8_t * dirBuffer;

    // Check if sub directory legitime. A legitime sub dir has:
    //  1st record = parent directory
    //  2nd record = this directory

    // Read 1st sector for the sub directory
    ReadDirSector(pstDirCtrlBlk->StartSectCurDir, pstDirCtrlBlk);
    dirBuffer = (uint8_t *)pstDirCtrlBlk->pwBuffer;

    // Check if 1st record is this directory
    if ( FSGetByte(dirBuffer, 0) != DOT_CHAR_CODE )
    {
        return false;
    }

    // Check if this directory has directory signature
    if ( !(FSGetByte(dirBuffer, DIR_REC_ATT_POS) & ATTR_DIRECTORY) )
    {
        return false;
    }

    // Check if this directory size is 0
    if ( FSGetDWord(dirBuffer, DIR_REC_SIZE_POS) != 0x0 )
    {
        return false;
    }

    // Check to make sure that start cluster is non-zero
    if ( (FSGetDWord(dirBuffer, DIR_REC_FIRST_CX_HIGH_POS) == 0x0) && (FSGetDWord(dirBuffer, DIR_REC_FIRST_CX_POS) == 0x0) )
    {
        return false;
    }

    // Check if 2nd record is parent directory
    if ( FSGetWord(dirBuffer, 0 + BYTES_PER_DIR_RECORD) != PARENT_DIR_DOT_DOT )
    {
        return false;
    }

    // Check if parent directory has directory signature
    if ( !(FSGetByte(dirBuffer, DIR_REC_ATT_POS + BYTES_PER_DIR_RECORD) & ATTR_DIRECTORY) )
    {
        return false;
    }

    // Check if parent directory size is 0
    if ( FSGetDWord(dirBuffer, DIR_REC_SIZE_POS + BYTES_PER_DIR_RECORD) != 0x0 )
    {
        return false;
    }

    // Check for cross linked files
    if ( CheckCrossLinkFile(pstDirCtrlBlk->wStartCxCurDir, pstDirCtrlBlk->Device) < 0 )
    {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Mark clusters for the current file used in the FATEntryStatus array.
//!
//! When calling this function the caller should check
//! if the file cluster chain terminates, otherwise we could
//! be trapped in the loop
//!
//! \param[in] wStartCluster Start Cluster for file to check.
///////////////////////////////////////////////////////////////////////////////
void ReserveCluster(uint32_t wStartCluster, uint8_t bLogDevNumber)
{
    uint32_t wCluster = wStartCluster;

    // This handles case of a 0 byte file
    if ( wCluster == 0 )
    {
        return;
    }

    do {
        UpdateBit(wCluster, g_checkdisk_context->FATEntryStatus, g_checkdisk_context->FATEntryStatusLength, bLogDevNumber, g_checkdisk_context->stPartitionBootSector.TypeFileSystem, SET_BIT);
        wCluster = g_checkdisk_context->GetNextCxFromFat(wCluster);
    } while (wCluster != BAD_CLUSTER && !IsLastCx(wCluster));
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Initializes the global structure type PARTITION_BOOT_SECTOR
//!     for the device passed in.
//!
//! \param [in] bLogDevNumber The logical device number.
//!
//! \retval true Initialized successfully.
//! \retval false An error occurred during initialization.
///////////////////////////////////////////////////////////////////////////////
bool InitPartitionBootSectorStruct(uint8_t bLogDevNumber)
{
    uint32_t wReadWord;
    uint32_t TotalDatasectors;
    uint32_t Totalclusters;
    uint32_t i;
    uint32_t SecValue = 1;
    uint8_t * readBuffer;
    PARTITION_BOOT_SECTOR * bootSector = &g_checkdisk_context->stPartitionBootSector;
    uint32_t cacheToken;

    // Read the BootSector
    EnterNonReentrantSection();
    if ((readBuffer = (uint8_t *)FSReadSector(bLogDevNumber, 0, 0, &cacheToken)) == NULL)
    {
        LeaveNonReentrantSection();
        return false;
    }

    wReadWord =  FSGetWord(readBuffer, BYTE_POS_SIGNATURE);
    if ( wReadWord != SIGN_WORD_VALUE )
    {
        FSReleaseSector(cacheToken);
        LeaveNonReentrantSection();
        return false;
    }

    // Fill the rest of the PARTITION_BOOT_SECTOR structure
    wReadWord = FSGetByte(readBuffer, BYTE_POS_NUM_FAT);
    bootSector->bNumberFats = wReadWord;

    wReadWord = FSGetWord(readBuffer, BYTE_POS_NUM_ROOT_SECT);
    bootSector->wNumberRootDirEntries = wReadWord;

    wReadWord = FSGetWord(readBuffer, BYTE_POS_NUM_FAT_SECT);
    if ( wReadWord == 0 )
    {
        wReadWord = FSGetDWord(readBuffer, BYTE_POS_NUM_FAT_SECT_32);
    }
    bootSector->wNumberFatSectors = wReadWord;
    wReadWord = FSGetByte(readBuffer, BYTE_POS_NUM_SECT_PER_CX);

    // check whether the sectors per cluster are valid
    for ( i = 0; i < 8; i++ )
    {
        if ( wReadWord == SecValue )
        {
            break;
        }
        SecValue <<= 1;
    }

    if ( SecValue == 256 )
    {
        FSReleaseSector(cacheToken);
        LeaveNonReentrantSection();
        return false;
    }
    
    bootSector->bSectPerCx = wReadWord;

    wReadWord =  FSGetWord(readBuffer, BYTE_POS_TOTAL_SECTS);
    if ( wReadWord == 0 )
    {
        wReadWord =  FSGetDWord(readBuffer, BYTE_POS_TOTAL_SECTS_32);
    }
    bootSector->dwTotalsectors  =  wReadWord;

    bootSector->wStartSectPrimaryFat =  FSGetWord(readBuffer, BYTE_POS_NUM_RES_SECT);

    bootSector->wBytesPerSector =  FSGetByte(readBuffer, BYTE_POS_BYTES_PER_SECTOR);
    bootSector->wBytesPerSector |=  ((uint32_t)FSGetByte(readBuffer, BYTE_POS_BYTES_PER_SECTOR + 1)) << 8;

    bootSector->wStartSectSecondaryFat = bootSector->wStartSectPrimaryFat + bootSector->wNumberFatSectors;
    bootSector->wStartSectRootDir = bootSector->wStartSectSecondaryFat + bootSector->wNumberFatSectors;
    bootSector->wStartSectData = bootSector->wStartSectRootDir + (bootSector->wNumberRootDirEntries / (g_checkdisk_context->cachedDirRecordsPerSector));
    bootSector->wNumberRootDirSectors = (bootSector->wNumberRootDirEntries) / (g_checkdisk_context->cachedDirRecordsPerSector);

    bootSector->dwNumHiddenSectors =  FSGetByte(readBuffer, BYTE_POS_NUM_HIDDEN_SECT);
    bootSector->dwNumHiddenSectors |= ((uint32_t)FSGetByte(readBuffer, BYTE_POS_NUM_HIDDEN_SECT + 1)) << 8;
    bootSector->dwNumHiddenSectors |= ((uint32_t)FSGetByte(readBuffer, BYTE_POS_NUM_HIDDEN_SECT + 2)) << 16;
    bootSector->dwNumHiddenSectors |= ((uint32_t)FSGetByte(readBuffer, BYTE_POS_NUM_HIDDEN_SECT + 3)) << 24;

    TotalDatasectors = bootSector->dwTotalsectors - (bootSector->wStartSectPrimaryFat + (bootSector->bNumberFats * bootSector->wNumberFatSectors) + bootSector->wNumberRootDirSectors);

    Totalclusters   =  (TotalDatasectors / bootSector->bSectPerCx) + 1;
    bootSector->dwTotalclusters = Totalclusters;
    if ( Totalclusters < 4085 )
    {
        bootSector->TypeFileSystem = FS_FAT12;
    }
    else if ( Totalclusters < 65525 )
    {
        bootSector->TypeFileSystem = FS_FAT16;
    }
    else
    {
        bootSector->TypeFileSystem = FS_FAT32;
    }

    if ( bootSector->TypeFileSystem == FS_FAT32 )
    {
        if ( bootSector->bNumberFats < 2 )
        {
            FSReleaseSector(cacheToken);
            LeaveNonReentrantSection();
            return false;
        }
        wReadWord = FSGetDWord(readBuffer, BYTE_POS_ROOT_DIR_CX);
    }
    else
    {
        wReadWord = 0;
    }
    bootSector->Rootdirstartcx = wReadWord;

    FSReleaseSector(cacheToken);
    LeaveNonReentrantSection();
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Counts the number of files and sub directories
//!     for the directory specified in structure DIR_CTRL_BLK.
//!
//! \param[in] pstDirCtrlBlk Structure defining the directory to process.
//!
//! \retval true Successfull.
//! \retval false Unsuccessful.
//!
///////////////////////////////////////////////////////////////////////////////
bool ScanFilesAndSubDirs(DIR_CTRL_BLK * pstDirCtrlBlk)
{
    uint32_t SectCounter = 0;
    uint32_t RecordCounter;
    uint32_t n;
    uint32_t StartRecordByte;
    uint8_t  DIRNameFirstByte;
    uint8_t  bAttributeByte;
    uint32_t wFirstClusterLowWord;
    uint32_t wLoopCount;
    uint32_t Sect;
    uint32_t CurCx;
    FILE_CTRL_BLK stFileCtrlBlk;
    int32_t wClusterCount;
    int32_t i;
    uint32_t wCxSizeBytes;
    uint32_t FileDirSizeCx;
    uint32_t wCluster;
    bool bLastEntry = false;  

    // Cx size in Bytes
    wCxSizeBytes = g_checkdisk_context->stPartitionBootSector.bSectPerCx * g_checkdisk_context->cachedSectorSize;

    // Initialize the sector to read to the start sector current dir
    Sect = pstDirCtrlBlk->StartSectCurDir;

    // Initialize Current Cx to start cluster current dir
    CurCx = pstDirCtrlBlk->wStartCxCurDir;
    g_checkdisk_context->FlagNeedReadSector = 1;
    g_nestinglevel++;
    
    //	Rootdircluster = CurCx;
    if ( g_nestinglevel > MAX_NESTING_LEVEL )
    {
        g_nestinglevel--;

        return true;
    }
    
    // Scan all sectors allocated for this directory
    do {
        // If we're in the root directory then we just loop through a set number
        //  of sectors.  If not in the root then the directory may have multiple
        //  clusters and we process them one at a time.
        if ( CurCx == 0 )
        {
            wLoopCount = g_checkdisk_context->stPartitionBootSector.wNumberRootDirSectors;
        }
        else
        {
            wLoopCount = g_checkdisk_context->stPartitionBootSector.bSectPerCx;
        }

        for ( n = 0 ; n < wLoopCount ; Sect++, n++, SectCounter++ )
        {
            // Read new sector
            if ( ReadDirSector(Sect, pstDirCtrlBlk) != true )
            {
                g_nestinglevel--;
                return false;
            }

            // Scan all entries of sector loaded in Buffer
            for ( RecordCounter = 0; RecordCounter < g_checkdisk_context->cachedDirRecordsPerSector; RecordCounter++ )
            {
                if ( g_checkdisk_context->FlagNeedReadSector == 1 )
                {
                    if ( ReadDirSector(Sect, pstDirCtrlBlk) != true )
                    {
                        g_nestinglevel--;
                        return false;
                    }
                    g_checkdisk_context->FlagNeedReadSector = 0;
                }

                StartRecordByte = RecordCounter * BYTES_PER_DIR_RECORD;
                DIRNameFirstByte = (uint8_t)FSGetByte((uint8_t *)pstDirCtrlBlk->pwBuffer, StartRecordByte);

                // Check if last directory record
                if ( DIRNameFirstByte == 0x0 )
                {
                    //last directory record, there should be no allocated directory entries after this one 
                    //(all of the DIR_Name[0] bytes in all of the entries after this one are also set to 0). 
                    bLastEntry = true;
                    continue;
                }
                if(bLastEntry)
                {
                    //After last entry, mark DIR_Name[0] as 0
                    PutByte((uint8_t *)pstDirCtrlBlk->pwBuffer, FILE_FREEENTRY_CODE, StartRecordByte);
                    pstDirCtrlBlk->Control = DIRTY;
                    continue;
                }

                // Check if record deleted or parent or this directory
                if ( (DIRNameFirstByte == FILE_DELETED_CODE) || (DIRNameFirstByte == DOT_CHAR_CODE) )
                {
                    continue;
                }

                // Check if record is part of long file name
                bAttributeByte = (uint8_t)FSGetByte((uint8_t *)pstDirCtrlBlk->pwBuffer, StartRecordByte + DIR_REC_ATT_POS);

                wFirstClusterLowWord =  FSGetWord((uint8_t *)pstDirCtrlBlk->pwBuffer, StartRecordByte + DIR_REC_FIRST_CX_POS);

                // If it is long filename entry, do nothing
                if ( (bAttributeByte == ATTR_LONG_NAME) && (wFirstClusterLowWord == 0x00) )
                {
                    continue;
                }

                // It is a directory
                if ( bAttributeByte & ATTR_DIRECTORY )
                {
                    if ( GetFileCtrlBlk(RecordCounter, Sect, pstDirCtrlBlk, &stFileCtrlBlk) == false )
                    {
                        HandleFailReadSector();
                        break;
                    }

                    // Flush last sector if needed
                    if ( pstDirCtrlBlk->Control == DIRTY )
                    {
                        if (FSWriteSector(pstDirCtrlBlk->Device, pstDirCtrlBlk->CurSect, 0, (uint8_t *)pstDirCtrlBlk->pwBuffer, 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
                        {
                            HandleFailReadSector();
                            break;
                        }
                    }

                    // Initialize the Directory Control Block Structure
                    pstDirCtrlBlk->wStartCxCurDir = stFileCtrlBlk.StartCluster;
                    pstDirCtrlBlk->StartSectCurDir = CxToSect(stFileCtrlBlk.StartCluster);
                    pstDirCtrlBlk->NumberFiles = 0;
                    pstDirCtrlBlk->Control = CLEAN;

                    // Scan the directory and if bad cluster chain delete it from the directory
                    // record
                    if ( ScanDirectory(pstDirCtrlBlk) != true )
                    {
                        g_CheckDiskAnyDirectoriesDeleted = 1;
                        if ( DeleteFileRecord(RecordCounter, Sect, pstDirCtrlBlk) != true )
                        {
                            HandleFailReadSector();
                            break;
                        }
                        else
                        {
                            g_checkdisk_context->FlagNeedReadSector = 1;
                            continue;       // go process the next sub directory
                        }
                    }


                    // Count Files in current directory
                    if ( ScanFilesAndSubDirs(pstDirCtrlBlk) != true )
                    {
                        HandleFailReadSector();
                        break;
                    }

                }
                else if ( !(bAttributeByte & ATTR_VOLUME_ID) )
                {
                    // This is a short file entry
                    if ( GetFileCtrlBlk(RecordCounter, Sect, pstDirCtrlBlk, &stFileCtrlBlk) != true )
                    {
                        // Can not read the sector
                        HandleFailReadSector();
                        g_nestinglevel--;
                        return false;
                    }

					// Allow "placeholder" file entries to remain; these are empty files that exist,
                    // but are 0-length, with no cluster yet allocated. These placeholder files are
                    // not technically allowed, but are created by MTP and must be allowed to
                    // remain or MTP will not work - see ClearQuest stmp00007483 for more details
                    if( (stFileCtrlBlk.StartCluster == 0) && (stFileCtrlBlk.Size == 0) )
                    {
                         // Do nothing. Skip over it
                         continue;
                    }
                    
                    if( stFileCtrlBlk.StartCluster == 0 || stFileCtrlBlk.Size == 0 )
                    {
                        // Either start cluster or size is 0 but the other is non-zero, which
                        // is an invalid combination. So delete the file.
                        if ( DeleteFileRecord(RecordCounter, Sect, pstDirCtrlBlk) != true )
                        {
                            g_nestinglevel--;
                            return false;   // Can not write to the sector
                        }
                        else
                        {
                            g_checkdisk_context->FlagNeedReadSector=1;
                            continue;
                        }
                    }
                    else if ( (wClusterCount = CheckCrossLinkFile(stFileCtrlBlk.StartCluster, pstDirCtrlBlk->Device)) < 0 ) // Count Length of file in clusters and check crosslink file and set bit for the file
                    {
                        if ( DeleteFileRecord(RecordCounter, Sect, pstDirCtrlBlk) != true )
                        {
                            g_nestinglevel--;
                            return false;   // Can not write to the sector
                        }
                        else
                        {
                            g_checkdisk_context->FlagNeedReadSector = 1;
                            continue;
                        }
                    }
#if 0
                    //Backing out the change for now SDK_1442
                    else if ( CheckPartialFile(pstDirCtrlBlk->Device, &stFileCtrlBlk, wClusterCount) == TRUE)
                    {
                        if ( DeleteFileRecord(RecordCounter, Sect, pstDirCtrlBlk) != true )
                        {
                            g_nestinglevel--;
                            return false;   // Can not write to the sector
                        }
                        else
                        {
                            g_checkdisk_context->FlagNeedReadSector = 1;
                            continue;
                        }
                    }
#endif
                    else
                    {
                        // First take care of the 0 file lenght special case
                        if ( wClusterCount != stFileCtrlBlk.Size )
                        {
                            // Convert directory file size from bytes to clusters.
                            // By using cluster count instead of bytes, we can use 32-bit math
                            // and not worry about overflow for 4GB files. No need to round up,
                            // because the following test already accounts for it.
                            FileDirSizeCx = stFileCtrlBlk.Size / wCxSizeBytes;

                            // File size must be greater than size of (NumberCx - 1)
                            // and smaller than size of NumberCx
                            if ( (FileDirSizeCx > wClusterCount) || (FileDirSizeCx < (wClusterCount-1)) )
                            {
                                wCluster = stFileCtrlBlk.StartCluster;
                                for ( i = 0; i < wClusterCount ; i++ )
                                {
                                    UpdateBit(wCluster, g_checkdisk_context->FATEntryStatus, g_checkdisk_context->FATEntryStatusLength, pstDirCtrlBlk->Device, g_checkdisk_context->stPartitionBootSector.TypeFileSystem, FREE_BIT);
                                    wCluster = g_checkdisk_context->GetNextCxFromFat(wCluster);
                                    if (wCluster == BAD_CLUSTER)
                                    {
                                        g_nestinglevel--;
                                        return false;
                                    }
                                }
                                
                                if ( DeleteFileRecord(RecordCounter,Sect, pstDirCtrlBlk) != true )
                                {
                                    g_nestinglevel--;
                                    return false;
                                }
                                else
                                {
                                    g_checkdisk_context->FlagNeedReadSector = 1;
                                    continue;
                                }
                            }
                        }
                    }


                } // else if ( !(wFirstClusterLowWord & DIR_REC_VOLUMEID_POS) )
                else   // the case of (wFirstClusterLowWord & DIR_REC_VOLUMEID_POS)
                {
                    // This is a volume ID
                    bool bDelete = false;
                    uint32_t wFirstClusterHighWord;
                    
                    // not root directory, volume ID must be in the root directory
                    if(g_nestinglevel!=1) 
                    {
                        bDelete = true;
                    }
                    else
                    {   
                        
                        //DIR_FstClusHI and DIR_FstClusLO must always be 0 for the volume label 
                        wFirstClusterHighWord =  FSGetWord((uint8_t *)pstDirCtrlBlk->pwBuffer, StartRecordByte + DIR_REC_FIRST_CX_HIGH_POS);
                        if(wFirstClusterHighWord || wFirstClusterLowWord)
                        {
                            bDelete = true;
                        }
                        
                    }
                    if(bDelete)
                    {
                        if ( DeleteFileRecord(RecordCounter,Sect, pstDirCtrlBlk) != true )
                        {
                            g_nestinglevel--;
                            return false;
                        }
                    }
                }
            } // for ( RecordCounter = 0; RecordCounter < g_checkdisk_context->cachedDirRecordsPerSector; RecordCounter++ )
        } // for ( n = 0 ; n < wLoopCount ; Sect++, n++, SectCounter++ )

        // For loop terminated because need another cluster
        // If no other cluster available or at the root => No more entries free
        // otherwise, keep searching directory records in the next cluster
        //	CurCx=0;
        if ( CurCx == 0 )
        {
            // CurCx = 0 => Current directory is the root
            // Therefore no other clusters available
            g_checkdisk_context->FlagNeedReadSector = 1;
            g_nestinglevel--;
            return true;
        }
        
        // The root is full of directory entries
        n++;
        CurCx = g_checkdisk_context->GetNextCxFromFat(CurCx);

        // Cx return is not valid. Error!
        if ( CurCx == BAD_CLUSTER || CurCx <= 1 )
        {
            g_nestinglevel--;
            return false;
        }

        if ( IsLastCx(CurCx) )     // That's the end of it
        {
            g_checkdisk_context->FlagNeedReadSector = 1;
            g_nestinglevel--;
            return true;
        }

        // stmp3686 solution: consider non-user-data 2 initial clx & non-user-data initial sectors. JN ?
        Sect = CxToSect(CurCx);
    } while ( 1 );
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Fills the File Control Block structure for the specified record.
//!
//! \param[in] bRecordNumber Record number in relation to the sector (0 to 31).
//! \param[in] wSectNumber Sector Number where record is located.
//! \param[in] pstDirCtrlBlk Pointer to Directory Ctrl Block Structure.
//! \param[in] pstFileCtrlBlk Pointer to the File Control Block structure to fill.
//!
//! \retval true Operation successful.
//! \retval false Impossible to read the sector.
///////////////////////////////////////////////////////////////////////////////
bool GetFileCtrlBlk(uint8_t bRecordNumber, uint32_t wSectNumber, DIR_CTRL_BLK * pstDirCtrlBlk, FILE_CTRL_BLK * pstFileCtrlBlk)
{
    uint32_t wStartRecByte;
    uint32_t clusterlo;
    uint32_t clusterhi;
    
    if ( ReadDirSector(wSectNumber, pstDirCtrlBlk) != true )
    {
        return false;
    }

    // Calculates the position of record 1st byte
    wStartRecByte = bRecordNumber * BYTES_PER_DIR_RECORD;

    // Fills the File Contol Block Structure
	pstFileCtrlBlk->StartNameCharacter = (uint8_t)FSGetByte((uint8_t *)pstDirCtrlBlk->pwBuffer, wStartRecByte);
    pstFileCtrlBlk->Attribut = FSGetByte((uint8_t *)pstDirCtrlBlk->pwBuffer, wStartRecByte + DIR_REC_ATT_POS);
    pstFileCtrlBlk->Size = FSGetDWord((uint8_t *)pstDirCtrlBlk->pwBuffer, wStartRecByte + DIR_REC_SIZE_POS);

    clusterlo =  FSGetWord((uint8_t *)pstDirCtrlBlk->pwBuffer, wStartRecByte + DIR_REC_FIRST_CX_POS);
    clusterhi =  FSGetWord((uint8_t *)pstDirCtrlBlk->pwBuffer, wStartRecByte + DIR_REC_FIRST_CX_HIGH_POS);
    pstFileCtrlBlk->StartCluster =  (uint32_t)clusterlo + (((uint32_t)clusterhi) << 16);
    
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Deletes a file record and its associate long file name (if any)
//!     from the directory entries
//!
//! \param[in] bRecordNumber Record number in relation to the sector (0 to 31).
//! \param[in] wSectNumber Sector Number where record is located.
//! \param[in] pstDirCtrlBlk Pointer to Directory Ctrl Block Structure.
//!
//! \retval true Operation successful.
//! \retval false Impossible to read/write the sector.
///////////////////////////////////////////////////////////////////////////////
bool DeleteFileRecord(uint8_t bRecordNumber, uint32_t wSectNumber, DIR_CTRL_BLK * pstDirCtrlBlk)
{
    uint32_t wStartRecByte;
    uint32_t wLoopCounter = 0;
    uint8_t bCurRecord;
    uint32_t wCurSect;
    
    g_checkdisk_context->glb_wFileCorrupted++;

    // Read new sector
    if ( ReadDirSector(wSectNumber, pstDirCtrlBlk) != true )
    {
        return false;
    }

    // Calculates the position of record 1st byte
    wStartRecByte = bRecordNumber * BYTES_PER_DIR_RECORD;

    // Mark the file "deleted"

    PutByte((uint8_t *)pstDirCtrlBlk->pwBuffer, FILE_DELETED_CODE, wStartRecByte);

    // Only store up to CHECKDISK_MAX_FAST_FILE_HANDLES fast file handles for deleted
    // files.
    // NOTE: We do NOT store a fast file handle for long file name records
    if( g_CheckDiskNumFastFileHandles < CHECKDISK_MAX_FAST_FILE_HANDLES )
    {
        g_CheckDiskFastFileHandles[ g_CheckDiskNumFastFileHandles++ ] = (uint64_t)(
            ( (uint64_t)pstDirCtrlBlk->Device << 44 ) |
            ( (uint64_t)wStartRecByte << 32 ) |
            ( (uint64_t)wSectNumber ) );
    }

    pstDirCtrlBlk->Control = DIRTY;

    // Chek if 1st record of the current directory
    if ( (bRecordNumber == 0) && (pstDirCtrlBlk->StartSectCurDir == wSectNumber) )
    {
        return true;            // we are done
    }

    // Check if long file names and delete each long file name entry
    // specific to this file
    if ( bRecordNumber == 0 )
    {
        bCurRecord = g_checkdisk_context->cachedDirRecordsPerSector - 1;
        wCurSect = wSectNumber - 1;
    }
    else
    {
        bCurRecord = bRecordNumber - 1;
        wCurSect = wSectNumber;
    }

    do {
        uint8_t  bAttributeByte;
        uint32_t wFirstClusterLowWord;

        // Read new sector
        if ( ReadDirSector(wCurSect, pstDirCtrlBlk) != true )
        {
            return false;
        }

        // Calculates the position of record 1st byte
        wStartRecByte = bCurRecord * BYTES_PER_DIR_RECORD;

        // Check if record is part of long file name
        bAttributeByte = (uint8_t)FSGetByte((uint8_t *)pstDirCtrlBlk->pwBuffer, wStartRecByte + DIR_REC_ATT_POS);
        wFirstClusterLowWord =  FSGetWord((uint8_t *)pstDirCtrlBlk->pwBuffer, wStartRecByte + DIR_REC_FIRST_CX_POS);

        if ( (bAttributeByte == ATTR_LONG_NAME) && (wFirstClusterLowWord == 0x00) )
        {
            // It is long filename record, delete it
            // Mark the file "deleted"
            PutByte((uint8_t *)pstDirCtrlBlk->pwBuffer,FILE_DELETED_CODE,wStartRecByte);
            pstDirCtrlBlk->Control = DIRTY;
        }
        else
        {
            return true;
        }

        // Decrement record number
        if ( bCurRecord == 0 )            // If last record in the sector, need a new sector
        {
            bCurRecord = g_checkdisk_context->cachedDirRecordsPerSector - 1;
            wCurSect--;
        }
        else
        {
            bCurRecord--;
        }
    } while ( wLoopCounter++ < MAX_ENTRIES_LONG_FILE_NAME );

    // If we arrive here this means that we fail to find
    // the end of a long file name
    return false;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Reads a sector in the directory data.
//
//! \param[in] wSectNumber Sector Number to read.
//! \param[in] pstDirCtrlBlk Pointer to Directory Ctrl Block Structure.
//
//! \retval true Operation successful.
//! \retval false Impossible to read/write the sector.
///////////////////////////////////////////////////////////////////////////////
bool ReadDirSector(uint32_t wSectNumber, DIR_CTRL_BLK * pstDirCtrlBlk)
{
    int32_t * readBuffer;
    uint32_t cacheToken;

    // Reads directory sector in buffer
    if ( wSectNumber != pstDirCtrlBlk->CurSect )
    {
        // Check if need to save the current sector first
        if ( pstDirCtrlBlk->Control != CLEAN )
        {
            if (FSWriteSector(pstDirCtrlBlk->Device, pstDirCtrlBlk->CurSect, 0, (uint8_t *)pstDirCtrlBlk->pwBuffer, 0, g_checkdisk_context->cachedSectorSize, 0) != SUCCESS)
            {
                return false;
            }
        }

        // Read new sector
        EnterNonReentrantSection();
        if ((readBuffer = FSReadSector(pstDirCtrlBlk->Device, (uint32_t)wSectNumber, 0, &cacheToken)) == NULL)
        {
            LeaveNonReentrantSection();
            return false;
        }
        memcpy(pstDirCtrlBlk->pwBuffer, readBuffer, g_checkdisk_context->cachedSectorSize);
        FSReleaseSector(cacheToken);
        LeaveNonReentrantSection();

        // Update current sector loaded in Directory Control Structure
        pstDirCtrlBlk->CurSect = wSectNumber;
        pstDirCtrlBlk->Control = CLEAN;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Calculates the 1st sector number for the Cluster passed
//!
//! The logical sector 0 is the Partition boot sector for this media.
//! The data area always starts at cluster #2.
//!
//! Therefore:
//!
//!     StartSect = ((wCx - 2)*SectPerCluster) + StartSectNumberForDataArea
//!
//! \param[in] wCx Cluster number
//!
//! \return The first sector number for wCx
///////////////////////////////////////////////////////////////////////////////
uint32_t  CxToSect(uint32_t wCx)
{
    return (((wCx - 2) * g_checkdisk_context->stPartitionBootSector.bSectPerCx) + g_checkdisk_context->stPartitionBootSector.wStartSectData);
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Records the fact that a sector could not be read/written
//!     on the media.
//!
//! Failing to read/write a sector on the media is a critical error
//! that should terminate the chkdsk activity on the specific media.
///////////////////////////////////////////////////////////////////////////////
void  HandleFailReadSector(void)
{
    g_checkdisk_context->glb_bFailReadSect = true;
}



