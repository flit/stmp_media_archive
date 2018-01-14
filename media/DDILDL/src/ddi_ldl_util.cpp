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
//! \file ddi_ldl_util.c
//! \brief Utilities used by the logical drive layer.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include <string.h>
#include "errordefs.h"
#include "ddi_media_internal.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/sectordef.h"
#include "hw/core/vmemory.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! The minimum sector size that a caller can set.
#define MIN_SECTOR_SIZE (512)

// When this is unpacked ascii, get nand media SN returns as in sdk4.410 and earlier which 
// is 1 ascii SN byte per 4 byte word.
typedef enum {
    UNPACKED_ASCII=0,
    PACKED_ASCII=1,
    RAW=2
} eNAND_MEDIA_SN_RETURN_FORM;

//! Set next line to media SN format you want: UNPACKED_ASCII or PACKED_ASCII or RAW.
#define NAND_SN_RETURN_FORM UNPACKED_ASCII

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media_internal.h
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT LogicalMedia * MediaGetMediaFromIndex(unsigned index)
{
    if (index >= g_ldlInfo.m_mediaCount)
    {
        return NULL;
    }

    return g_ldlInfo.m_media[index];
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media_internal.h
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT LogicalDrive * DriveGetDriveFromTag(DriveTag_t tag)
{
    unsigned i;
    LogicalDrive ** drive = g_ldlInfo.m_drives;

    // Scan all drives for the tag.
    for (i = 0; i < MAX_LOGICAL_DRIVES; i++, drive++)
    {
        // Only consider drives that have a valid API set on them.
        if (*drive && (*drive)->getTag() == tag)
        {
            return *drive;
        }
    }

    // The tag wasn't found.
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media_internal.h
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT LogicalDrive ** DriveFindEmptyEntry()
{
    // make sure at least one slot free before scanning each entry
    if (g_ldlInfo.m_driveCount >= MAX_LOGICAL_DRIVES)
    {
        // nope, all full
        return NULL;
    }

    // Scan all drives for the tag.
    unsigned i;
    for (i = 0; i < MAX_LOGICAL_DRIVES; i++)
    {
        if (!g_ldlInfo.m_drives[i])
        {
            return &g_ldlInfo.m_drives[i];
        }
    }

    // drive array is full
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaGetMediaTable(uint32_t u32LogMediaNumber, MediaAllocationTable_t ** pMediaTable)
{
    LogicalMedia * media = MediaGetMediaFromIndex(u32LogMediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    return media->getMediaTable(pMediaTable);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaFreeMediaTable(uint32_t mediaNumber, MediaAllocationTable_t * table)
{
    LogicalMedia * media = MediaGetMediaFromIndex(mediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    return media->freeMediaTable(table);
}

////////////////////////////////////////////////////////////////////////////////
//! The default implementation simply returns SUCCESS.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t LogicalMedia::freeMediaTable(MediaAllocationTable_t * table)
{
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t MediaGetInfo(uint32_t u32LogMediaNumber, uint32_t Type, void * pInfo)
{
    LogicalMedia * media = MediaGetMediaFromIndex(u32LogMediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        // Handle the initialized property as a special case, so callers can get it before the
        // media is actually initialized.
        if (Type == kMediaInfoIsInitialized)
        {
            *((bool *)pInfo) = false;
            return SUCCESS;
        }
        
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    return media->getInfo(Type, pInfo);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
unsigned MediaGetCount(void)
{
    return g_ldlInfo.m_mediaCount;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaGetInfoSize(unsigned mediaNumber, uint32_t selector, uint32_t * propertySize)
{
    LogicalMedia * media = MediaGetMediaFromIndex(mediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    return media->getInfoSize(selector, propertySize);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaSetInfo(unsigned mediaNumber, uint32_t selector, const void * value)
{
    LogicalMedia * media = MediaGetMediaFromIndex(mediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    return media->setInfo(selector, value);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaFlushDrives(uint32_t u32LogMediaNumber)
{
    LogicalMedia * media = MediaGetMediaFromIndex(u32LogMediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    return media->flushDrives();
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaSetBootDrive(uint32_t u32LogMediaNumber, DriveTag_t u32Tag)
{
    LogicalMedia * media = MediaGetMediaFromIndex(u32LogMediaNumber);

    if (!media)
    {
        return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_NUMBER;
    }
    else if (!media->isInitialized())
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    return media->setBootDrive(u32Tag);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
unsigned DriveGetCount(void)
{
    return g_ldlInfo.m_driveCount;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT DriveState_t DriveGetState(DriveTag_t tag)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);

    if (drive)
    {
        return drive->getState();
    }
    else
    {
        return kDriveNotPresent;
    }
}

#pragma ghs section text=".static.text"

DriveState_t LogicalDrive::getState() const
{
    if (isInitialized())
    {
        return kDriveReady;
    }
    else if (didFailInit())
    {
        return kDriveFailedInitialization;
    }
    else
    {
        return kDriveUninitialized;
    }
}

#pragma ghs section text=default

////////////////////////////////////////////////////////////////////////////////
//! \brief Handler for getting the size of common drive info selectors.
//!
//! Handles these selectors:
//! - #kDriveInfoTag
//! - #kDriveInfoSectorSizeInBytes
//! - #kDriveInfoSectorOffsetInParent
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_SECTOR_SIZE
////////////////////////////////////////////////////////////////////////////////
RtStatus_t LogicalDrive::getInfoSize(uint32_t selector, uint32_t * propertySize)
{
    switch (selector)
    {
        case kDriveInfoTag:
        case kDriveInfoSectorSizeInBytes:
        case kDriveInfoSectorOffsetInParent:
            *propertySize = sizeof(uint32_t);
            break;

        default:
            return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveGetInfoSize(DriveTag_t tag, uint32_t selector, uint32_t * propertySize)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);

    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER;
    }
    else if (!drive->isInitialized())
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Let the media driver's handler try first.
    return drive->getInfoSize(selector, propertySize);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Handler for setting common drive info values.
//!
//! Handles these selectors:
//! - #kDriveInfoTag
//! - #kDriveInfoSectorSizeInBytes
//! - #kDriveInfoSectorOffsetInParent
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_SECTOR_SIZE
////////////////////////////////////////////////////////////////////////////////
RtStatus_t LogicalDrive::setInfo(uint32_t Type, const void * pInfo)
{
    switch (Type)
    {
        case kDriveInfoTag:
            m_u32Tag = *((uint32_t *)pInfo);
            break;

        // The caller wants to modify the runtime sector size of this data drive.
        // We will have to recalculate several other fields based on this.
        case kDriveInfoSectorSizeInBytes:
        {
            uint32_t newSectorSize = *(uint32_t *)pInfo;

            // If the drive is a system drive, just verify that the new sector size
            // is the native size.
            if (m_Type == kDriveTypeSystem && newSectorSize != m_nativeSectorSizeInBytes)
            {
                return ERROR_DDI_LDL_LDRIVE_INVALID_SECTOR_SIZE;
            }

            // Make sure the requested size is within range.
            if (newSectorSize < MIN_SECTOR_SIZE || newSectorSize > m_nativeSectorSizeInBytes)
            {
                return ERROR_DDI_LDL_LDRIVE_INVALID_SECTOR_SIZE;
            }

            // Compute the shift to get from the native to new nominal sector size.
            // The maximum shift is 15, which is absolutely huge.
            int shift;
            for (shift = 0; shift < 16; shift++)
            {
                if (m_nativeSectorSizeInBytes >> shift == newSectorSize)
                {
                    break;
                }
            }

            // If we didn't find a matching shift value, then the requested sector size must not
            // a power of two, so return an error.
            if (shift == 16)
            {
                return ERROR_DDI_LDL_LDRIVE_INVALID_SECTOR_SIZE;
            }

            // Update drive descriptor.
            m_u32SectorSizeInBytes = newSectorSize;
            m_u32NumberOfSectors = m_numberOfNativeSectors * (m_nativeSectorSizeInBytes / m_u32SectorSizeInBytes);
            m_nativeSectorShift = shift;
            break;
        }

        case kDriveInfoSectorOffsetInParent:
            m_pbsStartSector = *(uint32_t *)pInfo;
            break;

        default:
            return ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveSetInfo(DriveTag_t tag, uint32_t Type, const void * pInfo)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);

    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_NUMBER;
    }
    else if (!drive->isInitialized())
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Let the media driver's handler try first.
    return drive->setInfo(Type, pInfo);
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT uint32_t MediaGetMaximumSectorSize(void)
{
    unsigned i;
    uint32_t maxSize = 0;
    
    // Iterate over all available media and find the largest sector size.
    for (i=0; i < g_ldlInfo.m_mediaCount; ++i)
    {
        uint32_t theSize = 0;
        if (MediaGetInfo(i, kMediaInfoSectorSizeInBytes, &theSize) != SUCCESS)
        {
            continue;
        }
        
        if (theSize > maxSize)
        {
            maxSize = theSize;
        }
    }
    
    // Backup if perhaps no media are inited yet.
    if (maxSize == 0)
    {
        maxSize = MAX_SECTOR_DATA_SIZE;
    }

    return maxSize;
}

RtStatus_t LogicalMedia::getInfoSize(uint32_t infoSelector, uint32_t * infoSize)
{
    assert(infoSize);
    *infoSize = sizeof(uint32_t);
    return SUCCESS;
}

#pragma ghs section text=".init.text"

RtStatus_t LogicalMedia::getInfo(uint32_t infoSelector, void * value)
{
    switch (infoSelector)
    {
        case kMediaInfoNumberOfDrives:
            *((uint32_t *)value) = m_u32NumberOfDrives;
            break;
                        
        case kMediaInfoSizeInBytes:
            *((uint64_t *)value) = m_u64SizeInBytes;            
            break;
            
        case kMediaInfoAllocationUnitSizeInBytes:
            *((uint32_t *)value) = m_u32AllocationUnitSizeInBytes;
            break;

        case kMediaInfoIsInitialized:
            *((bool *)value) = true;
            break;
            
        case kMediaInfoMediaState:
            *((MediaState_t *)value) = m_eState;
            break;

        case kMediaInfoIsWriteProtected:
            *((bool *)value) = m_bWriteProtected;
            break;

        case kMediaInfoPhysicalMediaType:
            *((PhysicalMediaType_t *)value) = m_PhysicalType;
            break;
        
        case kMediaInfoSizeOfSerialNumberInBytes:
        {
            uint32_t u32NandMediaSnSizeBytes;
            // In .asciiSizeInChars member, sdk4.410 and previous ver did not have null term, does in sdk4.420.

            // Size reported must match returned buffer size in kMediaInfoSerialNumber below - the enums enforce that. 
            #if (NAND_SN_RETURN_FORM == PACKED_ASCII)
                u32NandMediaSnSizeBytes = g_InternalMediaSerialNumber.asciiSizeInChars;
            #elif (NAND_SN_RETURN_FORM == UNPACKED_ASCII) // output goal: 1 ascii byte per 4 byte word.
                u32NandMediaSnSizeBytes = g_InternalMediaSerialNumber.asciiSizeInChars * (sizeof(uint32_t)); 
            #elif (NAND_SN_RETURN_FORM == RAW)
                u32NandMediaSnSizeBytes = g_InternalMediaSerialNumber.rawSizeInBytes;                 
            #endif
            
            *((uint32_t *)value) = u32NandMediaSnSizeBytes; // return the SN size
            
            break;
        }
        
        case kMediaInfoSerialNumber:
        {
            int i;
            #if (NAND_SN_RETURN_FORM == PACKED_ASCII)
                 // future possible alt: return SN as packed ascii version occupies 4x less RAM.   
                 for(i=0; i<g_InternalMediaSerialNumber.asciiSizeInChars; i++) // 33 byte copy for sdk4.420
                 {   //review: now does a byte for byte copy.
                     ((uint8_t *)value)[i] = g_InternalMediaSerialNumber.ascii[i]; 
                 }          
                 //((uint8_t *)value)[i] = 0x00; // null termination of ascii string is now part of .ascii[i] so don't need line 
            #elif (NAND_SN_RETURN_FORM == UNPACKED_ASCII)
                //-----  older ver uses unpacked ascii as 1 ascii byte per 4 byte word in return buffer.
                // This was the sdk4.410 and previous version which I didn't find any calls to but it was a public SDK interface except for scsi updater commands. 
                for(i=0; i<g_InternalMediaSerialNumber.asciiSizeInChars; i++) 
                {   // REVIEW: copies each ascii byte into a uint32_t and there will be 33 ascii bytes (including null) so 33 words in return buffer. 
                    // DEFECT in sdk4.410/4.400/4.3: .asciiSizeInChars member init in hw_lfi did not account for null char so null was not copied by this loop.
                    ((uint32_t *)value)[i] = g_InternalMediaSerialNumber.ascii[i]; 
                    //when null is properly included in size expected to give: 33 bytes each copied into a 4 byte word so result is 33 uint32 words = 132 bytes are written. 
                }
            #elif (NAND_SN_RETURN_FORM == RAW)
            {    // future possible alt: return SN as packed ascii version occupies 4x less RAM.   
                 for(i=0; i<g_InternalMediaSerialNumber.rawSizeInBytes; i++) // 33 byte copy for sdk4.420
                 {   //a byte for byte copy.
                     ((uint8_t *)value)[i] = g_InternalMediaSerialNumber.raw[i]; 
                 }          
                 //((uint8_t *)value)[i] = 0x00; // null termination line not needed for raw form. 
            #endif
            
            break;
        }

        case kMediaInfoIsSystemMedia:
            // The internal media is always the "system" media.
            *((bool *)value) = (getMediaNumber() == kInternalMedia);
            break;

        case kMediaInfoIsMediaPresent:
            // need to report if the media is present. For Internal NAND this is always true.
            // for other devices, that may not be the case and will need to be figured out.
            *((bool *)value) = true;
            break;

        case kMediaInfoExpectedTransferActivity:
             *((TransferActivityType_t *)value) = m_TransferActivityType;
             break;

        default:
            return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_INFO_TYPE;
    }
    
    return SUCCESS;
}

#pragma ghs section text=default

RtStatus_t LogicalMedia::setInfo(uint32_t infoSelector, const void * value)
{
    return ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_INFO_TYPE;
}


//! @}


