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
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_media
//! @{
//! \file
//! \brief Declarations related to block fingerprints.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__ddi_nand_fingerprints_h__)
#define __ddi_nand_fingerprints_h__

#include "types.h"

///////////////////////////////////////////////////////////////////////////////
// Typedefs
///////////////////////////////////////////////////////////////////////////////

// Forward declaration.
typedef struct _BootBlockStruct_t BootBlockStruct_t;

namespace nand {

//! \brief Nand Fingerprint structure.
//!
//! Fingerprints are used in conjunction with the ECC to determine
//! whether or not a block is valid.  They are strategically placed
//! in both the NCB and LDLB blocks.
typedef struct _FingerPrintValues {
    uint32_t m_u32FingerPrint1;
    uint32_t m_u32FingerPrint2;
    uint32_t m_u32FingerPrint3;
} FingerPrintValues_t;

///////////////////////////////////////////////////////////////////////////////
// Externs
///////////////////////////////////////////////////////////////////////////////

//! \brief NCB Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! first page of the block to indicate this is a NCB.  
extern const FingerPrintValues_t zNCBFingerPrints;

//! \brief LDLB Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! first page of the block to indicate this is a LDLB.  
extern const FingerPrintValues_t zLDLBFingerPrints;

//! \brief DBBT Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! first page of the block to indicate this is a DBBT.  
extern const FingerPrintValues_t zDBBTFingerPrints;

//! \brief BBRC Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! BBRC page to indicate this is a BBRC.  
extern const FingerPrintValues_t zBBRCFingerPrints;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

bool ddi_nand_media_doFingerprintsMatch(BootBlockStruct_t * pBootBlock, const FingerPrintValues_t * pFingerPrintValues);

} // namespace nand

#endif // __ddi_nand_fingerprints_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
