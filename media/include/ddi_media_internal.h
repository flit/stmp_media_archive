///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor. All rights reserved.
//
// Freescale Semiconductor
// Proprietary & Confidential
//
// This source code and the algorithms implemented therein constitute confidential
// information and may comprise trade secrets of Freescale Semiconductor or its
// associates, and any use thereof is subject to the terms and conditions of the
// Confidential Disclosure Agreement pursual to which this source code was
// originally received.
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media
//! @{
//! \file ddi_media_internal.h
//! \brief Contains private interface for the Logical Drive Layer.
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDILDL_INTERNAL_H
#define _DDILDL_INTERNAL_H

#include "drivers/media/ddi_media.h"
#include "os/threadx/tx_api.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

#if defined (__cplusplus)

/*!
 * \brief Properties of a logical media.
 *
 * Discovered at runtime by MediaInit().
 *
 * - One per instance of the driver.
 * - This struct is used mostly by the LDL.
 * - pMediaInfo points to a private driver info struct. In the MMC case, this is a ddi_mmc_media_info_t.
 */
class LogicalMedia
{
public:
    
    //! \brief Default constructor.
    LogicalMedia();
    
    //! \brief Destructor.
    virtual ~LogicalMedia();

    //! \name Logical media API
    //@{
    virtual RtStatus_t init() = 0;
    virtual RtStatus_t allocate(MediaAllocationTable_t * table) = 0;
    virtual RtStatus_t discover() = 0;
    virtual RtStatus_t getMediaTable(MediaAllocationTable_t ** table) = 0;
    virtual RtStatus_t freeMediaTable(MediaAllocationTable_t * table);
    virtual RtStatus_t getInfoSize(uint32_t infoSelector, uint32_t * infoSize);
    virtual RtStatus_t getInfo(uint32_t infoSelector, void * value);
    virtual RtStatus_t setInfo(uint32_t infoSelector, const void * value);
    virtual RtStatus_t erase() = 0;
    virtual RtStatus_t shutdown() = 0;
    virtual RtStatus_t flushDrives() = 0;
    virtual RtStatus_t setBootDrive(DriveTag_t tag) = 0;
    //@}
    
    //! \name Accessors
    //@{
    uint32_t getMediaNumber() const { return m_u32MediaNumber; }
    bool isInitialized() const { return m_bInitialized; }
    bool isWriteProtected() const { return m_bWriteProtected; }
    bool isRemovable() const { return m_isRemovable; }
    bool isAllocated() const { return m_bAllocated; }
    MediaState_t getState() const { return m_eState; }
    uint32_t getNumberOfDrives() const { return m_u32NumberOfDrives; }
    void setNumberOfDrives(uint32_t count) { m_u32NumberOfDrives = count; }
    uint64_t getSizeInBytes() const { return m_u64SizeInBytes; }
    uint32_t getAllocationUnitSizeInBytes() const { return m_u32AllocationUnitSizeInBytes; }
    PhysicalMediaType_t getPhysicalType() const { return m_PhysicalType; }
    //@}
    //! \brief Assign the expected transfer activity type
    inline RtStatus_t setTransferActivityType(TransferActivityType_t eTransferActivityType){
        m_TransferActivityType = eTransferActivityType;
        return SUCCESS;
    }
public:

    uint32_t m_u32MediaNumber;
    bool m_bInitialized;
    MediaState_t m_eState;
    bool m_bAllocated;
    bool m_bWriteProtected;
    bool m_isRemovable;
    uint32_t m_u32NumberOfDrives; // Includes ALL drive types on this media.
    uint64_t m_u64SizeInBytes;
    uint32_t m_u32AllocationUnitSizeInBytes;
    PhysicalMediaType_t m_PhysicalType;
    TransferActivityType_t m_TransferActivityType;

};

