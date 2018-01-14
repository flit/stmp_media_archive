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
//! \addtogroup ddi_media_nand_hal_internals
//! @{
//! \file   ddi_nand_hal_types.h
//! \brief  Declarations of the HAL type-specific NAND classes.
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDI_NAND_HAL_TYPE2_H
#define _DDI_NAND_HAL_TYPE2_H

#include "errordefs.h"
#include "types.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/nand/gpmi/ddi_nand_gpmi_dma.h"

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

// Forward declaration
union _nand_hal_id_decode;
struct OnfiParamPage;

/*!
 * \brief Base class implementing the commands common to most devices.
 *
 * This class is the base for all NAND type classes. The only method not implemented here is
 * the checkStatus() method, since it does not have a generic implementation.
 * Because this method is pure virtual in the parent class, subclasses are forced to
 * provide an implementation. checkStatus() does have a stub implementation in this class,
 * which allows instances to be created to use commands such as reset() and readID() before
 * the actual NAND type is known. The getStatus() method is common because the same
 * Get Status command code is sent to most NAND types.
 *
 * \note This class is in effect the replacement for the old TypeX_ functions.
 */
class CommonNandBase : public NandPhysicalMedia
{
public:

    //! \brief Factory function to instantiate a class of the specified NAND type.
    static CommonNandBase * createNandOfType(NandType_t nandType);
    
    //! \brief Default constructor.
    CommonNandBase() {}
    
    //! \brief Type-specific initialization.
    virtual RtStatus_t init();
    
    //! \brief Type-specific cleanup.
    virtual RtStatus_t cleanup();

    virtual RtStatus_t reset();

    virtual RtStatus_t readID(uint8_t * pReadIDCode);
    
    //! \copydoc CommonNandBase::readID(uint8_t*)
    //!
    //! This alternate read ID function simply lets you use the internal HAL read ID decoding
    //! structure much more easily.
    RtStatus_t readID(_nand_hal_id_decode * pReadIDCode) { return readID(reinterpret_cast<uint8_t *>(pReadIDCode)); }
    
    //! \name ONFI operations
    //@{
    virtual bool checkOnfiID();
    virtual RtStatus_t readOnfiParameterPage(OnfiParamPage * paramPage);
    virtual char * getDeviceName();
    //@}

    virtual RtStatus_t readRawData(uint32_t wSectorNum, uint32_t columnOffset, uint32_t readByteCount, SECTOR_BUFFER * pBuf);
    
    virtual RtStatus_t readPage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t readPageWithEcc(const NandEccDescriptor_t * ecc, uint32_t pageNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t readMetadata(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC);

    //! \brief Compare a hardware status value against an abstract status mask.
    virtual RtStatus_t checkStatus(uint32_t status, uint32_t Mask, uint32_t * abstractStatus);
    
    //! \brief Translate hardware status bits to abstract status bits.
    //!
    //! This function translates status bits obtained from hardware and translates
    //! it to the abstract status information that the software needs and
    //! understands. It does not actually read the current status from the NAND;
    //! the type-specific hardware status value must be passed into the \a pStatus
    //! parameter.
    //!
    //! Abstract status bit definitions:
    //!     - #kNandStatusPassMask
    //!     - #kNandStatusTrueReadyMask
    //!     - #kNandStatusCacheReadyMask
    //!     - #kNandStatusCachePreviousPassMask
    //!
    //! \return Abstract status value.
    virtual uint32_t convertStatusToAbstract(uint32_t status) { return status; }

    //! \brief Adjust the hardware write protect signal to permit changes.
    //!
    //! NAND hardware manufacturers provide a write protect signal that disables
    //! the writing machinery inside the chip. If this signal is asserted, the
    //! hardware's writing mechanism is disabled, even if it appears to be
    //! processing a write command successfully. This signal is particularly useful
    //! in protecting from corruption during start up and shut down.
    //!
    //! \retval SUCCESS
    virtual RtStatus_t enableWrites();

    //! \brief Adjust the hardware write protect signal to disallow changes.
    //!
    //! NAND hardware manufacturers provide a write protect signal that disables
    //! the writing machinery inside the chip. If this signal is asserted, the
    //! hardware's writing mechanism is disabled, even if it appears to be
    //! processing a write command successfully. This signal is particularly useful
    //! in protecting from corruption during start up and shut down.
    //!
    //! \retval SUCCESS
    virtual RtStatus_t disableWrites();
    
    virtual RtStatus_t writeRawData(uint32_t pageNumber, uint32_t columnOffset, uint32_t writeByteCount, const SECTOR_BUFFER * data);

    virtual RtStatus_t writePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary);

