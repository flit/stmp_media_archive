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
//! \addtogroup ddi_media_nand_hal
//! @{
//! \file   ddi_nand_hal.h
//! \brief  Declarations for users of the NAND HAL.
//!
//! This file embodies the NAND HAL interface. Files that use the NAND HAL
//! \em must include this file, and must \em not include any other.
//!
//! If you're including another NAND HAL header file besides this one, you have
//! almost certainly made a mistake.
//!
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDI_NAND_HAL_H
#define _DDI_NAND_HAL_H

#include "types.h"
#include "drivers/media/ddi_media_errordefs.h"
#include "drivers/media/sectordef.h"
#include "drivers/media/nand/gpmi/ddi_nand_gpmi.h"
#include "drivers/media/nand/gpmi/ddi_nand_ecc.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! Flags to use when acquiring a buffer for the NAND driver through the media buffer manager.
const unsigned kNandBufferFlags = kMediaBufferFlag_None;

//! \brief Abstract Status Bit Constants
//!
//! All NAND chips understand a "Read Status" command of some kind, but the
//! status information they return varies from model to model.
//!
//! The following bit field definitions embody an abstract status field that
//! contains only the bits our software needs and understands. The
//! type-specific API status functions (see \c NandPhysicalMedia::checkStatus())
//! all convert the values they get from hardware to these bit fields. This
//! isolates higher layers from device-dependent details.
//!
enum _nand_hal_status_masks
{
    kNandStatusPassMask = 0x000001, //!< Set when a write or erase operation has succeeded.
    kNandStatusTrueReadyMask = 0x000020,    //!< Reflects the ready/busy state of the NAND.
    kNandStatusCacheReadyMask = 0x000040,   //!< The cache is ready to be used.
    kNandStatusCachePreviousPassMask = 0x000100,    //!< The recent write operation succeeded.
    kNandStatusReadDisturbanceMask = 0x001000   //!< The NAND indicates that the block needs to be rewritten due to the level of bit errors.
};

//! \brief Freescale NAND Type Constants
//!
//! Freescale classifies NAND hardware according to their behaviors and how we
//! control them. Each type represents a set of NAND models that have equivalent
//! behavior for the purposes of our software. See
//! \c ddi_nand_hal_tables.cpp for the definitive assignment of
//! hardware models to Freescale NAND types.
typedef enum _nand_hal_nand_type {
    kNandTypeUnknown = 0,   //!< \warning NandHalInit uses  zero to indicate
                            //! that initialization is needed. Therefore, do
                            //! not use a ZERO in this typedef.
    kNandType1 = 1, //!< \em Deprecated.
    kNandType2 = 2, //!< SLC, 2K page, 64 page block
    kNandType4 = 4, //!< \em Deprecated.
    kNandType5 = 5, //!< Toshiba/Sandisk Large Page MLC, 2K page, 128 page block
    kNandType6 = 6, //!< Samsung Large Page MLC, 2K page, 128 page block
    kNandType7 = 7, //!< Samsung, Micron, and Intel SLC, 2K page, 128 page block
    kNandType8 = 8, //!< Samsung MLC, 4K+128 page, 128 page block
    kNandType9 = 9, //!< Toshiba MLC, 4K+218 page, 128 page block
    kNandType10 = 10, //!< Samsung SLC, 4K+128 page, 128 page block
    kNandType11 = 11, //!< Toshiba MLC, 8K+376 page, 128 page per block, BCH14
    kNandType12 = 12, //!< Hynix MLC, 4K page, 128 page per block, BCH12
    kNandType13 = 13, //!< Micron MLC, 4K+218 page, 128 page per block, BCH12
    kNandType14 = 14, //!< Micron MLC, 4K+224 page, 256 page per block, BCH12
    kNandType15 = 15, //!< Samsung MLC, 8K+436 page, 128 page per block, BCH16
    kNandType16 = 16, //!< Toshiba PBA-NAND, 8K+32 page, 128 pages per block, built-in ECC
    kNandType17 = 17,  //!< Micron MLC, 4K+224 page, 256 pages per block, BCH16
    kNandType18 = 18  //!< Micron MLC, 8K+448 page, 256 pages per block, BCH16
} NandType_t;

//! \brief Possible cell types for a NAND.
//!
//! The cell type of the NAND determines how many bits are encoded per cell.
//! A single-level cell (SLC) encodes one bit per cell, where the cell voltage
//! swings between Vcc and Vss/GND. A multi-level cell encodes at least two bits
//! per cell by using multiple voltage levels between Vcc and Vss. So an MLC
//! that encodes two bits has four voltage levels.
typedef enum _nand_hal_cell_type {
    kNandSLC,   //!< Single-level cell.
    kNandMLC    //!< Multi-level cell.
} NandCellType_t;

