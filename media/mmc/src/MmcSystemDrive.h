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
//! \file   MmcSystemDrive.h
//! \brief  Declarations for MMC System Drive classes.
////////////////////////////////////////////////////////////////////////////////
#ifndef _MMCSYSTEMDRIVE_H
#define _MMCSYSTEMDRIVE_H

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "drivers/media/mmc/src/MmcMedia.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace mmc
{

/*!
 * \brief MMC system drive.
 */
class MmcSystemDrive : public LogicalDrive
{
public:

    //! \brief Default constructor.
    MmcSystemDrive();

    //! \brief Destructor.
    virtual ~MmcSystemDrive();

    //! \brief Initialize the drive from an MBR partition entry.
    //! \param media Media object.
    //! \param partEntry Partition entry.
    //! \return SUCCESS or error code.
    RtStatus_t initFromPartitionEntry(MmcMedia* media, PartEntry_t* partEntry);

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
    virtual RtStatus_t repair() { return ERROR_DDI_LDL_UNIMPLEMENTED; }
    //@}

protected:
    MmcMedia* m_media;                  //!< The MMC media object that we belong to.
    mmchal::MmcSdDevice* m_device;      //!< Device object
    TransferManager* m_transferManager; //!< Transfer Manager for media read/write
    uint32_t m_startSectorNumber;       //!< Sector offset on the media where our drive starts
    uint64_t m_componentVersion;        //!< Component version number from SB file
    uint64_t m_projectVersion;          //!< Project version number from SB file
};

} // namespace mmc

#endif // _MMCSYSTEMDRIVE_H

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
