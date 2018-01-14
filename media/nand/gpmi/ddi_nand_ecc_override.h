#if defined(STMP378x)
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
//! \addtogroup ddi_media_nand_hal_ecc
//! @{
//! \file    ddi_nand_ecc_override.h
//! \brief   Provides public interface for overriding ECC parameters.
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDI_ECC_OVERRIDE_H
#define _DDI_ECC_OVERRIDE_H

//! Structure returned from ECC parameters override function.
typedef struct _nand_bch_Parameters
{
    uint32_t u32Block0Level;    //!< ECC level for Block 0 (0,2,4,...20)
    uint32_t u32BlockNLevel;    //!< ECC level for Block N (0,2,4,...20)
    uint32_t u32Block0Size;     //!< Block 0 size in bytes (typically 512)
    uint32_t u32BlockNSize;     //!< Block N size in bytes (typically 512)
    uint32_t u32BlockNCount;    //!< Block N count (does not include block 0)
    uint32_t u32MetadataBytes;  //!< Number of metadata bytes (typically 10)
    uint32_t u32EraseThreshold; //!< Erase threshold
} nand_bch_Parameters_t;

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////////////////////////////////////////////////////
//! \brief ECC parameters override callback function type.
//!
//! This address of this function is set by calling ddi_nand_set_ecc_override_callback().
//! This function must return a pointer to a nand_bch_Parameters_t structure.
//! If the default format is ECC8, the format
//! will be changed to BCH using the passed parameters. If the default format is BCH,
//! the BCH parameters will be modified. This function cannot be used change to ECC
//! encoding from BCH to ECC8. Parameters are not checked for consistency -
//! they must make sense for the actual NAND in use. An application must make this
//! call before calling MediaInit() for it to have any effect.
//!
//! Here is an example:
//! \code
//! #include "drivers/media/nand/gpmi/ddi_nand_ecc_override.h"
//! const nand_bch_Parameters_t bchParameters =
//! {
//!     8,      // u32Block0Level
//!     8,      // u32BlockNLevel
//!     512,    // u32Block0Size
//!     512,    // u32BlockNSize
//!     7,      // u32BlockNCount
//!     10,     // u32MetadataBytes
//!     0       // u32EraseThreshold
//! };
//! const nand_bch_Parameters_t *OverrideEccParameters()
//! {
//!     return &bchParameters;
//! }
//! ddi_nand_set_ecc_override_callback(&OverrideEccParameters);
//! \endcode
//!
//! \return Pointer to parameters structure.
////////////////////////////////////////////////////////////////////////////////
typedef const nand_bch_Parameters_t *(*NandEccOverrideCallback_t)(void);

////////////////////////////////////////////////////////////////////////////////
//! \brief Set ECC parameters override callback function.
//!
//! Set the address of the ECC override function that will be called
//! during media initialization. To be effective, this function must be called
//! before calling MediaInit() in the application.
//!
//! \param[in] pCallback The callback function.
//! \return    None.
////////////////////////////////////////////////////////////////////////////////
void ddi_nand_set_ecc_override_callback(NandEccOverrideCallback_t pCallback);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _DDI_ECC_OVERRIDE_H
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
#endif //STMP378x