/*!
 * \brief Describes the underlying NAND hardware. 
 * 
 * This structure describes attributes of the NANDs that are shared by all chip selects. 
 * Although instances of NandPhysicalMedia point to a copy of this structure, there is 
 * in fact only one NAND parameters struct for all chip selects. 
 * 
 * The basic unit of data transfer for the NAND HAL API is the "page." 
 * The NAND HAL decides how large a page will be at initialization time 
 * based on the determined device type. Each page consists of both a data portion 
 * and a metadata, or redundant area, portion. The metadata portion holds a few 
 * bytes of metadata about the page and/or block plus parity bytes for ECC. 
 * 
 * Due to limitations of the STMP boot ROM, some NANDs will store less than the full amount 
 * of data in pages read by the ROM. The firmware page parameters in this structure describe 
 * the size of the firmware pages, i.e., those pages read by the ROM. For many NANDs, these 
 * will be the same as regular page sizes. 
 * 
 * Some of the parameters in this structure refer to planes. Note that some manufacturers, 
 * notably Toshiba, refer to planes as "districts". Other than name, there is no difference. 
 */
typedef struct _nand_parameters {

    //! \name Read ID
    //@{
        uint8_t manufacturerCode;   //!< Manufacturer code from read ID command results.
        uint8_t deviceCode;         //!< Device code value from read ID commands results.
    //@}
    
    //! \name Type information
    //@{
        NandType_t NandType;        //!< The Freescale type for the underlying NAND hardware.
        NandCellType_t cellType;         //!< Cell type for this NAND.
    //@}

    //! \name ECC information
    //@{
        NandEccDescriptor_t eccDescriptor;        //!< The ECC Descriptor.
    //@}
    
    //! \name Bad blocks
    //@{
    
        unsigned maxBadBlockPercentage; //!< Maximum percent of blocks that can go bad during the NAND's lifetime.
    
    //@}

    //! \name Page Parameters
    //@{
        uint32_t pageTotalSize;      //!< The total page size, both data and metadata.
        uint32_t pageDataSize;       //!< The size of a page's data area.
        uint32_t pageMetadataSize;    //!< The size of a page's redundant area.
        uint32_t firmwarePageTotalSize; //!< Size of a firmware page, which may be different than data pages.
        uint32_t firmwarePageDataSize;  //!< Length of the data area of a firmware page.
        uint32_t firmwarePageMetadataSize;  //!< Number of metadata bytes in a firmware page.
    //@}

    //! \name Block Parameters
    //@{
        uint32_t pageToBlockShift; //!< \brief Shift a sector number this many bits to the right to get the number of the containing block.
        uint32_t pageInBlockMask; //!< \brief Use this mask on a sector number to get the number of the sector within the containing block.
        uint32_t wPagesPerBlock; //!< The number of pages in a block.
    //@}

    //! \name Device Addressing Parameters
    //@{
        uint32_t wNumColumnBytes;       //!< The number of bytes in a column address.
        uint32_t wNumRowBytes;          //!< The number of bytes in a row address.
    //@}
    
    //! \name Plane parameters
    //@{
        uint32_t planesPerDie;          //!< Number of planes.
    //@}
    
    //! \name Flags
    //@{
        //! \brief Whether the NAND follows the ONFI specification
        uint32_t isONFI:1;
    
        //! \brief Whether bad blocks must be converted to SGTL format.
        //!
        //! Due to the way the ECC engines work, where they insert parity bytes after every
        //! 512 bytes (or so) of data, the factory bad block marker position is overwritten
        //! with a valid data byte. This makes it impossible to tell factory marked bad blocks
        //! from valid data blocks. As a result, we have to convert factory marked bad blocks
        //! to have the bad block mark in the location where the ECC engine puts the first
        //! metadata byte.
        uint32_t requiresBadBlockConversion:1;
    
        //! \brief Whether to use smaller pages to hold firmware read by the ROM.
        //!
        //! The boot ROM has some limitations on its NAND support and ability to read pages. It
        //! only has a 2K buffer in RAM, so it has to be able to read one 2K section at a time of
        //! pages larger than 2K. There are cases where this is not possible, and pages read by the
        //! ROM must contain data in only the first 2K subpage. There are also other similar
        //! cases where firmware pages must be smaller than the full page size.
        uint32_t hasSmallFirmwarePages:1;
        
        //! \brief Whether the NAND performs ECC management on its own.
        //!
        //! Normal raw NANDs simply provide enough bytes per page to allow the host controller
        //! to store ECC parity bytes. But so-called "ECC free" NANDs have an internal ECC
        //! engine and hide the parity bytes from the host.
        uint32_t hasInternalECCEngine:1;
        
        //! \brief Whether commands can be issues to different dice simultaneously.
        //!
        //! This is for interleaving between dice within a single chip select, not between dice
        //! on different chip selects (which should normally be supported).
        uint32_t supportsDieInterleaving:1;
        
        //! \brief Whether multi-plane read operations are supported.
        uint32_t supportsMultiplaneRead:1;
        
        //! \brief Whether the NAND provides multi-plane write operations.
        uint32_t supportsMultiplaneWrite:1;
        
        //! \brief Whether multi-plane erase operations are supported.
        uint32_t supportsMultiplaneErase:1;
        
        //! \brief Whether the NAND allows read cache commands.
        uint32_t supportsCacheRead:1;
        
        //! \briefWhether the NAND allows write cache commands.
        uint32_t supportsCacheWrite:1;
        
        //! \brief Whether the NAND can use read cache commands with plane interleaving.
        uint32_t supportsMultiplaneCacheRead:1;
        
        //! \brief Whether the NAND can use write cache commands with plane interleaving.
        uint32_t supportsMultiplaneCacheWrite:1;
        
        //! \brief Whether copyback commands are supported.
        uint32_t supportsCopyback:1;
        
        //! \brief Whether multi-plane copyback is supported.
        uint32_t supportsMultiplaneCopyback:1;
        
        //! Unassigned flags bits.
        uint32_t _reservedFlags:18;
    //@}
} NandParameters_t;

