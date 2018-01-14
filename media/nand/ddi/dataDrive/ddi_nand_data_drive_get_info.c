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
//! \file ddi_nand_data_drive_get_info.c
//! \brief Contains a function to get certain information about the data drive.
///////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "Mapper.h"
#include "NssmManager.h"
#include "VirtualBlock.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Return specified information about the data drive.
//!
//! \param[in] selector Type of info requested.
//! \param[out] pInfo Filled with requested data.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DataDrive::getInfo(uint32_t selector, void * pInfo)
{
    int32_t i;
    uint32_t u32SectorsPerBlock = NandHal::getParameters().wPagesPerBlock;
    uint32_t temp;

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (selector)
    {
        case kDriveInfoSizeInSectors:
            temp = m_u32NumberOfSectors;
            temp /= u32SectorsPerBlock;
            temp *= u32SectorsPerBlock; 
            *((uint64_t *)pInfo) = temp;
            break;

        case kDriveInfoSizeOfSerialNumberInBytes:
            *((uint32_t *)pInfo) = g_InternalMediaSerialNumber.asciiSizeInChars * sizeof(uint32_t);
            break;

        case kDriveInfoSizeOfRawSerialNumberInBytes:    // Raw SN ver added
            *((uint32_t *)pInfo) = g_InternalMediaSerialNumber.rawSizeInBytes;
            break;

        case kDriveInfoSerialNumber:    //returns SN buffer as unpacked ascii (in least sig bytes)
            for (i = 0; i < g_InternalMediaSerialNumber.asciiSizeInChars; i++)
            {
                ((uint32_t *)pInfo)[i] = g_InternalMediaSerialNumber.ascii[i];
            }
            break;

        case kDriveInfoRawSerialNumber:    //returns SN buffer as packed Raw hex nibbles
            for (i = 0; i < g_InternalMediaSerialNumber.rawSizeInBytes; i++)
            {
                ((uint8_t *)pInfo)[i] = g_InternalMediaSerialNumber.raw[i];
            }
            break;

        case kDriveInfoMediaPresent:    // Always TRUE
            *((bool *)pInfo) = true;
            break;

        case kDriveInfoMediaChange:     // Always FALSE - Can't change.
            *((bool *)pInfo) = false;
            break;
        
        // Return the number of non-sequential sector maps allocated for this drive. Actually,
        // this applies to all data-type drives, not just this one. All NSSMs are shared between
        // all drives that use them.
        case kDriveInfoNSSMCount:
            *(uint32_t *)pInfo = g_nandMedia->getNssmManager()->getBaseNssmCount();
            break;
        
        // The optimal number of sectors in a multisector transaction is the number of planes
        // that we're using for virtual blocks.
        case kDriveInfoOptimalTransferSectorCount:
        {
            uint32_t eTransferType;
            eTransferType = MediaGetInfoTyped<uint32_t >(g_nandMedia->m_u32MediaNumber, kMediaInfoExpectedTransferActivity);
            // Get the profile type from media info. For player profile default use plane count = 1
            if((TransferActivityType_t)(eTransferType)==kTransferActivity_Random)
                *(uint32_t *)pInfo = 1;
            else
                *(uint32_t *)pInfo = VirtualBlock::getPlaneCount();
            break;
        }
            
        default:
            return LogicalDrive::getInfo(selector, pInfo);
  }

  return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
