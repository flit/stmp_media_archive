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
//! \file Metadata.h
//! \brief Class to wrap a metadata buffer.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_nand_metadata_h_)
#define _ddi_nand_metadata_h_

#include "types.h"
#include "error.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! \name Tag macros
//!
//! These macros describe various tags we place in pages so we can recognize
//! them as special to us.
//@{

    #define STM_TAG         (('S'<<16)|('T'<<8)|'M')
    #define STMP_TAG        ((STM_TAG<<8)|'P')
    #define BCB_TAG         (('B'<<16)|('C'<<8)|'B')
    #define BCB_SPACE_TAG   ((BCB_TAG<<8)|' ')

//@}

namespace nand
{

/*!
 * \brief Utility class to manage a metadata buffer.
 */
class Metadata
{
public:

    /*!
     * \brief Field layout of the metadata.
     *
     * There are two basic variants for the metadata fields, with several fields common to
     * both. The most common has fields for the logical block address and logical sector
     * index. This is used for all data drive blocks.
     *
     * But system and boot blocks use a different set of fields that replaces of logical
     * addresses with a four-byte tag or signature value. The tag value is written in
     * big endian order, with the LSB appearing in #tag3. This is why the tag is broken into
     * four byte-wide fields.
     *
     * That value of the flags field is inverted from normal usage. That is, a flag is set
     * if the bit is 0 and are cleared if the bit is 1. It is done this way because the
     * default NAND bit value for an erased page is a 1.
     */
    struct Fields
    {
        uint8_t blockStatus;    //!< Non-0xff value means the block is bad.
        uint8_t blockNumber;    //!< Logical block number used for system drives.
        union {
            struct {
                uint16_t lba0;   //!< Halfword 0 of the logical block address.
                uint16_t lsi;   //!< The logical sector index.
            };
            struct {
                uint8_t tag0;   //!< Byte 0 of the tag, MSB of the tag word.
                uint8_t tag1;   //!< Byte 1 of the tag.
                uint8_t tag2;   //!< Byte 2 of the tag.
                uint8_t tag3;   //!< Byte 3 of the tag, LSB of the tag word.
            };
        };
        uint16_t lba1;   //!< Halfword 1 of the logical block address.
        uint8_t flags;  //!< Flags fields.
        uint8_t reserved;   //!< Current unused.
    };
    
    //! \brief NAND metadata flag bitmasks.
    enum
    {
        //! \brief When set, this flag indicates that the block belongs to a hidden drive.
        kIsHiddenBlockFlag = 1,
        
        //! \brief Set to indicate that all pages in the block are sorted logically.
        //!
        //! This flag is set on the last page in a block only when every page in that block is
        //! written in ascending logical order and there are no duplicate logical pages. So
        //! physical page 0 of the block contains logical page 0 (of the range of logical pages
        //! that fit into that block, not necessarily logical page 0 of the entire drive),
        //! physical page 1 contains logical page 1, and so on.
        kIsInLogicalOrderFlag = 2    
    };
    
    //! \name Construction.
    //@{
    //! \brief Default constructor.
    Metadata() : m_fields(NULL) {}
    
    //! \brief Constructor taking the metadata buffer pointer.
    Metadata(SECTOR_BUFFER * buffer) : m_fields((Fields *)buffer) {}
    
    //! \brief Assignment operator.
    Metadata & operator = (const Metadata & other) { m_fields = other.m_fields; return *this; }
    //@}
    
    //! \name Accessors
    //@{
    //! \brief Returns the metadata buffer.
    inline SECTOR_BUFFER * getBuffer() { return (SECTOR_BUFFER *)m_fields; }
    
    //! \brief Changes the metadata buffer.
    inline void setBuffer(SECTOR_BUFFER * buffer) { m_fields = (Fields *)buffer; }
    //@}
    
    //! \name Field readers
    //@{
    //! \brief Get the logical block address.
    uint32_t getLba() const;
    
    //! \brief Get the logical sector index.
    uint16_t getLsi() const;
    
    //! \brief Get the erase block number.
    uint8_t getBlockNumber() const;
    
    //! \brief Get the four byte signature.
    uint32_t getSignature() const;
    
    //! \brief Returns true if the flag is set.
    bool isFlagSet(uint8_t flagMask) const;
    
    //! \brief Returns true if the block status field is non-ff.
    bool isMarkedBad() const;
    
    //! \brief Returns true if the metadata is all ff.
    bool isErased() const;
    //@}
    
    //! \name Field writers
    //@{
    //! \brief Sets the logical block address field.
    void setLba(uint32_t lba);
    
    //! \brief Sets the logical sector index field.
    void setLsi(uint16_t lsi);
    
    //! \brief Sets the erase block number.
    void setBlockNumber(uint8_t blockNumber);
    
    //! \brief Sets the signature field.
    void setSignature(uint32_t signature);
    
    //! \brief Sets a flag bit.
    void setFlag(uint8_t flagMask);
    
    //! \brief Clears a flag bit.
    void clearFlag(uint8_t flagMask);
    
    //! \brief Sets the block status byte to 0.
    void markBad();
    //@}
    
    //! \name Operations
    //@{
    //! \brief Set all bytes to 0xff.
    void erase();
    
    //! \brief Fill in metadata with an LBA and LSI.
    void prepare(uint32_t lba, uint32_t lsi);
    
    //! \brief Fill in metadata with a signature.
    void prepare(uint32_t signature);
    //@}

protected:
    Fields * m_fields;  //!< Pointer to the metadata.
};

} // namespace nand

#endif // _ddi_nand_metadata_h_

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