// Forward declaration.
class NandCopyPagesFilter;

/*!
 * \brief Abstract class representing a single NAND device or chip select. 
 * 
 * This is the root of a per-chip collection of data structures that describe 
 * the underlying NAND hardware and provide function pointers for fundamental 
 * operations. It contains virtual methods to perform all commands supported 
 * by the HAL library. The methods are implemented in NAND type-specific 
 * subclasses, thereby providing a common interface to many device types. 
 * 
 * While the form of these data structures suggests that each NAND chip could 
 * be different and independently controlled, this is <i>not</i> the case. In 
 * fact, each NAND chip must be exactly the same. 
 */
class NandPhysicalMedia
{
public:

    //! \brief Parameters common to all of the underlying NAND chips.
    NandParameters_t * pNANDParams;

    //! \name Chip properties
    //@{
        uint32_t wChipNumber;       //!< The number of the chip select to which this structure applies.
        uint32_t totalPages;        //!< The number of pages in this chip.
        uint32_t wTotalBlocks;       //!< The number of blocks in this chip.
        uint32_t wTotalInternalDice; //!< The number of die in this chip.
        uint32_t wBlocksPerDie;      //!< The number of blocks in a die.
        uint32_t m_firstAbsoluteBlock;   //!< First absolute block of this chip.
        uint32_t m_firstAbsolutePage;    //!< First absolute page of this chip.
    //@}
    
    //! \name Address conversion
    //@{
        //! \brief Convert a block number to a page number.
        //! \param block A block number.
        //! \return The page address for the first page of \a block.
        inline uint32_t blockToPage(uint32_t block) { return block << pNANDParams->pageToBlockShift; }
        
        //! \brief Convert a block number and relative page index to a page number.
        //! \param block A block number.
        //! \param offset The relative page offset within \a block. Must not be greater than
        //!     the number of pages per block.
        //! \return The page address for page \a index within \a block.
        inline uint32_t blockAndOffsetToPage(uint32_t block, uint32_t offset) { return (block << pNANDParams->pageToBlockShift) + offset; }
        
        //! \brief Convert an absolute block number and page offset to a chip-relative page number.
        //! \param block An absolute block number, although a relative block will also work.
        //! \param offset The offset of the apge within \a block. Must not be greater than
        //!     the number of pages per block.
        //! \return The page address for page \a index within \a block, relative to this chip.
        inline uint32_t blockAndOffsetToRelativePage(uint32_t block, uint32_t offset) { return (blockToRelative(block) << pNANDParams->pageToBlockShift) + offset; }
        
        //! \brief Convert a page address to a block number.
        //! \param page The page address to convert.
        //! \return The block number containing \a page.
        inline uint32_t pageToBlock(uint32_t page) { return page >> pNANDParams->pageToBlockShift; }
        
        //! \brief Convert a page address to a block number plus relative page index.
        //! \param page The page address.
        //! \param[out] block Contains the block number holding \a page.  When the range of \page
        //!                 is [0..pages-in-this-NAND), the range of \a block
        //!                 is [0..blocks-in-this-NAND).
        //!                 However, since all NANDs present are the same type and same size, this
        //!                 function works equally well with the ranges as
        //!                 [0..pages-in-all-NANDs) and [0..blocks-in-all-NANDs), respectively.
        //! \param[out] offset The relative page number within \a block for \a page.
        inline void pageToBlockAndOffset(uint32_t page, uint32_t * block, uint32_t * offset)
        {
            *block = page >> pNANDParams->pageToBlockShift;
            *offset = page & pNANDParams->pageInBlockMask;
        }
        
        //! \brief Make a block address relative to this chip.
        //! \param block Absolute block address.
        //! \return The relative block address.
        inline uint32_t blockToRelative(uint32_t block) { return block & (wTotalBlocks - 1); }
        
        //! \brief Make a page address relative to this chip.
        //! \param page Absolute page address.
        //! \return The \a page address relative to this chip.
        inline uint32_t pageToRelative(uint32_t page) { return page & ((wTotalBlocks << pNANDParams->pageToBlockShift) - 1); }
    
        //! \brief Get the absolute address of the first block of this chip.
        inline uint32_t baseAbsoluteBlock() { return m_firstAbsoluteBlock; }
        
        //! \brief Get the absolute address of the first page of this chip.
        inline uint32_t baseAbsolutePage() { return m_firstAbsolutePage; }
        
        //! \brief Get the die number for a relative block address.
        uint32_t relativeBlockToDie(uint32_t block) { return block / wBlocksPerDie; }
    //@}

