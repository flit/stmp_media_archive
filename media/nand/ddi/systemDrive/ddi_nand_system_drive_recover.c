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
//! \file ddi_nand_system_drive_recover.c
//! \brief Read disturbance recovery for system drives.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_system_drive_recover.h"
#include "os/threadx/os_tx_errordefs.h"
#include "os/thi/os_thi_api.h"
#include "hw/profile/hw_profile.h"
#include "os/dpc/os_dpc_api.h"
#include "drivers/rtc/ddi_rtc.h"
#include "hw/core/vmemory.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "Page.h"
#include "DiscoveredBadBlockTable.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark text=.static.text
#endif

#pragma ghs section text=".static.text"

////////////////////////////////////////////////////////////////////////////////
//! \brief Determine if a given System Drive is recoverable.
//!
//! This function will determine if a System Drive is recoverable.
//! Only certain system drives which are downloaded by host updater
//! in triplicate can be recovered.
//!
//! \param[in]  pDescriptor Pointer to the Logical Drive Descriptor.
//!
//! \retval TRUE  If this system drive is recoverable.
//! \retval FALSE  If this system drive is not recoverable.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT bool SystemDrive::isRecoverable()
{
  return ((DRIVE_TAG_BOOTMANAGER_S  == m_u32Tag) || (DRIVE_TAG_BOOTMANAGER2_S == m_u32Tag));
}

bool SystemDrive::isPrimaryFirmware()
{
    return m_u32Tag == DRIVE_TAG_BOOTMANAGER_S;
}

bool SystemDrive::isSecondaryFirmware()
{
    return m_u32Tag == DRIVE_TAG_BOOTMANAGER2_S;
}

