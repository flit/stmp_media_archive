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
//! \file ddi_ldl_drive_get_info.c
//! \brief Device driver interface Logical Drive Layer API to get info for any drive type.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "errordefs.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/sectordef.h"
#include "ddi_media_internal.h"
#include "os/threadx/tx_api.h"
#include <string.h>

SerialNumber_t g_InternalMediaSerialNumber;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Handles the common logical drive info selectors.
//!
//! This function handles the common drive info selectors that can be
//! serviced by reading fields of the logical drive descriptor structure
//! alone.
//!
//! \param wLogDriveNumber     Logical Drive Number
//! \param wType               Type of information to return
//! \param pInfo               Buffer to fill with info
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
////////////////////////////////////////////////////////////////////////////////
RtStatus_t LogicalDrive::getInfo(uint32_t Type, void * pInfo)
{
    unsigned i;
    
    switch (Type)
    {
        case kDriveInfoSectorSizeInBytes:
            *((uint32_t *)pInfo) = m_u32SectorSizeInBytes;
            break;

        case kDriveInfoEraseSizeInBytes:
            *((uint32_t *)pInfo) = m_u32EraseSizeInBytes;
            break;

        case kDriveInfoSizeInBytes :
            *((uint64_t *)pInfo) = m_u64SizeInBytes;
            break;

        case kDriveInfoSizeInMegaBytes:
            *((uint32_t *)pInfo) = (m_u64SizeInBytes) >> 20;
            break;

        case kDriveInfoSizeInSectors:
            *((uint64_t *)pInfo) = m_u32NumberOfSectors;
            break;

        case kDriveInfoType:
            *((LogicalDriveType_t *)pInfo) = m_Type;
            break;

        case kDriveInfoTag:
            *((uint32_t *)pInfo) = m_u32Tag;
            break;

        case kDriveInfoIsWriteProtected:
            *((bool *)pInfo) = m_bWriteProtected;
            break;
        
        case kDriveInfoNativeSectorSizeInBytes:
            *(uint32_t *)pInfo = m_nativeSectorSizeInBytes;
            break;
        
        case kDriveInfoSizeInNativeSectors:
            *(uint32_t *)pInfo = m_numberOfNativeSectors;
            break;
        
        case kDriveInfoComponentVersion:
            memset(pInfo, 0, sizeof(SystemVersion_t));
            break;
        
        case kDriveInfoProjectVersion:
            memset(pInfo, 0, sizeof(SystemVersion_t));
            break;
        
        case kDriveInfoSectorOffsetInParent:
            *(uint32_t *)pInfo = m_pbsStartSector;
            break;
        
        case kDriveInfoMediaPresent:
            // Assume the media is present by default.
            *(bool *)pInfo = true;
            break;

        case kDriveInfoMediaChange:
            // The default is that the media cannot change.
            *((bool *)pInfo) = false;
            break;

        case kDriveInfoSizeOfSerialNumberInBytes:
            *((uint32_t *)pInfo) = g_InternalMediaSerialNumber.asciiSizeInChars * sizeof(uint32_t);                    
            break;     

        case kDriveInfoSizeOfRawSerialNumberInBytes:    // Raw SN ver added 
            *((uint32_t *)pInfo) = g_InternalMediaSerialNumber.rawSizeInBytes; //                      
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
        
        case kDriveInfoOptimalTransferSectorCount:
            *((uint32_t *)pInfo) = 1;
            break;

        default:
            return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveGetInfo(DriveTag_t tag, uint32_t Type, void * pInfo)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }
    else if (!drive->isInitialized() )
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Give the drive API a first shot.
    return drive->getInfo(Type, pInfo);
}

