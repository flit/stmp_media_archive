///////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_lba_nand_media
//! @{
//
// Copyright (c) 2008 SigmaTel, Inc.
// 
//! \file ddi_lba_nand_media_definition.c
//! \brief Default media definition file for LBA NAND.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/include/ddi_media_internal.h" //! \todo malinclusion
#include "drivers/media/nand/include/ddi_nand.h" //! \todo malinclusion
#include "os/filesystem/filesystem.h"

#if defined(EXTERNAL_MEDIA_SDMMC)
#include "drivers/media/mmc/ddi/include/ddi_mmc_ddi.h" //! \todo malinclusion
#include "drivers/media/mmc/ddi/common/include/ddi_mmc_common.h"
#endif

///////////////////////////////////////////////////////////////////////////////
// External References
///////////////////////////////////////////////////////////////////////////////

extern LogicalDriveApi_t g_LbaNandDriveApi;
extern LogicalMediaApi_t g_LbaNandMediaApi;

///////////////////////////////////////////////////////////////////////////////
// Local Definitions
///////////////////////////////////////////////////////////////////////////////

#ifndef MMC
    #define NUM_LOGICAL_MEDIA       1
#else
    #define NUM_LOGICAL_MEDIA       2     // 1 external drive if MMC.
#endif

#ifdef NAND_IMAGER_UTILITY
    // Reserve 4MB for each system drive
    #define SYSTEM_DRIVE_SIZE_4MB  (1024*1024*4)
#endif

/////////////////////////////////////////////////////////////////////////////////
//  Currently set the minimun drive size to 8 Blocks.
/////////////////////////////////////////////////////////////////////////////////

#define MIN_DATA_DRIVE_SIZE ( 8 )

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

LogicalMedia_t g_Media[ NUM_LOGICAL_MEDIA ] =
{
    {
        &g_LbaNandMediaApi,
        0,                      // wMediaNumber
        FALSE,                  // bInitialized
        kMediaStateUnknown,      // eState
        FALSE,                  // bAllocated
        FALSE,                  // bWriteProtected
        false,                  // isRemovable
        0,                      // wNumberOfDrives
        0,                      // dwSizeInBytes
        0,                      // wAllocationUnitSizeInBytes
        kMediaTypeNand,          // PhysicalType
        NULL                    // pMediaInfo
    },
#ifdef EXTERNAL_MEDIA_SDMMC
    {
        &MmcMediaApi,           // pApi
        1,                      // wMediaNumber
        FALSE,                  // bInitialized
        kMediaStateUnknown,      // eState
        FALSE,                  // bAllocated
        FALSE,                  // bWriteProtected
        true,                   // isRemovable
        0,                      // wNumberOfDrives
        0,                      // dwSizeInBytes
        0,                      // wAllocationUnitSizeInBytes
        kMediaTypeMMC,           // PhysicalType
        NULL                    // pMediaInfo
    }
#endif
};

MediaAllocationTable_t g_MediaAllocationTable[NUM_LOGICAL_MEDIA] =
{
#ifdef NAND_IMAGER_UTILITY
    {
        5,
        { 1, kDriveTypeData,   DRIVE_TAG_DATA,                              0, FALSE },
        { 2, kDriveTypeHidden, DRIVE_TAG_DATA_HIDDEN,                       0, FALSE },      
        { 3, kDriveTypeHidden, DRIVE_TAG_DATA_HIDDEN_2,                     0, FALSE },      
        { 0, kDriveTypeSystem, DRIVE_TAG_BOOTMANAGER_S, SYSTEM_DRIVE_SIZE_4MB, FALSE },
        { 4, kDriveTypeSystem, DRIVE_TAG_RESOURCE_BIN,  SYSTEM_DRIVE_SIZE_4MB, FALSE }  
    },
#else
    {
        6,
        0, kDriveTypeSystem, DRIVE_TAG_BOOTMANAGER_S,        0x2DD2,  FALSE,
        1, kDriveTypeData,   DRIVE_TAG_DATA,                 0,       FALSE,
        2, kDriveTypeHidden, DRIVE_TAG_DATA_HIDDEN,          0,       FALSE,  
        3, kDriveTypeHidden, DRIVE_TAG_DATA_HIDDEN_2,        0,       FALSE,  
        4, kDriveTypeSystem, DRIVE_TAG_BOOTMANAGER2_S,       0x2DD2,  FALSE,
        5, kDriveTypeSystem, DRIVE_TAG_BOOTLET_S,            0,       FALSE,
    }
#endif
#ifdef EXTERNAL_MEDIA_SDMMC
    ,
    {
        1,
        0,kDriveTypeData,0,50176,FALSE,
    }
#endif
};

// Table of drive letter to drive tag associations used by the filesystem.
const FileSystemDriveAssociation_t g_fsDriveAssociations[] = {
        { 'a', DRIVE_TAG_DATA },
        { 'c', DRIVE_TAG_DATA_EXTERNAL },
        { 0 }
    };

///////////////////////////////////////
// Setup the global Media structs
///////////////////////////////////////

const uint32_t g_wNumMedia = NUM_LOGICAL_MEDIA;

const int g_MinDataDriveSize = MIN_DATA_DRIVE_SIZE;

#ifdef RTOS_THREADX
    TX_MUTEX g_NANDThreadSafeMutex;
#endif

LogicalDrive_t g_Drive[MAX_LOGICAL_DRIVES];

// Start with no drives.
uint32_t g_wNumDrives = 0;

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
