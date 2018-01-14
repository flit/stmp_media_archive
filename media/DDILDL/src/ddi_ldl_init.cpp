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
//! \file ddi_ldl_init.c
//! \brief Device Driver Interface to the Logical Drive Layer's init calls.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "error.h"
#include "ddi_media_internal.h"
#include "drivers/media/ddi_media.h"
#include "os/thi/os_thi_stack_context.h"
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

// TODOFSINITSHUTDOWN   Consider moving declaration & comment
// to \application\framework\sdk_os_media\src\sdk_os_media_sdmmc.c
// This boolean state global should be treated as framework and storage subsystem private.
// It is for external drive's state or its FS init state and is set true in file named above
// when an external media is present with drive intialized and
// in fwk configurations for which framework handles the FS, true also indicates that
// the FS is init for the drive. For app use, instead of reading this framework global,
// Use macro accessors in ddi_media.h (media or drive level) & FSapi.h (drive's FS status).
// See also ExternalMMCMediaPresent() in fsapi.h / readdevicerecord.c for an secondary alternative
// which does a FAT type check on the drive and attempts to acquire the ssp/mmc bus.
// Note that HL cases uses scsi.h state machine structure values for drive status
// and MTP uses g_eMtpExternalStoreState to keep & check the drive's FS init status.
//
bool g_bFrameworkExternalDriveOrFsInit = false;

LdlInfo g_ldlInfo = {0};

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaInit(uint32_t u32LogMediaNumber)
{
    g_bFrameworkExternalDriveOrFsInit = false;

    if (u32LogMediaNumber > MAX_LOGICAL_MEDIA)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    
    // Call the factory function provided in the media definition.
    const MediaDefinition_t * def = &g_mediaDefinition[u32LogMediaNumber];
    LogicalMedia * media = def->m_factoryFunction(def);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }

    assert(g_ldlInfo.m_media[u32LogMediaNumber] == NULL);
    g_ldlInfo.m_media[u32LogMediaNumber] = media;
    g_ldlInfo.m_mediaCount++;

    return media->init();
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaAllocate(uint32_t u32LogMediaNumber, MediaAllocationTable_t * pMediaTable)
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

    return media->allocate(pMediaTable);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaDiscoverAllocation(uint32_t u32LogMediaNumber)
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

    return media->discover();
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveInit(DriveTag_t tag)
{
    RtStatus_t RetValue;
    LogicalDrive * drive = DriveGetDriveFromTag(tag);

    if (drive)
    {
        RetValue = drive->init();

        // Keep track of whether the init succeeded.
        drive->setDidFailInit(RetValue != SUCCESS);
    }
    else
    {
        RetValue = ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }

    return RetValue;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveInitAll(void)
{
    DriveIterator_t iter;
    RtStatus_t status = DriveCreateIterator(&iter);
    if (status == SUCCESS)
    {
        // Iterate over all drives.
        DriveTag_t tag;
        while (DriveIteratorNext(iter, &tag) == SUCCESS)
        {
            // Init only uninitialized drives. If a drive failed init then we don't want
            // to try initing it again.
            DriveState_t state = DriveGetState(tag);
            if (state == kDriveUninitialized)
            {
                DriveInit(tag);
            }
        }

        DriveIteratorDispose(iter);
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media_internal.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveAdd(LogicalDrive * newDrive)
{
    LogicalDrive ** drive = NULL;

    // Just copy the new drive data into the next free slot on the drive array
    // and update the drive count.
    drive = DriveFindEmptyEntry();
    if ( drive )
    {
        *drive = newDrive;
        g_ldlInfo.m_driveCount++;
        
        // Update the number of drives for the media.
        LogicalMedia * media = newDrive->getMedia();
        if (media)
        {
            media->setNumberOfDrives(media->getNumberOfDrives() + 1);
        }

        return SUCCESS;
    }
    else
    {
        return ERROR_GENERIC;
    }
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media_internal.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveRemove(DriveTag_t driveToRemove)
{
    unsigned i;
    LogicalDrive ** drive = g_ldlInfo.m_drives;

    // Scan all drives for the tag.
    for (i = 0; i < MAX_LOGICAL_DRIVES; i++, drive++)
    {
        LogicalDrive * thisDrive = *drive;
        if (thisDrive && thisDrive->getTag() == driveToRemove)
        {
            // Update the number of drives for the media.
            LogicalMedia * media = thisDrive->getMedia();
            if (media)
            {
                media->setNumberOfDrives(media->getNumberOfDrives() - 1);
            }
            
            delete thisDrive;

            // Clear the entry in the drive array.
            *drive = NULL;
            g_ldlInfo.m_driveCount--;

            return SUCCESS;
        }
    }

    // No drive with this tag.
    return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
}

LogicalMedia::LogicalMedia()
:   m_u32MediaNumber(0),
    m_bInitialized(false),
    m_eState(kMediaStateUnknown),
    m_bAllocated(false),
    m_bWriteProtected(false),
    m_isRemovable(false),
    m_u32NumberOfDrives(0),
    m_u64SizeInBytes(0),
    m_u32AllocationUnitSizeInBytes(0),
    m_PhysicalType(kMediaTypeNand)
{
}

LogicalMedia::~LogicalMedia()
{
}

LogicalDrive::LogicalDrive()
:   m_bInitialized(false),
    m_bFailedInit(false),
    m_bPresent(false),
    m_bErased(false),
    m_bWriteProtected(false),
    m_u32NumberOfSectors(0),
    m_Type(kDriveTypeUnknown),
    m_u32Tag(0),
    m_u64SizeInBytes(0),
    m_u32SectorSizeInBytes(0),
    m_nativeSectorSizeInBytes(0),
    m_numberOfNativeSectors(0),
    m_nativeSectorShift(0),
    m_u32EraseSizeInBytes(0),
    m_pbsStartSector(0),
    m_logicalMedia(NULL)
{
}

LogicalDrive::~LogicalDrive()
{
}

//! @}