    //! \brief Common implementation just calls readPage().
    virtual RtStatus_t readFirmwarePage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    //! \brief Common implementation just calls writePage().
    virtual RtStatus_t writeFirmwarePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary);
    
    virtual RtStatus_t readMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t readMultipleMetadata(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t writeMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t eraseMultipleBlocks(MultiplaneParamBlock * blocks, unsigned blockCount);

    virtual RtStatus_t eraseBlock(uint32_t uBlockNumber);
    
    virtual RtStatus_t copyPages(NandPhysicalMedia * targetNand, uint32_t uSourceStartSectorNum, uint32_t uTargetStartSectorNum, uint32_t wNumSectors, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, NandCopyPagesFilter * filter, uint32_t * successfulPages);

    //! \brief Correct the data in the given buffer.
    //!
    //! \param[in]  pBuffer     The buffer of interest. Currently ignored.
    //! \param[in]  pAuxBuffer  The auxiliary buffer of interest. Used only for BCH ECC.
    //! \param[in] correctionInfo Optional results of the correction.
    //!
    //! \retval SUCCESS                             There were no errors.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIXED        Errors were detected and
    //!                                             fixed.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED   There were uncorrectable
    //!                                                 errors.
    virtual RtStatus_t correctEcc(SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo);
    
    //! \brief Perform any necessary adjustments to the page address before it is sent to the device.
    //!
    //! This default implementation does nothing; it simply returns the page number as is.
    virtual uint32_t adjustPageAddress(uint32_t pageNumber);

    virtual bool isBlockBad(uint32_t blockAddress, SECTOR_BUFFER * auxBuffer, bool checkFactoryMarkings=false, RtStatus_t * readStatus=NULL);
    
    virtual bool isOnePageMarkedBad(uint32_t pageAddress, bool checkFactoryMarkings, SECTOR_BUFFER * auxBuffer, RtStatus_t * readStatus);
    
    virtual RtStatus_t markBlockBad(uint32_t blockAddress, SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer);

    //! \brief The common implementation does nothing.
    virtual RtStatus_t enableSleep(bool isEnabled) { return SUCCESS; }
    
    virtual bool isSleepEnabled() { return false; }
    
protected:

#if defined(STMP378x)
    //! Buffer used to hold the first data chunk, containing the metadata, for reading and
    //! writing when using BCH.
    uint8_t * m_pMetadataBuffer;
#endif // defined(STMP378x)

    void initDma();

};

/*!
 * \brief Type 2 NAND - Small/Large Addressing SLC
 *
 * - 64 pages/block
 * - 2112 byte pages
 */
class Type2Nand : public CommonNandBase
{
public:

    //! \brief Convert a status byte to our abstract status form.
    virtual uint32_t convertStatusToAbstract(uint32_t status);

};

/*!
 * \brief Type 5 - Toshiba/Sandisk Large Page MLC
 *
 * - 128 pages/block
 * - Has cache
 * - 2112 byte pages
 * - No partial writes
 * - Internal copy-back
 */
class Type5Nand : public Type2Nand
{
};

/*!
 * \brief Type 6 - Samsung Large Page MLC
 *
 * - Like Type 5, but no cache
 * - 128 pages/block
 * - 2112 byte pages
 * - No internal copy-back
 */
class Type6Nand : public CommonNandBase
{
public:

