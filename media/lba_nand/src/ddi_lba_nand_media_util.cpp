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
//! \addtogroup ddi_lba_nand_media
//! @{
//! \file ddi_lba_nand_media_util.cpp
//! \brief Lba nand device driver media utility functions.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "ddi_lba_nand_media.h"
#include "drivers/rtc/ddi_rtc_persistent.h"

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaGetInfo(LogicalMedia_t *pDescriptor, uint32_t u32Type,
                               void *pInfo)
{
    int i;
    bool bSystemDrive = false;

    if (u32Type == kMediaInfoIsInitialized)
    {
        *((bool *)pInfo) = pDescriptor->bInitialized;
        return(SUCCESS);
    }

    if (!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Get the media object.
    LbaNandMedia *pMedia = (LbaNandMedia *)pDescriptor->pMediaInfo;
    if (!pMedia)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    switch (u32Type)
    {
        case kMediaInfoNumberOfDrives:
            *((uint32_t *)pInfo) = pDescriptor->u32NumberOfDrives;
            break;
                        
        case kMediaInfoSizeInBytes:
            *((uint64_t *)pInfo) = pDescriptor->u64SizeInBytes;            
            break;
            
        case kMediaInfoAllocationUnitSizeInBytes:
            *((uint32_t *)pInfo) = pDescriptor->u32AllocationUnitSizeInBytes;
            break;

        case kMediaInfoIsInitialized:
            *((bool *)pInfo) = true;
            break;
            
        case kMediaInfoMediaState:
            *((MediaState_t *)pInfo) = (pDescriptor->eState);
            break;

        case kMediaInfoIsWriteProtected:
            *((bool *)pInfo) = pDescriptor->bWriteProtected;
            break;

        case kMediaInfoPhysicalMediaType:
            *((PhysicalMediaType_t *)pInfo) = pDescriptor->PhysicalType;
            break;
        
        case kMediaInfoIsSystemMedia:
        {
            int iNumDrives;			
            uint32_t wLogicalMediaNum = pDescriptor->u32MediaNumber;
        
            iNumDrives = g_MediaAllocationTable[wLogicalMediaNum].u32NumEntries;
            // Scan through all the drives and return true is any are system drives.
            for (i=0; i<iNumDrives; i++)
            {
                if (g_MediaAllocationTable[wLogicalMediaNum].Entry[i].Type == kDriveTypeSystem)
                {
                    bSystemDrive = true;
                }
            }
            *((bool *)pInfo) = bSystemDrive;
            break;
        }

        case kMediaInfoIsMediaPresent:
            // need to report if the media is present. For Internal NAND this is always true.
            // for other devices, that may not be the case and will need to be figured out.
            *((bool *)pInfo) = true;
            break;

        case kMediaInfoPageSizeInBytes:
            //! \todo Return sector size of MDP (?)
            *((uint32_t *)pInfo) = 0;
            break;

        case kMediaInfoMediaMfgId:
            //! \todo Media Info Nand Mfg Id byte (1st byte of readId nand HW cmd response) (?)
            *((uint32_t *)pInfo) = 0;
            break;

        case kMediaInfoIdDetails:
            //! \todo Return all bytes from lba nand HW readId command (?)
            *((uint64_t *)pInfo) = 0;
            break;

        case kMediaInfoNumChipEnables:
            *((uint32_t *)pInfo) = pMedia->getPhysicalMediaCount();
            break;

        case kMediaInfoExpectedTransferActivity:
            {
                TransferActivityType_t eTransferActivityType;

                eTransferActivityType = (TransferActivityType_t)pMedia->getTransferActivityType();           
                *((TransferActivityType_t *)pInfo) = eTransferActivityType;
            }
            break;

        default :
            return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_INFO_TYPE;

    }
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaErase(LogicalMedia_t *pDescriptor, uint32_t u32MagicNumber,
                             uint8_t u8DoNotEraseHidden)
{
    RtStatus_t Status;

    // Make sure we're initialized.
    if(!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    
    // Make sure we're not write protected.
    if (pDescriptor->bWriteProtected)
    {
        return ERROR_DDI_NAND_LMEDIA_MEDIA_WRITE_PROTECTED;
    }

    // Get the lba nand media object.
    LbaNandMedia *pMedia = (LbaNandMedia *)pDescriptor->pMediaInfo;
    if (!pMedia)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    
    // Stop managing power save until the next discovery.
    pMedia->enablePowerSaveManagement(false);

    Status = pMedia->erase(u8DoNotEraseHidden);
    if (Status != SUCCESS)
    {
        return Status;
    }

    pDescriptor->eState = kMediaStateErased;

    return SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaFlushDrives(LogicalMedia_t *pDescriptor)
{
    if (pDescriptor->bInitialized == false)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // Get the media object.
    LbaNandMedia *pMedia = (LbaNandMedia *)pDescriptor->pMediaInfo;
    assert(pMedia);

    pMedia->flush();

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaSetBootDrive(LogicalMedia_t *pDescriptor, DriveTag_t Tag)
{
    uint32_t u32PersistentValue;
    
    if (Tag == DRIVE_TAG_BOOTMANAGER_S)
    {
        // Set boot to primary firmware
        u32PersistentValue = 0;
    }
    else
    {
        // Set boot to secondary firmware
        u32PersistentValue = 1;
    }

    return ddi_rtc_WritePersistentField(RTC_LBA_NAND_SECONDARY_BOOT, u32PersistentValue);
}


///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_media.h for the documentation of this function.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaSetInfo(LogicalMedia_t *pDescriptor, uint32_t u32Type, const void *pInfo)
{
    if (u32Type == kMediaInfoIsInitialized)
    {
        *((bool *)pInfo) = pDescriptor->bInitialized;
        return(SUCCESS);
    }

    if (!pDescriptor->bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Get the media object.
    LbaNandMedia *pMedia = (LbaNandMedia *)pDescriptor->pMediaInfo;
    if (!pMedia)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    switch (u32Type)
    {
        case kMediaInfoExpectedTransferActivity:
            return pMedia->setTransferActivityType( *(TransferActivityType_t *)pInfo );

        case kMediaInfoLbaNandIsPowerSaveForcedOn:
            g_LbaNandMediaInfo.setExitPowerSaveOnTransfer( !(*(bool *)pInfo) );
            pMedia->enablePowerSaveManagement(true);
            return SUCCESS;
    }        

    return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_INFO_TYPE;
}

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