bool SystemDrive::isMasterFirmware()
{
    return m_u32Tag == DRIVE_TAG_BOOTMANAGER_MASTER_S;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! This function returns the tag number of the master drive which should be 
//! used in recovering drive identified by u32SearchTag 
//!
//! \return Pointer to the master copy of this drive.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT SystemDrive * SystemDrive::getMasterDrive()
{
    return m_media->getRecoveryManager()->getMasterDrive();
}

////////////////////////////////////////////////////////////////////////////////
//!
//! This function returns the tag number of the back-up drive for system drive
//! identified by u32SearchTag
//!
//! \return Pointer to the backup for this drive.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT SystemDrive * SystemDrive::getBackupDrive()
{
    SystemDrive * backup = NULL;
    
    // Get the default backup for this drive.
    if (DRIVE_TAG_BOOTMANAGER_S == m_u32Tag)
    {
        backup = m_media->getRecoveryManager()->getSecondaryDrive();
    }
    else if (DRIVE_TAG_BOOTMANAGER2_S == m_u32Tag)
    {
        backup = m_media->getRecoveryManager()->getPrimaryDrive();
    }
    else if (m_u32Tag == DRIVE_TAG_BOOTMANAGER_MASTER_S)
    {
        // There is no backup for the master drive.
        return NULL;
    }
    
    // If the backup is unavailable, then we have to use the master as the backup.
    if (!backup || backup->isBeingRewritten())
    {
        backup = getMasterDrive();
    }
    
    return backup;
}

SystemDriveRecoveryManager::SystemDriveRecoveryManager()
:   m_primaryDrive(NULL),
    m_secondaryDrive(NULL),
    m_masterDrive(NULL),
    m_currentDrive(NULL),
    m_isAvailable(false),
    m_isRecoveryEnabled(true),
    m_isRecoveryActive(false),
    m_lastRecoveryElapsedTime(0)
{
    m_refreshCount[0] = 0;
    m_refreshCount[1] = 0;
}

void SystemDriveRecoveryManager::addDrive(SystemDrive * drive)
{
    if (drive->isPrimaryFirmware())
    {
        m_primaryDrive = drive;
        m_currentDrive = drive;
    }
    else if (drive->isSecondaryFirmware())
    {
        m_secondaryDrive = drive;
    }
    else if (drive->isMasterFirmware())
    {
        m_masterDrive = drive;
    }
    
    m_isAvailable = (m_primaryDrive && m_secondaryDrive && m_masterDrive);
}

void SystemDriveRecoveryManager::removeDrive(SystemDrive * drive)
{
    // Clear the appropriate drive reference.
    if (drive->isPrimaryFirmware())
    {
        m_primaryDrive = NULL;
    }
    else if (drive->isSecondaryFirmware())
    {
        m_secondaryDrive = NULL;
    }
    else if (drive->isMasterFirmware())
    {
        m_masterDrive = NULL;
    }
    
    // If this drive is the current drive.
    if (m_currentDrive == drive)
    {
        m_currentDrive = drive->getBackupDrive();
    }
    
    m_isAvailable = (m_primaryDrive && m_secondaryDrive && m_masterDrive);
}

////////////////////////////////////////////////////////////////////////////////
//!
//! This function returns descriptor for the drive which should be read when 
//! read of firmware drive is attempted. The original firmware drive may be in the process
//! of being restored or reads from firmware drive may be diverted to backup drive to
//! increase reliability.
//!
//! \return SUCCESS or Error code
////////////////////////////////////////////////////////////////////////////////  
SystemDrive * SystemDriveRecoveryManager::getCurrentFirmwareDrive()
{
    return m_currentDrive;
}

void SystemDriveRecoveryManager::setCurrentFirmwareDrive(SystemDrive * theDrive)
{
    m_currentDrive = theDrive;
}

void SystemDriveRecoveryManager::refreshSyncCompletion(DeferredTask * task, void * param)
{
    SystemDriveRecoveryManager * _this = reinterpret_cast<SystemDriveRecoveryManager *>(param);
    _this->m_isRecoveryActive = false;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Kick off the asynchronous recovery process.
//!
//! \param[in] failedDrive The pointer to the system drive to start recovering.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_DRIVE_NOT_RECOVERABLE
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t SystemDriveRecoveryManager::startRecovery(SystemDrive * failedDrive)
{
    // Is this drive recoverable?
    if (!failedDrive->isRecoverable())
    {
        return ERROR_DDI_LDL_LDRIVE_DRIVE_NOT_RECOVERABLE;
    }
    
    // Update globals.
    m_isRecoveryActive = true;
    m_refreshCount[failedDrive->isSecondaryFirmware() ? 1 : 0]++;
    
    // Set the current drive so that we read from
    // the opposite drive from the one we're recovering.
    m_currentDrive = failedDrive->getBackupDrive();

    // Kick off the rewrite task. The task will switch the current read drive to the drive
    // it just rewrote when finished.
    SystemDriveRewriteTask * task = new SystemDriveRewriteTask(failedDrive, true);
    task->setCompletion(refreshSyncCompletion, this);
    g_nandMedia->getDeferredQueue()->post(task);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! This function attempts to restore system drive which has suffered a read 
//! error.  If the system drive does not have back-ups, this condition is fatal.  
//! If the system drive does have back-ups, recovery is a simple matter of 
//! copying drive from master.
//!
//! \param[in] wSectorNumber Sector Number to Read.  This address is a relative 
//!                          address and not a physical address.
//! \param[in] pSectorData Pointer to data buffer
//!
//! \return SUCCESS or Error code
//! \todo Deal with a recovery already in progress!
//! \internal
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t SystemDrive::recoverFromFailedRead(uint32_t wSectorNumber, SECTOR_BUFFER * pSectorData)
{
    RtStatus_t ret;
    
    // Set the flag to indicate that nobody should try to read directly from this drive.
    // This will get cleared when the drive is completely recovered.
    m_isBeingRewritten = true;

    // Initiate drive refresh.
    ret = m_media->getRecoveryManager()->startRecovery(this);
    if (ret)
    {
        return ret;
    }

    // Get drive to re-read sector from. When we started recovery above, the current
    // firmware drive was updated for us.
    SystemDrive * backupDrive = m_media->getRecoveryManager()->getCurrentFirmwareDrive();
    if (!backupDrive)
    {
        return ERROR_DDI_LDL_LDRIVE_DRIVE_NOT_RECOVERABLE;
    }
    
    // Reread the failed page.
    return backupDrive->readSectorWithRecovery(wSectorNumber, pSectorData);
}

#if !defined(__ghs__)
#pragma mark --SystemDriveBlockRefreshTask--
#endif

SystemDriveBlockRefreshTask::SystemDriveBlockRefreshTask(SystemDrive * drive, uint32_t logicalBlockToRecover)
:   DeferredTask(kTaskPriority),
    m_drive(drive),
    m_logicalBlock(logicalBlockToRecover)
{
}

uint32_t SystemDriveBlockRefreshTask::getTaskTypeID() const
{
    return kTaskTypeID;
}

void SystemDriveBlockRefreshTask::task()
{
    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: inside SystemDriveBlockRefreshTask 0x%08x\n", (uint32_t)this);
    
    SimpleTimer elapsed;
    m_drive->refreshLogicalBlock(m_logicalBlock, m_drive->getBackupDrive());

    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: completed SystemDriveBlockRefreshTask 0x%08x in %u µs\n", (uint32_t)this, (uint32_t)elapsed.getElapsed());
}

bool SystemDriveBlockRefreshTask::examineOne(DeferredTask * task)
{
    // If this task is a block refresh task, examine it closer.
    if (task->getTaskTypeID() == kTaskTypeID)
    {
        SystemDriveBlockRefreshTask * refreshTask = static_cast<SystemDriveBlockRefreshTask *>(task);
        if (refreshTask->getDrive() == m_drive && refreshTask->getLogicalBlock() == m_logicalBlock)
        {
            // There is already a task in the queue that exactly matches me, so I
            // don't want to be placed into the queue. We don't want to have the block
            // refreshed multiple times when it's not necessary, since that will cause
            // excessive wear on the block.
            return true;
        }
    }
    
    // Allow insertion.
    return false;
}

void SystemDrive::refreshLogicalBlock(uint32_t logicalBlock, SystemDrive * sourceDrive)
{
    RtStatus_t status;
    NandPhysicalMedia * nand = m_pRegion->getNand();
    
    // Save the logical block so nobody tries to read from it while we're refreshing.
    m_logicalBlockBeingRefreshed = logicalBlock;
    
    // Convert the logical block to a logical page number that we'll use to read from the source.
    uint32_t logicalSourcePage = nand->blockToPage(logicalBlock);
    
    // Convert the logical block number to an absolute physical block.
    uint32_t adjustedLogicalBlock = skipBadBlocks(logicalBlock);
    Block physicalBlock(adjustedLogicalBlock + m_pRegion->m_u32AbPhyStartBlkAddr);
    
    // Erase the physical block in preparation for writing to it.
    status = physicalBlock.eraseAndMarkOnFailure();
    if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
    {
        // Add this new bad block to my region's bad block table.
        m_pRegion->getBadBlocks()->insert(physicalBlock);
        m_pRegion->setDirty();
        
        // Start a complete rewrite of this drive, so we can properly skip the new bad block.
        m_media->getRecoveryManager()->startRecovery(this);
        
        return;
    }
    else if (status != SUCCESS)
    {
        // Not much we can do if the erase failed for an unknown reason.
        m_logicalBlockBeingRefreshed = -1;
        return;
    }
    
    // Create the target page object to point at the first page of the physical block.
    BootPage targetPage(physicalBlock);
    targetPage.allocateBuffers();
    
    int remainingPages = nand->pNANDParams->wPagesPerBlock;
    while (remainingPages--)
    {
        DdiNandLocker lockMe;
        
        // Let the source drive read the page for us.
        status = sourceDrive->readSectorWithRecovery(logicalSourcePage, targetPage.getPageBuffer());
        if (status != SUCCESS)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: refreshLogicalBlock got error 0x%08x reading logical page %u from drive %2x\n", status, logicalSourcePage, sourceDrive->getTag());
            break;
        }
        
        // Now write the data to the target page. If the write fails, the block will be erased
        // and marked bad for us.
        status = targetPage.writeAndMarkOnFailure();
        if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
        {
            // Add this new bad block to my region's bad block table.
            m_pRegion->addNewBadBlock(physicalBlock);
            
            // Start a complete rewrite of this drive, so we can properly skip the new bad block.
            m_media->getRecoveryManager()->startRecovery(this);
            
            break;
        }
        else if (status != SUCCESS)
        {
            // Some other error occurred; there's really nothing we can do.
            break;
        }
        
        // It's possible that the source drive went into recovery, so make sure we can still
        // read from it.
        if (sourceDrive->isBeingRewritten())
        {
            // Switch to the master drive.
            sourceDrive = sourceDrive->getMasterDrive();
        }
        
        ++targetPage;
        ++logicalSourcePage;
    }
    
    // Clear the block number being refreshed.
    m_logicalBlockBeingRefreshed = -1;
}

#if !defined(__ghs__)
#pragma mark --SystemDriveRewriteTask--
#endif

SystemDriveRewriteTask::SystemDriveRewriteTask(SystemDrive * drive, bool switchToRecovered)
:   DeferredTask(kTaskPriority),
    m_recoveringDrive(drive),
    m_sourceDrive(NULL),
    m_rewriteStatus(0),
    m_switchToRecoveredDrive(switchToRecovered)
{
    m_sourceDrive = m_recoveringDrive->getBackupDrive();
}

uint32_t SystemDriveRewriteTask::getTaskTypeID() const
{
    return kTaskTypeID;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief System drive recovery task.
//!
//! This function is the entry point to a task which is dynamically created for 
//! system drive recovery as a response to read disturbance. It first erases the
//! system drive being recovered, and then copies every page from the master
//! copy to the failed drive.
//!
//! Upon completion, the NAND_SECONDARY_BOOT persistent bit is cleared if we were
//! recovering the primary drive, so that the boot ROM will once again boot from
//! the primary.
//!
//! \todo Handle new bad blocks. Allocation should pad system regions with a
//!     few blocks. Then this function should be able to deal with a new bad
//!     block by skipping over it and updating the DBBT appropriately.
////////////////////////////////////////////////////////////////////////////////
void SystemDriveRewriteTask::task()
{
    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: inside SystemDriveRewriteTask 0x%08x\n", (uint32_t)this);
    
    RtStatus_t status;
    int32_t i;
    uint32_t u32NumberOfSectors;
    SimpleTimer timer;
    
    // We always set the persistent bit that indicates that we've started drive
    // recovery. This is checked upon SDK boot-up so we can restart the recovery
    // if we were interrupted. It is also necessary to set NAND_SECONDARY_BOOT and
    // FIRMWARE_USE_BACKUP correctly.
    ddi_rtc_WritePersistentField(RTC_FIRMWARE_RECOVERY_IN_PROGRESS, 1);

    // If recovering the primary firmware drive, make sure the persistent bit is set so that
    // the ROM will not try to read from it until it is completely rewritten, just in case we
    // restart in the middle of the copy.
    if (m_recoveringDrive->isPrimaryFirmware())
    {
        ddi_rtc_WritePersistentField(RTC_NAND_SECONDARY_BOOT, 1);
    }
    
    // Tell the drive we're going to rewrite it.
    m_recoveringDrive->setIsBeingRewritten(true);
    
    status = m_recoveringDrive->erase();
    if (status == SUCCESS)
    {
        u32NumberOfSectors = m_recoveringDrive->getSectorCount();
        assert(u32NumberOfSectors == m_sourceDrive->getSectorCount());
        
        // Acquire a sector buffer.
        SectorBuffer sectorBuffer;
        status = sectorBuffer.acquire();
        if (status == SUCCESS)
        {
            // There aren't many elegant alternatives to calling readSectorInternal
            // here.  So, I'll just call it.
            for (i = 0; i < u32NumberOfSectors; i++)
            {
                // Lock only for this single page read and write, so we don't hold the whole
                // NAND driver hostage.
                DdiNandLocker lockThisPass;
                
                // Recovery is allowed.
                status = m_sourceDrive->readSectorWithRecovery(i, sectorBuffer);
                if (status != SUCCESS)
                {
                    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Error reading page %d from master during recovery: 0x%x\n", i, status);
                    break;
                }
        
                status = m_recoveringDrive->writeSector(i, sectorBuffer);
                if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    // The block has already been marked bad by writeSector(), we just have
                    // to restart writing the drive.
                    status = m_recoveringDrive->erase();
                    i = -1;
                    continue;
                }
                else if (status != SUCCESS)
                {
                    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Error writing page %d to drive %x during recovery: 0x%x\n", i, m_recoveringDrive->m_u32Tag, status);
                    break;
                }
        
                // It's possible that the source drive went into recovery, so make sure we can still
                // read from it.
                if (m_sourceDrive->isBeingRewritten())
                {
                    // Switch to the master drive.
                    m_sourceDrive = m_sourceDrive->getMasterDrive();
                }
            }
        }
    }
    
    // We're done rewriting the drive now.
    m_recoveringDrive->setIsBeingRewritten(false);
    
    // The drive has been restored, so clean up after ourself.
    if (status == SUCCESS)
    {
        // Handle the switch to recovered flag, but only if the recover succeeded.
        // Otherwise we'd find ourself in a quite a mess.
        if (m_switchToRecoveredDrive)
        {
            g_nandMedia->getRecoveryManager()->setCurrentFirmwareDrive(m_recoveringDrive);
        }
        
        // If we just finished restoring the primary firmware drive, we must
        // clear the persistent bit that tells the ROM to boot from the secondary drive.
        if (m_recoveringDrive->isPrimaryFirmware())
        {
            ddi_rtc_WritePersistentField(RTC_NAND_SECONDARY_BOOT, 0);
        }
    }
    
    // Clear this persistent bit since we're no longer recovering a drive.
    ddi_rtc_WritePersistentField(RTC_FIRMWARE_RECOVERY_IN_PROGRESS, 0);
    
    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Recovering system drive 0x%2x took %u µs (status=0x%08x)\n", m_recoveringDrive->m_u32Tag, (uint32_t)timer.getElapsed(), status);
    
    // In debug builds we want to halt here if there was an error, so hopefully
    // someone will notice and make changes to handle the failure case.
    assert(status == SUCCESS);
}

bool SystemDriveRewriteTask::examineOne(DeferredTask * task)
{
    // If this task is a block rewrite task, examine it closer.
    if (task->getTaskTypeID() == kTaskTypeID)
    {
        SystemDriveRewriteTask * rewriteTask = static_cast<SystemDriveRewriteTask *>(task);
        if (rewriteTask->getDrive() == m_recoveringDrive)
        {
            // There is already a task in the queue that exactly matches me, so I
            // don't want to be placed into the queue. We don't want to have the drive
            // rewritten multiple times when it's not necessary, since that will cause
            // excessive wear on the blocks.
            return true;
        }
    }
    
    // Allow insertion.
    return false;
}

#if !defined(__ghs__)
#pragma mark text=default
#endif

#pragma ghs section text=default

void SystemDriveRecoveryManager::printStatistics()
{
    tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "--- Start of Nand System Drive Read Disturbance Recovery Statistics\n");
    tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Primary refreshes: %d\n", m_refreshCount[0]);
    tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Secondary refreshes: %d\n", m_refreshCount[1]);
    tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Total refreshes: %d\n", m_refreshCount[0] + m_refreshCount[1]);
    tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Last refresh elapsed time: %d ms\n", (uint32_t)m_lastRecoveryElapsedTime / 1000);
    tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Current read drive: 0x%02x\n", m_currentDrive->m_u32Tag);
    tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "--- End of Nand System Drive Read Disturbance Recovery Statistics\n");
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