    //! \brief Convert a status byte to our abstract status form.
    virtual uint32_t convertStatusToAbstract(uint32_t status);
    
};

/*!
 * \brief Type 7 - Samsung, Micron, and Intel SLC
 *
 * - Like Type 6, but SLC
 * - Region split between odd/even
 * - 128 pages/block
 * - 2112 byte pages
 * - No internal copy-back
 */
class Type7Nand : public Type6Nand
{
};

/*!
 * \brief Type 8 - Samsung MLC
 *
 * - Like Type 6, but 4K pages
 * - Region split between odd/even
 * - 128 pages/block
 * - 4224 byte pages
 * - Has internal copy-back
 * - Reed-Solomon ECC4
 * - Uses 2K firmware pages
 *
 * On the 378x, we use BCH for this NAND type and there is nothing special we have to do.
 * But on chips with only ECC8, we have to go through hoops to work around the hardware's
 * limitation of only supporting 2112 byte pages with 4-bit ECC.
 */
class Type8Nand : public Type6Nand
{
public:

#if !defined (STMP378x)
    virtual RtStatus_t init();
    
    virtual RtStatus_t readPage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t readMetadata(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t writePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary);
#endif // STMP378x

};

/*!
 * \brief Type 9 - Toshiba Large Page MLC
 *
 * - 128 pages/block
 * - Has cache
 * - 4314 byte pages
 * - No partial writes
 * - Internal copy-back
 * - Reed-Solomon ECC8
 */
class Type9Nand : public Type2Nand
{
};

/*!
 * \brief Type 10 - Samsung SLC
 *
 * - Like Type 6, but 4K pages
 * - Region split between odd/even
 * - 64 pages/block
 * - 4224 byte pages
 * - Has internal copy-back
 * - Reed-Solomon ECC4
 * - Uses 2K firmware pages
 */
class Type10Nand : public Type8Nand
{
};

/*!
 * \brief Type 11 - Toshiba 8K Page MLC
 *
 * - 128 pages/block
 * - Split into districts
 * - Has cache
 * - 8568 byte pages
 * - No partial writes
 * - Internal copy-back, requires readback
 * - Multi-plane writes and copy-back
 * - Datasheet specifies 24 bit/1024 byte ECC, we can only manage 14bit/512byte with BCH
 */
class Type11Nand : public Type2Nand
{
public:
    //! \brief Adjust the page address to skip over holes in the address space.
    virtual uint32_t adjustPageAddress(uint32_t pageNumber);
};

/*!
 * \brief Type 12 - Hynix MLC ECC12
 *
 * - 128 pages/block
 * - Has cache
 * - 4320 byte pages
 * - No partial writes
 * - Internal copy-back, requires readback
 * - Multi-plane writes and copy-back
 * - 12 bit/512 byte ECC using BCH
 */
class Type12Nand : public Type2Nand
{
};

/*!
 * \brief Type 13 - Micron MLC ECC12
 *
 * - 128 pages/block
 * - Has cache
 * - 4314 byte pages
 * - No partial writes
 * - Internal copy-back, requires readback
 * - Multi-plane writes and copy-back
 * - 12 bit/512 byte ECC using BCH
 */
class Type13Nand : public Type2Nand
{
};

/*!
 * \brief Type 14 - Micron MLC ECC12 L62A/L63B
 *
 * - 256 pages/block
 * - Has cache
 * - 4320 byte pages
 * - No partial writes
 * - Internal copy-back, requires readback
 * - Multi-plane writes and copy-back
 * - 12 bit/512 byte ECC using BCH
 */
class Type14Nand : public Type2Nand
{
};

/*!
 * \brief Type 15 - Samsung 8K Page MLC
 *
 * - 128 pages/block
 * - Split into planes
 * - Has cache
 * - 8628 byte pages
 * - No partial writes
 * - Internal copy-back, requires readback
 * - Multi-plane writes and copy-back
 * - Datasheet specifies 24 bit/1024 byte ECC, we can only manage 16bit/512byte with BCH
 */
