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
//! \addtogroup ddi_mmc
//! @{
//! \file   MmcDataDrive.cpp
//! \brief  Implementation of the MMC Data Drive.
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "MmcDataDrive.h"
#include "TransferManager.h"

using namespace mmc;
using namespace mmchal;

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

MmcDataDrive::MmcDataDrive()
:   LogicalDrive(),
    m_media(0),
    m_device(0),
    m_transferManager(0),
    m_startSectorNumber(0)
{
}

MmcDataDrive::~MmcDataDrive()
{
}

RtStatus_t MmcDataDrive::initFromMedia(MmcMedia* media)
{
    // Save a copy of the MmcMedia subclass pointer.
    assert(media);
    m_media = media;

    // Save a pointer to the HAL device.
    m_device = m_media->getDevice();
    assert(m_device);

    // Save a pointer to the media transfer manager.
    m_transferManager = m_media->getTransferManager();
    assert(m_transferManager);

    // Final initialization will take place in init(), so
    // for now we set our initialized flag to false.
    m_bInitialized = false;

    m_bPresent = true;
    m_bErased = false;
    m_bWriteProtected = media->m_bWriteProtected;
    m_Type = kDriveTypeData;
    m_u32Tag = DRIVE_TAG_DATA_EXTERNAL;
    m_logicalMedia = media;

    m_nativeSectorSizeInBytes = m_media->getAllocationUnitSizeInBytes();
    // For MMC/SD, the nominal sector size is always the same as the native sector size.
    m_u32SectorSizeInBytes = m_nativeSectorSizeInBytes;
    m_nativeSectorShift = 0;

    // Set the device capacity to the total size of the media.
    m_u64SizeInBytes = m_media->getSizeInBytes();
    m_u32NumberOfSectors = m_u64SizeInBytes / m_u32SectorSizeInBytes;
    m_numberOfNativeSectors = m_u64SizeInBytes / m_nativeSectorSizeInBytes;

    m_u32EraseSizeInBytes = 0;  // not used

    // Set the start sector to the first sector on the drive.
    m_startSectorNumber = 0;

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcDataDrive: external, size=%d kb\n", (m_u64SizeInBytes / 1024uLL));
#endif

    return SUCCESS;
}

RtStatus_t MmcDataDrive::initFromPartitionEntry(MmcMedia* media, PartEntry_t* partEntry,
        LogicalDriveType_t driveType, uint32_t driveTag)
{
    // Save a copy of the mmc::Media subclass pointer.
    assert(media);
    m_media = media;

    // Save a pointer to the HAL device.
    m_device = m_media->getDevice();
    assert(m_device);

    // Save a pointer to the media transfer manager.
    m_transferManager = m_media->getTransferManager();
    assert(m_transferManager);

    // Final initialization will take place in init(), so
    // for now we set our initialized flag to false.
    m_bInitialized = false;

    m_bPresent = true;
    m_bErased = false;
    m_bWriteProtected = m_media->isWriteProtected();
    assert(!m_bWriteProtected); // Internal media cannot be write protected
    m_Type = driveType;
    m_u32Tag = driveTag;
    m_logicalMedia = media;

    m_nativeSectorSizeInBytes = m_media->getAllocationUnitSizeInBytes();
    // For MMC/SD, the nominal sector size is always the same as the native sector size.
    m_u32SectorSizeInBytes = m_nativeSectorSizeInBytes;
    m_nativeSectorShift = 0;

    // Set the device capacity based on the partition size.
    // Note that the partition size is in terms of MBR sectors (512 bytes).
    assert(kMbrBytesPerSector);
    assert(m_nativeSectorSizeInBytes);
    uint32_t nativeBlocksPerFatSector = m_nativeSectorSizeInBytes / kMbrBytesPerSector;
    assert(nativeBlocksPerFatSector);
    assert(partEntry);
    m_u32NumberOfSectors = partEntry->sectorCount / nativeBlocksPerFatSector;
    m_numberOfNativeSectors = m_u32NumberOfSectors;
    m_u64SizeInBytes = static_cast<uint64_t>(m_numberOfNativeSectors) * m_nativeSectorSizeInBytes;

    m_u32EraseSizeInBytes = 0;  // not used

    // Set the starting sector number of this drive.
    // Note that the partition start sector number is in terms of MBR sectors (512 bytes).
    uint64_t byteOffset = partEntry->firstSectorNumber * kMbrBytesPerSector;
    m_startSectorNumber = byteOffset / m_nativeSectorSizeInBytes;

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcDataDrive: internal, type=%d, start=%d, size=%d kb\n", m_Type,
        m_startSectorNumber, (m_u64SizeInBytes / 1024uLL));