    //! \name Basic operations
    //@{
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Reset the NAND.
    //!
    //! \retval SUCCESS The NAND was reset successfully.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t reset() = 0;
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Send the Read ID command to the NAND and return the results.
    //! \param pReadIDCode Points to a buffer of at least 6 bytes where the results
    //!     of the Read ID command are stored.
    //! \retval SUCCESS The NAND replied successfully and pReadIDCode contents
    //!     are valid.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t readID(uint8_t * pReadIDCode) = 0;
    
    //@}

    //! \name Reading
    //@{
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Read data from a page without correcting ECC.
    //!
    //! This is the common function used to read any number of bytes from any
    //! location on the NAND page. ECC correction is disabled. This allows the
    //! caller to read parts of the page that are normally inaccessible due to
    //! the way the ECC engine works.
    //!
    //! \param[in]  wSectorNum Sector number to read.
    //! \param[in] columnOffset Offset of first byte to read from the page.
    //! \param[in] readByteCount Number of bytes to read, starting from \a columnOffset.
    //! \param[in]  pBuf Pointer to buffer to fill with result.
    //!
    //! \retval SUCCESS
    //! \retval ERROR_DDI_NAND_DMA_TIMEOUT          The operation failed because
    //!                                             the DMA timed out.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t readRawData(uint32_t wSectorNum, uint32_t columnOffset, uint32_t readByteCount, SECTOR_BUFFER * pBuf) = 0;
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Read a page from the NAND, including both the data and redundant area.
    //!
    //! ECC will be used to correct bit errors, if any are present. Information about
    //! the corrected errors can be obtained through the \a pECC parameter. Pass a
    //! pointer to a #NandEccCorrectionInfo_t structure through \a pECC, and the
    //! structure will be filled in with bit error counts for each ECC payload
    //! present in the page..
    //!
    //! \param[in]  uSectorNumber       The number of the sector relative to the
    //!                                 containing chip.
    //! \param[out] pBuffer             A pointer to the receiving buffer.
    //! \param[out] pAuxiliary          A pointer to the auxiliary buffer. This
    //!                                 buffer provides a "scratch area" for the
    //!                                 hardware that will contain no useful
    //!                                 information after this function returns.
    //! \param[out] pECC                A pointer to a structure that will receive
    //!                                 ECC information about the read operation. Optional.
    //!
    //! \retval SUCCESS                             The operation succeeded.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIXED        The operation succeeded, but
    //!                                             ECC fixed some errors.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED   The operation failed because
    //!                                             ECC failed.
    //! \retval ERROR_DDI_NAND_DMA_TIMEOUT          The operation failed because
    //!                                             the DMA timed out.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t readPage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC) = 0;

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Read only the redundant area of a sector.
    //!
    //! \param[in]  uSectorNumber       The number of the sector relative to the
    //!                                 containing chip.
    //! \param[out] pBuffer             A pointer to the receiving buffer.
    //! \param[out] pECC                A pointer to a structure that will receive
    //!                                 ECC information about the read operation.
    //!
    //! \retval SUCCESS                             The operation succeeded.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIXED        The operation succeeded, but
    //!                                             ECC fixed some errors.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED   The operation failed because
    //!                                             ECC failed.
    //! \retval ERROR_DDI_NAND_DMA_TIMEOUT          The operation failed because
    //!                                             the DMA timed out.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t readMetadata(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC) = 0;

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Read a page using an arbitrary ECC descriptor.
    //!
    //! \warning This is a special API that should only be used in circumstances
    //!     where you know exactly what you are doing.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t readPageWithEcc(const NandEccDescriptor_t * ecc, uint32_t pageNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC) = 0;
    
    //@}
    
    //! \name Multiplane operations
    //@{
    
    /*!
     * \brief Information about one plane of a multiplane operation.
     *
     * This struct can be used for either page or block level operations.
     */
    struct MultiplaneParamBlock
    {
        uint32_t m_address;       //!< Address of the page relative to this chip select.
        SECTOR_BUFFER * m_buffer; //!< Not used for metadata reads.
        SECTOR_BUFFER * m_auxiliaryBuffer;
        NandEccCorrectionInfo_t * m_eccInfo;  //!< Only used for reads.
        RtStatus_t m_resultStatus;            //!< Result status for this page.
    };
    
