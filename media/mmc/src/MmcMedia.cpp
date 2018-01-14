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
//! \file   MmcMedia.cpp
//! \brief  Implementation of the MMC Media.
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "MmcMedia.h"
#include "TransferManager.h"
#include "drivers/ssp/mmcsd/ddi_ssp_mmcsd_board.h"
#include "drivers/media/mmc/src/MmcDataDrive.h"
#include "drivers/media/mmc/src/MmcSystemDrive.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/mmc/ddi_mmc.h"
#include <memory>

using namespace mmc;
using namespace mmchal;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

void updateChsEntries(uint64_t u64TotalSectors, PartTable_t *pMmcPartitionTable);

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

// See MmcMedia.h for documentation on the synchronization mutex.
TX_MUTEX g_mmcThreadSafeMutex;

//! \brief Indicates if we have initialized our synchronization mutex.
static bool g_mutexInitialized = false;

//! \brief Counts the number of initialized Media objects.
static unsigned g_numMedia = 0;

#if defined(DEBUG_TRACE) && !defined(NO_SDRAM)
uint32_t DebugTrace::m_value[DebugTrace::kTraceSize] = {0};
uint32_t DebugTrace::m_pos = 0;
#endif

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \brief Media constants.
enum _mmc_media_constants
{
    //! \brief Size of sector that we advertise for internal media.
    //!
    //! Must be a multiple of the MMC/SD block size (512).
    //! Because of ROM limitations, must be <= 2048.
    kInternalNativeSectorSizeInBytes = 2048,

    //! \brief Size of sector that we advertise for external media.
    //!
    //! Must be the FAT sector size (512), which is also the MMC/SD block size.
    //! \note We never write the MBR or PBS on the MMC/SD card, so we must use
    //! the block size reported by the device, which, for all practical purposes,
    //! is always 512 bytes.
    kExternalNativeSectorSizeInBytes = 512
};

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

//! \brief Get the MBR FAT file system code for a given drive size.
static uint8_t fileSystemForSize(uint64_t sizeInBytes)
{
    if (sizeInBytes < k1MByte * 4)
    {
        return kPartSysId_Fat12;
    }
    else if (sizeInBytes < k1MByte * 32)
    {
        return kPartSysId_Fat16;
    }
    else
    {
        return kPartSysId_Fat32;
    }
}

//! \brief Create media object from media definition.
LogicalMedia * mmc_media_factory(const MediaDefinition_t * def)
{
    LogicalMedia * media = new MmcMedia();
    media->m_u32MediaNumber = def->m_mediaNumber;
    media->m_isRemovable = def->m_isRemovable;
    media->m_PhysicalType = def->m_mediaType;

    return media;
}

MmcMedia::MmcMedia()
: LogicalMedia(),
  m_portId(kSspPortNone),
  m_device(0),
  m_deviceBlocksPerSector(0),
  m_transferManager(0)
{
}

MmcMedia::~MmcMedia()
{
    if (m_transferManager)
    {
        delete m_transferManager;
    }
}

RtStatus_t MmcMedia::init()
{
    // Initialize our synchronization object.
    if (!g_mutexInitialized)
    {
        RtStatus_t status = os_thi_ConvertTxStatus(tx_mutex_create(&g_mmcThreadSafeMutex, "MMC_TS_MUTEX", TX_INHERIT));
        if (status != SUCCESS)
        {
            return status;
        }

        g_mutexInitialized = true;
    }

    DdiMmcLocker locker;

    // Initialize the MMC HAL.
    MmcHal::init();

    // Get the HAL SSP port ID associated with this media and initialize the port.
    m_portId = ddi_ssp_mmcsd_GetMediaPortId(m_u32MediaNumber);
    RtStatus_t status = MmcHal::initPort(m_portId, m_isRemovable);
    if (SUCCESS != status)
    {
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
            "MmcMedia: Failed to initialize port %d, error=0x%x\n", m_portId, status);
#endif
        return status;
    }

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcMedia: Initialized port %d, removable=%d\n", m_portId, m_isRemovable);
#endif

    // At this point the HAL has setup the SSP Block, but we have not yet probed the
    // port to look for media. This is done in discover(). For internal media (eMMC/eSD),
    // discover is called immediately after init. For external (removable SD/MMC Card),
    // discover is called on insertion detection.

    m_bInitialized = true;
    m_eState = kMediaStateUnknown;

    // For internal media, the sector size (2048 bytes) is a multiple of the device block size (512 bytes).
    // For external media, the native sector size is the device block size (512 bytes).
    m_u32AllocationUnitSizeInBytes = m_isRemovable ? kExternalNativeSectorSizeInBytes : kInternalNativeSectorSizeInBytes;

    // Increment the number of initialized media.
    ++g_numMedia;

    return SUCCESS;
}