class Type15Nand : public Type6Nand
{
public:
     //! \brief Adjust the page address to skip over holes in the address space.
//     virtual uint32_t adjustPageAddress(uint32_t pageNumber);
};

// Disabled to get 3710 apps to build.
#if !defined(STMP37xx)

//! Set to 1 to enable use of read mode 2 for page reads.
#define PBA_USE_READ_MODE_2 0

//! Set to 1 to turn on support for using auto page program with data cache commands.
#define PBA_USE_CACHE_WRITE 0

//! Set to 1 to read back sleep mode status to verify that the device is in the expected mode.
#define PBA_VERIFY_SLEEP_MODE 0

#define PBA_MOVE_PAGE 1
/*!
 * \brief Type 16 - Toshiba PBA-NAND
 *
 * - 128 pages/block for 32nm, 256 pages/block for 24nm
 * - Split into even/odd districts
 * - Has cache
 * - 8224 byte pages (8192+32)
 * - No partial writes
 * - Internal copy-back, optional readback
 * - Multi-plane writes and copy-back
 * - Built-in ECC engine
 * - Several extra blocks per internal die
 * - Shares similar split address ranges to Type 11
 * - No external write enable signal.
 * - Uses 4K firmware pages since the ROM does not support disabling ECC
 */
class Type16Nand : public Type11Nand
{
public:
    //! \brief Default constructor.
    Type16Nand();

    //! \brief Type-specific initialization.
    virtual RtStatus_t init();
    
    //! \brief Type-specific cleanup.
    virtual RtStatus_t cleanup();

    virtual RtStatus_t readRawData(uint32_t wSectorNum, uint32_t columnOffset, uint32_t readByteCount, SECTOR_BUFFER * pBuf);
    
    virtual RtStatus_t writeRawData(uint32_t pageNumber, uint32_t columnOffset, uint32_t writeByteCount, const SECTOR_BUFFER * data);