    virtual RtStatus_t readMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount) = 0;
    virtual RtStatus_t readMultipleMetadata(MultiplaneParamBlock * pages, unsigned pageCount) = 0;
    virtual RtStatus_t writeMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount) = 0;
    virtual RtStatus_t eraseMultipleBlocks(MultiplaneParamBlock * blocks, unsigned blockCount) = 0;
    
    //@}
    
    //! \name Writing
    //@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Write data to a page without inserting ECC parity information.
    //!
    //! \param[in] pageNumber The number of the page relative to the containing chip.
    //! \param[in] columnOffset Offset of first byte to write to the page.
    //! \param[in] writeByteCount Number of bytes to write, starting from \a columnOffset.
    //! \param[out] data A pointer to the buffer to write.
    //!
    //! \retval SUCCESS The operation succeeded.
    //! \retval ERROR_DDI_NAND_HAL_WRITE_FAILED The operation failed.
    //! \retval ERROR_DDI_NAND_DMA_TIMEOUT          The operation failed because
    //!                                             the DMA timed out.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t writeRawData(uint32_t pageNumber, uint32_t columnOffset, uint32_t writeByteCount, const SECTOR_BUFFER * data) = 0;

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Write one page, including both the data and redundant area.
    //!
    //! \param[in]  uSectorNum       The number of the sector relative to the
    //!                                 containing chip.
    //! \param[out] pBuffer             A pointer to the buffer to write.
    //! \param[out] pAuxiliary          A pointer to the auxiliary buffer. This
    //!                                 buffer provides a "scratch area" for the
    //!                                 hardware that will contain no useful
    //!                                 information after this function returns.
    //!
    //! \retval SUCCESS The operation succeeded.
    //! \retval ERROR_DDI_NAND_HAL_WRITE_FAILED The operation failed.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t writePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary) = 0;
    
    //@}
    
    //! \name Firmware pages
    //@{
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Write one page in the format that the boot ROM can read.
    //!
    //! For most NAND types, this function will do exactly the same thing as
    //! writePage(). However, some NAND types require a page layout that is not
    //! entirely compatible with the boot ROM.
    //!
    //! \param[in]  uSectorNum       The number of the sector relative to the
    //!                                 containing chip.
    //! \param[out] pBuffer             A pointer to the buffer to write.
    //! \param[out] pAuxiliary          A pointer to the auxiliary buffer. This
    //!                                 buffer provides a "scratch area" for the
    //!                                 hardware that will contain no useful
    //!                                 information after this function returns.
    //!
    //! \retval SUCCESS The operation succeeded.
    //! \retval ERROR_DDI_NAND_HAL_WRITE_FAILED The operation failed.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t writeFirmwarePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary) = 0;
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Read a page from the NAND in the format required by the boot ROM.
    //!
    //! Most NAND types will simply call readPage() for this method. But a few
    //! types have special firmware page layouts that are required for
    //! compatibility with the boot ROM.
    //!
    //! \param[in]  uSectorNumber       The number of the sector relative to the
    //!                                 containing chip.
    //! \param[out] pBuffer             A pointer to the receiving buffer.
    //! \param[out] pAuxiliary          A pointer to the auxiliary buffer. This
    //!                                 buffer provides a "scratch area" for the
    //!                                 hardware that will contain no useful
    //!                                 information after this function returns.
    //! \param[out] pECC                A pointer to a structure that will receive
    //!                                 ECC information about the read operation. Optional.
    //!
    //! \retval SUCCESS                             The operation succeeded.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIXED        The operation succeeded, but
    //!                                             ECC fixed some errors.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED   The operation failed because
    //!                                             ECC failed.
    //! \retval ERROR_DDI_NAND_DMA_TIMEOUT          The operation failed because
    //!                                             the DMA timed out.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t readFirmwarePage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC) = 0;
    
    //@}
    
    //! \name Other
    //@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Erase a block synchronously.
    //!
    //! This function won't return until the block is erased.
    //!
    //! \param[in]  uBlockNumber        The number of the block relative to the
    //!                                 containing chip.
    //!
    //! \retval SUCCESS The operation succeeded.
    //! \retval ERROR_DDI_NAND_HAL_WRITE_FAILED The operation failed.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t eraseBlock(uint32_t uBlockNumber) = 0;

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Copy pages from one part of the NAND to another.
    //!
    //! This function will work even if the source and target sectors are on
    //! different dice or even chip enables. However, some NAND types may have support
    //! for accelerated page copies within the same die.
    //!
    //! This function stops copying if there is an unrecoverable error, such as a
    //! catastrophic ECC error. Benign ECC error codes are converted to SUCCESS
    //! before being returned, since the pages are being rewritten anyway, which
    //! will refresh the data. If an unrecoverable error does occur, you can use the
    //! \a successfulPages parameter to see how many pages were copied successfully,
    //! and to determine which page failed.
    //!
    //! \param[in] targetNand Nand object that contains the target pages.
    //! \param[in] uSourceStartSectorNum The number of the first source page
    //!     relative to the containing chip.
    //! \param[in] uTargetStartSectorNum The number of the first target page
    //!     relative to the containing chip.
    //! \param[in] wNumSectors    The number of consecutive pages to copy.
    //! \param[in] sectorBuffer Page-sized buffer to use for the copy.
    //! \param[in] auxBuffer Auxiliary buffer to use for the copy.
    //! \param[in] filter Optional filter object used to check and modify data
    //!     during the page copies.
    //! \param[out] successfulPages On exit this contains the number of pages that
    //!     were copied successfully. If the return code is #SUCCESS then this will
    //!     always be equal to \a wNumSectors. If an error is returned, then the
    //!     page that failed is the next one after this value. This parameter is optional
    //!     and may be set to NULL.
    //!
    //! \retval SUCCESS The copy operation for all pages succeeded.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED A source page had uncorrectable ECC errors.
    //! \retval ERROR_DDI_NAND_HAL_WRITE_FAILED The write operation for a target page failed.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t copyPages(NandPhysicalMedia * targetNand, uint32_t uSourceStartSectorNum, uint32_t uTargetStartSectorNum, uint32_t wNumSectors, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, NandCopyPagesFilter * filter, uint32_t * successfulPages) = 0;

    //@}
    
    //! \name Bad blocks
    //@{
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Checks if a block is marked bad.
    //!
    //! This method will check if a block is bad by reading the metadata of
    //! multiple pages in the block. A block is marked bad if any of the datasheet
    //! specified pages have the zero'th byte in the redundant area contain a
    //! non-FF value. Because different NAND vendors use different sets of pages
    //! to mark a block bad, this function scans the union of all pages
    //! specified by the vendors. If any of those pages are marked bad, then the
    //! entire block is bad. Note that the actual techniques used to identify bad
    //! blocks may vary between the different supported NAND types.
    //!
    //! A factory-fresh block is marked bad if any of the datasheet-specified pages
    //! have the zero'th byte in the redundant area contain a non-FF value.
    //! An STMP37xx-formatted block is marked bad if any of datasheet-specified
    //! pages have a non-FF value in a particular byte of the redundant area.
    //! 
    //! Finally, if \em none of the required pages can be read successfully, then the
    //! block is declared bad. This is nominally to guard against faulty blocks that
    //! have uncorrectable ECC errors on all pages, even when new.
    //!
    //! \param[in] blockAddress Relative address of block to test.
    //! \param[in] checkFactoryMarkings When this flag is true, the factory bad block
    //!     marking position is checked instead of the marking position used by
    //!     the 37xx SDK.
    //! \param[out] readStatus Optional. If there was an error reading one
    //!     of the pages of the block, it will be returned through this parameter.
    //!
    //! \retval true The block is bad, or none of the pages could be read successfully.
    //! \retval false The block is good.
    ////////////////////////////////////////////////////////////////////////////////
    virtual bool isBlockBad(uint32_t blockAddress, SECTOR_BUFFER * auxBuffer, bool checkFactoryMarkings=false, RtStatus_t * readStatus=NULL) = 0;
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Mark a block bad.
    //!
    //! Marks a block bad by setting all bytes of every single page within the block to zeroes.
    //! The only byte within each page that is actually required to be non-FFh in order to
    //! mark a block as bad is the first byte of the metadata, i.e. the first byte to follow
    //! the page's data content. However, the pages within factory-marked bad blocks of modern
    //! NANDs always read as all zeroes. So we're mimmicking that behaviour with this function.
    //! Another benefit of doing it this way is that the block will appear bad whether you
    //! look at the factory bad block marker or the SigmaTel bad block marker, because both
    //! bytes are set to 0.
    //!
    //! \param blockAddress Relative address of block to mark bad.
    //! \param pageBuffer Buffer that must be the full size of the NAND page.
    //! \param auxBuffer Auxiliary buffer used to verify whether the marking took hold.
    //!
    //! \retval SUCCESS The block was marked successfully.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t markBlockBad(uint32_t blockAddress, SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer) = 0;
    
    //@}
    
    //! \name Sleep
    //@{
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Controls whether sleep is allowed.
    //!
    //! For many NANDs, this method may do nothing at all, as sleep is an entirely
    //! automatic process controlled by the chip enable signal.
    //!
    //! \note This method really should sit above the individual chip level and
    //!     control all chips at the same time. However, we also need the NAND
    //!     types to be able to override this method.
    ////////////////////////////////////////////////////////////////////////////////
    virtual RtStatus_t enableSleep(bool isEnabled) = 0;
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Returns the current state of sleep mode support.
    ////////////////////////////////////////////////////////////////////////////////
    virtual bool isSleepEnabled() = 0;
    
    //@}
    
    //! \name Device properties
    //@{
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Returns the device part number as a string.
    //!
    //! \return A newly allocated string containing the NAND's part number, if
    //!     available. Currently only ONFI NANDs support this feature. All other
    //!     NAND types will return a NULL from this method.
    ////////////////////////////////////////////////////////////////////////////////
    virtual char * getDeviceName() = 0;
    
    //@}

};

