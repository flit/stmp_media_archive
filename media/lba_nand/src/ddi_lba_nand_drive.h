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
//! \addtogroup ddi_lba_nand_drive
//! @{
//! \file ddi_lba_nand_drive.h
//! \brief Definitions and types for the LBA NAND drive functions.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_lba_nand_drive_h_)
#define _ddi_lba_nand_drive_h_

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "ddi_lba_nand_internal.h"

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
//! \brief Initialize the appropriate drive.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor for the
//!            drive to initialize.
//!
//! \fntype Function
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LDRIVE_MEDIA_NOT_ALLOCATED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveInit(LogicalDrive_t *pDescriptor);

///////////////////////////////////////////////////////////////////////////////
//! \brief Shutdown the appropriate drive.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor for the
//!            drive to shutdown.
//!
//! \fntype Function
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveShutdown(LogicalDrive_t *pDescriptor);

///////////////////////////////////////////////////////////////////////////////
//! \brief Return the size of the info requested.
//!
//! \fntype Function
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//! \param[in] u32Type Type of info requested.
//! \param[out] pu32Size Size of info requested.
//!
//! \return Status of call or error.
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveGetInfoSize(LogicalDrive_t *pDescriptor,
                                   uint32_t u32Type,
                                   uint32_t *pu32Size);

///////////////////////////////////////////////////////////////////////////////
//! \brief Return specified information about the drive.
//!
//! \fntype Function
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//! \param[in] u32Type Type of info requested.
//! \param[out] pInfo Filled with requested data.
//!
//! \return Status of call or error.
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveGetInfo(LogicalDrive_t *pDescriptor,
                               uint32_t u32Type,
                               void *pInfo);

///////////////////////////////////////////////////////////////////////////////
//! \brief Set specified information about the drive.
//!
//! \fntype Function
//!
//! Only a small subset of drive info selectors can be modified. Attempting
//! to set a selector that cannot be changed will result in an error.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//! \param[in] u32Type Type of info requested: Tag, Component Version, Project
//!     Version, etc. 
//! \param[in] pInfo Pointer to data to set.
//!
//! \return Status of call or error.
//! \retval SUCCESS Data was set successfully.
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED Drive is not initialised.
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_INFO_TYPE Cannot modify the requested
//!         data field.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveSetInfo(LogicalDrive_t *pDescriptor,
                               uint32_t u32Type,
                               const void *pInfo);

///////////////////////////////////////////////////////////////////////////////
//! \brief Read a sector from a Drive.
//!
//! \fntype Function
//!
//! This function will read a sector from an LBA NAND drive.
//!
//! \param[in] pDescriptor Pointer to the Logical Drive Descriptor.
//! \param[in] u32SectorNumber Logical Sector Number to be read.
//! \param[out] pSectorData Pointer where sector data should be stored
//!             when reading.
//!
//! \return Status of call or error.
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE
//!
//! \post If successful, the data is in pSectorData.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveReadSector(LogicalDrive_t *pDescriptor,
                                  uint32_t u32SectorNumber,
                                  SECTOR_BUFFER *pSectorData);

///////////////////////////////////////////////////////////////////////////////
//! \brief Write a sector to a Drive.
//!
//! \fntype Function
//!
//! This function will write a sector to an LBA NAND drive.
//!
//! \param[in] pDescriptor Pointer to the Logical Drive Descriptor.
//! \param[in] u32SectorNumber Sector Number to write.
//! \param[out] pSectorData Pointer to the sector data to be written.
//!             stored.
//!
//! \return Status of call or error.
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE
//! \retval ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED
//!
//! \note   The write is synchronous.  The routine does not return until the
//!         write is complete.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveWriteSector(LogicalDrive_t *pDescriptor,
                                   uint32_t u32SectorNumber,
                                   const SECTOR_BUFFER *pSectorData);

///////////////////////////////////////////////////////////////////////////////
//! \brief Erase the Drive.
//!
//! \fntype Function
//!
//! This function will "Erase" the entire drive.
//!
//! \param[in] pDriveDescriptor Logical Drive Descriptor.
//! \param[in] u32MagicNumber Special code, to protect against accidental
//!            calls (e.g. due to a program bug that falls through to this routine).
//!            Unused currently.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE
//! \retval ERROR_DDI_LDL_LDRIVE_WRITE_PROTECTED
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveErase(LogicalDrive_t *pDescriptor,
                             uint32_t u32MagicNumber);

///////////////////////////////////////////////////////////////////////////////
//! \brief Flush a Drive.
//!
//! \fntype Function
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//!
//! \return Status of call.
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TYPE
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandDriveFlush(LogicalDrive_t *pDescriptor);

#ifdef __cplusplus
}
#endif

#endif // _ddi_lba_nand_drive_h_

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