#endif

    return SUCCESS;
}

RtStatus_t MmcDataDrive::init()
{
    DdiMmcLocker locker;

    // Nothing to do.
    m_bInitialized = true;

    return SUCCESS;
}

RtStatus_t MmcDataDrive::shutdown()
{
    DdiMmcLocker locker;

    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    RtStatus_t status = flush();
    if (SUCCESS != status)
    {
        return status;
    }

    m_bInitialized = false;

    return SUCCESS;
}

RtStatus_t MmcDataDrive::flush()
{
    DdiMmcLocker locker;

    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    assert(m_transferManager);
    RtStatus_t status = m_transferManager->stop();

    return status;
}

RtStatus_t MmcDataDrive::repair()
{
    RtStatus_t status = erase();

    if (SUCCESS == status)
    {
        return ERROR_DDI_LDL_LDRIVE_FS_FORMAT_REQUIRED;
    }
    else
    {
        return status;
    }
}

RtStatus_t MmcDataDrive::getInfo(uint32_t infoSelector, void* value)
{
    DdiMmcLocker locker;

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (infoSelector)
    {
        case kDriveInfoSizeOfSerialNumberInBytes:
        {
            assert(m_device);
            SerialNumber_t sn = m_device->getSerialNumber();

            //! \todo This returns the number of characters, not the number of bytes (which would be count * sizeof(int)).

            // Get the number of unpacked (32-bit sized) ASCII chars are in serial number.
            uint32_t count = sn.asciiSizeInChars;

            // If this is the internal drive, add 4 unpacked ASCII chars to count.
            // From the old code: "There is SCSI length specification for this serial number string, and the first LUN of enumeration
            // should conform to it."
            //! \todo Review serial number spec.
            if (isInternalDrive())
            {
                count += 4;
            }

#if defined(DEBUG_SERIAL_NUM) && !defined(NO_SDRAM)
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
                    "MmcDataDrive: kDriveInfoSizeOfSerialNumberInBytes returns %d\n", count);
            tss_logtext_Flush(0);
#endif
            *((uint32_t *)value) = count;
           break;
        }

        case kDriveInfoSizeOfRawSerialNumberInBytes:
        {
            assert(m_device);
            SerialNumber_t sn = m_device->getSerialNumber();

            // Number of raw bytes (packed ASCII, 2 chars per byte).
            uint32_t count = sn.rawSizeInBytes;

            // If this is the internal drive, add 2 packed ASCII chars to count.
            // From the old code: "There is SCSI length specification for this serial number string, and the first LUN of enumeration
            // should conform to it."
            //! \todo Review serial number spec.
            if (isInternalDrive())
            {
                count += 2;
            }

#if defined(DEBUG_SERIAL_NUM) && !defined(NO_SDRAM)
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
                    "MmcDataDrive: kDriveInfoSizeOfRawSerialNumberInBytes returns %d\n", count);
            tss_logtext_Flush(0);
#endif
            *((uint32_t *)value) = count;
            break;
        }

        case kDriveInfoSerialNumber:
        {
            // Returns SN buffer as unpacked ASCII (in least significant bytes).
            assert(m_device);
            SerialNumber_t sn = m_device->getSerialNumber();

            for (int i = 0; i < sn.asciiSizeInChars; i++)
            {
                ((uint32_t *)value)[i] = sn.ascii[i];
            }

            // If this is the internal drive, add 4 unpacked ASCII chars "0000".
            // From the old code: "There is SCSI length specification for this serial number string, and the first LUN of enumeration
            // should conform to it."
            //! \todo Review serial number spec.
            if (isInternalDrive())
            {
                for (int i = sn.asciiSizeInChars; i < sn.asciiSizeInChars + 4; i++)
                {
                    ((uint32_t *)value)[i] = 0x30; // "0" ASCII char
                }
            }

#if defined(DEBUG_SERIAL_NUM) && !defined(NO_SDRAM)
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
                    "MmcDataDrive: kDriveInfoSerialNumber returns:\n");
            int n = sn.asciiSizeInChars;
            if (isInternalDrive())
            {
                n += 4;
            }
            for (int i = 0; i < n; i++)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
                        "value[%d] = 0x%x\n", i, ((uint32_t *)value)[i]);
            }
            tss_logtext_Flush(0);