RtStatus_t MmcMedia::discover()
{
    DdiMmcLocker locker;

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    if (kMediaStateErased == m_eState)
    {
        // Cannot be discovered if erased.
        return ERROR_DDI_LDL_LMEDIA_MEDIA_ERASED;
    }

    // Get the device attached the port.
    // If no device is active, this probes the bus for a card and, it found, initializes and identifies the card.
    RtStatus_t status = MmcHal::probePort(m_portId, &m_device);
    if (SUCCESS != status)
    {
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
            "MmcMedia: Failed to probe port %d, error=0x%x\n", m_portId, status);
#endif
        return status;
    }
    assert(m_device);

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcMedia: Discovered device on port %d, name=[%s], size=%d kb\n", m_portId, m_device->getProductName(),
        (m_device->getCapacityInBytes() / 1024uLL));
#endif

    // Set the total size of the media.
    m_u64SizeInBytes = m_device->getCapacityInBytes();

    // Verify that the logical media sector size is at least as large as the device block size.
    assert(m_device->getBlockSizeInBytes());
    assert(m_u32AllocationUnitSizeInBytes >= m_device->getBlockSizeInBytes());

    // Set the number of device blocks per sector.
    m_deviceBlocksPerSector = m_u32AllocationUnitSizeInBytes / m_device->getBlockSizeInBytes();

    // Set Write Protect status.
    m_bWriteProtected = false;
    if (m_isRemovable)
    {
        m_bWriteProtected = m_device->isWriteProtected();
    }

    // Create a transfer manager for this media.
    m_transferManager = new TransferManager(this, m_device);
    if (!m_transferManager)
    {
        return ERROR_OUT_OF_MEMORY;
    }

    // Initialize the transfer manager.
    status = m_transferManager->init();
    if (SUCCESS != status)
    {
        return status;
    }

    if (m_isRemovable)
    {
        // For removable media, the entire media is a single data drive.
        status = createExternalDataDrive();
    }
    else
    {
        // For internal media, read the partition information from the MBR.
        // Note: Booting from a Boot Control Block (BCB) is not supported.
        // Note: Only one firmware copy (boot partition) is supported.
        status = createInternalDrives();
    }

    if (SUCCESS != status)
    {
        m_eState = kMediaStateUnknown;
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
            "MmcMedia: Failed to create drives on port %d, error=0x%x\n", m_portId, status);
#endif
        return status;
    }

    m_eState = kMediaStateAllocated;

    return SUCCESS;
}

//! \brief Allocate the system drive from the partition table.
void MmcMedia::allocSystemDrive(Allocator &alloc, PartitionTable::EntryIterator &partIterator,
        MediaAllocationTable_t* mediaTable)
{
    assert(mediaTable);
    for (int i = 0; i < mediaTable->u32NumEntries; i++)
    {
        MediaAllocationTableEntry_t *mediaEntry = &mediaTable->Entry[i];

        if (mediaEntry->Type == kDriveTypeSystem)
        {
            // Get the next available partition entry.
            PartEntry_t *partEntry = partIterator.getNext();
            partEntry->bootDescriptor = 0;  // non-bootable

            // Write the current offset as this drive's starting sector number.
            // Note that MBR sectors are in units of 512 bytes.
            partEntry->firstSectorNumber = alloc.getByteOffset() / kMbrBytesPerSector;

            uint64_t bytesToAlloc = mediaEntry->u64SizeInBytes;

            // Oddly, the ROM expects the system image to start 4 blocks (2048 bytes)
            // past where the partition entry points. So we add that much to the
            // requested size.
            bytesToAlloc += kMbrBootOffsetInBytes;

            // If ROM workaround for firmware start address is required, add this fixed
            // offset to the size and also set a magic first sector number.
            if (m_device->getRomHighCapacityFirmwareOffset())
            {
                bytesToAlloc += m_device->getRomHighCapacityFirmwareOffset();
                partEntry->firstSectorNumber = 1;
            }

            // Ask the allocator to reserve the requested number of bytes.
            // It returns the actual size reserved.
            bytesToAlloc = alloc.reserve(bytesToAlloc);

            // Write the requested size into the partition entry.
            // Note that MBR sectors are in units of 512 bytes.
            partEntry->sectorCount = bytesToAlloc / kMbrBytesPerSector;

            // Set the file system type to the special Sigmatel ID.
            partEntry->fileSystem = kMbrSigmatelId;

            // Only one system drive is allowed, so we are done.
            return;
        }
    }
}