/*!
 * \brief Abstract class for filtering page contents during a copy operation.
 */
class NandCopyPagesFilter
{
public:
    //! \brief Filter method.
    //!
    //! This pure virtual function will be called for each page that is copied using the
    //! NandPhysicalMedia::copyPages() API call. It can examine the page contents and modify it
    //! as necessary.
    //!
    //! \param fromNand NAND object for the source page.
    //! \param toNand NAND object for the destination page. May be the same as #fromNand.
    //! \param fromPage Relative address of the source page.
    //! \param toPage Relative address of the destination page.
    //! \param sectorBuffer Buffer containing the page data.
    //! \param auxBuffer Buffer holding the page's metadata.
    //! \param[out] didModifyPage The filter method should set this parameter to true if it has
    //!     modified the page in any way. This will let the HAL know that it cannot use copyback
    //!     commands to write the destination page.
    virtual RtStatus_t filter(NandPhysicalMedia * fromNand, NandPhysicalMedia * toNand, uint32_t fromPage, uint32_t toPage, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, bool * didModifyPage) = 0;
};

/*!
 * \brief Static interface to NAND HAL.
 */
class NandHal
{
public:

    //! \name Init and shutdown
    //@{
    
        //! \brief Initialize the entire HAL and identify connected NAND devices.
        static RtStatus_t init();

