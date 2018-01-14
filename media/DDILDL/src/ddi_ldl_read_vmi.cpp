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
//! \file ddi_ldl_read_vmi.c
//! \brief Backing store read function.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/ddi_media.h"
#include "ddi_media_internal.h"
#include "error.h"

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveReadSectorForVMI(DriveTag_t tag, uint32_t u32SectorNumber, SECTOR_BUFFER * pSectorData)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    
    // Drive must exist and must be a system drive.
    if (!drive || drive->getType() != kDriveTypeSystem)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }
    else if (!drive->isInitialized())
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    return drive->readSectorForVMI(u32SectorNumber, pSectorData);
}

RtStatus_t LogicalDrive::readSectorForVMI(uint32_t sector, SECTOR_BUFFER * buffer)
{
    return readSector(sector, buffer);
}

//! @}

