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
//! \addtogroup ddi_lba_nand_media
//! @{
//! \file ddi_lba_nand_media.h
//! \brief Internal declarations for the LBA NAND media layer.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_lba_nand_media_h_)
#define _ddi_lba_nand_media_h_

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
//! \brief Initialize the LBA NAND media.
//!
//! \fntype Function
//!
//! \param[in] pDescriptor Pointer to the Logical Drive Descriptor.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//!
//! \post The LBA NAND hardware has been setup and is ready for transfers.  The
//!       media descriptors have been initialized.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaInit(LogicalMedia_t *pDescriptor);

///////////////////////////////////////////////////////////////////////////////
//! \brief Allocate the drives on the NAND media.
//!
//! \fntype Function
//!
//! This function will carve up the LBA NAND media into the number of drives
//! specified.  Each drive is a contiguous unit.  There are system drives
//! which are used for storing code and data drives which are used for storing
//! data.
//!
//! \param[in] pDescriptor Pointer to the Logical Drive Descriptor.
//! \param[in] pTable Pointer to Media Allocation Table structure.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaAllocate(LogicalMedia_t *pDescriptor,
                                MediaAllocationTable_t *pTable);

///////////////////////////////////////////////////////////////////////////////
//! \brief Discover the allocation of drives on the LBA NAND media.
//!
//! \fntype Function
//!
//! This function will determine the partitions that the drives have been
//! allocated to.  Each drive is a contiguous unit.  There are system drives
//! which are used for storing code and data drives which are used for storing
//! data.
//!
//! \param[in] pDescriptor Pointer to the Logical Drive Descriptor.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_ERASED
//!
//! \post The media has been partitioned into drives and is almost ready for
//!       use (each drive must be initialized).
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaDiscoverAllocation(LogicalMedia_t *pDescriptor);

///////////////////////////////////////////////////////////////////////////////
//! \brief Returns the current media allocation table to the caller.
//!
//! \fntype Function
//!
//! \param[in] pDescriptor Pointer to the pointer to the logical media
//!     descriptor structure
//! \param[out] pTable Pointer to the pointer for the media allocation table
//!     structure 
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_LMEDIA_NOT_ALLOCATED
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaGetMediaTable(LogicalMedia_t *pDescriptor,
                                     MediaAllocationTable_t **pTable);

///////////////////////////////////////////////////////////////////////////////
//! \brief Reads specified information about the LBA NAND media.
//!
//! \fntype Function
//!
//! \param[in] pDescriptor Pointer to the logical media descriptor structure.
//! \param[in] u32Type Type of information requested.
//! \param[out] pInfo Pointer to information buffer to be filled.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_INFO_TYPE
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaGetInfo(LogicalMedia_t *pDescriptor,
                               uint32_t u32Type, void *pInfo);

///////////////////////////////////////////////////////////////////////////////
//! \brief Set specified information about the LBA NAND media.
//!
//! \fntype Function
//!
//! \param[in] pDescriptor Pointer to the logical media descriptor structure.
//! \param[in] u32Type Type of information requested.
//! \param[out] pInfo Pointer to information buffer to be set
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
//! \retval ERROR_DDI_LDL_LMEDIA_INVALID_MEDIA_INFO_TYPE
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaSetInfo(LogicalMedia_t *pDescriptor, 
                               uint32_t u32Type, const void *pInfo);

///////////////////////////////////////////////////////////////////////////////
//! \brief Erase the LBA NAND media.
//!
//! \fntype Function
//!
//! This function will erase the media, which typically occurs when an update
//! occurs.  In order to preserve the DRM data, the Hidden Data Drive needs
//! to be preserved.
//!
//! \param[in] pDescriptor Pointer to logical drive descriptor.
//! \param[in] u32MagicNumber Magic number to prevent accidental erasures.
//! \param[in] u8DoNotEraseHidden Flag to indicate hidden data drive should
//!            not be erased.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
//! \retval ERROR_DDI_NAND_LMEDIA_MEDIA_WRITE_PROTECTED
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaErase(LogicalMedia_t *pDescriptor,
                             uint32_t u32MagicNumber, 
                             uint8_t u8NoEraseHidden);

///////////////////////////////////////////////////////////////////////////////
//! \brief Shutdown the LBA NAND Media.
//!
//! \fntype Function
//!
//! This function is responsible for shutting down the media.
//!
//! \param[in] pDescriptor Pointer to the Logical Drive Descriptor.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaShutdown(LogicalMedia_t *pDescriptor);

///////////////////////////////////////////////////////////////////////////////
//! \brief Flush Drives on the LBA NAND Media.
//!
//! \fntype Function
//!
//! This function is responsible for flusing drives the media.
//!
//! \param[in] pDescriptor Pointer to the Logical Drive Descriptor.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaFlushDrives(LogicalMedia_t *pDescriptor);

///////////////////////////////////////////////////////////////////////////////
//! \brief Set bootable drive to the one specified in Tag.
//!
//! This function is responsible for setting the device to boot from primary or
//! Secondary firmware. It does this by setting persistent bit based on 
//! Drive Tag value.
//!
//! \param[in]  pDescriptor Pointer to the Logical Drive Descriptor.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t LbaNandMediaSetBootDrive(LogicalMedia_t *pDescriptor, DriveTag_t Tag);

#ifdef __cplusplus
}
#endif

#endif // _ddi_lba_nand_media_h_

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
