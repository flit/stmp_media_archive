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
// Filename: ddi_mmc_media_definition.c
// Description: Default media definition file for MMC. Used for DDILDL unit test.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "drivers/media/mmc/ddi_mmc.h"
#include "os/filesystem/filesystem.h"
#include "os/thi/os_thi_api.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#if defined(EXTERNAL_MEDIA_SDMMC)
    #define NUM_LOGICAL_MEDIA   (2)     // Internal=NAND/SD/LBA, External=MMC
#else
    #define NUM_LOGICAL_MEDIA   (1)     // Internal=NAND/SD/LBA, No External
#endif

/////////////////////////////////////////////////////////////////////////////////
//  Currently set the minimum drive size to 8 Blocks.
/////////////////////////////////////////////////////////////////////////////////

#define MIN_DATA_DRIVE_SIZE ( 8 )

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

const MediaDefinition_t g_mediaDefinition[] =
    {
        {
            0,                      // Media 0
#if defined(INTERNAL_MEDIA_SDMMC)
            mmc_media_factory,
            kMediaTypeMMC,          // PhysicalType
#else
            nand_media_factory,
            kMediaTypeNand,         // PhysicalType
#endif
            false                   // Not removable
        },
#if defined(EXTERNAL_MEDIA_SDMMC)
        {
            1,                      // Media 1
            mmc_media_factory,
            kMediaTypeMMC,          // PhysicalType
            true                    // Removable
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

const int g_MinDataDriveSize = MIN_DATA_DRIVE_SIZE;

#ifdef RTOS_THREADX
    TX_MUTEX g_NANDThreadSafeMutex;
#endif