//! \brief Allocate all hidden drives from the partition table.
void MmcMedia::allocHiddenDrives(Allocator &alloc, PartitionTable::EntryIterator &partIterator,
        MediaAllocationTable_t* mediaTable)
{
    assert(mediaTable);
    for (int i = 0; i < mediaTable->u32NumEntries; i++)
    {
        MediaAllocationTableEntry_t *mediaEntry = &mediaTable->Entry[i];

        if (mediaEntry->Type == kDriveTypeHidden)
        {
            // Get the next available partition entry.
            PartEntry_t *partEntry = partIterator.getNext();
            partEntry->bootDescriptor = 0;  // non-bootable

            // Write the current offset as this drive's starting sector number.
            // Note that MBR sectors are in units of 512 bytes.
            partEntry->firstSectorNumber = alloc.getByteOffset() / kMbrBytesPerSector;

            // Use default size if no zero size requested.
            uint64_t bytesToAlloc = mediaEntry->u64SizeInBytes;
            if (0 == bytesToAlloc)
            {
                bytesToAlloc = kMbrMinDataDriveSizeInBytes;
            }

            // Ask the allocator to reserve the requested number of bytes.
            // It returns the actual size reserved.
            bytesToAlloc = alloc.reserve(bytesToAlloc);

            // Write the requested size into the partition entry.
            // Note that MBR sectors are in units of 512 bytes.
            partEntry->sectorCount = bytesToAlloc / kMbrBytesPerSector;

            // Set the file system type based on the partition size.
            partEntry->fileSystem = fileSystemForSize(bytesToAlloc);

            // Continue on to find the next hidden drive in the media table
            // (there should be two).
        }
    }
}

//! \brief Allocate a data drive from the partition table.
void MmcMedia::allocDataDrive(Allocator &alloc, PartitionTable::EntryIterator &partIterator)
{
    // There is only one data drive, and we don't need the size from the media allocation table.
    // Also, this routine does not advance the allocator.

    // Get the next available partition entry.
    PartEntry_t *partEntry = partIterator.getNext();
    partEntry->bootDescriptor = kPartBootIdBootable;

    // Write the current offset as this drive's starting sector number.
    // Note that MBR sectors are in units of 512 bytes.
    partEntry->firstSectorNumber = alloc.getByteOffset() / kMbrBytesPerSector;

    // Set the bytes to allocate to the remainder of the media.
    uint64_t bytesToAlloc = m_u64SizeInBytes - alloc.getByteOffset();

    // Write the requested size into the partition entry.
    // Note that MBR sectors are in units of 512 bytes.
    partEntry->sectorCount = bytesToAlloc / kMbrBytesPerSector;

    // Set the file system type based on the partition size.
    partEntry->fileSystem = fileSystemForSize(bytesToAlloc);
}

RtStatus_t MmcMedia::allocate(MediaAllocationTable_t* mediaTable)
{
    DdiMmcLocker locker;

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    if (kMediaStateErased != m_eState)
    {
        // Cannot be allocated if not erased.
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_ERASED;
    }

    // Reset the media state until we've succeeded.
    m_eState = kMediaStateUnknown;

    // Verify that media discover was called and at least was able
    // to probe the device.
    assert(m_device);
    assert(m_u64SizeInBytes);

    // Get a buffer to use for the partition table.
    SectorBuffer buffer;
    RtStatus_t status = buffer.acquire();
    if (SUCCESS != status)
    {
        return status;
    }
    buffer.fill(0);

    // The partition entries in this partition table are filled-in by the drive allocation
    // calls below. Later the partition table will be written to the media.
    PartitionTable partTable(buffer);
    // Get a partition entry iterator.
    PartitionTable::EntryIterator partEntryIt(partTable.getTable());

    // Drives on the media are allocated in allocation unit-sized chunks.
    // For devices with erase group size restrictions, this must be at least one
    // erase group size. We enforce a multiple of the native sector size.
    assert(m_u32AllocationUnitSizeInBytes);
    const uint32_t allocUnitSizeInBytes = ROUND_UP(m_device->getEraseGroupSizeInBytes(), m_u32AllocationUnitSizeInBytes);

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcMedia: Alloc unit size is %d bytes (%d blocks)\n", allocUnitSizeInBytes,
        allocUnitSizeInBytes / m_device->getBlockSizeInBytes());
    tss_logtext_Flush(0);
