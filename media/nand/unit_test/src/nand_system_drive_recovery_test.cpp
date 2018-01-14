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
///////////////////////////////////////////////////////////////////////////////
#include "drivers/media/common/media_unit_test_helpers.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "drivers/media/nand/ddi/systemDrive/ddi_nand_system_drive.h"
#include "drivers/media/nand/ddi/common/ddi_nand_ddi.h"
#include "drivers/media/nand/ddi/common/DeferredTask.h"
#include "drivers/media/nand/ddi/media/ddi_nand_media.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

uint32_t g_sectorCount = 0;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

void wait_for_deferred_tasks();
RtStatus_t test_sys_drives();
RtStatus_t prepare_sys_drives();
RtStatus_t run_test();

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////
    
void wait_for_deferred_tasks()
{
    nand::DeferredTaskQueue * q = g_nandMedia->getDeferredQueue();

    q->drain();
}

// ------------------------------------------------------------------------------------------

RtStatus_t test_sys_drives()
{
    RtStatus_t status;
    LogicalDrive * logicalDrive = DriveGetDriveFromTag(DRIVE_TAG_BOOTMANAGER_S);
    nand::SystemDrive * drive = static_cast<nand::SystemDrive *>(logicalDrive);
    
    int z = 0;
    uint32_t lastSector = 0;
    int count;
    for (count=0; count < 1000000; ++count)
    {
        // Select a random sector to read.
        uint32_t thisSector;
        bool isSequential = false;
        
        // There's a chance that we read sequential sectors instead of totally random ones.
        // Of course, if we are at the end of the drive, we have to pick another sector.
        if ((lastSector < g_sectorCount - 2) && random_percent(3000)) // 30.00%
        {
            thisSector = lastSector + 1;
            isSequential = true;
        }
        else
        {
            thisSector = random_range(g_sectorCount - 1);
        }
        
        lastSector = thisSector;

        // Fill the compare buffer with this sector's expected data.
        fill_data_buffer(s_dataBuffer, thisSector);
        
        // Insert random errors.
        if (random_percent(250)) // 2.50%
        {
            g_nand_hal_insertReadError = ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR;
            FASTPRINT("Inserting ECC_FIXED_REWRITE_SECTOR on sector %u (count=%u)\n", thisSector, count);
            z = 0;
        }
        else if (random_percent(50))    // 0.50%
        {
            g_nand_hal_insertReadError = ERROR_DDI_NAND_HAL_ECC_FIX_FAILED;
            FASTPRINT("Inserting ECC_FIX_FAILED on sector %u (count=%u)\n", thisSector, count);
            z = 0;
        }
        else
        {
            FASTPRINT("%s%s", isSequential ? "+" : ".", ++z > 32 ? "\n" : "");
            if (z > 32)
            {
                z = 0;
            }
        }
        
        // Read this page of the system drive.
        status = drive->readSector(thisSector, s_readBuffer);
        if (status != SUCCESS)
        {
            FASTPRINT("Read sector %u returned 0x%08x\n", thisSector, status);
        }
        
        // Make sure we got back the data we expect.
        if (!compare_buffers(s_dataBuffer, s_readBuffer, g_actualBufferBytes))
        {
            FASTPRINT("Page read mismatch (line %d)\n", __LINE__);
        }
        
        // Allow some time to interleave deferred tasks.
        tx_thread_sleep(5);
    }
    
    wait_for_deferred_tasks();
    tx_thread_sleep(100);
    
    return SUCCESS;
}

RtStatus_t prepare_sys_drives()
{
    RtStatus_t status;
    int i;

    LogicalDrive * drives[3] = {0};
    drives[0] = DriveGetDriveFromTag(DRIVE_TAG_BOOTMANAGER_S);
    drives[1] = DriveGetDriveFromTag(DRIVE_TAG_BOOTMANAGER2_S);
    drives[2] = DriveGetDriveFromTag(DRIVE_TAG_BOOTMANAGER_MASTER_S);
    
    g_sectorCount = drives[0]->getSectorCount();
    drives[0]->getInfo(kDriveInfoSectorSizeInBytes, &g_actualBufferBytes);
    
    uint32_t n;
    for (n=0; n < g_sectorCount; ++n)
    {
        fill_data_buffer(s_dataBuffer, n);
        
        for (i=0; i < 3; ++i)
        {
            LogicalDrive * drive = drives[i];
            DriveTag_t tag = drive->getTag();
            
            // Erase drive before writing the first sector.
            if (n==0)
            {
                FASTPRINT("Erasing drive %2x...\n", tag);
                
                status = drive->erase();
                if (status != SUCCESS)
                {
                    FASTPRINT("Erasing drive %2x returned 0x%08x\n", tag, status);
                    return status;
                }
            }
            
            status = drive->writeSector(n, s_dataBuffer);
            if (status != SUCCESS)
            {
                FASTPRINT("Writing sector %u of drive %2x returned 0x%08x\n", n, tag, status);
                return status;
            }
        }
    }
    
    FASTPRINT("Done filling drives.\n");
    
    return status;
}

RtStatus_t run_test()
{
    RtStatus_t status;
    
//     test_random_percent(1000);
//     test_random_percent(5000);
    
    status = MediaInit(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media init returned 0x%08x\n", status);
        return status;
    }
    
    status = MediaDiscoverAllocation(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media discover returned 0x%08x\n", status);
        return status;
    }
    
    status = DriveInit(DRIVE_TAG_BOOTMANAGER_S);
    if (status != SUCCESS)
    {
        FASTPRINT("Initing primary system drive returned 0x%08x\n", status);
        return status;
    }
    
    status = DriveInit(DRIVE_TAG_BOOTMANAGER2_S);
    if (status != SUCCESS)
    {
        FASTPRINT("Initing secondary system drive returned 0x%08x\n", status);
        return status;
    }
    
    status = DriveInit(DRIVE_TAG_BOOTMANAGER_MASTER_S);
    if (status != SUCCESS)
    {
        FASTPRINT("Initing master system drive returned 0x%08x\n", status);
        return status;
    }
    
    status = prepare_sys_drives();
    if (status != SUCCESS)
    {
//         FASTPRINT("Media discover returned 0x%08x\n", status);
        return status;
    }
    
    status = test_sys_drives();
    if (status != SUCCESS)
    {
//         FASTPRINT("Media discover returned 0x%08x\n", status);
        return status;
    }
    
    status = MediaShutdown(kInternalMedia);
    if (status != SUCCESS)
    {
        FASTPRINT("Media shutdown returned 0x%08x\n", status);
        return status;
    }
    
    tss_logtext_Flush(TX_WAIT_FOREVER);
    
    return SUCCESS;
}

RtStatus_t test_main(ULONG param)
{
    RtStatus_t status;
    
    // Initialize the Media
    status = SDKInitialization();

    if (status == SUCCESS)
    {
        status = run_test();
    }
    
    if (status == SUCCESS)
    {
        FASTPRINT("unit test passed!\n");
    }
    else
    {
        FASTPRINT("unit test failed: 0x%08x\n", status);
    }
    
    exit(status);
    return status;
}

