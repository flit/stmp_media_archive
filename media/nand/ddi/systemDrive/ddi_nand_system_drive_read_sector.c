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
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_system_drive
//! @{
//! \file ddi_nand_system_drive_read_sector.c
//! \brief Read routines for Nand System Drives.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "os/threadx/tx_api.h"
#include "hw/core/vmemory.h"
#include "ddi_nand_media.h"
#include "ddi_nand_system_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand_system_drive_recover.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#pragma alignvar(32)
//! The system drive read and write sector calls use this global buffer instead of an
//! aux buffer acquired from the buffer manager because allocating memory
//! with malloc() requires paging in code, which would cause a deadlock
//! on the buffer manager mutex.
SECTOR_BUFFER SystemDrive::s_auxBuffer[NOMINAL_AUXILIARY_SECTOR_ALLOC_SIZE];

extern RtStatus_t g_nand_hal_insertReadError;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark text=.static.text
#endif

#pragma ghs section text=".static.text"

////////////////////////////////////////////////////////////////////////////////
//! \brief Read a page from the NAND.
//!
//! This function will call the internal Read Page From NAND function.
//! This interface hides read wear leveling.
//!
//! \param[in]  u32SectorNumber Sector Number to be read.
//! \param[out]  pSectorData Pointer where sector data should be stored
//!              when reading.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
//! \post If successful, the data is in pSectorData.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t SystemDrive::readSector(uint32_t wSectorNumber, SECTOR_BUFFER * pSectorData)
{
    SystemDriveRecoveryManager * manager = m_media->getRecoveryManager();
    bool isRecoveryEnabled = manager->isRecoveryEnabled();
    SystemDrive * actualDrive = this;
    
    if (isRecoveryEnabled && !isMasterFirmware())
    {
        actualDrive = manager->getCurrentFirmwareDrive();
    }

    assert(actualDrive);
    return actualDrive->readSectorWithRecovery(wSectorNumber, pSectorData);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Internal function to read a page from the NAND.
//!
//! This function will read a page from the NAND. If an error occurs or
//! the data could not be corrected by ECC, the read recovery process is
//! initiated and the sector is re-read from a backup copy.
//!
//! \param[in]  u32SectorNumber Sector Number to be read.
//! \param[out]  pSectorData Pointer where sector data should be stored
//!              when reading.
//! \param[in] doRecover Determines if action should be taken upon a read error
//!     to recover the system drive. Pass true to enable recovery.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
//! \post If successful, the data is in pSectorData.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t SystemDrive::readSectorWithRecovery(uint32_t wSectorNumber, SECTOR_BUFFER * pSectorData)
{
    uint32_t logicalBlockNumber;
    uint32_t physicalBlockNumber;
    uint32_t wSectorOffsetBlock;
    RtStatus_t status;
    uint32_t chipRelativeSectorNumber;
    NandPhysicalMedia * nand;

    // Make sure we're initialized
    if (m_bInitialized != TRUE)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

    // Make sure the sector is within bounds
    if (wSectorNumber >= m_u32NumberOfSectors)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    nand = m_pRegion->m_nand;

    // Get the logical block and sector offset.
    nand->pageToBlockAndOffset(wSectorNumber, &logicalBlockNumber, &wSectorOffsetBlock);
    
    // Check if this logical block is being refreshed right now.
    if (logicalBlockNumber == m_logicalBlockBeingRefreshed)
    {
        // It is! So we have to read this block from our backup drive.
        SystemDrive * backupDrive = getBackupDrive();
        if (!backupDrive)
        {
            // For some reason, there is no backup drive, so we have no choice but
            // to return an error.
            return ERROR_DDI_NAND_FIRMWARE_REFRESH_BUSY;
        }
        
        return backupDrive->readSectorWithRecovery(wSectorNumber, pSectorData);
    }
    
    // Convert logical to absolute physical block.
    physicalBlockNumber = skipBadBlocks(logicalBlockNumber) + m_pRegion->m_u32AbPhyStartBlkAddr;
    
    // Make sure the block is still within bounds.
    if (physicalBlockNumber >= m_pRegion->m_u32AbPhyStartBlkAddr + m_pRegion->m_iNumBlks)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    // Convert back from absolute block to relative page.
    chipRelativeSectorNumber = nand->blockAndOffsetToRelativePage(physicalBlockNumber, wSectorOffsetBlock);

#if DEBUG
    // We don't ever want a read from the master drive to fail, unless it's a read error.
    if (g_nand_hal_insertReadError && isMasterFirmware())
    {
        g_nand_hal_insertReadError = 0;
    }
#endif // DEBUG
    
    // Always read the correct sector size.
    status = nand->readFirmwarePage(chipRelativeSectorNumber, pSectorData, s_auxBuffer, NULL);
    
    bool isRecoveryEnabled = isRecoverable() && m_media->getRecoveryManager()->isRecoveryEnabled();

    if (status == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR && isRecoveryEnabled)
    {
        // The number of bit errors on this page was at the threshold, so we must initiate
        // a rewrite of the block.
        SystemDriveBlockRefreshTask * task = new SystemDriveBlockRefreshTask(this, logicalBlockNumber);
        m_media->getDeferredQueue()->post(task);
        
        status = SUCCESS;
    }
    else if (status == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED && isRecoveryEnabled)
    {
        //! \todo Consider initiating recovery if the sector looks entirely erased. This would help
        //!     recovery from an interrupted recovery.
        status = recoverFromFailedRead(wSectorNumber, pSectorData);
    }
    else if (is_read_status_success_or_ecc_fixed(status))
    {
        status = SUCCESS;
    }

    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Skip bad blocks during System Drive reads.
//!
//! This function will check the bad block table to determine if we need to
//! skip bad blocks during system drive reads.  System drives are
//! allocated in sequential order so just skipping a bad block
//! and continuing will work fine.
//!
//! \param[in]  wLogicalBlockNumber Block to check in Bad Block Table.
//!
//! \return Adjusted block number (still relative to zero but adjusted for BB.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT uint32_t SystemDrive::skipBadBlocks(uint32_t wLogicalBlockNumber)
{
    BadBlockTable & bbt = (*m_pRegion->getBadBlocks());
    uint32_t wBadBlock;
    uint32_t wAdjustedBlockNumber = wLogicalBlockNumber;
    int i;
    
    // There are bad blocks, so proceed
    for (i = 0 ; i < bbt.getCount() ; i++)
    {
        // Get next bad block number
        wBadBlock = bbt[i] - m_pRegion->m_u32AbPhyStartBlkAddr;

        if (wBadBlock <= wAdjustedBlockNumber)
        {
            // If bad block is between begin of region and desired block,
            // add one to the desired block to compensate for the bad block
            wAdjustedBlockNumber++;
        }
        else
        {
            // Bad blocks greater than desired block do not matter
            break;
        }
    }

    return wAdjustedBlockNumber;
}

#if !defined(__ghs__)
#pragma mark text=default
#endif

#pragma ghs section text=default

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
