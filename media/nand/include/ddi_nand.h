///////////////////////////////////////////////////////////////////////////////
// Copyright (c) SigmaTel, Inc. All rights reserved.
// 
// SigmaTel, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of SigmaTel, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand
//! @{
//! \file ddi_nand.h
//! \brief Contains public declarations for the NAND driver.
//!
//! This file contains declarations of public interfaces to
//! the NAND driver.
//!
///////////////////////////////////////////////////////////////////////////////

#if !defined(_ddi_nand_h_)
#define _ddi_nand_h_

#include "types.h"
#include "errordefs.h"
#include "drivers/media/ddi_media.h"

///////////////////////////////////////////////////////////////////////////////
// Constants
///////////////////////////////////////////////////////////////////////////////

enum
{
    //! \brief DriveSetInfo() key for control over system drive recovery. [bool]
    //!
    //! Use DriveSetInfo() to modify this property of system drives. Setting it
    //! to true will enable automatic recovery of system drives when an error is
    //! encountered during a page read. The drive will be completely erased and
    //! rewritten from the master copy. Setting this property to false will disable
    //! the recovery functionality.
    kDriveInfoNandSystemDriveRecoveryEnabled = 'nsre'
};

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

////////////////////////////////////////////////////////////////////////////////
//! \brief Repairs NAND boot structures if necessary.
//!
//! The #RTC_NAND_SECONDARY_BOOT persistent bit is read to determine if the
//! boot ROM has encountered any errors while loading from the NAND.
//! If any of the primary boot blocks, the NCB and LDLB, are damaged, they
//! will be immediately repaired. Then a refresh of the primary firmware
//! system drive that will run in the background is started. This function
//! returns before the firmware refresh has completed.
//!
//! \retval SUCCESS Either no repair was needed, or the repair was successful.
//!
//! \pre The NAND driver and all drives must be fully initialised.
//! \pre Demand paging must be initialised for no-SDRAM systems.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_repair_boot_media(void);

////////////////////////////////////////////////////////////////////////////////
//! \brief Shutdown the NAND HAL and GPMI.
////////////////////////////////////////////////////////////////////////////////
void ddi_nand_hal_shutdown(void);

////////////////////////////////////////////////////////////////////////////////
//! \brief Function to create the NAND logical media instance.
////////////////////////////////////////////////////////////////////////////////
LogicalMedia * nand_media_factory(const MediaDefinition_t * def);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

#endif // _ddi_nand_h_
//! @}