#endif
            break;
        }

        case kDriveInfoRawSerialNumber:
        {
            // Returns SN buffer as packed Raw hex nibbles.
            assert(m_device);
            SerialNumber_t sn = m_device->getSerialNumber();

            for (int i = 0; i < sn.rawSizeInBytes; i++)
            {
                ((uint8_t *)value)[i] = sn.raw[i];
            }

            // If this is the internal drive, add 2 packed ASCII chars to count.
            // From the old code: "There is SCSI length specification for this serial number string, and the first LUN of enumeration
            // should conform to it."
            //! \todo Review serial number spec.
            if (isInternalDrive())
            {
                for (int i = sn.rawSizeInBytes; i < sn.rawSizeInBytes + 2; i++)
                {
                    ((uint8_t *)value)[i] = 0x00; // append a zero raw byte (2 zero nibbles per loop)
                }
            }

#if defined(DEBUG_SERIAL_NUM) && !defined(NO_SDRAM)
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
                    "MmcDataDrive: kDriveInfoRawSerialNumber returns:\n");
            int n = sn.rawSizeInBytes;
            if (isInternalDrive())
            {
                n += 2;
            }
            for (int i = 0; i < n; i++)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
                        "value[%d] = 0x%x\n", i, ((uint8_t *)value)[i]);
            }
            tss_logtext_Flush(0);
#endif
            break;
        }

        case kDriveInfoMediaPresent:
            //! \todo If this API is needed, read card detect line for external media.
            *((bool *)value) = true;
            break;

        case kDriveInfoMediaChange:
            //! \todo If this API is needed, track state of card detect line for external media.
            *((bool *)value) = false;
            break;

        default:
            return LogicalDrive::getInfo(infoSelector, value);
  }

  return SUCCESS;
}

RtStatus_t MmcDataDrive::setInfo(uint32_t infoSelector, const void * value)
{
    DdiMmcLocker locker;

    return LogicalDrive::setInfo(infoSelector, value);
}

RtStatus_t MmcDataDrive::readSector(uint32_t sector, SECTOR_BUFFER * buffer)
{
    DdiMmcLocker locker;

    assert(buffer);

    //! \todo Possibly ask HAL if card is still present before attempting transfer.

    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we won't go out of bounds.
    if (sector >= m_u32NumberOfSectors)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    // Add our starting sector offset from the beginning of the media.
    sector += m_startSectorNumber;

    assert(m_transferManager);
    RtStatus_t status = m_transferManager->readSector(sector, buffer);

    if (SUCCESS != status)
    {
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
                "MmcDataDrive: Failed to read sector %d, error=0x%x\n", sector, status);
#endif
    }

    return status;
}

RtStatus_t MmcDataDrive::writeSector(uint32_t sector, const SECTOR_BUFFER * buffer)
{
    DdiMmcLocker locker;

    assert(buffer);

    //! \todo Possibly ask HAL if card is still present before attempting transfer.

    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we won't go out of bounds.
    if (sector >= m_u32NumberOfSectors)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    // Add our starting sector offset from the beginning of the media.
    sector += m_startSectorNumber;

    assert(m_transferManager);
    RtStatus_t status = m_transferManager->writeSector(sector, buffer);

    if (SUCCESS != status)
    {
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
                "MmcDataDrive: Failed to write sector %d, error=0x%x\n", sector, status);
#endif
    }

    return status;
}

RtStatus_t MmcDataDrive::erase()
{
    DdiMmcLocker locker;

    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure we're not write protected.
    if (m_bWriteProtected)
    {
        return ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED;
    }

    // Stop any open transfer manager operations.
    // This forces the device to be deselected on the bus.
    assert(m_transferManager);
    RtStatus_t status = m_transferManager->forceStop();
    if (SUCCESS != status)
    {
        return status;
    }

    // Erase the entire drive.
    assert(m_media);
    uint32_t firstBlock = m_media->sectorsToDeviceBlocks(m_startSectorNumber);
    uint32_t lastBlock = firstBlock + m_media->sectorsToDeviceBlocks(m_numberOfNativeSectors) - 1;
    assert(m_device);
    status = m_device->erase(firstBlock, lastBlock);
    if (SUCCESS != status)
    {
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
                "MmcDataDrive: Failed to erase drive, first=%d, last=%d\n", firstBlock, lastBlock);
#endif
        return status;
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