#endif

    Allocator alloc(allocUnitSizeInBytes);

    // Start the allocation at unit 1. This leaves an
    // initial unit-sized hole at the beginning of the media, which leaves room
    // for the MBR to be written at block 0.
    uint32_t driveStartOffset = allocUnitSizeInBytes;

    // If ROM workaround for firmware start address is required, use that fixed offset instead.
    // Note: this workaround requires that the system (firmware) drive is allocated first.
    if (m_device->getRomHighCapacityFirmwareOffset())
    {
        driveStartOffset = m_device->getRomHighCapacityFirmwareOffset();

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
            "MmcMedia: Implementing ROM firmware start workaround, offset=%d\n", driveStartOffset);
        tss_logtext_Flush(0);
#endif
    }

    alloc.reserve(driveStartOffset);

    // The drive allocation methods perform the follow functions:
    // 1. Look up the requested drive size in the media allocation table.
    // 2. Use the allocator to get the next available offset on the media.
    // 3. Write the drive offset and size to the next available partition entry.
    // 4. Bump up the allocator to the next available offset.

    // Allocate the system drive.
    allocSystemDrive(alloc, partEntryIt, mediaTable);

    // Allocate the hidden drives.
    allocHiddenDrives(alloc, partEntryIt, mediaTable);

    // Verify that the minimum drive size is available for the data drive.
    if ((m_u64SizeInBytes - alloc.getByteOffset()) < kMbrMinDataDriveSizeInBytes)
    {
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }

    // Allocate the data drive to use the remainder of the media.
    allocDataDrive(alloc, partEntryIt);

    // Write the MBR.
    status = partTable.saveToDevice(*m_device);
    if (SUCCESS != status)
    {
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
            "MmcMedia: Failed to save partition table to device on port %d, error=0x%x\n", m_portId, status);
#endif
    }

    // We're done allocating!
    m_eState = kMediaStateAllocated;

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcMedia: Allocated media on port %d\n", m_portId);
#endif

    return status;
}