        //! \brief Shutdown the HAL, preventing further access to the NANDs.
        static RtStatus_t shutdown();
    
    //@}
    
    //! \name Chip selects
    //@{

        //! \brief Return the number of active chip selects.
        static unsigned getChipSelectCount();
        
        //! \brief Return the chip select number given an absolute block address.
        static unsigned getChipSelectForAbsoluteBlock(uint32_t block);
        
        //! \brief Return the chip select number given an absolute page address.
        static unsigned getChipSelectForAbsolutePage(uint32_t page);
        
        //! \brief Returns the combined number of blocks of all chip selects.
        static uint32_t getTotalBlockCount();
        
        //! \brief Determine whether a block address is valid.
        static bool isAbsoluteBlockValid(uint32_t block);
        
        //! \brief Determine whether a page address is valid.
        static bool isAbsolutePageValid(uint32_t page);
    
    //@}
    
    //! \name Nand objects
    //@{
        
        //! \brief Returns the first NAND object.
        static inline NandPhysicalMedia * getFirstNand() { return getNand(0); }
    
        //! \brief Return the NAND object for a given chip select.
        static NandPhysicalMedia * getNand(unsigned chipSelect);
        
        //! \brief Return the NAND object for a given absolute block address.
        static NandPhysicalMedia * getNandForAbsoluteBlock(uint32_t block);
        
        //! \brief Return the NAND object for a given absolute page address.
        static NandPhysicalMedia * getNandForAbsolutePage(uint32_t page);
    
    //@}
    
    //! \name Shared parameters
    //@{
    
        //! \brief Access the shared parameters object.
        static NandParameters_t & getParameters();
    
    //@}

    /*!
     * \brief Helper class to temporarily adjust sleep enablement.
     */
    class SleepHelper
    {
    public:
        //! \brief Constructor. Saves previous sleep state and changes to new.
        SleepHelper(bool isEnabled)
        {
            NandPhysicalMedia * nand = getNand(0);
            m_wasEnabled = nand->isSleepEnabled();
            nand->enableSleep(isEnabled);
        }
        
        //! \brief Destructor. Restores sleep state to the previous value.
        ~SleepHelper()
        {
            getNand(0)->enableSleep(m_wasEnabled);
        }
        
    protected:
        bool m_wasEnabled;  //!< Sleep state when this object was constructed.
    };

};

namespace nand {

class PageAddress;

#pragma ghs section text=".static.text"

/*!
 * \brief Helper class to represent block addresses.
 */
class BlockAddress
{
public:

    inline BlockAddress() : m_address(0) {}
    inline BlockAddress(uint32_t absoluteBlock) : m_address(absoluteBlock) {}
    inline BlockAddress(uint32_t nand, uint32_t relativeBlock) { m_address = NandHal::getNand(nand)->baseAbsoluteBlock() + relativeBlock; }
    explicit inline BlockAddress(const PageAddress & page) { set(page); }
    inline BlockAddress(const BlockAddress & other) : m_address(other.m_address) {}
    
    inline BlockAddress & operator = (const BlockAddress & other)
    {
        set(other);
        return *this;
    }
    
    //! \brief Returns the absolute block address.
    inline uint32_t get() const { return m_address; }
    
    //! \brief Change the address.
    inline void set(const BlockAddress & addr) { m_address = addr.m_address; }
    
    //! \brief Change the address.
    inline void set(const PageAddress & addr);
    
    //! \brief Returns the block as a page.
    inline PageAddress getPage() const;
    
    //! \brief Returns the absolute block address.
    inline operator uint32_t () const { return m_address; }
    
    //! \brief Conversion operator to page address.
    inline operator PageAddress () const;
    
    //! \brief Returns true if the block address is valid.
    inline bool isValid() const { return NandHal::isAbsoluteBlockValid(m_address); }
    
    //! \brief Get the block's NAND object.
    inline NandPhysicalMedia * getNand() const { return isValid() ? NandHal::getNandForAbsoluteBlock(m_address) : NULL; }
    
    //! \brief Get the block address as a NAND relative block.
    inline uint32_t getRelativeBlock() const { return getNand()->blockToRelative(m_address); }
    
    //! \brief Prefix increment operator to advance the address to the next block.
    inline BlockAddress & operator ++ () { ++m_address; return *this; }
    
    //! \brief Prefix decrement operator.
    inline BlockAddress & operator -- () { --m_address; return *this; }
    
    //! \brief Increment operator.
    inline BlockAddress & operator += (uint32_t amount) { m_address += amount; return *this; }
    
    //! \brief Decrement operator.
    inline BlockAddress & operator -= (uint32_t amount) { m_address -= amount; return *this; }

protected:
    uint32_t m_address; //!< Absolute block address.
};

/*!
 * \brief Helper class to represent page addresses.
 */
class PageAddress
{
public:
    
