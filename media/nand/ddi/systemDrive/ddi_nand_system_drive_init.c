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
//! \file ddi_nand_system_drive_init.c
//! \brief This file handles the intialization of the system drive.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand_system_drive.h"
#include "drivers/media/sectordef.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "ddi_nand_media.h"
#include "components/sb_info/cmp_sb_info.h"
#include "hw/core/vmemory.h"
#include "ddi_nand_system_drive_recover.h"
#include <string.h>

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

SystemDrive::SystemDrive(Media * media, Region * region)
:   LogicalDrive(),
    m_media(media),
    m_wStartSector(0),
    m_pRegion(0),
    m_isBeingRewritten(false),
    m_logicalBlockBeingRefreshed(-1)
{
    m_bInitialized = false;
    m_bPresent = true;
    m_bErased = false;
    m_bWriteProtected = false;
    m_Type = region->m_eDriveType;
    m_u32Tag = region->m_wTag;
    m_logicalMedia = media;

    // For Samsung 4K page NANDS with 128 bytes of metadata, we force system drives
    // to only have 2K sectors. This is necessary because the 37xx boot ROM cannot
    // shift and mask to reach the second 2K worth of data in the 4K page.
    // Also, all devices using BCH hold only 2K of data in firmware pages.
    m_u32SectorSizeInBytes = NandHal::getParameters().firmwarePageDataSize;
    m_nativeSectorSizeInBytes = m_u32SectorSizeInBytes;
    m_nativeSectorShift = 0;
    m_u32EraseSizeInBytes = m_u32SectorSizeInBytes * NandHal::getParameters().wPagesPerBlock;
    m_u32NumberOfSectors = (region->m_iNumBlks - region->getBadBlockCount())    // Number of Good Blocks
        * (NandHal::getParameters().wPagesPerBlock);
    m_numberOfNativeSectors = m_u32NumberOfSectors;
    m_u64SizeInBytes = (uint64_t)m_u32NumberOfSectors * m_u32SectorSizeInBytes;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Initialises a system drive.
//!
//! \param[in] pDescriptor Pointer to the logical drive descriptor structure.
//!
//! \return Returns either SUCCESS or error code if fail.
//!
//! \pre Media init must have retrieved all bad block information from the
//!     NAND prior to the invocation of this function.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t SystemDrive::init()
{
    Region * pLocalRegionInfo;
    bool bFoundRegion = FALSE;

    // If we've already been initialized, just return SUCCESS.
    if (m_bInitialized)
    {
        return SUCCESS;
    }
    
    DdiNandLocker locker;
    
    // First I need to find in which region is the drive
    Region::Iterator it = m_media->createRegionIterator();
    while ((pLocalRegionInfo = it.getNext()))
    {
        if (m_Type == pLocalRegionInfo->m_eDriveType && m_u32Tag == pLocalRegionInfo->m_wTag)
        {
            // We found the drive region
            bFoundRegion = TRUE;

            // Save the region pointer so I can access it faster
            m_pRegion = reinterpret_cast<SystemRegion *>(pLocalRegionInfo);
            m_pRegion->m_pLogicalDrive = this;

            // Calculates the start sector for the current drive related to the chip
            m_wStartSector = m_pRegion->m_nand->blockToPage(m_pRegion->m_iStartPhysAddr);

            break;
        }
    }

    if (bFoundRegion == FALSE)
    {
        // Failed to find a region for this drive.
        m_bInitialized = FALSE;

        return(ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE);
    }
    
    // Make sure the region's tag is set correctly.
    m_pRegion->m_wTag = m_u32Tag;

    m_bPresent = true;
    m_bInitialized = true;
    
    // Tell the recovery manager about us.
    m_media->getRecoveryManager()->addDrive(this);

    return SUCCESS;
}

RtStatus_t SystemDrive::flush()
{
    return SUCCESS;
}

RtStatus_t SystemDrive::shutdown()
{
    // Must drain the deferred queue just in case there are any tasks that apply to us.
    m_media->getDeferredQueue()->drain();

    // Tell the recovery manager that we can longer be used.
    m_media->getRecoveryManager()->removeDrive(this);
    
    m_bInitialized = false;
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

