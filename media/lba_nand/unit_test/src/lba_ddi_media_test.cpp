///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor, Inc. All rights reserved.
// 
// Freescale Semiconductor, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of Freescale Semiconductor, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"

extern "C" {
#include "os/thi/os_thi_api.h"
}

//! Test pattern size in bytes.
const int k_iPatternSizeInBytes = 2048;

//! Test pattern short value.
const uint16_t k_u16PatternVal = 0xAA55;

//! Media number of the internal media.
const uint32_t k_u32LogMediaNumber = 0;

extern MediaAllocationTable_t g_MediaAllocationTable[];
extern uint32_t g_wNumDrives;

RtStatus_t TestDrives();
RtStatus_t WriteTest(DriveTag_t Tag, uint32_t u32SectorNumber);

///////////////////////////////////////////////////////////////////////////////
//! \brief Execute the unit test.
//! 
//! This function is the entry point for the program.
//! 
//! \param[in] param Unused.
//! 
//! \retval 0  SUCCESS (always return SUCCESS).
/////////////////////////////////////////////////////////////////////////////////
RtStatus_t test_main(ULONG param)
{
    RtStatus_t Status;
    uint32_t u32NumDrives;
    MediaAllocationTable_t *pMediaTable = &g_MediaAllocationTable[k_u32LogMediaNumber];

    // Initialize the internal media.
    Status = MediaInit(k_u32LogMediaNumber);
    printf("MediaInit returned 0x%08x\n", Status);
    
    // First see what we can discover on the media.
    if (Status == SUCCESS)
    {
        Status = MediaDiscoverAllocation(k_u32LogMediaNumber);
        printf("MediaDiscoverAllocation returned 0x%08x\n", Status);
        
        // Ignore the previous status.
        Status = SUCCESS;
    }

    // Now erase the media.
    if (Status == SUCCESS)
    {
        Status = MediaErase(k_u32LogMediaNumber, 0, true);
        printf("MediaErase returned 0x%08x\n", Status);
    }

    // Allocate drives.
    if (Status == SUCCESS)
    {
        Status = MediaAllocate(k_u32LogMediaNumber, pMediaTable);
        printf("MediaAllocate returned 0x%08x\n", Status);
    }

    // See if we can discover what we just allocated.
    g_wNumDrives = 0;
    if (Status == SUCCESS)
    {
        Status = MediaDiscoverAllocation(k_u32LogMediaNumber);
        printf("MediaDiscoverAllocation returned 0x%08x\n", Status);
    }

    // Test Media Get Info.
    if (Status == SUCCESS)
    {
        Status = MediaGetInfo(k_u32LogMediaNumber, kMediaInfoNumberOfDrives, &u32NumDrives);
        printf("MediaGetInfo returned 0x%08x\n", Status);
        printf("Media number of drives = %d\n", u32NumDrives);
    }

    // Test all drives.
    if (Status == SUCCESS)
    {
        Status = TestDrives();
    }

    if (Status == SUCCESS)
    {
        Status = MediaShutdown(k_u32LogMediaNumber);
        printf("MediaShutdown returned 0x%08x\n", Status);
    }

    // Done!
    if (Status == SUCCESS)
    {
        printf("Test passed!\r\n");
    }
    else
    {
        printf("Test failed with error: 0x%08x\n", (unsigned int)Status);
    }
    
    exit(Status);
    return Status;
}

