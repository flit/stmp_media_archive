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
//! \addtogroup ddi_lba_nand_media
//! @{
//! \file ddi_lba_nand_media_init.cpp
//! \brief This file initializes the LBA NAND Media.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "ddi_lba_nand_media.h"
#include "ddi_lba_nand_internal.h"
#include "ddi_lba_nand_hal_internal.h"
#include "hw/core/vmemory.h"
#include "hw/otp/hw_otp.h"
#include "drivers/rtc/ddi_rtc_persistent.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! \brief Logical Media API table for LBA NAND
const LogicalMediaApi_t g_LbaNandMediaApi = 
{
    LbaNandMediaInit,
    LbaNandMediaAllocate,
    LbaNandMediaDiscoverAllocation,
    LbaNandMediaGetMediaTable,
    NULL,
    LbaNandMediaGetInfo,
    LbaNandMediaSetInfo,
    LbaNandMediaErase,
    LbaNandMediaShutdown,
    LbaNandMediaFlushDrives,
    LbaNandMediaSetBootDrive
};

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

TX_MUTEX g_LbaNandMediaMutex;
TX_SEMAPHORE g_LbaNandMediaSemaphore;
LbaNandMediaInfo g_LbaNandMediaInfo;

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMediaInit(LogicalMedia_t *pDescriptor)
{
    RtStatus_t Status;

    assert(pDescriptor);

    // Ask the Lba Media to initialize its synchronization objects.
    Status = os_thi_ConvertTxStatus(tx_mutex_create(&g_LbaNandMediaMutex, "LBA-NAND_MEDIA_MUTEX", TX_INHERIT));
    if (Status != SUCCESS)
    {
        return Status;
    }

    Status = os_thi_ConvertTxStatus(tx_semaphore_create(&g_LbaNandMediaSemaphore, "LBA-NAND_MEDIA_SEMA", 1));
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Initialize the NAND serial number to the same as the chip
    hw_otp_GetChipSerialNumber(&g_InternalMediaSerialNumber);
    
    // Initialize the NAND HAL library.
    Status = ddi_lba_nand_hal_init();
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Get the number of devices (chip selects).
    unsigned uNumDevices = ddi_lba_nand_hal_get_device_count();
    assert(uNumDevices > 0);

    // Create the media object.
    LbaNandMedia *pMedia = new LbaNandMedia();
    assert(pMedia);

    // Add the physical media objects (one per device).
    for (int i = 0; i < uNumDevices; i++)
    {
        Status = pMedia->addPhysicalMedia(ddi_lba_nand_hal_get_device(i));
        if (Status != SUCCESS)
        {
            delete pMedia;
            return Status;
        }
     }

    // Initialize the LogicalMedia descriptor.
    pDescriptor->u64SizeInBytes = pMedia->getSizeInBytes();
    pDescriptor->PhysicalType = kMediaTypeNand;
    pDescriptor->bWriteProtected = false;
    pDescriptor->pMediaInfo = (void *)pMedia;
    pDescriptor->bInitialized = true;
    pDescriptor->eState = kMediaStateUnknown;

    // Set the sector size to the size we use for the MBR and VFP.
    pDescriptor->u32AllocationUnitSizeInBytes = kLbaNandSectorSize;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaAllocate(LogicalMedia_t *pDescriptor,
                                MediaAllocationTable_t *pTable)
{
    int i;
    RtStatus_t Status;
    uint64_t u64AllocatedSize;
    MediaAllocationTableEntry_t * tableEntry;

    assert(pDescriptor);
    assert(pTable);

    if (pDescriptor->bInitialized == false)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // Get the media object.
    LbaNandMedia *pMedia = (LbaNandMedia *)pDescriptor->pMediaInfo;
    assert(pMedia);

    // Make sure the object is ready to receive new drives.
    pMedia->resetDrives();

    // Add the system drives.
    for (i = 0; i < pTable->u32NumEntries; i++)
    {
        tableEntry = &pTable->Entry[i];
        
        if (tableEntry->Type == kDriveTypeSystem)
        {
            if (tableEntry->u32Tag == DRIVE_TAG_BOOTLET_S)
            {
                Status = pMedia->addBootletDrive();
            }
            else
            {
                Status = pMedia->addSystemDrive(tableEntry->u64SizeInBytes, tableEntry->u32Tag);
            }
            if (Status != SUCCESS)
            {
                return Status;
            }
        }
    }

    // Commit the system drives to the media.
    // This may change the size of the MDP, so it must be done before
    // adding data drives.
    Status = pMedia->commitSystemDrives();
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Process the hidden drives, which must be added before the data drive,
    // since the data drive can possibly span multiple chip selects.
    bool foundDataDrive = false;
    for (i = 0; i < pTable->u32NumEntries; i++)
    {
        tableEntry = &pTable->Entry[i];
        
        // Add the hidden drives.
        if (tableEntry->Type == kDriveTypeHidden)
        {
            Status = pMedia->addHiddenDrive(tableEntry->u64SizeInBytes, &u64AllocatedSize, tableEntry->u32Tag);
            if (Status != SUCCESS)
            {
                return Status;
            }

            // Set the real size allocated.
            tableEntry->u64SizeInBytes = u64AllocatedSize;
        }
        // Add the data drive.
        else if (tableEntry->Type == kDriveTypeData)
        {
            // Only one data drive is allowed, but it will span multiple chip selects.
            if (foundDataDrive)
            {
                return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
            }
            
            foundDataDrive = true;
        }
    }

    // Add the data drive last.
    if (foundDataDrive)
    {
        Status = pMedia->addDataDrive(&u64AllocatedSize);
        if (Status != SUCCESS)
        {
            return Status;
        }

        // Set the real size allocated.
        tableEntry->u64SizeInBytes = u64AllocatedSize;
    }

    // Commit the data drives to the media.
    Status = pMedia->commitDataDrives();
    if (Status != SUCCESS)
    {
        return Status;
    }

    pDescriptor->eState = kMediaStateAllocated;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMediaDiscoverAllocation(LogicalMedia_t *pDescriptor)
{
    RtStatus_t Status;

    assert(pDescriptor);

    if (pDescriptor->bInitialized == false)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    
    if (pDescriptor->eState == kMediaStateErased)
    {
        // Cannot be discovered if erased.
        return ERROR_DDI_LDL_LMEDIA_MEDIA_ERASED;
    }

    // Get the media object.
    LbaNandMedia *pMedia = (LbaNandMedia *)pDescriptor->pMediaInfo;
    assert(pMedia);

    // Resurrect the drive info from the media.
    Status = pMedia->loadDrives();
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Fill in the logical drive information.
    LbaNandMedia::DriveIterator Iter(pMedia);
    LbaNandMedia::Drive *pDrive = Iter.next();
    MediaAllocationTableEntry_t *pCurMediaAllocationTableEntry;
    int iNumDrives = 1;             // We always have a data drive in each media
                                    // which by default is drive 0
    uint32_t u32MediaNumber = pDescriptor->u32MediaNumber;

    while (pDrive)
    {
        LogicalDriveShim * shim = new LogicalDriveShim(&g_LbaNandDriveApi);
        if (!shim)
        {
            return ERROR_OUT_OF_MEMORY;
        }
        
        LogicalDrive_t & LogicalDriveDesc = *(LogicalDrive_t *)(*shim);
        
        // Clear logical drive descriptor. Only fields not equal to zero
        // will be explicitly set.
        memset(&LogicalDriveDesc, 0, sizeof(LogicalDriveDesc));

//        LogicalDriveDesc.pApi = &g_LbaNandDriveApi;
        LogicalDriveDesc.Type = pDrive->getType();
        LogicalDriveDesc.bPresent = true;
        LogicalDriveDesc.u32Tag = (uint32_t)pDrive->getTag();
        LogicalDriveDesc.pLogicalMediaDescriptor = pDescriptor;

        // The media object was allocated by media init and will be deleted
        // by media shutdown.
        LogicalDriveDesc.pMediaInfo = (void *)pMedia;
        // The lba nand drive object has the same lifetime as the media object -
        // it will be deleted when the media object is deleted.
        LogicalDriveDesc.pDriveInfo = (void *)pDrive;

        LogicalDriveDesc.u32SectorSizeInBytes = pDrive->getSectorSize();
        LogicalDriveDesc.nativeSectorSizeInBytes = LogicalDriveDesc.u32SectorSizeInBytes;
        LogicalDriveDesc.nativeSectorShift = 0;
        
        LogicalDriveDesc.u32EraseSizeInBytes = LogicalDriveDesc.u32SectorSizeInBytes;
        LogicalDriveDesc.u32NumberOfSectors = pDrive->getSectorCount();
        LogicalDriveDesc.numberOfNativeSectors = LogicalDriveDesc.u32NumberOfSectors;

        LogicalDriveDesc.u64SizeInBytes = (
            (uint64_t)LogicalDriveDesc.u32NumberOfSectors
            * LogicalDriveDesc.u32SectorSizeInBytes);

        // Fill Up MediaAllocationTableEntry_t
        if (LogicalDriveDesc.Type == kDriveTypeData)
        {
            // The data drive always goes into the first media allocation table entry.
            pCurMediaAllocationTableEntry = &(g_MediaAllocationTable[u32MediaNumber].Entry[0]);
            pCurMediaAllocationTableEntry->u32DriveNumber = u32MediaNumber; // really?
        }
        else
        {
            // A system drive goes into the next available slot in the media allocation table.
            pCurMediaAllocationTableEntry = &(g_MediaAllocationTable[u32MediaNumber].Entry[iNumDrives]);
            pCurMediaAllocationTableEntry->u32DriveNumber = iNumDrives;

            // Increment the number of drives discovered in this media.
            iNumDrives++;
        }

        pCurMediaAllocationTableEntry->Type = LogicalDriveDesc.Type;
        pCurMediaAllocationTableEntry->u32Tag = LogicalDriveDesc.u32Tag;
        pCurMediaAllocationTableEntry->u64SizeInBytes = LogicalDriveDesc.u64SizeInBytes;
        pCurMediaAllocationTableEntry->bRequired = false;

        // Add our new drive.
        Status = DriveAdd(shim);
        if (Status != SUCCESS)
        {
            return Status;
        }

        pDrive = Iter.next();
    }

    g_MediaAllocationTable[u32MediaNumber].u32NumEntries = iNumDrives;
    pDescriptor->u32NumberOfDrives = g_MediaAllocationTable[pDescriptor->u32MediaNumber].u32NumEntries;
    pDescriptor->eState = kMediaStateAllocated;
    
    // Start automatically managing power save mode.
    pMedia->enablePowerSaveManagement(true);

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaGetMediaTable(LogicalMedia_t *pDescriptor,
                                     MediaAllocationTable_t **pTable)
{
    assert(pDescriptor);
    assert(pTable);

    if (pDescriptor->eState != kMediaStateAllocated)
    {
        return ERROR_DDI_NAND_LMEDIA_NOT_ALLOCATED;
    }

    // Return the address of the Media Allocation Table indexed to the proper Media Number.
    *pTable = &(g_MediaAllocationTable[pDescriptor->u32MediaNumber]);
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaShutdown(LogicalMedia_t *pDescriptor)
{
    RtStatus_t Status;

    assert(pDescriptor);

    if (pDescriptor->bInitialized == false)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    LbaNandMedia *pMedia = (LbaNandMedia *)pDescriptor->pMediaInfo;
    assert(pMedia);

    pMedia->enablePowerSaveManagement(false); 

    Status = pMedia->flush();

    // Shut down the HAL library.
    ddi_lba_nand_hal_shutdown();

    // Delete the media object.
    delete pMedia;
    
    // Delete the media mutex.
    tx_mutex_delete(&g_LbaNandMediaMutex);

    // Zero out the logical media descriptor.
    pDescriptor->u64SizeInBytes = 0;
    pDescriptor->PhysicalType = kMediaTypeNand;    // This is == 0 as well, why???
    pDescriptor->bWriteProtected = 0;
    pDescriptor->pMediaInfo = 0;
    pDescriptor->bInitialized = 0;
    pDescriptor->u32AllocationUnitSizeInBytes = 0;
    pDescriptor->eState = kMediaStateUnknown;

    // Reset the number of drives to its default value. This is necessary in order to be
    // able to re-init media without a reboot. Otherwise the drive table gets incrementally
    // bigger every discovery.
    //! \todo Should not be setting this global directly. The LDL should handle this for us
    //!     when we remove all of our drives (which needs to be added).
    g_wNumDrives = 0;
    
    // Clear the NAND secondary boot persistent bit. If this bit is set then we won't
    // be able to boot LBA-NAND.
    ddi_rtc_WritePersistentField(RTC_NAND_SECONDARY_BOOT, 0);

    return Status;
}

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
