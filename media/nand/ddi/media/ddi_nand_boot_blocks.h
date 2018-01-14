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
//! \file    ddi_nand_ddi.h
//! \brief   Internal declarations for the NAND media layer.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__ddi_nand_boot_blocks_h__)
#define __ddi_nand_boot_blocks_h__

#include "types.h"
#include "ddi_nand_fingerprints.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

// Forward declarations.
typedef struct _BadBlockTableNand_t BadBlockTableNand_t;
typedef struct _BootBlockStruct_t BootBlockStruct_t;

namespace nand {

//! Value for #BootBlockLocation_t::u when the boot block address is unknown.
#define NAND_BOOT_BLOCK_UNKNOWN (0xffffffff)

//! Define setting the NAND Chip Enable to GPMI_CE1.
#define OTHER_NAND_FOR_SECONDARY_BCBS    (NAND1)

//! \brief Values for the #BootBlockLocation_t::bfBlockProblem field.
enum _nand_boot_block_state
{
    kNandBootBlockValid = 0,            //!< There is no problem with the boot block, and it
                                        //!  contains actual boot-block data.
    kNandBootBlockInvalid = 1,          //!< The boot block is corrupt or erased.
    kNandBootBlockEmpty = 2,            //!< The boot block address is known/chosen/laid-out,
                                        //!  but there is no data written to the boot-block.
    kNandBootBlockUnknown = 3           //!< The state of this boot block is currently unknown.
};

//! \brief Structure to track the location of a boot block.
typedef union _BootBlockLocation {
    struct {
        uint32_t bfBlockAddress : 28;   //!< Chip-relative block address.
        uint32_t bfBlockProblem : 2;    //!< One of the #_nand_boot_block_state values.
        uint32_t bfNANDNumber   : 2;    //!< The number of the chip on which the boot block resides.
    } b;        //!< Bit fields.
    uint32_t u; //!< Combined bits.
    
    //! \brief Returns whether the boot block is valid.
    inline bool isValid() const
    {
        return (b.bfBlockProblem == kNandBootBlockValid);
    }

    //! \brief Compare a BootBlockLocation_t struct with a nand number and block address.
    //!
    //! If the nand number and address match those in the struct, then expression will
    //! have a value of true. In addition, the boot block location must also be valid.
    //!
    //! \param nand Index of NAND chip enable.
    //! \param addr Chip enable relative block address to compare against the boot block location.
    //! \retval true The passed in address matches the boot block's location and the boot block
    //!     is valid.
    //! \retval false The boot block is invalid and/or the provided address does not match.
    inline bool doesAddressMatch(uint32_t nand, uint32_t addr) const
    {
        return (isValid() && (nand) == b.bfNANDNumber && (addr) == b.bfBlockAddress);
    }

} BootBlockLocation_t;

//! \brief Number of pages to skip while searching for boot blocks.
//!
//! The value is always 64, because that is what the 37xx ROM uses.
const uint32_t kBootBlockSearchStride = 64;

/*!
 * \brief Information about the boot blocks and their locations.
 *
 * The DBBT locations in this struct has slightly different usage than the other boot block
 * locations. They point to the beginning of the respective DBBT search area, rather than the
 * actual block containing the DBBT copy. This means that to read a DBBT, you must search
 * for it starting at the location in this struct. In most cases, the location specified here
 * will actually contain the DBBT, but not always. It is even possible for the location here
 * to be a bad block.
 *
 * However, the BootBlockLocation_t::b::bfBlockProblem field of the two DBBT locations is still
 * valid, and indicates whether there is a valid DBBT within the search area. This field is set
 * by Media::findBootControlBlocks().
 */
struct BootBlocks
{
    bool m_isNCBAddressValid; //!< Whether the addresses for NCB1 and NCB2 are valid. Also implies that the NCB exists.
    BootBlockLocation_t m_ncb1;   //!< NAND Control Block Address
    BootBlockLocation_t m_ncb2;   //!< NAND Control Block Address
    BootBlockLocation_t m_ldlb1;   //!< NAND Logical Device Layout Block.
    BootBlockLocation_t m_ldlb2;   //!< NAND Logical Device Layout Block.
    BootBlockLocation_t m_dbbt1;   //!< First Discovered Bad Block Table search area start address.
    BootBlockLocation_t m_dbbt2;   //!< Second Discovered Bad Block Table search area start address.
    BootBlockLocation_t m_primaryFirmware;   //!< Firmware Primary address, used only during allocation.
    BootBlockLocation_t m_secondaryFirmware;   //!< Firmware Secondary address, used only during allocation.
    BootBlockLocation_t m_currentFirmware;   //!< Current address of FW to load.
    
    bool hasValidNCB() const { return m_isNCBAddressValid; }
};

} // namespace nand

#endif // __ddi_nand_boot_blocks_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

