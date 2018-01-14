///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor. All rights reserved.
// 
// Freescale Semiconductor
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute confidential
// information and may comprise trade secrets of Freescale Semiconductor or its
// associates, and any use thereof is subject to the terms and conditions of the
// Confidential Disclosure Agreement pursual to which this source code was
// originally received.
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media
//! @{
//! \file ddi_ldl_write.c
//! \brief Write functions for the logical drive layer.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "errordefs.h"
#include <string.h>
#include "ddi_media_internal.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "hw/profile/hw_profile.h"
#include "os/filesystem/fsapi.h"
#include "drivers/media/cache/media_cache.h"

extern "C" {
#include "os/thi/os_thi_stack_context.h"
}

#if defined(NO_SDRAM)
#include "os/threadx/tx_api.h"
TX_MUTEX g_WriteSectorMutex;

#if defined(USE_NAND_STACK)
extern TX_MUTEX g_NANDThreadSafeMutex;
#endif // defined(USE_NAND_STACK)
#endif // defined(NO_SDRAM)

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Shuts down all drives belonging to a particular media.
////////////////////////////////////////////////////////////////////////////////
static RtStatus_t ddi_ldl_shutdown_media_drives(unsigned mediaNumber)
{
    // Before shutting down the media, shut down all of its drives.
    DriveTag_t tag;
    DriveIterator_t iter;
    if (DriveCreateIterator(&iter) == SUCCESS)
    {
        while (DriveIteratorNext(iter, &tag) == SUCCESS)
        {
            LogicalDrive * drive = DriveGetDriveFromTag(tag);
            
            // Shut down this drive if it belongs to the media we're interested in.
            if (drive && (drive->getMedia() && drive->getMedia()->getMediaNumber() == mediaNumber))
            {
                // Only shutdown the drive if it was actually inited.
                if (drive->isInitialized())
                {
                    DriveShutdown(tag);
                }
                
                DriveRemove(tag);
            }
        }
        
        DriveIteratorDispose(iter);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaErase(uint32_t u32LogMediaNumber, uint32_t u32MagicNumber, uint8_t u8NoEraseHidden)
{
    LogicalMedia * media = MediaGetMediaFromIndex(u32LogMediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // First shutdown all drives belonging to this media.
    ddi_ldl_shutdown_media_drives(u32LogMediaNumber);
    
    // Now erase the media.
    return media->erase();
}

#ifdef NO_SDRAM
#pragma ghs section text=".static.text"
#endif

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveWriteSector(DriveTag_t tag, uint32_t u32SectorNumber, const SECTOR_BUFFER * pSectorData)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER;
    }
    else if (!drive->isInitialized())
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
    
#if defined(USE_NAND_STACK) && defined(NO_SDRAM)
    if (drive->getMedia()->getPhysicalType() != kMediaTypeMMC)
    {
        static RtStatus_t s_RetValue;
        TX_THREAD *pCurrentThread;
        
        tx_mutex_get(&g_NANDThreadSafeMutex, TX_WAIT_FOREVER);
        pCurrentThread = tx_thread_identify();
        if (pCurrentThread != NULL)
        {
            os_thi_SaveStackContext(&g_NewNandStackContext, pCurrentThread, &g_OldNandStackContext, 40);
        }

        s_RetValue = drive->writeSector(u32SectorNumber, pSectorData);
            
        if (pCurrentThread != NULL)
        {
            os_thi_RestoreStackContext(&g_OldNandStackContext, pCurrentThread);
        }
        tx_mutex_put(&g_NANDThreadSafeMutex);

        return s_RetValue;
    }
#endif // defined(USE_NAND_STACK) && defined(NO_SDRAM)

    return drive->writeSector(u32SectorNumber, pSectorData);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveFlush(DriveTag_t tag)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER;
    }
    else if (!drive->isInitialized() )
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

#if (defined(USE_NAND_STACK) && defined(NO_SDRAM))
    if (drive->getMedia()->getPhysicalType() != kMediaTypeMMC)
    {
        static RtStatus_t s_RetValue;
        TX_THREAD *pCurrentThread;
    
        tx_mutex_get(&g_NANDThreadSafeMutex, TX_WAIT_FOREVER);
        pCurrentThread=tx_thread_identify();
        if (pCurrentThread != NULL)
        {
            os_thi_SaveStackContext(&g_NewNandStackContext, pCurrentThread, &g_OldNandStackContext, 40);
        }

        s_RetValue = drive->flush();

        if (pCurrentThread != NULL)
        {
            os_thi_RestoreStackContext(&g_OldNandStackContext, pCurrentThread);
        }
        tx_mutex_put(&g_NANDThreadSafeMutex);
        
        return s_RetValue;
    }
#endif

    return drive->flush();
}

