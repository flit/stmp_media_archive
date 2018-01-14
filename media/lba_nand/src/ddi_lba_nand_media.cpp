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
//! \file ddi_lba_nand_media_.cpp
//! \brief This file implements acces to the LBA NAND Media.
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <stdlib.h>
#include "ddi_lba_nand_internal.h"
#include "ddi_lba_nand_mbr.h"
#include "drivers/media/nand/rom_support/rom_nand_boot_blocks.h"
#include "drivers/media/nand/rom_support/ddi_nand_hamming_code_ecc.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "application/framework/sdk_os_media_player/lba_nand_bootlet/src/lba_nand_configblock.h"
#include "drivers/media/nand/gpmi/ddi_nand_gpmi.h"
#include "drivers/media/nand/gpmi/ddi_nand_ecc.h"
#include "hw/core/vmemory.h"
#include "os/dpc/os_dpc_api.h"
#include "os/thi/os_thi_api.h"
#include "hw/digctl/hw_digctl.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! Order of allocated hidden drives.
const DriveTag_t kHiddenDriveTag[] = { DRIVE_TAG_DATA_HIDDEN, DRIVE_TAG_DATA_HIDDEN_2 };

//! Minimum data drive sector count is 2MB worth of 2K sectors.
const unsigned k_uMinDataDriveSectorCount = (8 * 256 * 1024) / 2048;

//! Size of VFP sector in boot mode.
//! \todo Can the hal tell us this?
const unsigned k_uBootModeSectorSize = 512;

//! Number of bytes to add to the VFP size when allocating. This is to try to
//! ensure that the VFP is large enough to prevent future repartitioning.
const unsigned kVfpAdditionalBytes = (32 * 1024 * 1024);

