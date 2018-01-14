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
//! \file   TransferManager.cpp
//! \brief  Implementation of the MMC TransferManager class.
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "TransferManager.h"
#include "hw/profile/hw_profile.h"
#include <os/dpc/os_dpc_api.h>
#include <os/thi/os_thi_api.h>

using namespace mmc;
using namespace mmchal;

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! \brief Invalid sector number constant.
//!
//! Use a gigantic unsigned int to represent an invalid sector number.
//! Need to be able to add 1 and have it still be gigantic.
const uint32_t kInvalidSectorNumber = ~0U - 1;

//! \brief Power save timeouts.
//!
//! \todo The difference between these values is pretty trivial for hostlink, it is
//! probably OK to use 10ms for both player and hostlink.
enum _power_save_timeouts
{
    kTimeoutInMs_OptimizedForPower = 10,    //!< Short timeout value to optimize for power
    kTimeoutInMs_OptimizedForSpeed = 50,    //!< Long timeout value to optimize for speed
};

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! \brief Timer used to automatically enter low power state.
TX_TIMER g_powerSaveTimer;

#if defined(DEBUG_TRACE) && !defined(NO_SDRAM)
//! \brief Debug timer fire time.
uint32_t g_lastTimerFire = 0;
#endif

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

TransferManager::TransferManager(MmcMedia* media, MmcSdDevice* device)
: m_media(media),
  m_device(device),
  m_blocksPerSector(m_media->sectorsToDeviceBlocks(1)),
  m_currentOperation(kOperationIdle),
  m_lastSectorNumber(kInvalidSectorNumber),
  m_powerManager(this, device),
  m_sequencer()
{
}

RtStatus_t TransferManager::init()
{
    // Initialize the power manager.
    return m_powerManager.init();
}

//! \brief Start or continue a multi-read or multi-write operation.
RtStatus_t TransferManager::execute(Operation_t operation, Transfer& transfer, uint32_t sectorNumber)
{

#if defined(DEBUG_TRACE) && !defined(NO_SDRAM)
    DebugDumpState(operation, sectorNumber);
#endif

    // Exit low power state to select the device on the bus.
    m_powerManager.exitLowPowerState();

    assert(m_device);
    assert(m_media);

    RtStatus_t status = SUCCESS;

    // Determine if the current sector number is the next sequential sector number.
    bool isSequential = (m_lastSectorNumber + 1 == sectorNumber);

    if ((operation == m_currentOperation) && isSequential)
    {
        // We have already started a multi operation AND the requested sector number
        // is the next sequential sector, so just continue the multi operation.
        status = transfer.next(m_device, m_blocksPerSector);
        if (SUCCESS != status)
        {
            return status;
        }

        // Increment the last sector number (set it to the current sector number).
        ++m_lastSectorNumber;
    }
    else
    {
        // We are in a multi-write operation, or are idle, or the requested sector
        // number is out of sequence, so we need to start a new multi operation.

        if (kOperationIdle != m_currentOperation)
        {
            // We switched directions, so first stop the current multi operation.
            status = stop();
            if (SUCCESS != status)
            {
                return status;
            }

            // Clear sequential flag since we changed directions.
            isSequential = false;
        }

        // Convert native sector number to device block number.
        uint32_t blockNumber = m_media->sectorsToDeviceBlocks(sectorNumber);

        status = transfer.start(m_device, blockNumber, m_blocksPerSector);
        if (SUCCESS != status)
        {
            return status;
        }

        // Remember the last sector number we read and set the current operation.
        m_lastSectorNumber = sectorNumber;
        m_currentOperation = operation;

        // If this is not yet a long sequence, enter low power state.
        if (!m_sequencer.isSequenceLong(isSequential))
        {
            status = m_powerManager.enterLowPowerState();
            if (SUCCESS != status)
            {
                return status;
            }
        }
    }

    return SUCCESS;
}

RtStatus_t TransferManager::readSector(uint32_t sectorNumber, SECTOR_BUFFER* buffer)
{
    ReadTransfer transfer(buffer);

    return execute(kOperationRead, transfer, sectorNumber);
}

