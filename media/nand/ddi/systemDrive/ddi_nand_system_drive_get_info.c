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
//! \addtogroup ddi_nand_system_drive
//! @{
//! \file ddi_nand_system_drive_get_info.c
//! \brief Implementation of getInfo() API.
///////////////////////////////////////////////////////////////////////////////

#include <types.h>
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_system_drive.h"
#include "components/sb_info/cmp_sb_info.h"
#include "ddi_nand_system_drive_recover.h"

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Return specified information about the system drive.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//! \param[in] selector Type of info requested.
//! \param[out] pInfo Filled with requested data.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
////////////////////////////////////////////////////////////////////////////////
RtStatus_t nand::SystemDrive::getInfo(uint32_t Type, void * pInfo)
{
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    uint64_t version = 0;
    uint64_t dummy;
    switch (Type)
    {
        case kDriveInfoComponentVersion :
            cmp_sb_info_GetVersionInfo(m_pRegion->m_wTag, &version, &dummy);
            *((uint64_t *)pInfo) = version;
            break;

        case kDriveInfoProjectVersion :
            cmp_sb_info_GetVersionInfo(m_pRegion->m_wTag, &dummy, &version);
            *((uint64_t *)pInfo) = version;
            break;
        
        case kDriveInfoNandSystemDriveRecoveryEnabled:
            *((bool *)pInfo) = m_media->getRecoveryManager()->isRecoveryEnabled();
            break;

        default:
            return LogicalDrive::getInfo(Type, pInfo);
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}


