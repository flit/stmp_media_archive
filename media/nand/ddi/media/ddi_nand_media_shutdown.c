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
//! \addtogroup ddi_nand_media
//! @{
//! \file ddi_nand_media_shutdown.c
//! \brief This file contains code to shutdown the NAND media layer.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include <string.h>
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_media.h"
#include "ddi_nand_ddi.h"
#include "Mapper.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand.h"
#include "hw/core/vmemory.h"
#include "hw/power/hw_power.h"
#include <stdlib.h>
#include "DeferredTask.h"
#include "ddi_nand_system_drive_recover.h"
#include "NonsequentialSectorsMap.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Flush the NAND drives
//!
//! This function is responsible for flushing the drives on the nand media.
//!
//! \return Status of call or error.
//!
//! \see ddi_nand_flush_data_drives()
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::flushDrives()
{
    // Flush NSSMs.
    if (m_nssmManager)
    {
        m_nssmManager->flushAll();
    }
    
    // Flush the mapper.
    if (m_mapper && m_mapper->isInitialized())
    {
        m_mapper->flush();
    }
    
    return SUCCESS;
}

void  Media::deleteRegions()
{
    // Delete each of the valid Region objects.
    unsigned i;
    for (i=0; i < m_iNumRegions; ++i)
    {
        if (m_pRegionInfo[i])
        {
            delete m_pRegionInfo[i];
            m_pRegionInfo[i] = NULL;
        }
    }

    m_iNumRegions = 0;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Shutdown the NAND Media
//!
//! This function is responsible for shutting down the NAND media. All memory
//! allocated by the NAND driver is freed. Even the NAND HAL is shutdown, which
//! in turn shuts down the GPMI driver.
//!
//! \return Status of call or error.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::shutdown()
{
    // Don't shutdown if we aren't initialized yet.
    if (!m_bInitialized)
    {
        return SUCCESS;
    }
    
    // Flush everything.
    flushDrives();
    
    // First thing is to wait until all deferred tasks are finished.
    m_deferredTasks->drain();
    
    // Clear Serial number (why??)
    memset((void *)&g_InternalMediaSerialNumber, 0, sizeof(SerialNumber_t));

    // Free the NSSM map memory.
    if (m_nssmManager)
    {
        delete m_nssmManager;
        m_nssmManager = NULL;
    }
    
    // Shut down the mapper.
    if (m_mapper)
    {
        m_mapper->shutdown();
        delete m_mapper;
        m_mapper = NULL;
    }
    
    // Shut down the system drive recovery manager.
    if (m_recoveryManager)
    {
        delete m_recoveryManager;
        m_recoveryManager = NULL;
    }
    
    // Shut down the deferred tasks.
    if (m_deferredTasks)
    {
        delete m_deferredTasks;
        m_deferredTasks = NULL;
    }

    // Dispose of bad block table memory.
    m_globalBadBlockTable.release();

    // Zero out the LogicalMedia fields.
    m_u64SizeInBytes = 0;
    m_PhysicalType = kMediaTypeNand;
    m_bWriteProtected = 0;
    m_bInitialized = 0;
    m_u32AllocationUnitSizeInBytes = 0;
    m_eState = kMediaStateUnknown;

    // Free regions.
    deleteRegions();

    delete [] m_pRegionInfo;
    m_pRegionInfo = NULL;
    
    // Shut down the HAL library.
    NandHal::shutdown();
    
#ifdef RTOS_THREADX
    // Destroy our synchronization objects.
    tx_mutex_delete(&g_NANDThreadSafeMutex);
#endif

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}



