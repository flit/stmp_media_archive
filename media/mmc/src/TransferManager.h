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
//! \file   TransferManager.h
//! \brief  Declarations for MMC Transfer Manager class.
////////////////////////////////////////////////////////////////////////////////
#ifndef _TRANSFERMANAGER_H
#define _TRANSFERMANAGER_H

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "drivers/ssp/mmcsd/ddi_ssp_mmcsd.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "MmcMedia.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace mmc
{

// Forward declaration.
class TransferManager;

/*!
 * \brief Select and Deselect the device on the bus to control power usage.
 *
 * This class is responsible for selecting and deselecting the device on the bus. The device
 * must be selected to perform data transfer operations. When selected, the device consumes
 * more power. Using a DPC timer, the device is deselected when not in
 * use in order to save power.
 */
class PowerManager
{
public:
    //! \brief Constructor.
    //! \param transferManager TransferManager object.
    //! \param device Device object.
    PowerManager(TransferManager* transferManager, mmchal::MmcSdDevice* device);

    //! \brief Destructor.
    ~PowerManager();

    //! \brief Initialize.
    //! \return SUCCESS or error code.
    RtStatus_t init();

    //! \brief Optimize for power savings.
    void optimizeForPower();

    //! \brief Optimize for speed.
    void optimizeForSpeed();

    //! \brief Deselect the device on the bus to enter low power state.
    //! \return SUCCESS or error code.
    RtStatus_t enterLowPowerState();

    //! \brief Select the device on the bus to exit low power state.
    //!
    //! Restarts the timer to allow automatic entry into power save mode
    //! when there have been no transfers on the bus for a while.
    //! \return SUCCESS or error code.
    RtStatus_t exitLowPowerState();

    //! \brief Return true if power state is low.
    //! \retval true Device is in low power state.
    //! \retval false Device is not in low power state.
    bool isPowerStateLow() const { return m_powerStateLow; }

    //! \brief Enable or disable sleep mode support.
    //! \param enable true to enable sleep mode support, false to disable.
    void enableSleep(bool enable);

private:
    TransferManager* m_transferManager;     //!< Associated Transfer Manager object
    mmchal::MmcSdDevice* m_device;          //!< Associated Device object
    uint32_t m_powerSaveTimeoutInMs;        //!< Timeout to enter low power state
    bool m_powerStateLow;                   //!< True if currently in low power state (deselected)
    bool m_isSleepEnabled;                  //!< True if media sleep is enabled
};

/*!
 * \brief Force the device into low power mode between transfers for short sequences.
 *
 * This class is responsible for determining the length of the current sequence of consecutive
 * sector numbers. If the sequence is short, force the device into low power mode between sector
 * transfers in defiance of the high power timer. As soon as the sequence is long enough,
 * allow the device to stay in high power state.
 */
class Sequencer
{
public:
    //! \brief Default constructor.
    Sequencer() : m_sequenceCount(0) {};

    //! \brief Update the sequence count.
    //! \retval true Sequence is now long.
    //! \retval false Sequence is not yet long.
    bool isSequenceLong(bool isSequential);

    //! \brief Return sequence count.
    //! \return Sequence count.
    inline unsigned getSequenceCount() const { return m_sequenceCount; }

private:
    //! \brief Constants used by the sequencer.
    enum _sequencer_constants
    {
         kSequenceThreshold = 3 //!< Number of consecutive sectors before sequence is considered long
    };

private:
    unsigned m_sequenceCount;   //!< Number of consecutive sectors in current sequence
};

/*!
 * \brief Optimize data transfer to the device using open-ended multi-transfers.
 *
 * This class is responsible for starting, continuing, and stopping multi-transfers.
 * The basic idea is to use a multi-transfer Start command for the first sector in a sequence,
 * then use a plain DMA transfer (no command) for the next sequential sectors. If a sector
 * comes in out of order or for a different direction (read vs. write), the current multi-transfer
 * is closed and a new one is started. As part of this process, this class uses a PowerManager
 * object to deselect the device on the bus when there are no transfers in progress, which
 * saves power.
 */
class TransferManager
{
public:
    //! \brief Constructor.
    //! \param media Media object.
    //! \param device Device object.
    TransferManager(MmcMedia* media, mmchal::MmcSdDevice* device);

    //! \brief Destructor.
    ~TransferManager();

    //! \name Initialization and configuration.
    //@{
    //! \brief Initialize.
    //! \return SUCCESS or error code.
    RtStatus_t init();

    //! \brief Optimize for power savings.
    inline void optimizeForPower() { m_powerManager.optimizeForPower(); }

    //! \brief Optimize for speed.
    inline void optimizeForSpeed() { m_powerManager.optimizeForSpeed(); }

    //! \brief Enable or disable sleep mode support.
    //! \param enable true to enable sleep mode support, false to disable.
    inline void enableSleep(bool enable) { m_powerManager.enableSleep(enable); }
    //@}

    //! \name Transfer operations.
    //@{
    //! \brief Read a sector.
    //! \param sectorNumber Sector to read.
    //! \param buffer Buffer to contain data.
    //! \return SUCCESS or error code.
    RtStatus_t readSector(uint32_t sectorNumber, SECTOR_BUFFER* buffer);

    //! \brief Write a sector.
    //! \param sectorNumber Sector to write.
    //! \param buffer Buffer containing data.
    //! \return SUCCESS or error code.
    RtStatus_t writeSector(uint32_t sectorNumber, const SECTOR_BUFFER* buffer);

    //! \brief Exit the current multi-transfer mode, if any is active.
    //! \return SUCCESS or error code.
    RtStatus_t stop();

    //! \brief Stop any multi-transfers in progress and enter low power state.
    //!
    //! Use this before directly calling device I/O operations (read/write/erase).
    //! \post Leaves the device deselected on the bus.
    //! \return SUCCESS or error code.
    RtStatus_t forceStop();
    //@}

private:
    //! \brief Transfer operation.
    typedef enum _operation
    {
        kOperationIdle,     //!< No operation in progress
        kOperationRead,     //!< Multi-read is active
        kOperationWrite     //!< Multi-write is active
    } Operation_t;

    /*!
     * \brief Abstract base class for Transfers.
     *
     * Generic methods the work for both Read and Write multi-transfers.
     * Subclass is responsible for holding the sector buffer because it may be const.
     */
    class Transfer
    {
    public:
        //! \brief Start a multi-transfer. See subclass documentation.
        virtual RtStatus_t start(mmchal::MmcSdDevice* device, uint32_t blockNumber, uint32_t blocksPerSector) const = 0;

        //! \brief Continue a multi-transfer. See subclass documentation.
        virtual RtStatus_t next(mmchal::MmcSdDevice* device, uint32_t blocksPerSector) const = 0;
    };

    /*!
     * \brief Multi-read transfers methods.
     */
    class ReadTransfer : public Transfer
    {
    public:
        //! \brief Constructor.
        //! \param buffer Sector buffer for data.
        ReadTransfer(SECTOR_BUFFER* buffer) : m_buffer(buffer) {}

        //! \brief Start a multi-read transfer.
        //! \param device Device object.
        //! \param blockNumber Block number.
        //! \param blocksPerSector Number of blocks per native sector.
        //! \return SUCCESS or error code.
        inline virtual RtStatus_t start(mmchal::MmcSdDevice* device, uint32_t blockNumber, uint32_t blocksPerSector) const
            { return device->startMultiRead(blockNumber, blocksPerSector, m_buffer); }

        //! \brief Continue a multi-read transfer.
        //! \param device Device object.
        //! \param blocksPerSector Number of blocks per native sector.
        //! \return SUCCESS or error code.
        inline virtual RtStatus_t next(mmchal::MmcSdDevice* device, uint32_t blocksPerSector) const
            { return device->continueMultiRead(blocksPerSector, m_buffer); }

    private:
        SECTOR_BUFFER* m_buffer;    //!< Sector buffer
    };

    /*!
     * \brief Multi-write transfers methods.
     */
    class WriteTransfer : public Transfer
    {
    public:
        //! \brief Constructor.
        //! \param buffer Sector buffer containing data.
        WriteTransfer(const SECTOR_BUFFER* buffer) : m_buffer(buffer) {}

        //! \brief Start a multi-write transfer.
        //! \param device Device object.
        //! \param blockNumber Block number.
        //! \param blocksPerSector Number of blocks per native sector.
        //! \return SUCCESS or error code.
        inline virtual RtStatus_t start(mmchal::MmcSdDevice* device, uint32_t blockNumber, uint32_t blocksPerSector) const
            { return device->startMultiWrite(blockNumber, blocksPerSector, m_buffer); }

        //! \brief Continue a multi-write transfer.
        //! \param device Device object.
        //! \param blocksPerSector Number of blocks per native sector.
        //! \return SUCCESS or error code.
        inline virtual RtStatus_t next(mmchal::MmcSdDevice* device, uint32_t blocksPerSector) const
            { return device->continueMultiWrite(blocksPerSector, m_buffer); }

    private:
        const SECTOR_BUFFER* m_buffer;    //!< Sector buffer
    };

private:
    RtStatus_t execute(Operation_t operation, Transfer& transfer, uint32_t sectorNumber);

#if defined(DEBUG_TRACE) && !defined(NO_SDRAM)
    void DebugDumpState(Operation_t operation, uint32_t sectorNumber);
#endif

private:
    MmcMedia* m_media;                  //!< Media object
    mmchal::MmcSdDevice* m_device;      //!< Device object
    uint32_t m_blocksPerSector;         //!< Media blocks per sector
    Operation_t m_currentOperation;     //!< Current transfer operation
    uint32_t m_lastSectorNumber;        //!< Sector number of the previous transfer
    PowerManager m_powerManager;        //!< Power manager object
    Sequencer m_sequencer;              //!< Sequence manager object
};

} // namespace mmc

#endif // _TRANSFERMANAGER_H

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
