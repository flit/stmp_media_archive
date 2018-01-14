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
//! \addtogroup ddi_lba_nand_drive
//! @{
//! \file ddi_lba_nand_drive.cpp
//! \brief This file contains the LBA-NAND drive functions.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "ddi_lba_nand_drive.h"
#include "components/sb_info/cmp_sb_info.h"
#include "hw/core/vmemory.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! Logical Drive API table for LBA NAND drive
const LogicalDriveApi_t g_LbaNandDriveApi = {
    LbaNandDriveInit,
    LbaNandDriveShutdown,
    LbaNandDriveGetInfoSize,
    LbaNandDriveGetInfo,
    LbaNandDriveSetInfo,
    LbaNandDriveReadSector,
    LbaNandDriveReadSector,
    LbaNandDriveWriteSector,
    LbaNandDriveErase,
    LbaNandDriveFlush,
    NULL    // pRepair
};

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandDriveInit(LogicalDrive_t *pDescriptor)
{
    // If we've already been initialized, just return SUCCESS.
    if (pDescriptor->bInitialized)
    {
        return SUCCESS;
    }

    // If not found during discovery, return an error.
    if (!pDescriptor->bPresent)
    {
        return ERROR_DDI_LDL_LDRIVE_MEDIA_NOT_ALLOCATED;
    }

    // Get the lba nand drive object.
    LbaNandMedia::Drive *pDrive = (LbaNandMedia::Drive *)pDescriptor->pDriveInfo;
    if (!pDrive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE;
    }

    // Verify that the drive type and tag match.
    if ((pDrive->getType() != pDescriptor->Type) ||
        ((uint32_t)pDrive->getTag() != pDescriptor->u32Tag))
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE;
    }

    pDescriptor->bInitialized = true;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveShutdown(LogicalDrive_t *pDescriptor)
{
    return LbaNandDriveFlush(pDescriptor);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t LbaNandDriveReadSector(LogicalDrive_t *pDescriptor,
                                  uint32_t u32SectorNumber,
                                  SECTOR_BUFFER *pSectorData)
{
    // Make sure we're initialized.
    if (!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Get the lba nand drive object.
    LbaNandMedia::Drive *pDrive = (LbaNandMedia::Drive *)pDescriptor->pDriveInfo;
    if (!pDrive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE;
    }

    return pDrive->readSector(u32SectorNumber, pSectorData);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveWriteSector(LogicalDrive_t *pDescriptor,
                                   uint32_t u32SectorNumber,
                                   const SECTOR_BUFFER *pSectorData)
{
    // Make sure we're initialized.
    if (!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we're not write protected.
    if (pDescriptor->bWriteProtected)
    {
        return ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED;
    }

    // Get the lba nand drive object.
    LbaNandMedia::Drive *pDrive = (LbaNandMedia::Drive *)pDescriptor->pDriveInfo;
    if (!pDrive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE;
    }

    return pDrive->writeSector(u32SectorNumber, pSectorData);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveErase(LogicalDrive_t *pDescriptor,
                             uint32_t wMagicNumber)
{
    // Make sure we're initialized.
    if (!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we're not write protected.
    if (pDescriptor->bWriteProtected)
    {
        return ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED;
    }

    // Get the lba nand drive object.
    LbaNandMedia::Drive *pDrive = (LbaNandMedia::Drive *)pDescriptor->pDriveInfo;
    if (!pDrive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE;
    }

    return pDrive->erase();
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveFlush(LogicalDrive_t *pDescriptor)
{
    // Make sure we're initialized.
    if (!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Get the lba nand drive object.
    LbaNandMedia::Drive *pDrive = (LbaNandMedia::Drive *)pDescriptor->pDriveInfo;
    if (!pDrive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE;
    }

    return pDrive->flush();
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveGetInfoSize(LogicalDrive_t *pDescriptor, uint32_t u32Type,
                                   uint32_t *pu32Size)
{
    // Allow common LDL code to handle the request.
    return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveGetInfo(LogicalDrive_t *pDescriptor, uint32_t u32Type,
                               void *pInfo)
{
    uint64_t compVersion = 0;
    uint64_t projVersion = 0;
    uint64_t dummy;

    if (!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (u32Type)
    {
        case kDriveInfoComponentVersion:
            if (pDescriptor->Type != kDriveTypeSystem)
            {
                return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
            }
            cmp_sb_info_GetVersionInfo(pDescriptor->u32Tag, &compVersion, &dummy);
            *((uint64_t *)pInfo) = compVersion;
            break;

        case kDriveInfoProjectVersion:
            if (pDescriptor->Type != kDriveTypeSystem)
            {
                return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
            }
            cmp_sb_info_GetVersionInfo(pDescriptor->u32Tag, &dummy, &projVersion);
            *((uint64_t *)pInfo) = projVersion;
            break;

        default:
            return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_data_drive.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveSetInfo(LogicalDrive_t *pDescriptor, uint32_t u32Type,
                               const void *pInfo)
{
    // Allow common LDL code to handle the request.
    return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
}

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
