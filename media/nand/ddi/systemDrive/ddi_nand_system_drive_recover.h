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
//! \file ddi_nand_system_drive_recover.h
//! \brief Read disturbance recovery for system drives.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "os/threadx/tx_api.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_media.h"
#include "ddi_nand_system_drive.h"
#include "ddi_nand_hal.h"
#include "ddi_nand.h"
#include "DeferredTask.h"

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

namespace nand {

/*!
 * \brief Task to rewrite a block of a system drive.
 */
class SystemDriveBlockRefreshTask : public DeferredTask
{
public:
    
    //! \brief Constants for the block update task.
    enum _task_constants
    {
        //! \brief Unique ID for the type of this task.
        kTaskTypeID = 'sysb',
        
        //! \brief Priority for this task type.
        kTaskPriority = 10
    };

    //! \brief Constructor.
    SystemDriveBlockRefreshTask(SystemDrive * drive, uint32_t logicalBlockToRecover);
    
    //! \brief Return a unique ID for this task type.
    virtual uint32_t getTaskTypeID() const;
    
    //! \brief Check for preexisting duplicate tasks in the queue.
    virtual bool examineOne(DeferredTask * task);
    
    //! \brief Return the drive being repaired.
    const SystemDrive * getDrive() const { return m_drive; }

    //! \brief Return the logical block that needs to be refreshed.
    uint32_t getLogicalBlock() const { return m_logicalBlock; }

protected:
    SystemDrive * m_drive;      //!< The system drive needing update.
    uint32_t m_logicalBlock;    //!< Logical block number to update.

    //! \brief The refresh task.
    virtual void task();
    
};

/*!
 * \brief Task to rewrite an entire system drive.
 */
class SystemDriveRewriteTask : public DeferredTask
{
public:
    
    //! \brief Constants for the block update task.
    enum _task_constants
    {
        //! \brief Unique ID for the type of this task.
        kTaskTypeID = 'sysw',
        
        //! \brief Priority for this task type.
        kTaskPriority = 8
    };

    //! \brief Constructor.
    SystemDriveRewriteTask(SystemDrive * drive, bool switchToRecovered);
    
    //! \brief Return a unique ID for this task type.
    virtual uint32_t getTaskTypeID() const;
    
    //! \brief Check for preexisting duplicate tasks in the queue.
    virtual bool examineOne(DeferredTask * task);
    
    //! \brief Return the drive being repaired.
    const SystemDrive * getDrive() const { return m_recoveringDrive; }
    
    //! \brief Gets the status of the rewrite operation.
    RtStatus_t getStatus() const { return m_rewriteStatus; }

protected:
    SystemDrive * m_recoveringDrive;      //!< The system drive needing update.
    SystemDrive * m_sourceDrive;     //!< The master copy of the system drive needing update.
    RtStatus_t m_rewriteStatus; //!< Status after the rewrite finishes.
    
    //! If true, the read pointer will be switched to the drive that was just recovered
    //! upon completion of recovery. Otherwise the read pointer will stay where it was.
    bool m_switchToRecoveredDrive;

    //! \brief The refresh task.
    virtual void task();
};

/*!
 * \brief Class for managing recovery of a failed system drive.
 *
 * The main purpose of this class is to track which system drive firmware should be read from,
 * and to hold the set of firmware copies. It also maintains some other information about the
 * system drive read disturbance recovery handling.
 */
class SystemDriveRecoveryManager
{
public:
    //! \brief Constructor.
    SystemDriveRecoveryManager();
    
    //! \brief Inform the manager about the existence of a drive.
    void addDrive(SystemDrive * drive);
    
    //! \brief Tell the manager that a drive is no longer available.
    void removeDrive(SystemDrive * drive);

    //! \brief Returns the drive which firmware should be read from.
    SystemDrive * getCurrentFirmwareDrive();
    
    void setCurrentFirmwareDrive(SystemDrive * theDrive);
    
    SystemDrive * getPrimaryDrive() { return m_primaryDrive; }
    SystemDrive * getSecondaryDrive() { return m_secondaryDrive; }
    SystemDrive * getMasterDrive() { return m_masterDrive; }
    
    bool isRecoveryEnabled() const { return m_isAvailable && m_isRecoveryEnabled; }
    void setIsRecoveryEnabled(bool isEnabled) { m_isRecoveryEnabled = isEnabled; }
    
    RtStatus_t startRecovery(SystemDrive * failedDrive);

    void printStatistics();
    
protected:
    
    //! \brief Recovery task completion callback.
    static void refreshSyncCompletion(DeferredTask * task, void * param);

    //! \name Firmware drives
    //@{
    SystemDrive * m_primaryDrive;   //!< First copy of the firmware.
    SystemDrive * m_secondaryDrive; //!< Second copy of the firmware.
    SystemDrive * m_masterDrive;    //!< Master copy of the firmware, only ever used to restore one of the first two copies.
    SystemDrive * m_currentDrive;   //!< Drive which firmare will actually be read from.
    //@}

    //! \name Recovery state
    //@{
    bool m_isAvailable; //!< Whether recovery is available and configured.
    bool m_isRecoveryEnabled;   //!< True if automatic recovery is enabled.
    bool m_isRecoveryActive;      //!< True when the recovery process is running.
    //@}
    
    //! \name Statistics
    //@{
    unsigned m_refreshCount[2]; //!< Number of times the primary and secondary drives have been refreshed. Primary is index 0 and secondary is index 1.
    int64_t m_lastRecoveryElapsedTime;    //!< How long the most recent recovery took in microseconds.
    //@}
    
};

} // namespace nand

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