RtStatus_t MmcMedia::getMediaTable(MediaAllocationTable_t ** pTable)
{
    DdiMmcLocker locker;

    if (m_eState != kMediaStateAllocated)
    {
        return ERROR_DDI_LDL_LDRIVE_MEDIA_NOT_ALLOCATED;
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

RtStatus_t MmcMedia::freeMediaTable(MediaAllocationTable_t * table)
{
    DdiMmcLocker locker;

    if (table)
    {
        free((void *)table);
    }

    return SUCCESS;
}

RtStatus_t MmcMedia::getInfo(uint32_t infoSelector, void * value)
{
    DdiMmcLocker locker;

    if (infoSelector == kMediaInfoIsInitialized)
    {
        *((bool *)value) = m_bInitialized;
        return SUCCESS;
    }

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (infoSelector)
    {
        case kMediaInfoIsMediaPresent:
            *((bool *)value) = m_device ? true : false;
            break;

        case kMediaInfoPageSizeInBytes:
            *((uint32_t *)value) = m_u32AllocationUnitSizeInBytes;
            break;

        case kMediaInfoNumChipEnables:
            // Always 1 for MMC.
            *((uint32_t *)value) = 1;
            break;

        case kMediaInfoMediaMfgId:
            if (!m_device)
            {
                return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_DISCOVERED;
            }

            // Media Info Mfg Id byte from CID register.
            *((uint32_t *)value) = m_device->getManufacturerId();
            break;

        case kMediaInfoProductName:
        {
            if (!m_device)
            {
                return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_DISCOVERED;
            }

            // Product name from CID register.
            const char *name = m_device->getProductName();
            memcpy(value, name, strlen(name) + 1);
            break;
        }

        default:
            return LogicalMedia::getInfo(infoSelector, value);

    }

    return SUCCESS;
}

RtStatus_t MmcMedia::setInfo(uint32_t infoSelector, const void * value)
{
    DdiMmcLocker locker;

    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    switch (infoSelector)
    {
        case kMediaInfoExpectedTransferActivity:
        {
            if (!m_device)
            {
                return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_DISCOVERED;
            }

            TransferActivityType_t activity = *(TransferActivityType_t *)value;

            assert(m_transferManager);
            if (kTransferActivity_Random == activity)
            {
                // Random sector access activity (i.e. player mode) so optimize for power.
                m_transferManager->optimizeForPower();
            }
            else
            {
                // Sequential sector access activity (i.e. hostlink mode) so optimize for speed.
                m_transferManager->optimizeForSpeed();
            }

            return SUCCESS;
        }

        case kMediaInfoIsSleepAllowed:
            assert(m_transferManager);
            m_transferManager->enableSleep(*(bool *)value);
            return SUCCESS;

        default:
            return LogicalMedia::setInfo(infoSelector, value);
    }
}

RtStatus_t MmcMedia::erase()
{
    DdiMmcLocker locker;

    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // Make sure we're not write protected.
    if (m_bWriteProtected)
    {
        return ERROR_DDI_NAND_LMEDIA_MEDIA_WRITE_PROTECTED;
    }

    // Stop any open transfer manager operations.
    // This forces the device to be deselected on the bus.
    assert(m_transferManager);
    RtStatus_t status = m_transferManager->forceStop();
    if (SUCCESS != status)
    {
        return status;
    }

    // Erase the entire device.
    assert(m_device);
    status = m_device->erase();
    if (SUCCESS != status)
    {
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_ERROR | LOGTEXT_EVENT_DDI_MMC_GROUP,
            "MmcMedia: Failed to erase media on port %d, error=0x%x\n", m_portId, status);
#endif
        return status;
    }

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcMedia: Erased media on port %d\n", m_portId);
#endif

    // Update the Media State to Erased.
    m_eState = kMediaStateErased;

    return SUCCESS;
}

RtStatus_t MmcMedia::shutdown()
{
    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        // OK to shutdown uninitialized media.
        return SUCCESS;
    }

    DdiMmcLocker locker;

    // Release our device.
    MmcHal::releaseDevice(m_portId);
    m_device = 0;

    // Delete our transfer manager.
    if (m_transferManager)
    {
        delete m_transferManager;
    }

    // Shutdown the HAL.
    MmcHal::shutdown();

    // Zero out the LogicalMedia fields.
    m_u64SizeInBytes = 0;
    m_bWriteProtected = 0;
    m_bInitialized = 0;
    m_u32AllocationUnitSizeInBytes = 0;
    m_eState = kMediaStateUnknown;

    // Decrement the number of initialized media.
    --g_numMedia;

    // Destroy our synchronization object.
    if (!g_numMedia)
    {
        assert(g_mutexInitialized);
        tx_mutex_delete(&g_mmcThreadSafeMutex);
        g_mutexInitialized = false;
    }

#if defined(DEBUG_DDI_MMC) && !defined(NO_SDRAM)
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_MMC_GROUP,
        "MmcMedia: Shutdown media on port %d\n", m_portId);
#endif

    return SUCCESS;
}

RtStatus_t MmcMedia::flushDrives()
{
    DdiMmcLocker locker;

    // Make sure we're initialized.
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    assert(m_transferManager);
    RtStatus_t status = m_transferManager->stop();

    return SUCCESS;
}

RtStatus_t MmcMedia::setBootDrive(DriveTag_t tag)
{
    DdiMmcLocker locker;

    //! \todo This is for Live Update but not currently called. Could be implemented like NAND.
    return SUCCESS;
}

