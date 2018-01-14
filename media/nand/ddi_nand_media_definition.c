////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2003
//
// Filename: ddi_nand_media_definition.c
// Description: Default media definition file for NAND.
////////////////////////////////////////////////////////////////////////////////

#include <types.h>
#include <drivers/ddi_media.h>
#include "../include/ddi_media_internal.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "drivers/media/mmc/ddi_mmc.h"

////////////////////////////////////////////////////////////////////////////////
// defs
////////////////////////////////////////////////////////////////////////////////


#ifndef MMC
    #define NUM_LOGICAL_MEDIA       1
#else
    #define NUM_LOGICAL_MEDIA       2     // 1 external drive if MMC.
#endif

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

const MediaDefinition_t g_mediaDefinition[] =
    {
        {
            0,                      // Media 0
#if defined(INTERNAL_MEDIA_SDMMC)
            mmc_media_factory,      // MMC factory function
            kMediaTypeMMC,          // PhysicalType
#else
            nand_media_factory,     // NAND factory function
            kMediaTypeNand,         // PhysicalType
#endif
            false                   // Not removable
        },
#if defined(EXTERNAL_MEDIA_SDMMC)
        {
            1,                      // Media 1
            kMediaTypeMMC,          // PhysicalType
            true                    // Removable
        }
#endif
    };

#ifdef RTOS_THREADX
    TX_MUTEX g_NANDThreadSafeMutex;
#endif

