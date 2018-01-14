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
//! \file   MmcMedia.h
//! \brief  Declarations for MMC Media classes.
////////////////////////////////////////////////////////////////////////////////
#ifndef _MMCMEDIA_H
#define _MMCMEDIA_H

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "drivers/media/include/ddi_media_internal.h"
#include "drivers/ssp/mmcsd/ddi_ssp_mmcsd.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/mmc/src/mbr_types.h"
#include "components/telemetry/tss_logtext.h"
#include "simple_mutex.h"

///////////////////////////////////////////////////////////////////////////////
// External References
///////////////////////////////////////////////////////////////////////////////

//! \brief Mutex used to lock access to Media and Drives.
//!
//! The DDI MMC driver locks all external entry points to Media and Drives.
//! In particular, this serializes access from the file system and VMI paging.
//! In addition, this protects all access to the SSP MMCSD driver HAL and Device
//! objects since the DDI MMC driver is the only client.
extern TX_MUTEX g_mmcThreadSafeMutex;

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace mmc
{

// Forward declaration.
class TransferManager;

#if defined(DEBUG_TRACE) && !defined(NO_SDRAM)
/*!
 * \brief Utility class to hold a wrapping array of debug trace values.
 */
class DebugTrace
{
public:
    enum _trace_size { kTraceSize = 100 };
    inline static void add(uint32_t value) { if (m_pos < kTraceSize) { m_value[m_pos++] = value; } }
    inline static void addWrap(uint32_t value) { wrap(); m_value[m_pos++] = value; }
    inline static void wrap() { if (m_pos == kTraceSize) { m_pos = 0; } }
    static uint32_t m_pos;
    static uint32_t m_value[kTraceSize];
};
#endif // DEBUG_TRACE

/*!
 * \brief Utility class to automatically lock and unlock the MMC driver.
 */
class DdiMmcLocker : public SimpleMutex
{
public:
    //! \brief Locks the mutex that serializes access to the MMC driver.
    DdiMmcLocker()
    :   SimpleMutex(g_mmcThreadSafeMutex)
    {
    }

    //! \brief Unlocks the MMC driver mutex.
    //!
    //! Before the mutex is unlocked it is prioritized, which makes sure that
    //! the highest priority thread that is blocked on the mutex will be the
    //! next in line to hold it.
    ~DdiMmcLocker()
    {
        tx_mutex_prioritize(m_mutex);
    }
};

/*!
 * \brief Access to MBR partition table structure.
 */
class PartitionTable
{
public:
    /*!
     * \brief Iterator for partition table partition entry array.
     */
    class EntryIterator
    {
    public:
        //! \brief Constructor.
        //! \param partTable Partition table.
        EntryIterator(PartTable_t* partTable) : m_partTable(partTable), m_current(0) {}

        //! \brief Get pointer to next entry.
        //! \return Partition table.
        PartEntry_t* getNext();

        //! \brief Reset to start of table.
        void reset() { m_current = 0; }

    private:
        PartTable_t* m_partTable;   //!< The MBR Partition Table
        unsigned m_current;         //!< Current index into partition entry array
    };

public:
    //! \brief Constructor.
    PartitionTable(SECTOR_BUFFER* buffer) : m_buffer(buffer) {}

    //! \brief Initialize media from a device object.
    //!
    //! This reads the MBR partition table from the device.
    //! \param device Device.
    //! \return SUCCESS or error code.
    RtStatus_t initFromDevice(mmchal::MmcSdDevice &device) const;

    //! \brief Save partition table to device.
    //!
    //! \pre The partition entries must already be populated.
    //! \param device Device.
    //! \return SUCCESS or error code.
    RtStatus_t saveToDevice(mmchal::MmcSdDevice &device) const;

    //! \brief Get a pointer to the partition table.
    inline PartTable_t* getTable() const { return reinterpret_cast<PartTable_t *>(m_buffer); }

private:
    SECTOR_BUFFER* m_buffer;    //!< Sector buffer used to hold partition table
};

/*!
 * \brief Utility class to keep track of drive sector allocation.
 */
class Allocator
{
public:
    //! \brief Constructor.
    //! \param unitSizeBytes Size of allocation unit in bytes.
    Allocator(uint64_t unitSizeBytes) : m_unitSizeBytes(unitSizeBytes), m_byteOffset(0) {}

    //! \brief Reserve bytes.
    //! \param byteCount Number of bytes to reserve.
    //! \return Number of bytes actually reserved.
    uint64_t reserve(uint64_t byteCount);

    //! \brief Get current byte offset.
    //! \return Current byte offset.
    uint64_t getByteOffset() const { return m_byteOffset; }

private:
    uint64_t m_unitSizeBytes;   //!< Allocation unit size
    uint64_t m_byteOffset;      //!< Current byte offset
};

/*!
 * \brief MMC media class.
 */
class MmcMedia : public ::LogicalMedia
{
public:

    //! \brief Default constructor.
    MmcMedia();

    //! \brief Destructor.
    virtual ~MmcMedia();

    //! \name Logical media API
    //@{
    virtual RtStatus_t init();
    virtual RtStatus_t allocate(MediaAllocationTable_t * table);
    virtual RtStatus_t discover();
    virtual RtStatus_t getMediaTable(MediaAllocationTable_t ** table);
    virtual RtStatus_t freeMediaTable(MediaAllocationTable_t * table);
    virtual RtStatus_t getInfo(uint32_t infoSelector, void * value);
    virtual RtStatus_t setInfo(uint32_t infoSelector, const void * value);
    virtual RtStatus_t erase();
    virtual RtStatus_t shutdown();
    virtual RtStatus_t flushDrives();
    virtual RtStatus_t setBootDrive(DriveTag_t tag);
    //@}

    //! \brief Get the MMC/SD device object.
    //! \return Device object.
    inline mmchal::MmcSdDevice* getDevice() const { return m_device; }

    //! \brief Get the read/write sector Transfer Manager.
    //! \return TransferManager object.
    inline TransferManager* getTransferManager() const { return m_transferManager; }

    //! \brief Convert native sectors to device blocks.
    //! \param sectors Count in native sectors.
    //! \return Count in device blocks.
    inline uint32_t sectorsToDeviceBlocks(uint32_t sectors) const { return sectors * m_deviceBlocksPerSector; }

private:
    RtStatus_t createExternalDataDrive();
    RtStatus_t createInternalDrives();
    void allocSystemDrive(Allocator &alloc, PartitionTable::EntryIterator &partIterator,
            MediaAllocationTable_t* mediaTable);
    void allocHiddenDrives(Allocator &alloc, PartitionTable::EntryIterator &partIterator,
            MediaAllocationTable_t* mediaTable);
    void allocDataDrive(Allocator &alloc, PartitionTable::EntryIterator &partIterator);

private:
    SspPortId_t m_portId;               //!< SSP port ID (SSP1 or SSP2)
    mmchal::MmcSdDevice* m_device;      //!< MMC/SD device
    uint32_t m_deviceBlocksPerSector;   //!< Number of MMC/SD block per native sector
    TransferManager* m_transferManager; //!< Transfer manager for read/write sector
};

} // namespace mmc

#endif // _MMCMEDIA_H

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