RtStatus_t TransferManager::writeSector(uint32_t sectorNumber, const SECTOR_BUFFER* buffer)
{
    WriteTransfer transfer(buffer);

    return execute(kOperationWrite, transfer, sectorNumber);
}

TransferManager::~TransferManager()
{
    // Stop any multi-transfer that is in progress and deselect device on bus.
    forceStop();
}

RtStatus_t TransferManager::stop()
{
    RtStatus_t status = ERROR_GENERIC;

    assert(m_device);

    switch (m_currentOperation)
    {
    case kOperationWrite:
        status = m_device->stopWriteTransmission();
        break;
    case kOperationRead:
        status = m_device->stopReadTransmission();
        break;
    case kOperationIdle:
        // If already idle, don't try to force a stop.
        status = SUCCESS;
        break;
    default:
        assert(0);
        break;
    }

    // Reset to idle if stop was successful.
    if (SUCCESS == status)
    {
        m_currentOperation = kOperationIdle;
    }

    return status;
}

RtStatus_t TransferManager::forceStop()
{
    // This forces a stop of any outstanding transfers and enters low power state.
    RtStatus_t status = m_powerManager.enterLowPowerState();

    return status;
}

#if defined(DEBUG_TRACE) && !defined(NO_SDRAM)
void TransferManager::DebugDumpState(Operation_t operation, uint32_t sectorNumber)
{
    // Some code for tracing transfer state.
    DebugTrace::add(hw_profile_GetMilliseconds());
    DebugTrace::add(operation);
    DebugTrace::add(sectorNumber);
    DebugTrace::add(m_currentOperation);
    DebugTrace::add(m_powerManager.isPowerStateLow());
    DebugTrace::add(m_lastSectorNumber);
    DebugTrace::add(m_sequencer.getSequenceCount());
    DebugTrace::add(g_lastTimerFire);
    g_lastTimerFire = 0;

    // Some code for tracing power and timer.
//    static unsigned numLow = 0;
//    static unsigned numHigh = 0;
//    static unsigned numTimer = 0;
//    if (m_powerManager.isPowerStateLow())
//    {
//        ++numLow;
//    }
//    else
//    {
//        ++numHigh;
//    }
//    if (g_lastTimerFire)
//    {
//        ++numTimer;
//        g_lastTimerFire = 0;
//    }
//    DebugTrace::addWrap(~0); //marker
//    DebugTrace::addWrap(numLow);
//    DebugTrace::addWrap(numHigh);
//    DebugTrace::addWrap(numTimer);
}
#endif // DEBUG_TRACE

