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
//! \addtogroup ddi_nand_media
//! @{
//! \file ddi_nand_media_get_info.c
//! \brief The nand device driver interface presented as the media abstraction
//!     get info API.
///////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_media.h"
#include "hw/core/vmemory.h"
#include "auto_free.h"
#include <string.h>

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Reads specified information about the NAND media.
//!
//! \param[in] pDescriptor Pointer to the logical media descriptor structure.
//! \param[in] Type Type of information requested.
//! \param[out] pInfo Pointer to information buffer to be filled.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_INFO_TYPE
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t Media::getInfo(uint32_t Type, void * pInfo)
{
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
        
    DdiNandLocker locker;
    
    switch (Type)
    {
        case kMediaInfoPageSizeInBytes:
            // The result includes metadata bytes.
            *((uint32_t *)pInfo) = NandHal::getParameters().pageTotalSize;
            break;

        // Media Info Nand Mfg Id byte (1st byte of readId nand HW cmd response)
        // eg: 0xEC for samsung.
        case kMediaInfoMediaMfgId:
            *((uint32_t *)pInfo) = NandHal::getParameters().manufacturerCode;
            break;

        // Media Info Nand Flash all bytes from nand HW readId command.
        case kMediaInfoIdDetails:
        {
            // Read the id bytes from the first nand.
            union {
                uint8_t bytes[8];
                uint64_t u64;
            } idResult = {0};
            NandHal::getFirstNand()->readID(idResult.bytes);
            
            // returns 6 byte struct on the stack in a dword
            // In stupdaterapp.exe, the bytes are shown LSB first. i.e., the LSB is manufacturer,
            // next byte is device code, etc.
            *((uint64_t *)pInfo) = idResult.u64;
            break;
        }

        case kMediaInfoNumChipEnables:
            *((uint32_t *)pInfo) = NandHal::getChipSelectCount();
            break;
           
       case kMediaInfoSectorMetadataSizeInBytes:
            *((uint32_t *)pInfo) = NandHal::getParameters().pageMetadataSize;
            break;
        
        case kMediaInfoProductName:
        {
            auto_free<char> name = NandHal::getFirstNand()->getDeviceName();
            if (name)
            {
                memcpy(pInfo, name, strlen(name) + 1);
            }
            else
            {
                // Fill in a null byte just in case the caller tries to use the string.
                *(uint8_t *)pInfo = 0;
                return ERROR_DDI_LDL_UNIMPLEMENTED;
            }
            break;
        }

        default:
            return LogicalMedia::getInfo(Type, pInfo);
    }
    
    return SUCCESS;
}

RtStatus_t Media::setInfo(uint32_t selector, const void * value)
{
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
    
    DdiNandLocker locker;
    
    switch (selector)
    {
        case kMediaInfoExpectedTransferActivity:
            return LogicalMedia::setTransferActivityType( *(TransferActivityType_t *)value );
            
        case kMediaInfoIsSleepAllowed:
            NandHal::getFirstNand()->enableSleep(*(bool *)value);
            break;
        
        default:
            return LogicalMedia::setInfo(selector, value);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Returns the current media allocation table to the caller.
//!
//! \param[in] pDescriptor Pointer to the pointer to the logical media
//!     descriptor structure
//! \param[out] pTable Pointer to the pointer for the media allocation table
//!     structure 
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_LMEDIA_NOT_ALLOCATED
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::getMediaTable(MediaAllocationTable_t ** pTable)
{
    if (m_eState != kMediaStateAllocated)
    {
      return ERROR_DDI_NAND_LMEDIA_NOT_ALLOCATED;
    }

    DriveIterator_t iter;
    unsigned myDriveCount = 0;
    DriveTag_t tag;
    LogicalDrive * drive;

    // Create the drive iterator.
    RtStatus_t status = DriveCreateIterator(&iter);
    if (status != SUCCESS)
    {
        return status;
    }

    // Iterate over all drives known by the LDL and count my drives.
    while (DriveIteratorNext(iter, &tag) == SUCCESS)
    {
        drive = DriveGetDriveFromTag(tag);
        if (drive && drive->m_logicalMedia == this)
        {
            ++myDriveCount;
        }
    }
    
    DriveIteratorDispose(iter);

    // Recreate the iterator to scan again.
    status = DriveCreateIterator(&iter);
    if (status != SUCCESS)
    {
        return status;
    }

    // Allocate a media table with the exact number of drives belonging to us.
    MediaAllocationTable_t * table = (MediaAllocationTable_t *)malloc(sizeof(MediaAllocationTable_t) - (sizeof(MediaAllocationTableEntry_t) * MAX_MEDIA_TABLE_ENTRIES) + (sizeof(MediaAllocationTableEntry_t) * myDriveCount));
    if (!table)
    {
        return ERROR_OUT_OF_MEMORY;
    }
    
    // We always have a data drive in each media which by default is drive 0.
    int iNumDrives = 1;
    MediaAllocationTableEntry_t * tableEntry;
    
    // Iterate over all drives known by the LDL.
    while (DriveIteratorNext(iter, &tag) == SUCCESS)
    {
        drive = DriveGetDriveFromTag(tag);
        
        // Skip this drive if it's invalid or doesn't belong to us.
        if (!drive || drive->m_logicalMedia != this)
        {
            continue;
        }
        
        switch (drive->m_Type)
        {
            case kDriveTypeData:
                // Drive Type is Data Drive
                // Fill Up MediaAllocationTableEntry_t
                tableEntry = &table->Entry[0];
                tableEntry->u32DriveNumber = m_u32MediaNumber;

                break;
            
            case kDriveTypeHidden:
            case kDriveTypeSystem:
                // Fill Up MediaAllocationTableEntry_t
                tableEntry = &table->Entry[iNumDrives];
                tableEntry->u32DriveNumber = iNumDrives;

                // Increment the number of drives discovered in this media
                iNumDrives++;
                assert(iNumDrives <= myDriveCount);
                
                break;
        }

        // Fill in the common parts of the media table entry.
        tableEntry->Type = drive->m_Type;
        tableEntry->u32Tag = drive->m_u32Tag;
        tableEntry->u64SizeInBytes = drive->m_u64SizeInBytes;
        tableEntry->bRequired = false;
    }
    
    DriveIteratorDispose(iter);

    // Fill Up MediaAllocationTable_t
    table->u32NumEntries = iNumDrives;
    
    if (pTable)
    {
        *pTable = table;
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! Disposes of the table that we allocated in Media::getMediaTable().
////////////////////////////////////////////////////////////////////////////////
//!
RtStatus_t Media::freeMediaTable(MediaAllocationTable_t * table)
{
    if (table)
    {
        free((void *)table);
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