    virtual RtStatus_t readPageWithEcc(const NandEccDescriptor_t * ecc, uint32_t pageNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t readPage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t readMetadata(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t writePage(uint32_t uSectorNumber, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary);

    virtual RtStatus_t readFirmwarePage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t writeFirmwarePage(uint32_t uSectorNumber, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary);

    //! PBA-NAND does not have an external write enable signal.
    virtual RtStatus_t enableWrites() { return SUCCESS; }

    //! PBA-NAND does not have an external write enable signal.
    virtual RtStatus_t disableWrites() {return SUCCESS; }
    
    //! \brief Convert a status byte to our abstract status form.
    virtual uint32_t convertStatusToAbstract(uint32_t status);
    
    //! \name Multiplane support
    //@{
    virtual RtStatus_t readMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t readMultipleMetadata(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t writeMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t eraseMultipleBlocks(MultiplaneParamBlock * blocks, unsigned blockCount);
    //@}

    virtual RtStatus_t reset();

    virtual RtStatus_t readID(uint8_t * pReadIDCode);
    
    virtual RtStatus_t eraseBlock(uint32_t uBlockNumber);
    
    virtual RtStatus_t enableSleep(bool isEnabled);
    
    virtual bool isSleepEnabled();

    virtual bool isBlockBad(uint32_t blockAddress, SECTOR_BUFFER * auxBuffer, bool checkFactoryMarkings=false, RtStatus_t * readStatus=NULL);

    virtual RtStatus_t copyPages(NandPhysicalMedia * targetNand, uint32_t uSourceStartSectorNum, uint32_t uTargetStartSectorNum, uint32_t wNumSectors, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, NandCopyPagesFilter * filter, uint32_t * successfulPages);

    virtual uint32_t adjustPageAddress(uint32_t pageAddress);

protected:
    
    //! \brief Supported generations of PBA-NAND.
    typedef enum _pba_nand_generation
    {
        k32nm,  //!< First generation, 32nm geometry, 128 pages/block.
        k24nm   //!< Second generation, 24nm geometry, 256 pages/block.
    } ChipGeneration_t;
    
    //! \name Chip type
    //@{
    ChipGeneration_t m_chipGeneration;  //!< \brief The generation of this PBA-NAND chip.
    bool m_is4GB;   //!< Whether this is a 4GB device.
    //@}
    
    //! \name DMA descriptors
    //@{
    NandDma::ReadRawData m_pageReadDma;
    NandDma::ReadStatus m_pageStatusReadDma;
    NandDma::Component::CommandAddress m_pageResumeReadDma;
    NandDma::WriteRawData m_pageWriteDma;
    NandDma::ReadStatus m_statusReadDma;
    NandDma::Component::CommandAddress m_resumeReadDma;
    NandDma::ReadEccData m_firmwareReadDma;
    NandDma::ReadStatus m_firmwareStatusReadDma;
    NandDma::Component::CommandAddress m_firmwareResumeReadDma;
    NandDma::ReadRawData m_metadataReadDma;
    NandDma::ReadStatus m_metadataStatusReadDma;
    NandDma::Component::CommandAddress m_metadataResumeReadDma;
    NandDma::ReadRawData m_modeDma; //!< \brief Mode change DMA chain. We keep one per chip instance so we don't have to modify the chip select every time we use the DMA.
    //@}

    //! \brief Multiread DMA components
    struct MultiplaneReadDma {
        uint8_t inputPage0Buffer[4] __ALIGN4__;
        uint8_t inputPage1Buffer[4] __ALIGN4__;
        uint8_t readColumnPage0Buffer[6] __ALIGN4__;
        uint8_t readColumnPage1Buffer[6] __ALIGN4__;
        uint8_t randomDataCommand0Buffer[3] __ALIGN4__;
        uint8_t randomDataCommand1Buffer[3] __ALIGN4__;
        NandDma::Component::CommandAddress inputPage0Dma;
        NandDma::Component::CommandAddress inputPage1Dma;
        NandDma::Component::CommandAddress readCommandDma;
        NandDma::Component::WaitForReady waitDma;
        NandDma::ReadStatus statusDma;
        NandDma::Component::CommandAddress readColumnPage0Dma;
        NandDma::Component::CommandAddress randomDataCommand0Dma;
        NandDma::Component::CommandAddress finishRandomDataCommand0Dma;
        NandDma::Component::ReceiveRawData receivePageData0Dma;
        NandDma::Component::ReceiveRawData receivePageMetadata0Dma;
        NandDma::Component::CommandAddress readColumnPage1Dma;
        NandDma::Component::CommandAddress randomDataCommand1Dma;
        NandDma::Component::CommandAddress finishRandomDataCommand1Dma;
        NandDma::Component::ReceiveRawData receivePageData1Dma;
        NandDma::Component::ReceiveRawData receivePageMetadata1Dma;
        NandDma::Component::Terminator terminationDma;
        NandDma::WrappedSequence multiReadDma;
    } m_multiread;
    
    struct MovePageDma {
        NandDma::ReadRawData sourcePageReadDma;
        NandDma::ReadStatus PageStatusDma;
        NandDma::WriteRawData targetPageWriteDma;
    } m_movePage;

    //! \name State
    //@{
    bool m_isSleepEnabled;  //!< \brief True if sleep mode is actively being controlled on this chip.
    bool m_isAsleep;        //!< \brief True when the NAND is actually in sleep mode.
    bool m_isInFastReadMode;    //!< \brief Whether the NAND is in read mode 2.
    //@}
    
#if PBA_USE_CACHE_WRITE
    //! \name Cache write state
    //@{
    SECTOR_BUFFER * m_cacheWriteBuffer;   //!< Buffer to hold the data of a page waiting to be written.
    SECTOR_BUFFER * m_cacheWriteAuxBuffer;    //!< Buffer holding the metadata of the page waiting to be written.
    void * m_actualCacheWriteAuxBuffer; //!< The actual buffer returned from malloc() or equivalent.
    bool m_isInCacheWrite;          //!< Whether a cache write sequence is ongoing or ready.
    bool m_hasPageInCacheBuffer;    //!< True if there is valid data in the cache write buffer, #m_cacheWriteBuffer.
    uint32_t m_cacheWriteBlock;     //!< Block address of the page in the cache write buffer.
    uint32_t m_cacheWriteBufferedPageOffset;    //!< Page index into the block for the buffered page.
    //@}
#endif // PBA_USE_CACHE_WRITE

    //! \name DMA initialization
    //@{
    void buildPageReadWriteDma();
    void buildFirmwareReadDma();
    void buildMetadataReadDma();
    void buildModeChangeDma();
    void buildMultireadDma();
    void buildMovePageDma();
    //@}
    // Code allows page to be moved
    RtStatus_t movePage(
        uint32_t uSectorNumber, 
        uint32_t uTargetStartSectorNum, 
        SECTOR_BUFFER * auxBuffer);
    
    //! \brief Sends a command to enter or exit sleep mode.
    RtStatus_t setSleepMode(bool isEnabled);

    //! \brief Queries the device for the current sleep mode state.
    RtStatus_t getSleepModeState(uint8_t * isEnabled);

    //! \brief Send a command to set a PBA mode.
    RtStatus_t changeMode(uint8_t commandByte);

    //! \brief Enable or disable fast read mode (mode 2);
    RtStatus_t enableFastReadMode(bool isEnabled);

    //! \brief Helper function to write a page.
    RtStatus_t writePageFromBuffer(uint32_t address, uint8_t programCommand, const SECTOR_BUFFER * pageBuffer, const SECTOR_BUFFER * auxBuffer);

#if PBA_USE_CACHE_WRITE
    void flushWriteCacheBuffer();
    RtStatus_t writeBufferedPage(uint8_t programCommand);
#else
    void flushWriteCacheBuffer() {}
#endif // PBA_USE_CACHE_WRITE

    void clearEccInfo(NandEccCorrectionInfo_t * pECC);
    
    RtStatus_t getReadPageStatus();
    
    void fillMultiplaneReadStatus(MultiplaneParamBlock * pb, bool isItem0District0);
    void fillMultiplaneWriteStatus(MultiplaneParamBlock * pb, bool isItem0District0);
    
    /*!
     * \brief Disables sleep mode for the life of the object.
     *
     * During construction, sleep mode is disabled for the specified PBA-NAND. Then upon
     * destruction, sleep mode is enabled. This allows you to easily wrap other commands
     * to ensure that sleep mode is exited and re-entered.
     */
    class SleepController
    {
    public:
        //! \brief Constructor. Removes device from sleep mode.
        inline SleepController(Type16Nand * nand)
        :   m_nand(nand)
        {
            if (m_nand->isSleepEnabled())
            {
                m_nand->setSleepMode(false);
            }
        }
        
        //! \brief Destructor. Puts device back into sleep mode.
        inline ~SleepController()
        {
            if (m_nand->isSleepEnabled())
            {
                m_nand->setSleepMode(true);
            }
        }
    
    protected:
        Type16Nand * m_nand;    //!< The PBA-NAND object we're controlling.
    };
    
};

/*!
 * \brief Type 17 - Micron MLC ECC16 L73A
 *
 * - 256 pages/block
 * - Has cache
 * - 4320 byte pages
 * - Multi-plane writes and copy-back
 * - 16 bit/512 byte ECC using BCH
 */
class Type17Nand : public Type2Nand
{
};

/*!
 * \brief Type 18 - Micron MLC ECC16 8K page L74A
 *
 * - 256 pages/block
 * - Has cache
 * - 8640 byte pages
 * - Multi-plane writes and copy-back
 * - 16 bit/512 byte ECC using BCH
 */
class Type18Nand : public Type2Nand
{
};

#endif // !37xx

#endif // #ifndef _DDI_NAND_HAL_TYPE2_H
//! @}