#ifdef NO_SDRAM
#pragma ghs section text=default
#endif

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveErase(DriveTag_t tag, uint32_t u32MagicNumber)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER;
    }
    else if (!drive->isInitialized() )
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    return drive->erase();
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaShutdown(uint32_t u32LogMediaNumber)
{
    LogicalMedia * media = MediaGetMediaFromIndex(u32LogMediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // First shutdown all drives belonging to this media.
    ddi_ldl_shutdown_media_drives(u32LogMediaNumber);
    
    // Now shutdown the media.
    RtStatus_t status = media->shutdown();
    
    delete media;
    g_ldlInfo.m_media[u32LogMediaNumber] = NULL;
    --g_ldlInfo.m_mediaCount;
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Verify if the supplied sector contains valid fields of a
//!        Partition Boot Sector. Internal function.
//! \param[in] pSectorBuffer - Sector Data (presumably of a Partition Boot Sector)
//!
//! \retval SUCCESS
//! \retval ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND 
//! \internal
////////////////////////////////////////////////////////////////////////////////
RtStatus_t VerifyPBS(uint8_t* pSectorData)
{
    uint32_t u8SecValue = 1;
    uint8_t u8SecPerClus = pSectorData[0x0d];
    uint32_t i;

    // Verify that the Sectors Per Cluster field is a power of 2 value
    for (i = 0; i < 8; i++)
    {
        if (u8SecPerClus == u8SecValue)
        {
            break;
        }
        u8SecValue <<= 1;
    }

    if ((u8SecValue == 256) || ((pSectorData[0] != 0xEB) && (pSectorData[0] != 0xE9)))
    {
	     return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
    }

    // Verify the Boot Sector signature field (should be 0xAA55)
    if (((pSectorData[0x1fe] == 0x55) && (pSectorData[0x1ff] == 0xAA)) ||
        ((pSectorData[0x7fe] == 0x55) && (pSectorData[0x7ff] == 0xAA)))
    {
        return SUCCESS;
    }

    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t FSDataDriveInit(DriveTag_t tag)
{
    uint32_t u32PbsTotalSectors;
    uint32_t ProbablePBSFlag = 0;
    uint8_t* pSectorData;
    RtStatus_t retval;
    uint32_t pbsOffset;
    MediaCacheParamBlock_t pb = {0};
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }
    
    // Flush the cache for this drive.
    pb.flags = kMediaCacheFlag_FlushDrive | kMediaCacheFlag_Invalidate;
    pb.drive = tag;
    media_cache_flush(&pb);
    
    // Reset the PBS start offset to 0.
    drive->setInfo<uint32_t>(kDriveInfoSectorOffsetInParent, 0);

    // First read sector 0
    pb.flags = kMediaCacheFlag_NoPartitionOffset;
    pb.sector = 0;
    pb.requestSectorCount = 1;
    pb.mode = WRITE_TYPE_RANDOM;
    retval = media_cache_read(&pb);
    pSectorData = pb.buffer;
    if (retval != SUCCESS || pSectorData == 0)
    {
        return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
    }

    // First, extract the assumed start sector. We don't want to set the g_u32MbrStartSector
    // global yet, because ReadSector() uses MediaRead(), which offsets based on that global's
    // value. Thus, we'd get a double offset when trying to read the PBS.
    pbsOffset = pSectorData[0x1c6] + (pSectorData[0x1c7] << 8) + (pSectorData[0x1c8] << 16) + (pSectorData[0x1c9] << 24);
    
    // Release the last read.
    media_cache_release(pb.token);

    // Now read what may be the first sector of the first partition
    pb.sector = pbsOffset;
    retval = media_cache_read(&pb);
    pSectorData = pb.buffer;
    if (retval != SUCCESS || pSectorData == 0)
    {
        // The read failed, hence we might not have this sector as MBR, assume PBS
        ProbablePBSFlag = 1;
    }
    else
    {
        if ((retval = VerifyPBS(pSectorData)) != SUCCESS)
        {
            // The verification failed, so assume PBS
            ProbablePBSFlag = 1;
        }
    }

    // Ok, so Sector 0 might be a PBS, verify that this is indeed the case.
    if (ProbablePBSFlag == 1)
    {
        pbsOffset = 0;

        media_cache_release(pb.token);
        
        pb.sector = pbsOffset;
        retval = media_cache_read(&pb);
        pSectorData = pb.buffer;
        if (retval != SUCCESS || pSectorData == 0)
        {
            // Not necessary to media_cache_release() here, because the read failed.

            retval = ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
            return retval;
        }
        
        if ((retval = VerifyPBS(pSectorData)) != SUCCESS)
        {
            media_cache_release(pb.token);
            return retval;
        }
    }

    // Get Total Sectors from PBS (first look at small 2-byte count field at 0x13&0x14)
    u32PbsTotalSectors = pSectorData[0x13] + (pSectorData[0x14] << 8);

    if (u32PbsTotalSectors == 0)
    {
        // Total Sectors is in the large 4-byte count field beginning at 0x20
        u32PbsTotalSectors = pSectorData[0x20] + (pSectorData[0x21] << 8) + (pSectorData[0x22] << 16) + (pSectorData[0x23] << 24);
    }

    // Release the last read.
    media_cache_release(pb.token);

    if (u32PbsTotalSectors == 0)
    {
        return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
    }
    
    // Finally, save the offset to the PBS.
    drive->setInfo<uint32_t>(kDriveInfoSectorOffsetInParent, pbsOffset);
    
    // Flush cache again, since we've modified the PBS offset, thus changing the meaning
    // of "sector 0".
    memset(&pb, 0, sizeof(pb));
    pb.flags = kMediaCacheFlag_FlushDrive | kMediaCacheFlag_Invalidate;
    pb.drive = tag;
    media_cache_flush(&pb);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveShutdown(DriveTag_t tag)
{
    // Flush the drive before shutting down.
    RtStatus_t RetValue = DriveFlush(tag);
    if (RetValue != SUCCESS)
    {
        return RetValue;
    }

    // Look up the drive by tag and perform sanity checks.
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER;
    }
    else if (!drive->isInitialized() )
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Invoke the API.
    return drive->shutdown();
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveRepair(DriveTag_t tag, uint32_t u32MagicNumber)
{
    RtStatus_t status;

    // Get the drive object.
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER;
    }

    // Shutdown the drive if it was already initialized.
    if (drive->isInitialized())
    {
        status = DriveShutdown(tag);
        if (status != SUCCESS)
        {
            return status;
        }
    }

    bool bMediaErased = false;

    status = drive->repair();
    
    // If media was erased, remember to return this error to the caller.
    if (status == ERROR_DDI_LDL_LDRIVE_FS_FORMAT_REQUIRED)
    {
        bMediaErased = true;
        status = SUCCESS;
    }
    if (status != SUCCESS)
    {
        return status;
    }

    // Initialize the drive.
    status = DriveInit(tag);
    if (status != SUCCESS)
    {
        return status;
    }

    return bMediaErased ? ERROR_DDI_LDL_LDRIVE_FS_FORMAT_REQUIRED : SUCCESS;
}
//! @}