//! \brief Test all drives.
RtStatus_t TestDrives(void)
{
    DriveIterator_t Iter;
    RtStatus_t Status = DriveCreateIterator(&Iter);

    // Iterate over all drives.
    DriveTag_t Tag;
    while ((Status == SUCCESS) && (DriveIteratorNext(Iter, &Tag) == SUCCESS))
    {
        Status = DriveInit(Tag);
        printf("DriveInit returned 0x%08x\n", Status);

        // Test Drive Get Info.
        uint64_t u64SizeInSectors;
        if (Status == SUCCESS)
        {
            Status = DriveGetInfo(Tag, kDriveInfoSizeInSectors, &u64SizeInSectors);
            printf("DriveGetInfo returned 0x%08x\n", Status);
            printf("Drive size in sectors = %d\n", (uint32_t)u64SizeInSectors);
        }

        // Perform the write test.
        if (Status == SUCCESS)
        {
            // Test the first sector.
            uint32_t u32SectorNumber = 0;
            Status = WriteTest(Tag, u32SectorNumber);
            printf("WriteTest of drive 0x%x sector %d returned 0x%08x\n", Tag, u32SectorNumber, Status);

            // Test the last sector.
            u32SectorNumber = (uint32_t)(u64SizeInSectors - 1);
            if (Status == SUCCESS)
            {
                Status = WriteTest(Tag, u32SectorNumber);
                printf("WriteTest drive 0x%x of sector %d returned 0x%08x\n", Tag, u32SectorNumber, Status);
            }
        }

        // Shutdown the drive.
        if (Status == SUCCESS)
        {
            Status = DriveShutdown(Tag);
            printf("DriveShutdown returned 0x%08x\n", Status);
        }
    }
    
    DriveIteratorDispose(Iter);

    return Status;
}

//! \brief Perform a non-destructive write test on the specified drive.
RtStatus_t WriteTest(DriveTag_t Tag, uint32_t u32SectorNumber)
{
    RtStatus_t Status;
    SECTOR_BUFFER *pSaveBuffer;
    SECTOR_BUFFER *pPatternBuffer;
    uint16_t *pu16Data;
    int i;

    // Get a buffer for the saved data.
    Status = media_buffer_acquire(kMediaBufferType_Sector, kMediaBufferFlag_None, &pSaveBuffer);
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Read the current data.
    Status = DriveReadSector(Tag, u32SectorNumber, pSaveBuffer);
    if (Status != SUCCESS)
    {
        printf("DriveWriteSector returned 0x%08x\n", Status);
        media_buffer_release(pSaveBuffer);
        return Status;
    }

    // Get a buffer for the pattern data.
    Status = media_buffer_acquire(kMediaBufferType_Sector, kMediaBufferFlag_None, &pPatternBuffer);
    if (Status != SUCCESS)
    {
        media_buffer_release(pSaveBuffer);
        return Status;
    }

    // Fill buffer with a test pattern.
    pu16Data = (uint16_t *)pPatternBuffer;
    for (i = 0; i < k_iPatternSizeInBytes/sizeof(uint16_t); i++)
    {
        *pu16Data++ = k_u16PatternVal;
    }

    // Write the pattern.
    Status = DriveWriteSector(Tag, u32SectorNumber, pPatternBuffer);
    if (Status != SUCCESS)
    {
        printf("DriveWriteSector returned 0x%08x\n", Status);
        media_buffer_release(pSaveBuffer);
        media_buffer_release(pPatternBuffer);
        return Status;
    }

    memset(pPatternBuffer, 0, k_iPatternSizeInBytes);

    // Read the pattern back.
    Status = DriveReadSector(Tag, u32SectorNumber, pPatternBuffer);
    if (Status != SUCCESS)
    {
        printf("DriveReadSector returned 0x%08x\n", Status);
        media_buffer_release(pSaveBuffer);
        media_buffer_release(pPatternBuffer);
        return Status;
    }

    // Test the pattern.
    pu16Data = (uint16_t *)pPatternBuffer;
    for (i = 0; i < k_iPatternSizeInBytes/sizeof(uint16_t); i++)
    {
        if (*pu16Data != k_u16PatternVal)
        {
            printf("Pattern mismatch at word %d: 0x%04x != 0x%04x\n",
                   i, *pu16Data, k_u16PatternVal);
            Status = ERROR_GENERIC;
            break;
        }
        pu16Data++;
    }

    // Write the original data back.
    RtStatus_t WriteStatus = DriveWriteSector(Tag, u32SectorNumber, pSaveBuffer);
    if (WriteStatus != SUCCESS) printf("DriveWriteSector returned 0x%08x\n", Status);
    // Don't overwrite the pattern match status if it failed.
    if ((WriteStatus != SUCCESS) && (Status == SUCCESS))
    {
        Status = WriteStatus;
    }

    // Release the buffers.
    media_buffer_release(pSaveBuffer);
    media_buffer_release(pPatternBuffer);

    return Status;
}
