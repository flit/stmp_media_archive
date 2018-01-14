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
//! \file   MmcSystemDrive.cpp
//! \brief  Implementation of the MMC System Drive.
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "MmcSystemDrive.h"
#include "TransferManager.h"
#include "components/sb_info/cmp_sb_info.h"

using namespace mmc;
using namespace mmchal;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

MmcSystemDrive::MmcSystemDrive()
:   LogicalDrive(),
    m_media(0),
    m_device(0),
    m_transferManager(0),
    m_startSectorNumber(0),
    m_componentVersion(0),
    m_projectVersion(0)
{
}

MmcSystemDrive::~MmcSystemDrive()
{
}

RtStatus_t MmcSystemDrive::initFromPartitionEntry(MmcMedia* media, PartEntry_t* partEntry)
{
    // Save a copy of the mmc::Media subclass pointer.
    assert(media);
    m_media = media;

    // Save a pointer to the HAL device.
    m_device = m_media->getDevice();
    assert(m_device);

    // Save a pointer to the media transfer manaager.
    m_transferManager = m_media->getTransferManager();
    assert(m_transferManager);

    // Final initialization will take place in init(), so
    // for now we set our initialized flag to false.
    m_bInitialized = false;

    m_bPresent = true;
    m_bErased = false;
    m_bWriteProtected = m_media->isWriteProtected();
    assert(!m_bWriteProtected); // Internal media cannot be write protected
    m_Type = kDriveTypeSystem;
    m_u32Tag = DRIVE_TAG_BOOTMANAGER_S;
    m_logicalMedia = media;

    // For internal media, the sector size (typically 2048 bytes) is a multiple of the device block size (512 bytes).
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
    m_u64SizeInBytes = m_numberOfNativeSectors * m_nativeSectorSizeInBytes;

    m_u32EraseSizeInBytes = 0;  // not used

    // Set the starting sector number of this drive.
    // Note that the partition start sector number is in terms of MBR sectors (512 bytes).
    // Oddly, the ROM expects the system image to start 4 blocks (2048 bytes)
    // past where the partition entry points. So we add that much to the
    // requested size.
    uint32_t firstSectorNumber = partEntry->firstSectorNumber + kMbrBootOffset;
    uint64_t byteOffset = firstSectorNumber * kMbrBytesPerSector;

    // If ROM workaround for firmware start address is required, use this fixed offset as the
    // drive start offset.
    if (m_device->getRomHighCapacityFirmwareOffset())
    {
        byteOffset = m_device->getRomHighCapacityFirmwareOffset();
    }

    m_startSectorNumber = byteOffset / m_nativeSectorSizeInBytes;


#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcSystemDrive: start=%d, size=%d kb\n", m_startSectorNumber, (m_u64SizeInBytes / 1024uLL));
#endif

    return SUCCESS;
}

RtStatus_t MmcSystemDrive::init()
{
    DdiMmcLocker locker;

    // This must be set before the cmp_sb_info call below because it ends up
    // calling DriveReadSector().
    m_bInitialized = true;

    // Get the component and project versions for later use by getInfo().
    // DriveInit() for the system drive is called before paging starts,
    // so we can call code here that is init.text without triggering paging.
    cmp_sb_info_GetVersionInfo(m_u32Tag, &m_componentVersion, &m_projectVersion);

    return SUCCESS;
}

RtStatus_t MmcSystemDrive::shutdown()
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

RtStatus_t MmcSystemDrive::flush()
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

RtStatus_t MmcSystemDrive::getInfo(uint32_t infoSelector, void * value)
{
    DdiMmcLocker locker;

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (infoSelector)
    {
        case kDriveInfoComponentVersion :
            *((uint64_t *)value) = m_componentVersion;
            break;

        case kDriveInfoProjectVersion :
            *((uint64_t *)value) = m_projectVersion;
            break;

        case kDriveInfoIsWriteProtected :
            *((bool *)value) = m_bWriteProtected;
            break;

        default:
            return LogicalDrive::getInfo(infoSelector, value);
    }

    return SUCCESS;
}

RtStatus_t MmcSystemDrive::setInfo(uint32_t infoSelector, const void * value)
{
    DdiMmcLocker locker;

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (infoSelector)
    {
        case kDriveInfoTag :
            m_u32Tag = *((uint32_t *)value);
            break;

        default:
            return LogicalDrive::setInfo(infoSelector, value);
    }

    return SUCCESS;
}

RtStatus_t MmcSystemDrive::readSector(uint32_t sector, SECTOR_BUFFER * buffer)
{
    DdiMmcLocker locker;

    assert(buffer);

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
                "MmcSystemDrive: Failed to read sector %d, error=0x%x\n", sector, status);
#endif
    }

    return status;
}

RtStatus_t MmcSystemDrive::writeSector(uint32_t sector, const SECTOR_BUFFER * buffer)
{
    DdiMmcLocker locker;

    assert(buffer);

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
                "MmcSystemDrive: Failed to write sector %d, error=0x%x\n", sector, status);
#endif
    }

    return status;
}

RtStatus_t MmcSystemDrive::erase()
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
            "MmcSystemDrive: Failed to erase drive, first=%d, last=%d\n", firstBlock, lastBlock);
#endif
        return status;
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