//! \brief Create internal media drives from partition table on media.
RtStatus_t MmcMedia::createInternalDrives()
{
    // Get a buffer.
    SectorBuffer buffer;
    RtStatus_t status = buffer.acquire();
    if (SUCCESS != status)
    {
        return status;
    }
    buffer.fill(0);

    // Read the partition table from the media.
    PartitionTable partTable(buffer);
    assert(m_device);
    status = partTable.initFromDevice(*m_device);
    if (SUCCESS != status)
    {
        // The MBR is not found, which can make Windows upset.
        // The firmware updater will fail with "Write Sector Failed" and you must
        // run it a second time. This is only an issue for new devices because they
        // don't yet have an MBR. Also, MfgTool does have this issue because
        // writes the entire image, including MBR, to the raw device.
#if !defined(NO_SDRAM)
        tss_logtext_Print(LOGTEXT_VERBOSITY_WARNING | LOGTEXT_EVENT_DDI_MMC_GROUP,
                "PartitionTable: Warning: Partition table not found\n");
#endif
        return status;
    }

    // Get a partition entry iterator.
    assert(partTable.getTable());
    PartitionTable::EntryIterator it(partTable.getTable());
    PartEntry_t* partEntry;

    // First hidden drive takes this tag.
    // We increment the hiddenTag for the second hidden drive found,
    // which makes the tag DRIVE_TAG_DATA_HIDDEN_2.
    uint32_t hiddenTag = DRIVE_TAG_DATA_HIDDEN;

    // Create one drive from each partition entry.
    while (partEntry = it.getNext())
    {
        LogicalDrive* logicalDrive = 0;

        if (partEntry->bootDescriptor == kPartBootIdBootable)
        {
            // This is the data drive.
            MmcDataDrive* drive = new MmcDataDrive;
            if (!drive)
            {
                return ERROR_OUT_OF_MEMORY;
            }

            drive->initFromPartitionEntry(this, partEntry);
            logicalDrive = drive;
        }
        else if (partEntry->fileSystem == kMbrSigmatelId)
        {
            // This is the system drive.
            MmcSystemDrive* drive = new MmcSystemDrive;
            if (!drive)
            {
                return ERROR_OUT_OF_MEMORY;
            }

            drive->initFromPartitionEntry(this, partEntry);
            logicalDrive = drive;
        }
        else
        {
            // This is a hidden data drive.
            MmcDataDrive *drive = new MmcDataDrive;
            if (!drive)
            {
                return ERROR_OUT_OF_MEMORY;
            }

            drive->initFromPartitionEntry(this, partEntry, kDriveTypeHidden, hiddenTag++);
            logicalDrive = drive;
        }

        // Add our new drive.
        RtStatus_t status = DriveAdd(logicalDrive);
        if (SUCCESS != status)
        {
            return status;
        }
    }

    return SUCCESS;
}

//! \brief Create external data drive from media.
RtStatus_t MmcMedia::createExternalDataDrive()
{
    MmcDataDrive *dataDrive = new MmcDataDrive;
    if (!dataDrive)
    {
        return ERROR_OUT_OF_MEMORY;
    }

    dataDrive->initFromMedia(this);

    // Add our new data drive.
    RtStatus_t status = DriveAdd(dataDrive);

    return status;
}

PartEntry_t* PartitionTable::EntryIterator::getNext()
{
    assert(m_partTable);
    if (m_current < kNumPartitionEntries)
    {
        return &m_partTable->partition[m_current++];
    }
    else
    {
        return 0;
    }
}

RtStatus_t PartitionTable::initFromDevice(MmcSdDevice &device) const
{
    // Read the MBR.
    RtStatus_t status = device.readBlock(kMbrBlockNumber, m_buffer);
    if (SUCCESS != status)
    {
        return status;
    }

    // Validate the MBR.
    assert(getTable());
    if (getTable()->signature != kPartSignature)
    {
        return ERROR_DDI_MMC_CONFIG_BLOCK_NOT_FOUND;
    }

    return SUCCESS;
}

RtStatus_t PartitionTable::saveToDevice(MmcSdDevice &device) const
{
    // Prepare the partition table.
    assert(getTable());
    getTable()->signature = kPartSignature;
    uint64_t totalSectors = device.getCapacityInBytes() / kMbrBytesPerSector;
    updateChsEntries(totalSectors, getTable());

    // Write the MBR.
    RtStatus_t status = device.writeBlock(kMbrBlockNumber, m_buffer);
    if (SUCCESS != status)
    {
        return status;
    }

    return SUCCESS;
}

uint64_t Allocator::reserve(uint64_t byteCount)
{
    // Round up count to next unit size.
    const uint64_t actualSize = ROUND_UP(byteCount, m_unitSizeBytes);
    m_byteOffset += actualSize;
    return actualSize;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