/*!
 * \brief Properties of a logical drive.
 *
 * Discovered at runtime by MediaDiscover().
 *
 * - Again, used mostly by the LDL.
 * - One per accessibly region of the device.
 * - A drive may not necessarily be equivalent to a partition, since the drive may encompass all
 * sectors of a media, including the MBR. This is normally the way the MMC/SD drive is set up; it's
 * sector count matches that of its media.
 * - Drives may overlap. So you can have one drive that is for the entire media and contains all
 * partitions, and another drive that is just one partition on the media.
 * - pLogicalMediaDescriptor points to the LogicalMedia_t for that device containing the drive. All
 * logical drives belonging to the same device (i.e., all partitions of a device) must point to the
 * same LogicalMedia_t.
 * - pMediaInfo has the same value as pLogicalMediaDescriptor->pMediaInfo.
 * - pDriveInfo points to a private driver struct. For MMC, this is the ddi_mmc_drive_info_t struct.
 * - Native sectors are the actual sectors read from/written to the device. The media driver always
 * uses native sectors.
 * - Nominal sectors are what our filesystem uses, as presented by the media cache.
 *
 */
class LogicalDrive
{
public:
    
    //! \brief Default constructor.
    LogicalDrive();
    
    //! \brief Destructor.
    virtual ~LogicalDrive();
    
    //! \name Logical drive API
    //@{
    virtual RtStatus_t init() = 0;
    virtual RtStatus_t shutdown() = 0;
    virtual RtStatus_t getInfoSize(uint32_t infoSelector, uint32_t * infoSize);
    virtual RtStatus_t getInfo(uint32_t infoSelector, void * value);
    virtual RtStatus_t setInfo(uint32_t infoSelector, const void * value);
    virtual RtStatus_t readSectorForVMI(uint32_t sector, SECTOR_BUFFER * buffer);
    virtual RtStatus_t readSector(uint32_t sector, SECTOR_BUFFER * buffer) = 0;
    virtual RtStatus_t writeSector(uint32_t sector, const SECTOR_BUFFER * buffer) = 0;
    virtual RtStatus_t openMultisectorTransaction(uint32_t start, uint32_t count, bool isRead) { return SUCCESS; }
    virtual RtStatus_t commitMultisectorTransaction() { return SUCCESS; }
    virtual RtStatus_t erase() = 0;
    virtual RtStatus_t flush() = 0;
    virtual RtStatus_t repair() = 0;
    //@}
    
    //! \name Accessors
    //@{
    bool isInitialized() const { return m_bInitialized; }
    bool isErased() const { return m_bErased; }
    bool didFailInit() const { return m_bFailedInit; }
    void setDidFailInit(bool didFail) { m_bFailedInit = didFail; }
    uint32_t getSectorCount() const { return m_u32NumberOfSectors; }
    uint32_t getNativeSectorCount() const { return m_numberOfNativeSectors; }
    uint32_t getSectorSize() const { return m_u32SectorSizeInBytes; }
    uint32_t getNativeSectorSize() const { return m_nativeSectorSizeInBytes; }
    uint32_t getNativeSectorShift() const { return m_nativeSectorShift; }
    uint32_t getEraseSize() const { return m_u32EraseSizeInBytes; }
    uint64_t getSizeInBytes() const { return m_u64SizeInBytes; }
    uint32_t getPbsStartSector() const { return m_pbsStartSector; }
    LogicalDriveType_t getType() const { return m_Type; }
    DriveTag_t getTag() const { return m_u32Tag; }
    LogicalMedia * getMedia() { return m_logicalMedia; }
    DriveState_t getState() const;
    //@}
    
    //! \name Template forms
    //@{
    template <typename T> inline T getInfo(uint32_t selector)
    {
        T value;
        getInfo(selector, (void *)&value);
        return value;
    }

    template <typename T> inline T getInfo(uint32_t selector, RtStatus_t & status)
    {
        T value;
        status = getInfo(selector, (void *)&value);
        return value;
    }
    
    template <typename T> inline RtStatus_t setInfo(uint32_t selector, T value)
    {
        return setInfo(selector, (const void *)&value);
    }
    //@}

public:

    bool m_bInitialized;   //!< True if the drive has been inited.
    bool m_bFailedInit;    //!< True if an attempt was made to init the drive but it failed for some reason. Ignored if bInitialized is true.
    bool m_bPresent;       //!< Indicates if a system drive is present.
    bool m_bErased;
    bool m_bWriteProtected;
    uint32_t m_u32NumberOfSectors;
    LogicalDriveType_t m_Type;
    DriveTag_t m_u32Tag;
    uint64_t m_u64SizeInBytes;             //!< Total drive size in bytes.
    uint32_t m_u32SectorSizeInBytes;       //!< Nominal sector size, can be configured by the application in some cases.
    uint32_t m_nativeSectorSizeInBytes;    //!< Native sector size, determined by the underlying media driver.
    uint32_t m_numberOfNativeSectors;      //!< Number of native sectors big the drive is.
    uint32_t m_nativeSectorShift;          //!< Shift to convert between native and nominal sector sizes.
    uint32_t m_u32EraseSizeInBytes;
    uint32_t m_pbsStartSector;    //!< Offset in nominal sectors to the PBS.
    LogicalMedia * m_logicalMedia;  //!< Logical media that owns this drive.

};

/*!
 * \brief Set of available media and drives.
 */
struct LdlInfo
{
    uint32_t m_mediaCount;
    LogicalMedia * m_media[MAX_LOGICAL_MEDIA];
    uint32_t m_driveCount;
    LogicalDrive * m_drives[MAX_LOGICAL_DRIVES];
};

extern LdlInfo g_ldlInfo;

#endif // __cplusplus

///////////////////////////////////////////////////////////////////////////////
// Variables
///////////////////////////////////////////////////////////////////////////////

extern const MediaDefinition_t g_mediaDefinition[];

#ifdef RTOS_THREADX
    extern TX_MUTEX g_NANDThreadSafeMutex;
#endif

extern SerialNumber_t g_InternalMediaSerialNumber;

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

#if defined(__cplusplus)

///////////////////////////////////////////////////////////////////////////////
//! \brief Returns the media structure given a media index.
//!
//! \param tag Zero based index into the available media..
//! \return Either a pointer to the media object is returned, or NULL if
//!     the index is out of range.
///////////////////////////////////////////////////////////////////////////////
LogicalMedia * MediaGetMediaFromIndex(unsigned index);

///////////////////////////////////////////////////////////////////////////////
//! \brief Returns the drive structure given a drive tag.
//!
//! \param tag The unique tag for the drive.
//! \return Either a pointer to the drive structure is returned, or NULL if
//!     a drive with the specified tag does not exist.
///////////////////////////////////////////////////////////////////////////////
LogicalDrive * DriveGetDriveFromTag(DriveTag_t tag);

///////////////////////////////////////////////////////////////////////////////
//! \brief find an empty drive array entry if one exists
//!
//! \return Either a pointer to the drive structure is returned, or NULL if
//!     no empty entries exist in the drive array
///////////////////////////////////////////////////////////////////////////////
LogicalDrive ** DriveFindEmptyEntry(void);

///////////////////////////////////////////////////////////////////////////////
//! \brief Adds a new media to the LDL.
//! \param newMedia
//! \param mediaNumber
//! \retval SUCCESS
///////////////////////////////////////////////////////////////////////////////
RtStatus_t MediaAdd(LogicalMedia * newMedia, unsigned mediaNumber);

///////////////////////////////////////////////////////////////////////////////
//! \brief Adds a new drive to the LDL.
//!
//! \retval SUCCESS
///////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveAdd(LogicalDrive * newDrive);

///////////////////////////////////////////////////////////////////////////////
//! \brief Removes a specific drive from the LDL.
//!
//! \retval SUCCESS
///////////////////////////////////////////////////////////////////////////////
RtStatus_t DriveRemove(DriveTag_t driveToRemove);

#endif //__cplusplus

#endif // _DDILDL_INTERNAL_H
//! @}