//! Delay in milliseconds to enable power save mode.
const unsigned kPowerSaveEnableDelay = 10;

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//! \brief Return number of sectors required for requested byte size.
//!
//! \fntype Function
//!
//! Round bytes to the nearest sector boundry and return sector count.
//!
//! \param[in] u64NumBytes Number of bytes.
//! \param[in] uBytesPerSector Bytes per sector.
//!
//! \return Number of sectors.
///////////////////////////////////////////////////////////////////////////////
inline unsigned LbaNandMedia::roundBytesToSectors(uint64_t u64NumBytes,
                                                  unsigned uBytesPerSector)
{
    return (u64NumBytes + (uBytesPerSector - 1)) / uBytesPerSector;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Return the MBR file system ID appropriate for the specified drive size.
//!
//! \fntype Function
//!
//! \param[in] u64ByteCount Number of bytes in drive.
//!
//! \return File system ID.
///////////////////////////////////////////////////////////////////////////////
inline uint8_t LbaNandMedia::sysIdForSize(uint64_t u64ByteCount)
{
    if (u64ByteCount <= (4 * k_iOneMb))
    {
        return k_ePartSysIdFat12;
    }
    else if (u64ByteCount <= (32 * k_iOneMb))
    {
        return k_ePartSysIdFat16;
    }
    else
    {
        return k_ePartSysIdFat32;
    }
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT LbaNandMedia::LbaNandMedia()
{
    m_uNumPhysicalMedia = 0;
    m_uNumDrives = 0;
    resetDrives();
    
    // Make sure power save mode is disabled. We won't enable auto power
    // management until discovery is complete.
    m_managePowerSave = false;
    m_powerSaveEnabled = true;  // Set this to the opposite of what we pass...
    enableAllPowerSaveMode(false); // ...into this function.
    
    // But go ahead and create our power save timer.
    tx_timer_create(&m_powerSaveTimer, "LBA-NAND:powersave",
        (void (*)(ULONG))enterPowerSaveModeTimer,
        (ULONG)this, // param
        0, // sched ticks
        0, // resched tics
        false); // auto activate
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT void LbaNandMedia::resetDrives()
{
    for (int i = 0; i < m_uNumDrives; i++)
    {
        if (m_pDrive[i])
        {
            delete m_pDrive[i];
            m_pDrive[i] = NULL;
        }
    }
    m_uNumDrives = 0;

    // No bootlet drive.
    m_bootletDrive = NULL;

    // Start with one sector allocated for the config block in the VFP.
    m_uVfpSectorsAllocated = 1;

    // Start with one sector allocated for the MBR in the MDP.
    m_uMdpSectorsAllocated = 1;

    m_u64SizeInBytes = 0;
    m_uNumSystemDrives = 0;
    m_uNumHiddenDrives = 0;
    m_uNumDataDrives = 0;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
LbaNandMedia::~LbaNandMedia()
{
    resetDrives();
    
    // Dispose of the power save timer.
    tx_timer_delete(&m_powerSaveTimer);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::flush()
{
    RtStatus_t Status;
    
    // Flush each device.
    for (int i = 0; i < m_uNumPhysicalMedia; i++)
    {
        // It can take longer to flush than the delay to enter power save mode,
        // so we must make sure to exit power save before each device.
        exitPowerSaveMode();

        // Only flush the data partition.
        Status = m_pPhysicalMedia[i]->getDataPartition()->flushCache();
        if (Status != SUCCESS)
        {
            return Status;
        }
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::erase(uint8_t u8DoNotEraseHidden)
{
    RtStatus_t Status;

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    
    // Note: We never erase the PNP.

    // Erase the VFP and MDP on all devices.
    for (int iDevice = 0; iDevice < m_uNumPhysicalMedia; iDevice++)
    {
        assert(m_pPhysicalMedia[iDevice]);

        // Get the VFP.
        LbaNandPhysicalMedia::LbaPartition *pPartition = m_pPhysicalMedia[iDevice]->getFirmwarePartition();
        assert(pPartition);

        // Erase the VFP.
        Status = pPartition->eraseSectors(0, pPartition->getSectorCount());
        if (Status != SUCCESS)
        {
            return Status;
        }

        // Get the MDP.
        pPartition = m_pPhysicalMedia[iDevice]->getDataPartition();
        assert(pPartition);

        uint32_t u32StartSector = 0;
        uint32_t u32SectorCount = pPartition->getSectorCount();

        // If this is the first device and u8DoNotEraseHidden is specified,
        // only erase the data drive. Note that the hidden drive data will still be
        // lost if we have to repartition the device in the allocate call that
        // follows this media erase. To try to prevent this, we intentionally increase
        // the VFP size over what is actually needed and only repartition if there
        // is not enough room to hold all system drives. However, there will still
        // be cases where we must repartition.
        if ((iDevice == 0) && u8DoNotEraseHidden)
        {
            uint32_t u32DataDriveStartSector = 0;

            Status = readDataDriveInfo(&u32DataDriveStartSector);
            if (Status == SUCCESS)
            {
                u32StartSector = u32DataDriveStartSector;
                u32SectorCount -= u32StartSector;
            }
        }

        // Erase the MDP.
        Status = pPartition->eraseSectors(u32StartSector, u32SectorCount);
        if (Status != SUCCESS)
        {
            return Status;
        }
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::addPhysicalMedia(LbaNandPhysicalMedia *pPhysicalMedia)
{
    assert(pPhysicalMedia);

    if (m_uNumPhysicalMedia >= k_uMaxPhysicalMedia)
    {
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }

    // Get the partition objects.
    LbaNandPhysicalMedia::LbaPartition *pVfp = pPhysicalMedia->getFirmwarePartition();
    assert(pVfp);
    LbaNandPhysicalMedia::LbaPartition *pMdp = pPhysicalMedia->getDataPartition();
    assert(pMdp);
    LbaNandPhysicalMedia::LbaPartition *pPnp = pPhysicalMedia->getBootPartition();
    assert(pPnp);

    // Increment the total media size.
    uint64_t u64PhysicalMediaSize = (uint64_t)pVfp->getSectorCount() * pVfp->getSectorSize();
    u64PhysicalMediaSize += (uint64_t)pMdp->getSectorCount() * pMdp->getSectorSize();
    u64PhysicalMediaSize += (uint64_t)pPnp->getSectorCount() * pPnp->getSectorSize();
    m_u64SizeInBytes += u64PhysicalMediaSize;

    m_pPhysicalMedia[m_uNumPhysicalMedia++] = pPhysicalMedia;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::addBootletDrive()
{
    // Is there already a bootlet drive?
    if (m_bootletDrive)
    {
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }
    assert(m_uNumDrives <= k_uMaxDrives);

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    assert(m_pPhysicalMedia[0]);

    BootletDrive *pDrive = new BootletDrive(this);
    assert(pDrive);

    // Init the bootlet drive and pass it the first LBA-NAND device.
    pDrive->init(m_pPhysicalMedia[0]);

    m_pDrive[m_uNumDrives++] = pDrive;
    m_bootletDrive = pDrive;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
//! \todo Need to support drive tag values specified by the media allocation table.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::addSystemDrive(uint64_t u64SizeInBytes, DriveTag_t tag)
{
    if (m_uNumSystemDrives >= k_uMaxSystemDrives)
    {
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }
    assert(m_uNumDrives <= k_uMaxDrives);

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    assert(m_pPhysicalMedia[0]);

    // All system drives go on the VFP of the first device.
    LbaNandPhysicalMedia::LbaPartition *pVfp = m_pPhysicalMedia[0]->getFirmwarePartition();
    assert(pVfp);

    LbaNandMedia::Drive *pDrive = new LbaNandMedia::Drive(this, kDriveTypeSystem, tag);
    assert(pDrive);

    unsigned uNumSectorsRequired = roundBytesToSectors(u64SizeInBytes, pVfp->getSectorSize());

    // A system drive cannot span devices, so it has only one region.
    pDrive->addRegion(m_pPhysicalMedia[0], pVfp, m_uVfpSectorsAllocated, uNumSectorsRequired);

    m_pDrive[m_uNumDrives++] = pDrive;

    // Increment the total number of sectors allocated on the VFP so far.
    m_uVfpSectorsAllocated += uNumSectorsRequired;

    m_uNumSystemDrives++;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
//! \todo Need to support drive tag values specified by the media allocation table.
//!     To do this, we have to store the tag value somewhere in the MBR.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::addHiddenDrive(uint64_t u64SizeInBytes, uint64_t *pu64AllocatedSize, DriveTag_t tag)
{
    assert(pu64AllocatedSize);

    if (m_uNumHiddenDrives >= k_uMaxHiddenDrives)
    {
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }
    assert(m_uNumDrives <= k_uMaxDrives);

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    assert(m_pPhysicalMedia[0]);

    // All hidden drives go on the MDP of the first device.
    LbaNandPhysicalMedia::LbaPartition *pMdp = m_pPhysicalMedia[0]->getDataPartition();
    assert(pMdp);

    // Hidden drives can be any size, but are set to the minimum if the size is 0.
    unsigned uNumSectorsRequired = roundBytesToSectors(u64SizeInBytes, pMdp->getSectorSize());
    if (uNumSectorsRequired == 0)
    {
        uNumSectorsRequired = k_uMinDataDriveSectorCount;
    }

    // Verify this drive will fit in the MDP.
    if ((m_uMdpSectorsAllocated + uNumSectorsRequired) > pMdp->getSectorCount())
    {
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }

    LbaNandMedia::Drive *pDrive =
        new LbaNandMedia::Drive(this, kDriveTypeHidden, kHiddenDriveTag[m_uNumHiddenDrives]);
    assert(pDrive);

    // A hidden drive cannot span devices, so it has only one region.
    pDrive->addRegion(m_pPhysicalMedia[0], pMdp, m_uMdpSectorsAllocated, uNumSectorsRequired);

    m_pDrive[m_uNumDrives++] = pDrive;

    // Increment the total number of sectors allocated on the MDP so far.
    m_uMdpSectorsAllocated += uNumSectorsRequired;

    m_uNumHiddenDrives++;

    // Return the size actually allocated.
    *pu64AllocatedSize = uNumSectorsRequired * pMdp->getSectorSize();

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::addDataDrive(uint64_t *pu64AllocatedSize)
{
    assert(pu64AllocatedSize);

    if (m_uNumDataDrives >= k_uMaxDataDrives)
    {
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }
    assert(m_uNumDrives <= k_uMaxDrives);

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // Only one data drive is allowed, and it must be added after the
    // hidden drives are added. The data drive uses the remainder of space on the
    // first device and spans across all the rest of the devices.
    LbaNandMedia::Drive *pDrive =
        new LbaNandMedia::Drive(this, kDriveTypeData, DRIVE_TAG_DATA);
    assert(pDrive);

    uint64_t u64NumBytesAllocated = 0;

    for (int iDevice = 0; iDevice < m_uNumPhysicalMedia; iDevice++)
    {
        assert(m_pPhysicalMedia[iDevice]);

        // The data drive goes on the MDP of this device.
        LbaNandPhysicalMedia::LbaPartition *pMdp = m_pPhysicalMedia[iDevice]->getDataPartition();
        assert(pMdp);

        unsigned uNumSectorsToAllocate = pMdp->getSectorCount();
        unsigned uFirstSector = 0;

        // On device zero, decrease the number of sectors available
        // by the number allocated so far for hidden drives.
        if (iDevice == 0)
        {
            uNumSectorsToAllocate -= m_uMdpSectorsAllocated;
            uFirstSector = m_uMdpSectorsAllocated;
        }

        // Skip this device if the minimum number of sectors is not available.
        if (uNumSectorsToAllocate < k_uMinDataDriveSectorCount)
        {
            continue;
        }

        pDrive->addRegion(m_pPhysicalMedia[iDevice], pMdp, uFirstSector, uNumSectorsToAllocate);

        u64NumBytesAllocated += uNumSectorsToAllocate * pMdp->getSectorSize();
    }

    if (u64NumBytesAllocated == 0)
    {
        delete pDrive;
        return ERROR_DDI_LDL_LMEDIA_ALLOCATION_TOO_LARGE;
    }

    m_pDrive[m_uNumDrives++] = pDrive;

    m_uNumDataDrives++;

    // Return the size actually allocated.
    *pu64AllocatedSize = u64NumBytesAllocated;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::commitSystemDrives()
{
    RtStatus_t Status;

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    assert(m_pPhysicalMedia[0]);

    // Must have at least one system drive.
    if (m_uNumSystemDrives == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    
    // Set the VFP size on all devices
    for (int iDevice = 0; iDevice < m_uNumPhysicalMedia; iDevice++)
    {
        // Get the current size of the VFP on this device.
        LbaNandPhysicalMedia::LbaPartition * vfp = m_pPhysicalMedia[iDevice]->getFirmwarePartition();
        assert(vfp);
        uint32_t currentVfpSize = vfp->getSectorCount();
        
        // Only repartition the device if:
        //  - For device 0: repartition if the VFP is too small to hold all of the desired drives
        //  - For all other devices: repartition if the VFP size is non-zero
        if ((iDevice == 0 && currentVfpSize < m_uVfpSectorsAllocated) || (iDevice != 0 && currentVfpSize != 0))
        {
            unsigned uVfpSize = 0;

            // On device 0, the VFP size is the size of all the firmware copies.
            if (iDevice == 0)
            {
                assert(m_uVfpSectorsAllocated != 0);
                
                // Add extra sectors to the VFP size so we usually won't have to
                // ever repartition again.
                uVfpSize = m_uVfpSectorsAllocated + kVfpAdditionalBytes / vfp->getSectorSize();
            }
            
            Status = m_pPhysicalMedia[iDevice]->setVfpSize(uVfpSize);
            if (Status != SUCCESS)
            {
                return Status;
            }
        }
    }

    // Get a buffer.
    SectorBuffer buffer;
    if (buffer.didFail())
    {
        return buffer.getStatus();
    }

    // Write boot blocks to the PNP.
    if (m_bootletDrive)
    {
        // This call flushes the partition after writing.
        Status = m_bootletDrive->writeBootBlocks(buffer);
        if (Status != SUCCESS)
        {
            return Status;
        }
    }

    // Format and write the config block to the VFP.
    Status = writeConfigBlock(buffer);
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Flush the VFP cache on the first physical device.
    Status = m_pPhysicalMedia[0]->getFirmwarePartition()->flushCache();

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::commitDataDrives()
{
    RtStatus_t Status;

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    assert(m_pPhysicalMedia[0]);

    // Must have at least one hidden drive.
    if (m_uNumHiddenDrives == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    
    // Get a buffer.
    SectorBuffer buffer;
    if (buffer.didFail())
    {
        return buffer.getStatus();
    }

    // Format and write the MBR to the MDP.
    Status = writeMbr(buffer);
    if (Status != SUCCESS)
    {
       return Status;
    }

    // Flush the media cache on the first physical device.
    Status = m_pPhysicalMedia[0]->getDataPartition()->flushCache();

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::loadDrives()
{
    RtStatus_t Status;

    if (m_uNumPhysicalMedia == 0)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }
    assert(m_pPhysicalMedia[0]);

    resetDrives();

    // Create the fixed size bootlet drive.
    BootletDrive *pDrive = new BootletDrive(this);
    assert(pDrive);

    pDrive->init(m_pPhysicalMedia[0]);

    m_pDrive[m_uNumDrives++] = pDrive;
    m_bootletDrive = pDrive;

    // Get a buffer.
    SectorBuffer buffer;
    if (buffer.didFail())
    {
        return buffer.getStatus();
    }

    // Read the config block from the VFP and create the system drives.
    Status = readConfigBlock(buffer);
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Read the MBR from the MDP and create the hidden and data drives.
    Status = readMbr(buffer);

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
LbaNandMedia::Drive *LbaNandMedia::getDriveAtIndex(unsigned uIndex) const
{
    if (uIndex < m_uNumDrives)
    {
        return m_pDrive[uIndex];
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Create and write the MBR.
//!
//! \fntype Function
//!
//! Create and write the MBR to the data partition of the first physical device.
//!
//! \param[in] buffer Sector Buffer.
//!
//! \return Status of call or error.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::writeMbr(SectorBuffer & buffer)
{
    // Get the MDP on the first device.
    LbaNandPhysicalMedia::LbaPartition *pMdp = m_pPhysicalMedia[0]->getDataPartition();
    assert(pMdp);

    unsigned uSectorSize = pMdp->getSectorSize();
    buffer.fill(0);

    // Fill in the Partition Table.
    // The first and second partitions point to the hidden drives.
    // The third partition points to the data drive.
    // The fourth partition is unused.
    Mbr::PartitionTable_t *pPartitionTable = (Mbr::PartitionTable_t *)buffer.getBuffer();
    pPartitionTable->u16Signature = k_iPartSignature;
    Mbr::PartitionEntry_t *pPartitionEntry = pPartitionTable->Partitions;

    // We only support two different hidden drive tags.
    assert(m_uNumHiddenDrives <= k_uMaxHiddenDrives);

    int iPartitionIndex;

    // Fill in the Partition Table Entries for the Hidden Drives.
    for (iPartitionIndex = 0; iPartitionIndex < m_uNumHiddenDrives; iPartitionIndex++)
    {
        LbaNandMedia::Drive *pDrive = getDriveForTag(kHiddenDriveTag[iPartitionIndex]);
        assert(pDrive);

        pPartitionEntry[iPartitionIndex].u8FileSystem = sysIdForSize(pDrive->getSectorCount() * uSectorSize);
        pPartitionEntry[iPartitionIndex].u8BootDescriptor = 0; // non-bootable
        pPartitionEntry[iPartitionIndex].u32FirstSectorNumber = pDrive->getFirstSectorNumber();
        pPartitionEntry[iPartitionIndex].u32SectorCount = pDrive->getSectorCount();
    }

    // Fill in the Partition Table Entries for the Data Drive.
    LbaNandMedia::Drive *pDrive = getDriveForTag(DRIVE_TAG_DATA);
    assert(pDrive);

    pPartitionEntry[iPartitionIndex].u8FileSystem = sysIdForSize(pDrive->getSectorCount() * uSectorSize);
    pPartitionEntry[iPartitionIndex].u8BootDescriptor = k_ePartBootIdBootable;
    pPartitionEntry[iPartitionIndex].u32FirstSectorNumber = pDrive->getFirstSectorNumber();
    pPartitionEntry[iPartitionIndex].u32SectorCount = pDrive->getSectorCount();

    //! \todo Fill in CHS entries.

    // Write the Partition Table.
    return pMdp->writeSector(k_u32MbrSectorNumber, buffer);
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Create and write the Config Block.
//!
//! \fntype Function
//!
//! Create and write the firmware config block to the vender firmware partition
//! of the first physical device.
//!
//! \param[in] buffer Sector Buffer.
//!
//! \return Status of call or error.
//! \todo Set the primary and secondary boot tags from the first and second
//!     drives, not from hard coded values.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::writeConfigBlock(SectorBuffer & buffer)
{
    // Get the VFP on the first device.
    LbaNandPhysicalMedia::LbaPartition *pVfp = m_pPhysicalMedia[0]->getFirmwarePartition();
    assert(pVfp);

    buffer.fill(0);

    // Fill in the config block.
    lba_nand_ConfigBlock_t *pConfigBlock = (lba_nand_ConfigBlock_t *)buffer.getBuffer();
    pConfigBlock->u32Signature = LBA_NAND_CB_SIGNATURE;
    pConfigBlock->version = LBA_NAND_CB_VERSION;
    pConfigBlock->u32PrimaryBootTag = DRIVE_TAG_BOOTMANAGER_S;
    pConfigBlock->u32SecondaryBootTag = DRIVE_TAG_BOOTMANAGER2_S;
    pConfigBlock->u32NumCopies = m_uNumSystemDrives;

    assert(m_uNumSystemDrives <= k_uMaxSystemDrives);

    int i = 0;
    Drive * pDrive;
    DriveIterator iter(this);
    while ((pDrive = iter.next()) != NULL)
    {
        if ((pDrive->getType() == kDriveTypeSystem) && (pDrive->getTag() != DRIVE_TAG_BOOTLET_S))
        {
            pConfigBlock->DriveInfo[i].u32ChipNum = 0;
            pConfigBlock->DriveInfo[i].u32DriveType = (uint32_t) pDrive->getType();
            pConfigBlock->DriveInfo[i].u32Tag = pDrive->getTag();

            // Sector numbers and counts stored in the config block are
            // in terms of boot mode sector size.
            uint64_t u64SizeInBytes = pDrive->getFirstSectorNumber() * pDrive->getSectorSize();
            pConfigBlock->DriveInfo[i].u32FirstSectorNumber = u64SizeInBytes / k_uBootModeSectorSize;
            u64SizeInBytes = pDrive->getSectorCount() * pDrive->getSectorSize();
            pConfigBlock->DriveInfo[i].u32SectorCount = u64SizeInBytes / k_uBootModeSectorSize;
            i++;
        }
    }

    // Write the config block.
    return pVfp->writeSector(k_u32ConfigBlockSectorNumber, buffer);
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Get the drive object associated with the specified tag.
//!
//! \fntype Function
//!
//! Return a pointer to the first drive that uses the specified drive tag.
//!
//! \param[in] Tag Drive tag.
//!
//! \return Drive object associated with tag.
//! \retval Pointer to drive object if found.
//! \retval NULL if drive not found.
///////////////////////////////////////////////////////////////////////////////
LbaNandMedia::Drive *LbaNandMedia::getDriveForTag(DriveTag_t Tag) const
{
    for (int i = 0; i < m_uNumDrives; i++)
    {
        if (m_pDrive[i]->getTag() == Tag)
        {
            return m_pDrive[i];
        }
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Read the MBR and create associated drives.
//!
//! \fntype Function
//!
//! Read the MBR from the data partition of the first physical device and create
//! associated drive objects.
//!
//! \param[in] buffer Sector Buffer.
//!
//! \return Status of call or error.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::readMbr(SectorBuffer & buffer)
{
    RtStatus_t Status;

    // Get the MDP on the first device.
    LbaNandPhysicalMedia::LbaPartition *pMdp = m_pPhysicalMedia[0]->getDataPartition();
    assert(pMdp);

    // Read the MBR.
    Status = pMdp->readSector(k_u32MbrSectorNumber, buffer);
    if (Status != SUCCESS)
    {
       return Status;
    }

    // Verify the MBR.
    Mbr::PartitionTable_t *pPartitionTable = (Mbr::PartitionTable_t *)buffer.getBuffer();
    if (pPartitionTable->u16Signature != k_iPartSignature)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    Mbr::PartitionEntry_t *pPartitionEntry = pPartitionTable->Partitions;
    int iPartitionIndex;

    // Get the Partition Table Entries for the Hidden Drives
    for (iPartitionIndex = 0; iPartitionIndex < k_iPtblMaxNumEntries; iPartitionIndex++)
    {
        // Ignore the rest of the drives if we run out of drives.
        if (m_uNumDrives >= k_uMaxDrives)
        {
            break;
        }

        // Stop when we've found the data drive.
        if (pPartitionEntry[iPartitionIndex].u8BootDescriptor == k_ePartBootIdBootable)
        {
            break;
        }

        // We only support two different hidden drive tags.
        if (m_uNumHiddenDrives > k_uMaxHiddenDrives)
        {
            continue;
        }

        // Boot Descriptor should be zero for hidden drives.
        if (pPartitionEntry[iPartitionIndex].u8BootDescriptor != 0)
        {
            continue;
        }

        LbaNandMedia::Drive *pDrive =
            new LbaNandMedia::Drive(this, kDriveTypeHidden, kHiddenDriveTag[iPartitionIndex]);
        assert(pDrive);

        // A hidden drive cannot span devices, so it has only one region.
        pDrive->addRegion(m_pPhysicalMedia[0], pMdp,
                          pPartitionEntry[iPartitionIndex].u32FirstSectorNumber,
                          pPartitionEntry[iPartitionIndex].u32SectorCount);

        m_pDrive[m_uNumDrives++] = pDrive;

        m_uNumHiddenDrives++;
    }

    // I suppose it is OK not to find a data drive.
    if (iPartitionIndex == k_iPtblMaxNumEntries)
    {
        return SUCCESS;
    }

    assert(m_uNumDrives < k_uMaxDrives);

    LbaNandMedia::Drive *pDrive =
        new LbaNandMedia::Drive(this, kDriveTypeData, DRIVE_TAG_DATA);
    assert(pDrive);

    m_pDrive[m_uNumDrives++] = pDrive;

    m_uNumDataDrives++;

    int iDevice = 0;
    unsigned uTotalSectorCount = pPartitionEntry[iPartitionIndex].u32SectorCount;
    unsigned uDiscoveredSectorCount = 0;

    while (uDiscoveredSectorCount < uTotalSectorCount)
    {
        // If we run out of physical media devices before we run out sectors, just quit.
        if (iDevice >= m_uNumPhysicalMedia)
        {
            break;
        }
        assert(m_pPhysicalMedia[iDevice]);

        // Get the total number of sectors on this device.
        LbaNandPhysicalMedia::LbaPartition *pMdp = m_pPhysicalMedia[iDevice]->getDataPartition();
        assert(pMdp);
        unsigned uDeviceSectorCount = pMdp->getSectorCount();

        unsigned uFirstSectorNumber = 0;

        // On device 0, decrease the number of sectors available on this device by the
        // starting sector of the drive.
        if (iDevice == 0)
        {
            uDeviceSectorCount -= pPartitionEntry[iPartitionIndex].u32FirstSectorNumber;
            uFirstSectorNumber = pPartitionEntry[iPartitionIndex].u32FirstSectorNumber;
        }

        pDrive->addRegion(m_pPhysicalMedia[iDevice], pMdp, uFirstSectorNumber, uDeviceSectorCount);

        uDiscoveredSectorCount += uDeviceSectorCount;
        iDevice++;
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Read data drive info from the MBR.
//!
//! \fntype Function
//!
//! Read the MBR and return info about the data drive.
//!
//! \param[out] pu32StartSector Returned starting sector number.
//!
//! \return Status of call or error.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::readDataDriveInfo(uint32_t *pu32StartSector) const
{
    RtStatus_t Status;

    assert(pu32StartSector);

    // Get a buffer.
    SectorBuffer buffer;
    if (buffer.didFail())
    {
        return buffer.getStatus();
    }

    // Get the MDP on the first device.
    LbaNandPhysicalMedia::LbaPartition *pMdp = m_pPhysicalMedia[0]->getDataPartition();
    assert(pMdp);

    // Read the MBR.
    Status = pMdp->readSector(k_u32MbrSectorNumber, buffer);
    if (Status != SUCCESS)
    {
        return Status;
    }

    // Verify the MBR.
    Mbr::PartitionTable_t *pPartitionTable = (Mbr::PartitionTable_t *)buffer.getBuffer();
    if (pPartitionTable->u16Signature != k_iPartSignature)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    Mbr::PartitionEntry_t *pPartitionEntry = pPartitionTable->Partitions;
    int iPartitionIndex;

    for (iPartitionIndex = 0; iPartitionIndex < k_iPtblMaxNumEntries; iPartitionIndex++)
    {
        // Stop when we've found the data drive.
        if (pPartitionEntry[iPartitionIndex].u8BootDescriptor == k_ePartBootIdBootable)
        {
            break;
        }
    }

    // Return an error if the data drive is not found.
    if (iPartitionIndex == k_iPtblMaxNumEntries)
    {
        return ERROR_GENERIC;
    }

    *pu32StartSector = pPartitionEntry[iPartitionIndex].u32FirstSectorNumber;

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Read the Config Block and create associated drives.
//!
//! \fntype Function
//!
//! Read the firmware config block from the vender firmware partition of the
//! first physical device and create associated drive objects.
//!
//! \param[in] buffer Sector Buffer.
//!
//! \return Status of call or error.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::readConfigBlock(SectorBuffer & buffer)
{
    RtStatus_t Status;

    // Get the VFP on the first device.
    LbaNandPhysicalMedia::LbaPartition *pVfp = m_pPhysicalMedia[0]->getFirmwarePartition();
    assert(pVfp);

    // Read the config block.
    Status = pVfp->readSector(k_u32ConfigBlockSectorNumber, buffer);
    if (Status != SUCCESS)
    {
       return Status;
    }

    // Verify the config block.
    lba_nand_ConfigBlock_t *pConfigBlock = (lba_nand_ConfigBlock_t *)buffer.getBuffer();
    if ((pConfigBlock->u32Signature != LBA_NAND_CB_SIGNATURE) || (pConfigBlock->version != LBA_NAND_CB_VERSION)
        || (pConfigBlock->u32NumCopies == 0))
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    m_uNumSystemDrives = pConfigBlock->u32NumCopies;

    // We only support two different system drive tags.
    if (m_uNumSystemDrives > k_uMaxSystemDrives)
    {
        m_uNumSystemDrives = k_uMaxSystemDrives;
    }

    for (int i = 0; i < m_uNumSystemDrives; i++)
    {
        assert(m_uNumDrives < k_uMaxDrives);

        LbaNandMedia::Drive *pDrive =
            new LbaNandMedia::Drive(this, (LogicalDriveType_t)pConfigBlock->DriveInfo[i].u32DriveType,
                                     (DriveTag_t)pConfigBlock->DriveInfo[i].u32Tag);
        assert(pDrive);

        // Sector numbers and counts stored in the config block are
        // in terms of boot mode sector size.
        uint64_t u64SizeInBytes = pConfigBlock->DriveInfo[i].u32FirstSectorNumber * k_uBootModeSectorSize;
        unsigned uFirstSectorNumber = u64SizeInBytes / pVfp->getSectorSize();
        u64SizeInBytes = pConfigBlock->DriveInfo[i].u32SectorCount * k_uBootModeSectorSize;
        unsigned uSectorCount = u64SizeInBytes / pVfp->getSectorSize();

        // A system drive cannot span devices, so it has only one region.
        pDrive->addRegion(m_pPhysicalMedia[0], pVfp, uFirstSectorNumber, uSectorCount);

        m_pDrive[m_uNumDrives++] = pDrive;
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT LbaNandMedia::Drive::Drive(LbaNandMedia * media, LogicalDriveType_t eType, DriveTag_t Tag)
{
    m_media = media;
    m_eType = eType;
    m_Tag = Tag;
    m_u32SectorCount = 0;
    m_uNumRegions = 0;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
LbaNandMedia::Drive::~Drive()
{
    for (int i = 0; i < m_uNumRegions; i++)
    {
        delete m_pRegion[i];
    }
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT void LbaNandMedia::Drive::addRegion(LbaNandPhysicalMedia *pPhysicalMedia,
                                    LbaNandPhysicalMedia::LbaPartition *pPartition,
                                    uint32_t u32FirstSectorNumber,
                                    uint32_t u32SectorCount)
{
    assert(pPartition);
    assert(m_uNumRegions < k_uMaxRegions);

    Region *pRegion = new Region(pPhysicalMedia, pPartition, u32FirstSectorNumber, u32SectorCount);
    assert(pRegion);

    m_pRegion[m_uNumRegions++] = pRegion;

    // Increment the total sector count.
    m_u32SectorCount += u32SectorCount;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT uint32_t LbaNandMedia::Drive::getSectorSize() const
{
    // Return the sector size of the first region.
    if (m_uNumRegions >= 1)
    {
        return m_pRegion[0]->getSectorSize();
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT uint32_t LbaNandMedia::Drive::getFirstSectorNumber() const
{
    // Return the first sector number of the first region.
    if (m_uNumRegions >= 1)
    {
        return m_pRegion[0]->getFirstSectorNumber();
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Return the region object that contains the specified sector.
//!
//! \fntype Function
//!
//! The sector number passed in is relative to the entire drive. The region
//! corresponding to this sector is found and returned. In addition, the sector
//! number is updated to be relative to the start of the region.
//!
//! \param[in,out] pu32SectorNumber Sector number.
//!
//! \return Region.
//! \retval Pointer to region object if found.
//! \retval NULL if region not found.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT LbaNandMedia::Drive::Region *LbaNandMedia::Drive::regionForSector(uint32_t *pu32SectorNumber) const
{
    assert(pu32SectorNumber);

    LbaNandMedia::Drive::Region *pRegion = NULL;

    for (int i = 0; i < m_uNumRegions; i++)
    {
        uint32_t uRegionSectorCount = m_pRegion[i]->getSectorCount();

        // See if this sector falls in this region.
        if (*pu32SectorNumber < uRegionSectorCount)
        {
            pRegion = m_pRegion[i];
            break;
        }

        // Move on to try the next region.
        *pu32SectorNumber -= uRegionSectorCount;
    }

    return pRegion;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::Drive::writeSector(uint32_t u32SectorNumber, const SECTOR_BUFFER *pBuffer)
{
    LbaNandMedia::Drive::Region *pRegion = regionForSector(&u32SectorNumber);

    if (!pRegion)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }
    
    // Manage power save mode.
    m_media->exitPowerSaveMode();

#if INTERNAL_MANAGED_BLOCK_LENGTH
#else
    // Checking the expected transfer activity type is a temporary solution to handle 
    // the different access behaviors between hostlink and player
    if(m_eType == kDriveTypeData||
       m_eType ==  kDriveTypeHidden)   // remove this constraint if the media trnsfer activity type is applying to all drives
    {
        if(m_media->getTransferActivityType() == kTransferActivity_Random)
        {
            pRegion->startTransferSequence(1);
        }
    }
#endif

    return pRegion->writeSector(u32SectorNumber, pBuffer);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t LbaNandMedia::Drive::readSector(uint32_t u32SectorNumber, SECTOR_BUFFER *pBuffer)
{
    LbaNandMedia::Drive::Region *pRegion = regionForSector(&u32SectorNumber);

    if (!pRegion)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    if( g_LbaNandMediaInfo.shouldExitPowerSaveOnTransfer() )
    {  // Manage power save mode.
        m_media->exitPowerSaveMode();
    }

#if INTERNAL_MANAGED_BLOCK_LENGTH
#else
    // Checking the expected transfer activity type is a temporary solution to handle 
    // the different access behaviors between hostlink and player
    if(m_eType == kDriveTypeData||
       m_eType ==  kDriveTypeHidden)   // remove this constraint if the media trnsfer activity type is applying to all drives
    {
        if(m_media->getTransferActivityType() == kTransferActivity_Random)
        {
            pRegion->startTransferSequence(1);
        }
    }
#endif
    
    return pRegion->readSector(u32SectorNumber, pBuffer);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t LbaNandMedia::Drive::flush()
{
    for (int i = 0; i < m_uNumRegions; i++)
    {
        RtStatus_t Status = m_pRegion[i]->flush();
        if (Status != SUCCESS)
        {
            return Status;
        }
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::Drive::erase()
{
    for (int i = 0; i < m_uNumRegions; i++)
    {
        RtStatus_t Status = m_pRegion[i]->erase();
        if (Status != SUCCESS)
        {
            return Status;
        }
    }

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT LbaNandMedia::Drive::Region::Region(LbaNandPhysicalMedia *pPhysicalMedia,
                                    LbaNandPhysicalMedia::LbaPartition *pPartition,
                                    uint32_t u32FirstSectorNumber,
                                    uint32_t u32SectorCount)
{
    assert(pPartition);

    m_pPhysicalMedia = pPhysicalMedia;
    m_pPartition = pPartition;
    m_u32FirstSectorNumber = u32FirstSectorNumber;
    m_u32SectorCount = u32SectorCount;
#if INTERNAL_MANAGED_BLOCK_LENGTH
    m_lastAccessSector = kRegionInvalidSector;
    m_lastOperation = kActivityRead;
    m_inSequenceCounter = 0;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::Drive::Region::writeSector(uint32_t u32SectorNumber,
                                                    const SECTOR_BUFFER *pBuffer)
{
    assert(m_pPartition);

    u32SectorNumber += m_u32FirstSectorNumber;

    // The drive object should have calculated the sector number
    // correctly, so we just assert here.
    assert(u32SectorNumber < (m_u32FirstSectorNumber + m_u32SectorCount));

#if INTERNAL_MANAGED_BLOCK_LENGTH
    // remove this constraint if the media trnsfer activity type is applying to all drives
    if(m_pPhysicalMedia->getDataPartition() == m_pPartition)
    {
        if(m_lastOperation != kActivityWrite)
        {
            m_inSequenceCounter = 0;
        }
        else if(u32SectorNumber == m_lastAccessSector+1)
        {
            m_inSequenceCounter++;
        }
        else
        {                
            m_inSequenceCounter = 0;
        }
        
        m_lastAccessSector = u32SectorNumber;
        m_lastOperation = kActivityWrite;
        
        if(m_inSequenceCounter < kInSequenceThreshold)
        {
            m_pPartition->startTransferSequence(1);
        }
    }

    RtStatus_t  status;
    
    status = m_pPartition->writeSector(u32SectorNumber, pBuffer);
    if(m_inSequenceCounter < kInSequenceThreshold)
    {
        m_pPhysicalMedia->enablePowerSaveMode(TRUE);
    }
    return status;

#else

    return m_pPartition->writeSector(u32SectorNumber, pBuffer);

#endif
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t LbaNandMedia::Drive::Region::readSector(uint32_t u32SectorNumber,
                                                   SECTOR_BUFFER *pBuffer)
{
    assert(m_pPartition);

    u32SectorNumber += m_u32FirstSectorNumber;

    // The drive object should have calculated the sector number
    // correctly, so we just assert here.
    assert(u32SectorNumber < (m_u32FirstSectorNumber + m_u32SectorCount));

#if INTERNAL_MANAGED_BLOCK_LENGTH
    bool bExitPowerSaveOnTransfer = g_LbaNandMediaInfo.shouldExitPowerSaveOnTransfer();
    
    // remove this constraint if the media trnsfer activity type is applying to all drives
    if(m_pPhysicalMedia->getDataPartition() == m_pPartition || bExitPowerSaveOnTransfer==false )
    {
        if(m_lastOperation != kActivityRead || bExitPowerSaveOnTransfer==false)
        {
            m_inSequenceCounter = 0;
        }
        else if(u32SectorNumber == m_lastAccessSector+1)
        {
            m_inSequenceCounter++;
        }
        else
        {                
            m_inSequenceCounter = 0;
        }
        
        m_lastAccessSector = u32SectorNumber;
        m_lastOperation = kActivityRead;
        
        if(m_inSequenceCounter < kInSequenceThreshold)
        {
            m_pPartition->startTransferSequence(1);
        }
    }

    RtStatus_t  status;
    
    status = m_pPartition->readSector(u32SectorNumber, pBuffer);
    if( (m_inSequenceCounter<kInSequenceThreshold) || bExitPowerSaveOnTransfer==false )
    {
        m_pPhysicalMedia->enablePowerSaveMode(TRUE);
    }
    return status;
    
#else
    return m_pPartition->readSector(u32SectorNumber, pBuffer);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t LbaNandMedia::Drive::Region::startTransferSequence(uint32_t u32SectorCount)
{
    assert(m_pPartition);

    return m_pPartition->startTransferSequence(u32SectorCount);
}


///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t LbaNandMedia::Drive::Region::flush()
{
    assert(m_pPartition);

    // Flush this region's partition.
    return m_pPartition->flushCache();
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::Drive::Region::erase()
{
    assert(m_pPartition);
    return m_pPartition->eraseSectors(m_u32FirstSectorNumber, m_u32SectorCount);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
LbaNandMedia::DriveIterator::DriveIterator(const LbaNandMedia *pMedia)
{
    assert(pMedia);

    m_pMedia = pMedia;
    m_uCurrentIndex = 0;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
LbaNandMedia::Drive *LbaNandMedia::DriveIterator::next()
{
    return m_pMedia->getDriveAtIndex(m_uCurrentIndex++);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t LbaNandMedia::BootletDrive::init(LbaNandPhysicalMedia * nand)
{
    // The bootlet drive goes on the PNP of the first device.
    LbaNandPhysicalMedia::LbaPartition *pPnp = nand->getBootPartition();
    assert(pPnp);

    // We have only one region and it is a fixed size. The actual data sectors
    // start immediately after the boot blocks.
    addRegion(nand, pPnp, kBootBlockCount, pPnp->getSectorCount() - kBootBlockCount);

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::BootletDrive::writeBootBlocks(SectorBuffer & buffer)
{
    RtStatus_t status;

    // Grab the PNP partition from our sole region.
    uint32_t u32SectorNumber = 0;
    Region * region = regionForSector(&u32SectorNumber);
    assert(region);
    LbaNandPhysicalMedia::LbaPartition * partition = region->getPartition();
    assert(partition);

    // Write each of the boot blocks in successsion.
    status = writeNCB(partition, buffer);

    if (status == SUCCESS)
    {
        writeLDLB(partition, buffer);
    }

    if (status == SUCCESS)
    {
        writeDBBT(partition, buffer);
    }

    // Flush cache buffers on the device.
    region->flush();

    return status;
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::BootletDrive::writeNCB(LbaNandPhysicalMedia::LbaPartition * partition, SectorBuffer & buffer)
{
    // Wipe the buffer.
    buffer.fill(0);

    BootBlockStruct_t * ncb = (BootBlockStruct_t *)buffer.getBuffer();

    // Set NCB fingerprints.
    ncb->m_u32FingerPrint1 = NCB_FINGERPRINT1;
    ncb->m_u32FingerPrint2 = NCB_FINGERPRINT2;
    ncb->m_u32FingerPrint3 = NCB_FINGERPRINT3;

    // Use the current GPMI timings.
    NAND_Timing1_struct_t timings;
    timings = *ddi_gpmi_get_current_timings();

    // Fill in NCB block 1.
    ncb->NCB_Block1.m_NANDTiming.NAND_Timing = timings;
    ncb->NCB_Block1.m_u32DataPageSize = LARGE_SECTOR_DATA_SIZE;
    ncb->NCB_Block1.m_u32TotalPageSize = LARGE_SECTOR_TOTAL_SIZE;
    ncb->NCB_Block1.m_u32SectorsPerBlock = 64;
    ncb->NCB_Block1.m_u32SectorInPageMask = 0;
    ncb->NCB_Block1.m_u32SectorToPageShift = 0;
    ncb->NCB_Block1.m_u32NumberOfNANDs = ddi_lba_nand_hal_get_device_count();

    // Fill in NCB block 2.
    ncb->NCB_Block2.m_u32NumRowBytes = 3;
    ncb->NCB_Block2.m_u32NumColumnBytes = 2;
    ncb->NCB_Block2.m_u32TotalInternalDie = 1;
    ncb->NCB_Block2.m_u32InternalPlanesPerDie = 1;
    ncb->NCB_Block2.m_u32CellType = 1;
    
#if defined(STMP37xx) || defined(STMP377x)
    ncb->NCB_Block2.m_u32ECCType = BV_GPMI_ECCCTRL_ECC_CMD__DECODE_4_BIT;
#elif defined(STMP378x)
    ncb->NCB_Block2.m_u32ECCType = kNandEccType_RS4;
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

    // Use the standard NAND read commands.
    ncb->NCB_Block2.m_u32Read1stCode = 0x00;
    ncb->NCB_Block2.m_u32Read2ndCode = 0x30;

//#if defined(STMP378x)
//    ncb->NCB_Block2.m_u32EccBlock0Size;
//    ncb->NCB_Block2.m_u32EccBlockNSize;
//    ncb->NCB_Block2.m_u32EccBlock0EccLevel;
//    ncb->NCB_Block2.m_u32NumEccBlocksPerPage;
//    ncb->NCB_Block2.m_u32MetadataBytes;
//    ncb->NCB_Block2.m_u32EraseThreshold;
//    ncb->NCB_Block2.m_u32BootPatch;
//    ncb->NCB_Block2.m_u32PatchSectors;
//    ncb->NCB_Block2.m_u32Firmware_startingNAND2;
//#endif

#if defined(STMP37xx) || defined(STMP377x)
    // Write the NCB out.
    return partition->writeSector(kNcbSectorNumber, buffer);
#elif defined(STMP378x)
    // Allocate enough temporary buffer for encoding NCB
    uint8_t * pageBuffer = (uint8_t *)malloc(LARGE_SECTOR_TOTAL_SIZE);
    if (!pageBuffer)
    {
        return ERROR_OUT_OF_MEMORY;
    }

    // Zero out the newly created buffer before using it.
    memset(pageBuffer, 0, LARGE_SECTOR_TOTAL_SIZE);

	// Encode NCB using software ECC.
    hw_digctl_ChipAndRevision chipRev = hw_digctl_GetChipRevision();
	if (chipRev == HW_3780_TA1 || chipRev == HW_3780_TA2)
	{
		EncodeHammingAndRedundancy((unsigned char *)buffer.getBuffer(), pageBuffer);
	}
	else
	{
        // Copy the NCB into the page-sized buffer.
		memcpy(pageBuffer + NAND_HC_ECC_OFFSET_DATA_COPY, buffer, NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES);
		CalculateHammingForNCB_New((unsigned char *)pageBuffer + NAND_HC_ECC_OFFSET_DATA_COPY, pageBuffer + NAND_HC_ECC_OFFSET_PARITY_COPY);
	}

    // The LBA HAL knows to not use hardware ECC for sector 0 of the PNP.
    RtStatus_t status = partition->writeSector(kNcbSectorNumber, (const SECTOR_BUFFER *)pageBuffer);

    // Free our temporary page buffer.
    free(pageBuffer);
    
    return status;
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::BootletDrive::writeLDLB(LbaNandPhysicalMedia::LbaPartition * partition, SectorBuffer & buffer)
{
    // Wipe the buffer.
    buffer.fill(0);
    BootBlockStruct_t * ldlb = (BootBlockStruct_t *)buffer.getBuffer();

    // Set LDLB fingerprints.
    ldlb->m_u32FingerPrint1 = LDLB_FINGERPRINT1;
    ldlb->m_u32FingerPrint2 = LDLB_FINGERPRINT2;
    ldlb->m_u32FingerPrint3 = LDLB_FINGERPRINT3;

    // Set version fields.
    ldlb->LDLB_Block1.LDLB_Version.m_u16Major = LDLB_VERSION_MAJOR;
    ldlb->LDLB_Block1.LDLB_Version.m_u16Minor = LDLB_VERSION_MINOR;
    ldlb->LDLB_Block1.LDLB_Version.m_u16Sub = LDLB_VERSION_SUB;

    ldlb->LDLB_Block2.FirmwareVersion.m_u16Major  = LDLB_VERSION_MAJOR;
    ldlb->LDLB_Block2.FirmwareVersion.m_u16Minor  = LDLB_VERSION_MINOR;
    ldlb->LDLB_Block2.FirmwareVersion.m_u16Sub    = LDLB_VERSION_SUB;

    // Fill in the NAND bitmap field, even though the ROM doesn't currently use it.
    unsigned deviceCount = ddi_lba_nand_hal_get_device_count();
    uint32_t bitmap = NAND_1_BITMAP; // There is always at least one chip.
    if (deviceCount > 1)
    {
        bitmap |= NAND_2_BITMAP;
    }
    if (deviceCount > 2)
    {
        bitmap |= NAND_3_BITMAP;
    }
    if (deviceCount > 3)
    {
        bitmap |= NAND_4_BITMAP;
    }

    ldlb->LDLB_Block1.m_u32NANDBitmap = bitmap;

    // Set the firmware length to the full number of sectors in the partition, minus
    // how many boot blocks there are.
    uint32_t firmwareSectorCount = partition->getSectorCount() - kBootBlockCount;

    // Firmware 1 info.
    ldlb->LDLB_Block2.m_u32Firmware_startingNAND = 0;
    ldlb->LDLB_Block2.m_u32Firmware_startingSector = kFirmwareSectorNumber;
    ldlb->LDLB_Block2.m_u32Firmware_sectorStride = 0;
    ldlb->LDLB_Block2.m_uSectorsInFirmware = firmwareSectorCount;

    // Firmware 2 info.
    ldlb->LDLB_Block2.m_u32Firmware_startingNAND2 = 0;
    ldlb->LDLB_Block2.m_u32Firmware_startingSector2 = kFirmwareSectorNumber;
    ldlb->LDLB_Block2.m_u32Firmware_sectorStride2 = 0;
    ldlb->LDLB_Block2.m_uSectorsInFirmware2 = firmwareSectorCount;

    // DBBT info.
    ldlb->LDLB_Block2.m_u32DiscoveredBBTableSector = kDbbtSectorNumber;
    ldlb->LDLB_Block2.m_u32DiscoveredBBTableSector2 = kDbbtSectorNumber;

    // Now write the LDLB.
    return partition->writeSector(kLdlbSectorNumber, buffer);
}

///////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_internal.h for the documentation of this method.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMedia::BootletDrive::writeDBBT(LbaNandPhysicalMedia::LbaPartition * partition, SectorBuffer & buffer)
{
    // Wipe the buffer.
    buffer.fill(0);
    BootBlockStruct_t * dbbt = (BootBlockStruct_t *)buffer.getBuffer();

    // Set DBBT fingerprints. We don't need to set anything else since all of the bad block
    // counts are zero.
    dbbt->m_u32FingerPrint1 = DBBT_FINGERPRINT1;
    dbbt->m_u32FingerPrint2 = DBBT_FINGERPRINT2;
    dbbt->m_u32FingerPrint3 = DBBT_FINGERPRINT3;

    // Now write the DBBT.
    return partition->writeSector(kDbbtSectorNumber, buffer);
}

void LbaNandMedia::enterPowerSaveModeDpc(uint32_t param)
{
    LbaNandMedia * media = reinterpret_cast<LbaNandMedia *>(param);

    // Protect against unexpectedly entering power save mode due to a delay in
    // execution of the DPC.
    if (media && media->m_managePowerSave)
    {
        media->enableAllPowerSaveMode(true);
    }
    tx_semaphore_put(&g_LbaNandMediaSemaphore);
}

//! The only thing this timer callback does is to post a function to the DPC to
//! do the real work of enabling power save mode. This is necessary because the
//! timer context is very limited and cannot wait for DMAs.
__STATIC_TEXT void LbaNandMedia::enterPowerSaveModeTimer(uint32_t param)
{
  if(tx_semaphore_get(&g_LbaNandMediaSemaphore, TX_NO_WAIT) == TX_SUCCESS)
    os_dpc_Send(OS_DPC_LOW_LEVEL_DPC, enterPowerSaveModeDpc, param, TX_NO_WAIT);
}

//! The commands to enable or disable power save mode are only sent if the device
//! is not already in the desired mode.
void LbaNandMedia::enableAllPowerSaveMode(bool isEnabled)
{
    LbaNandMediaLocker locker;
    
    if (isEnabled != m_powerSaveEnabled)
    {
        unsigned i;
        for (i=0; i < m_uNumPhysicalMedia; ++i)
        {
            LbaNandPhysicalMedia * device = m_pPhysicalMedia[i];
            device->enablePowerSaveMode(isEnabled);
        }
        m_powerSaveEnabled = isEnabled;
    }
}

void LbaNandMedia::exitPowerSaveMode()
{
    LbaNandMediaLocker locker;

    // Turn off power save mode for all devices.
    enableAllPowerSaveMode(false);
    
    if (m_managePowerSave)
    {
        // Update the timer to expire a fixed amount of time from now.
        tx_timer_deactivate(&m_powerSaveTimer);
        tx_timer_change(&m_powerSaveTimer, OS_MSECS_TO_TICKS(kPowerSaveEnableDelay), 0);
        tx_timer_activate(&m_powerSaveTimer);
    }
}

void LbaNandMedia::enablePowerSaveManagement(bool isEnabled)
{
    m_managePowerSave = isEnabled;
    
    // This serves two purposes. When enabling, this activates the timer. When
    // disabling, it ensures that power save mode is disabled on all devices.
    exitPowerSaveMode();
    
    // To disable, we also need to make sure the timer is not active.
    if (!isEnabled)
    {
        tx_timer_deactivate(&m_powerSaveTimer);
    }
}

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}