///////////////////////////////////////////////////////////////////////////////
//! \brief DPC-level function to enter power save mode.
//!
//! Called by timer through DPC to enter power save mode.
//!
//! \param[in] param Pointer to PowerManager.
//!
//! \return None.
///////////////////////////////////////////////////////////////////////////////
static void enterLowPowerStateDpc(uint32_t param)
{
    PowerManager* powerManager = reinterpret_cast<PowerManager*>(param);

    assert(powerManager);

    // Attempt to get the DdiMmcLocker mutex. If it is not available,
    // set the timer up to fire again in a little bit and return.
    if (tx_mutex_get(&g_mmcThreadSafeMutex, TX_NO_WAIT) != TX_SUCCESS)
    {
        tx_timer_change(&g_powerSaveTimer, OS_MSECS_TO_TICKS(kTimeoutInMs_OptimizedForPower), 0);
        tx_timer_activate(&g_powerSaveTimer);
        return;
    }

#if defined(DEBUG_TRACE) && !defined(NO_SDRAM)
    g_lastTimerFire = hw_profile_GetMilliseconds();
#endif

    // Enter low power state.
    powerManager->enterLowPowerState();

    // Unlock the DdiMmcLocker mutex.
    if (tx_mutex_put(&g_mmcThreadSafeMutex) != TX_SUCCESS)
    {
        assert(0);
    }
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Timer-level function to enter power save mode.
//!
//! Called by timer to enter power save mode.
//!
//! \param[in] param Pointer to media info.
//!
//! \return None.
///////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT static void powerSaveTimeout(ULONG param)
{
    // Post DPC to do the dirty work.
    RtStatus_t status = os_dpc_Send(OS_DPC_HIGH_LEVEL_DPC, enterLowPowerStateDpc, param, TX_NO_WAIT);

    // If we can't queue the DPC, set the timer up to fire again in a little bit.
    if (SUCCESS != status)
    {
        tx_timer_change(&g_powerSaveTimer, OS_MSECS_TO_TICKS(kTimeoutInMs_OptimizedForPower), 0);
        tx_timer_activate(&g_powerSaveTimer);
    }
}

PowerManager::PowerManager(TransferManager* transferManager, MmcSdDevice* device)
: m_transferManager(transferManager),
  m_device(device),
  m_powerSaveTimeoutInMs(kTimeoutInMs_OptimizedForSpeed),
  // Device starts out in low power mode (deselected) after probe completes.
  m_powerStateLow(true),
  m_isSleepEnabled(true)
{
}

PowerManager::~PowerManager()
{
    enterLowPowerState();
    tx_timer_delete(&g_powerSaveTimer);
}

RtStatus_t PowerManager::init()
{
    // Create the power safe timer.
    uint32_t txStatus = tx_timer_create(&g_powerSaveTimer, "MMC:power",
            powerSaveTimeout,
            reinterpret_cast<ULONG>(this),
            0, 0, TX_NO_ACTIVATE);
    if (TX_SUCCESS != txStatus)
    {
        return os_thi_ConvertTxStatus(txStatus);
    }

    // By default we start out optimized for speed, not power.
    m_powerSaveTimeoutInMs = kTimeoutInMs_OptimizedForSpeed;

    return SUCCESS;
}

void PowerManager::optimizeForPower()
{
    m_powerSaveTimeoutInMs = kTimeoutInMs_OptimizedForPower;
}

void PowerManager::optimizeForSpeed()
{
    m_powerSaveTimeoutInMs = kTimeoutInMs_OptimizedForSpeed;
}

RtStatus_t PowerManager::enterLowPowerState()
{
    if (!m_powerStateLow)
    {
        RtStatus_t status;

        // Make sure high power timer is deactivated.
        tx_timer_deactivate(&g_powerSaveTimer);

        // We interpret media sleep disabled to mean never deselect the device.
        if (!m_isSleepEnabled)
        {
            return SUCCESS;
        }

        // Stop in progress transfer, if any.
        assert(m_transferManager);
        status = m_transferManager->stop();
        if (SUCCESS != status)
        {
            return status;
        }

        // Deselect the device on the bus.
        assert(m_device);
        status = m_device->deselect();
        if (SUCCESS != status)
        {
            return status;
        }

        m_powerStateLow = true;
    }

    return SUCCESS;
}

RtStatus_t PowerManager::exitLowPowerState()
{
    if (m_powerStateLow)
    {
        RtStatus_t status;

        // Select the device on the bus.
        assert(m_device);
        status = m_device->select();
        if (SUCCESS != status)
        {
            return status;
        }

        m_powerStateLow = false;
    }

    // Reset the timer to allow automatic entry into power save mode
    // when there have been no transfers on the bus for a while.
    // We interpret media sleep disabled to mean never deselect the device.
    //! \todo Consider starting the timer after transfer instead of after select
    if (m_isSleepEnabled)
    {
        tx_timer_deactivate(&g_powerSaveTimer);
        tx_timer_change(&g_powerSaveTimer, OS_MSECS_TO_TICKS(m_powerSaveTimeoutInMs), 0);
        tx_timer_activate(&g_powerSaveTimer);
    }

    return SUCCESS;
}

void PowerManager::enableSleep(bool enable)
{
    m_isSleepEnabled = enable;

    // If enabling sleep, force low power state now.
    if (m_isSleepEnabled)
    {
        enterLowPowerState();
    }
}

bool Sequencer::isSequenceLong(bool isSequential)
{
    // If the sequential flag is set then the current sector number is one greater than the last
    // one, so increment the sequence count. Otherwise, reset the sequence count.
    if (isSequential)
    {
        ++m_sequenceCount;
    }
    else
    {
        m_sequenceCount = 0;
    }

    // If we have reached the minimum sequence threshold, reset the sequence count and return true.
    // Otherwise, return false.
    if (kSequenceThreshold == m_sequenceCount)
    {
        m_sequenceCount = 0;
        return true;
    }
    else
    {
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
