///////////////////////////////////////////////////////////////////////////////
// Copyright (c) SigmaTel, Inc. All rights reserved.
// 
// SigmaTel, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of SigmaTel, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup media_buf_mgr
//! @{
//! \file media_buffer_manager_init.c
//! \brief Initialisation code for the buffer manager.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "media_buffer_manager_internal.h"
#include "drivers/media/ddi_media_errordefs.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! \brief Number of sector buffers to pre-allocate.
#define SECTOR_BUFFER_COUNT (0)

//! \brief Number of auxiliary buffers to pre-allocate.
#define AUX_BUFFER_COUNT (0)

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

#if SECTOR_BUFFER_COUNT > 0
#pragma alignvar(32)
//! \brief Pre-allocated sector buffers.
static SECTOR_BUFFER s_sectorBuffers[SECTOR_BUFFER_COUNT][NOMINAL_DATA_SECTOR_ALLOC_SIZE];
#endif

#if AUX_BUFFER_COUNT > 0
#pragma alignvar(32)
//! \brief Pre-allocated auxiliary buffers.
static SECTOR_BUFFER s_auxBuffers[AUX_BUFFER_COUNT][NOMINAL_AUXILIARY_SECTOR_ALLOC_SIZE];
#endif

//! Name of the media buffer manager mutex.
const char kMediaBufferMutexName[] = "bm";

//! Name of the application timer used to time out temporary buffers.
const char kMediaBufferTimeoutTimerName[] = "bm:to";

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

// See media_buffer_manager.h for the documentation for this function.
RtStatus_t media_buffer_init(void)
{
    UINT txStatus;
#if (SECTOR_BUFFER_COUNT > 0) || (AUX_BUFFER_COUNT > 0)
    int i;
#endif    

    // Only initialise once.
    if (g_mediaBufferManagerContext.isInited)
    {
        return SUCCESS;
    }
    
    // Create the mutex protecting the global context.
    txStatus = tx_mutex_create(&g_mediaBufferManagerContext.mutex, (CHAR *)kMediaBufferMutexName, TX_INHERIT);
    if (txStatus != TX_SUCCESS)
    {
        return os_thi_ConvertTxStatus(txStatus);
    }
    
    // Create the timeout timer.
    txStatus = tx_timer_create(&g_mediaBufferManagerContext.timeoutTimer, (CHAR *)kMediaBufferTimeoutTimerName, media_buffer_timeout, 0, 0, 0, TX_NO_ACTIVATE);
    if (txStatus != TX_SUCCESS)
    {
        return os_thi_ConvertTxStatus(txStatus);
    }
    
    // Init the rest.
    g_mediaBufferManagerContext.nextTimeout = NO_NEXT_TIMEOUT;
    g_mediaBufferManagerContext.bufferToDispose = NO_NEXT_TIMEOUT;
    g_mediaBufferManagerContext.isInited = true;
    
#if SECTOR_BUFFER_COUNT > 0
    // Add some buffers.
    for (i=0; i < SECTOR_BUFFER_COUNT; ++i)
    {
        media_buffer_add(kMediaBufferType_Sector, kMediaBufferFlag_None, s_sectorBuffers[i]);
    }
#endif
    
#if AUX_BUFFER_COUNT > 0
    for (i=0; i < AUX_BUFFER_COUNT; ++i)
    {
        media_buffer_add(kMediaBufferType_Auxiliary, kMediaBufferFlag_None, s_auxBuffers[i]);
    }
#endif
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

//! @}


