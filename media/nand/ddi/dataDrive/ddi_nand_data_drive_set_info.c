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
//! \addtogroup ddi_nand_data_drive
//! @{
//! \file ddi_nand_data_drive_set_info.c
//! \brief Contains a function to set certain information about the data drive.
///////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "NssmManager.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Set specified information about the data drive
//!
//! Only a small subset of drive info selectors can be modified. Attempting
//! to set a selector that cannot be changed will result in an error.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//! \param[in] wType Type of info requested: Tag, Component Version, Project
//!     Version, etc. 
//! \param[in] pInfo Pointer to data to set.
//!
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED Drive is not initialised.
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE Cannot modify the requested
//!     data field.
//! \retval SUCCESS Data was set successfully.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::setInfo(uint32_t Type, const void * pInfo)
{
    if (Type != kDriveInfoSectorSizeInBytes && !m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
        
    switch (Type)
    {
        // Change the number of non-sequential sector maps allocated for this drive.
        case kDriveInfoNSSMCount:
        {
            uint32_t newCount;
            RtStatus_t result;
            
            DdiNandLocker locker;
            
            newCount = *(uint32_t *)pInfo;
            result = m_media->getNssmManager()->allocate(newCount);
            
            return result;
        }
        
        default:
            return LogicalDrive::setInfo(Type, pInfo);
    }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
