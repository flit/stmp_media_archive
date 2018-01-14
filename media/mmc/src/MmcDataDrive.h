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
//! \file   MmcDataDrive.h
//! \brief  Declarations for MMC Data Drive classes.
////////////////////////////////////////////////////////////////////////////////
#ifndef _MMCDATADRIVE_H
#define _MMCDATADRIVE_H

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "MmcMedia.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace mmc
{

/*!
 * \brief MMC data drive.
 */
class MmcDataDrive : public LogicalDrive
{
public:

    //! \brief Default constructor.
    MmcDataDrive();

    //! \brief Destructor.
    virtual ~MmcDataDrive();

    //! \brief Initialize the drive from a logical media object.
    //!
    //! This initializer uses the whole media as a data drive. It is designed
    //! for use with external media.
    //! \param media Media object.
    //! \return SUCCESS or error code.
    RtStatus_t initFromMedia(MmcMedia* media);

    //! \brief Initialize the drive from an MBR partition entry.
    //!
    //! This initializer is used for the data drive on the internal media.
    //! \param media Media object.
    //! \param partEntry Partition entry.
    //! \param driveType Logical drive type.
    //! \param driveTag Drive tag.
    //! \return SUCCESS or error code.
    RtStatus_t initFromPartitionEntry(MmcMedia* media, PartEntry_t* partEntry,
            LogicalDriveType_t driveType = kDriveTypeData,
            uint32_t driveTag = DRIVE_TAG_DATA);

    //! \name Logical drive API
    //@{
    virtual RtStatus_t init();
    virtual RtStatus_t shutdown();
    virtual RtStatus_t getInfo(uint32_t infoSelector, void * value);
    virtual RtStatus_t setInfo(uint32_t infoSelector, const void * value);
    virtual RtStatus_t readSector(uint32_t sector, SECTOR_BUFFER * buffer);
    virtual RtStatus_t writeSector(uint32_t sector, const SECTOR_BUFFER * buffer);
    virtual RtStatus_t erase();
    virtual RtStatus_t flush();
    virtual RtStatus_t repair();
    //@}

private:
    bool isInternalDrive() { return (DRIVE_TAG_DATA == m_u32Tag); }

protected:
    MmcMedia* m_media;                  //!< The MMC media object that we belong to.
    mmchal::MmcSdDevice* m_device;      //!< Device object
    TransferManager* m_transferManager; //!< Transfer Manager for media read/write
    uint32_t m_startSectorNumber;       //!< Native sector offset on the media where our drive starts
};

} // namespace mmc

#endif // _MMCDATADRIVE_H

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
