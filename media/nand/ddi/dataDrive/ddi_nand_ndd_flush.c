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
//! \addtogroup ddi_nand_data_drive
//! @{
//! \file ddi_nand_ndd_flush.c
//! \brief This file handles flushing and shutdown of the data drive.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "Mapper.h"
#include "ddi_nand.h"
#include "drivers/media/include/ddi_media_internal.h"
#include <string.h>
#include "NssmManager.h"
#include "DeferredTask.h"

using namespace nand;

//! \brief Optional to make DataDrive::flush() actually perform a flush.
//!
//! By default, Media::flushDrives() is the only API that will actually flush
//! the NSSMs and mapper. Set this macro to 1 to make DataDrive::flush() do the same.
//! This is disabled by default because the drive flush API is called every time Fflush()
//! is invoked, which ends up being way too often. Because NSSM flushes cause paired
//! blocks to be merged, the flush can actually take quite some time, and have further
//! negative impact on performance as blocks have to be split again.
#define ENABLE_DATA_DRIVE_FLUSH 0

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Flush the data drive.
//!
//! Currently does nothing.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//!
//! \retval SUCCESS
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::flush()
{
    // Make sure we're initialized
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

#if ENABLE_DATA_DRIVE_FLUSH
    DdiNandLocker locker;

    // Flush NSSMs and the mapper.
    return m_media->flushDrives();
#else
    return SUCCESS;
#endif
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Shuts down the specified data drive.
//!
//! Do shutdown steps which are only to be called once, during shutdown.
//! This includes flushing the non-sequential sectors map, the zone map, and
//! the phy map to the NAND.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//!
//! \return SUCCESS or an error from flushing the mapper.
//! \retval SUCCESS
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::shutdown()
{
    // Make sure we're initialized
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // Must drain the deferred queue just in case there are any tasks that apply to us.
    m_media->getDeferredQueue()->drain();

    // Flush everything.
    m_media->flushDrives();

    // Free the region pointer array.
    delete [] m_ppRegion;
    m_ppRegion = NULL;
    m_u32NumRegions = 0;

    // Delete transaction semaphore.
    tx_semaphore_delete(&m_transactionSem);

    m_bInitialized = false;

	return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
