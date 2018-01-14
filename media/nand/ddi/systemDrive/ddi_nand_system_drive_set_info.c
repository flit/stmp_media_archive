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
//! \addtogroup ddi_nand_system_drive
//! @{
//! \file ddi_nand_system_drive_set_info.c
//! \brief Contains a function to set certain information about the data drive.
//!
///////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_system_drive.h"
#include "ddi_nand_system_drive_recover.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief System drive API to set logical drive information.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
////////////////////////////////////////////////////////////////////////////////
RtStatus_t SystemDrive::setInfo(uint32_t Type, const void * pInfo)
{
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (Type)
    {
        case kDriveInfoTag :
            m_u32Tag = *((uint32_t *)pInfo);
            break;
        
        case kDriveInfoNandSystemDriveRecoveryEnabled:
            m_media->getRecoveryManager()->setIsRecoveryEnabled(*((bool *)pInfo));
            break;

        default:
            return LogicalDrive::setInfo(Type, pInfo);
    }

    return SUCCESS;
            
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