    inline PageAddress() : m_address(0) {}
    inline PageAddress(uint32_t absolutePage) : m_address(absolutePage) {}
    inline PageAddress(uint32_t absoluteBlock, uint32_t pageOffset) { m_address = NandHal::getNandForAbsoluteBlock(absoluteBlock)->blockAndOffsetToPage(absoluteBlock, pageOffset); }
    inline PageAddress(NandPhysicalMedia * nand, uint32_t relativePage) { m_address = nand->baseAbsolutePage() + relativePage; }
    inline PageAddress(uint32_t nandNumber, uint32_t relativeBlock, uint32_t pageOffset) { NandPhysicalMedia * nand = NandHal::getNand(nandNumber); m_address = nand->baseAbsolutePage() + nand->blockAndOffsetToPage(relativeBlock, pageOffset); }
    explicit inline PageAddress(const BlockAddress & block, uint32_t pageOffset=0) { m_address = NandHal::getNandForAbsoluteBlock(block.get())->blockAndOffsetToPage(block.get(), pageOffset); }
    inline PageAddress(const PageAddress & other) : m_address(other.m_address) {}
    
    inline PageAddress & operator = (const PageAddress & other)
    {
        set(other);
        return *this;
    }
    
    //! \brief Returns the absolute page address.
    inline uint32_t get() const { return m_address; }
    
    //! \brief Change the address.
    inline void set(const PageAddress & addr) { m_address = addr.m_address; }
    
    //! \brief Change the address.
    inline void set(const BlockAddress & addr) { m_address = addr.get() << NandHal::getParameters().pageToBlockShift; }
    
    //! \brief Returns the page as a block.
    inline BlockAddress getBlock() const { return BlockAddress(*this); }
    
    //! \brief Returns the absolute page address.
    inline operator uint32_t () const { return m_address; }
    
    //! \brief Conversion operator to block address.
    inline operator BlockAddress () const { return getBlock(); }
    
    //! \brief Returns true if the page address is valid.
    inline bool isValid() const { return NandHal::isAbsolutePageValid(m_address); }
    
    //! \brief Get the page's NAND object.
    inline NandPhysicalMedia * getNand() const { return isValid() ? NandHal::getNandForAbsolutePage(m_address) : NULL; }
    
    //! \brief Get the page as a NAND relative page address.
    inline uint32_t getRelativePage() const { return getNand()->pageToRelative(m_address); }
    
    //! \brief Get the page offset within the block.
    inline uint32_t getPageOffset() const { return m_address % NandHal::getParameters().wPagesPerBlock; }
    
    //! \brief Prefix increment operator to advance the page address to the next page.
    inline PageAddress & operator ++ () { ++m_address; return *this; }
    
    //! \brief Prefix decrement operator.
    inline PageAddress & operator -- () { --m_address; return *this; }
    
    //! \brief Increment operator.
    inline PageAddress & operator += (uint32_t amount) { m_address += amount; return *this; }
    
    //! \brief Decrement operator.
    inline PageAddress & operator -= (uint32_t amount) { m_address -= amount; return *this; }

protected:
    uint32_t m_address; //!< The absolute page address.
};

inline void BlockAddress::set(const PageAddress & addr)
{
    m_address = addr.get() >> NandHal::getParameters().pageToBlockShift;
}

inline PageAddress BlockAddress::getPage() const
{
    return PageAddress(*this);
}

inline BlockAddress::operator PageAddress () const
{
    return getPage();
}

#pragma ghs section text=default

//! \name Combined Reading and ECC Return Value Utilities
//!
//! For the 37xx, reading and applying ECC is a single operation. Thus, the
//! return value from a read operation is more complicated than it used to be.
//! One could say that, when you read, there are "varying levels of success."
//! These inline utilities make it more convenient to test the return value of
//! from a read operation for these varying levels of success.
//!
//! Note that the implementation of these functions depends critically on the fact
//! that the ECC checking code can return only the values comprehended here. If
//! it changes in the future to return more values, these macros will have to
//! change.
//!
//! @{

    //! \brief Tests for a successful read which was perfect, or in which all errors were corrected
    //! and the page shows no decay.
    inline bool is_read_status_success_or_ecc_fixed_without_decay(RtStatus_t status)
    {
        return (status == SUCCESS || status == ERROR_DDI_NAND_HAL_ECC_FIXED);
    }

    //! \brief Tests for a read in which all errors were corrected
    //! and decay is allowed.
    inline bool is_read_status_ecc_fixed(RtStatus_t status)
    {
        return (status == ERROR_DDI_NAND_HAL_ECC_FIXED || status == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR);
    }

    //! \brief Tests for a successful read which was perfect, or one in which all errors were corrected
    //! and decay is allowed.
    inline bool is_read_status_success_or_ecc_fixed(RtStatus_t status)
    {
        return (status == SUCCESS || status == ERROR_DDI_NAND_HAL_ECC_FIXED || status == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR);
    }

    //! \brief Tests for a read that failed, but not because of ECC problems.
    inline bool is_read_status_error_excluding_ecc(RtStatus_t status)
    {
        return (status != SUCCESS && status != ERROR_DDI_NAND_HAL_ECC_FIXED && status != ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR && status != ERROR_DDI_NAND_HAL_ECC_FIX_FAILED);
    }

//! @}

} // namespace nand

#endif // #ifndef _DDI_NAND_HAL_H
//! @}
