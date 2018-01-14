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
//! \addtogroup ddi_nand_media
//! @{
//! \file Block.h
//! \brief Class to wrap a block of a NAND.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_nand_block_h_)
#define _ddi_nand_block_h_

#include "drivers/media/nand/hal/ddi_nand_hal.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand
{

//! \brief Constant for the first page offset within a block.
const uint32_t kFirstPageInBlock = 0;

/*!
 * \brief Representation of one block of a NAND.
 */
class Block : public BlockAddress
{
public:
    //! \name Init and cleanup
    //@{
    //! \brief Default constructor, inits to block 0.
    Block();
    
    //! \brief
    explicit Block(const BlockAddress & addr);
    
    //! \brief Assignment operator.
    Block & operator = (const Block & other);
    
    //! \brief Assignment operator to change block address.
    Block & operator = (const BlockAddress & addr);

    //! \brief Assignment operator to change block address.
    Block & operator = (const PageAddress & page);
    //@}

    //! \name Addresses
    //@{
    void set(const BlockAddress & addr);

    //! \brief Prefix increment operator to advance the address to the next block.
    Block & operator ++ ();
    
    //! \brief Prefix decrement operator.
    Block & operator -- ();
    
    //! \brief Increment operator.
    Block & operator += (uint32_t amount);
    
    //! \brief Decrement operator.
    Block & operator -= (uint32_t amount);
    //@}
    
    //! \name Accessors
    //@{
    //! \brief Returns the number of pages in this block.
    inline unsigned getPageCount() const { return m_nand->pNANDParams->wPagesPerBlock; }
    
    //! \brief Returns the NAND object owning this block.
    inline NandPhysicalMedia * getNand() const { return m_nand; }
    //@}

    //! \name Operations
    //@{
    //! \brief 
    RtStatus_t readPage(unsigned pageOffset, SECTOR_BUFFER * buffer, SECTOR_BUFFER * auxBuffer, NandEccCorrectionInfo_t * eccInfo=0);

    //! \brief 
    RtStatus_t readMetadata(unsigned pageOffset, SECTOR_BUFFER * buffer, NandEccCorrectionInfo_t * eccInfo=0);

    //! \brief 
    RtStatus_t writePage(unsigned pageOffset, const SECTOR_BUFFER * buffer, SECTOR_BUFFER * auxBuffer);
    
    //! \brief Erase this block.
    RtStatus_t erase();
    
    //! \brief Test whether the block is marked bad.
    bool isMarkedBad(SECTOR_BUFFER * auxBuffer=NULL, RtStatus_t * status=NULL);
    
    //! \brief Erase this block and mark it bad.
    RtStatus_t markBad();
    
    //! \brief Erase the block and mark it bad if the erase fails.
    //!
    //! If the erase fails, then the erase error code will be returned even if marking the block
    //! bad succeeded. This lets the caller know not to use the block.
    RtStatus_t eraseAndMarkOnFailure();
    
    //! \brief Tests whether the block is already erased.
    bool isErased();
    //@}

protected:
    NandPhysicalMedia * m_nand; //!< The physical NAND owning this block.
};

} // namespace nand

#endif // _ddi_nand_block_h_

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
