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
//! \file    ddi_nand_ecc.h
//! \brief   Provides public interface for using the ECC8 block.
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDI_ECC_H
#define _DDI_ECC_H

#include "types.h"
#include "errordefs.h"
#include "registers/regsapbh.h"
#include "registers/regsecc8.h"
#if defined(STMP378x)
    #include "registers/regsbch.h"
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \brief All of the supported ECC types.
//!
//! Constants for the various types of ECC that are supported by the NAND
//! driver. Used in the #NandTypeDescriptor_t structures in the
//! HAL tables.
typedef enum _nand_hal_ecc_type
{
    kNandEccType_RS4,   //!< Reed-Solomon 4-bit.
    kNandEccType_RS8,   //!< Reed-Solomon 8-bit.
#if defined(STMP378x)
    kNandEccType_BCH0,  //!< BCH-ECC-0
    kNandEccType_BCH2,  //!< BCH-ECC-2
    kNandEccType_BCH4,  //!< BCH-ECC-4
    kNandEccType_BCH6,  //!< BCH-ECC-6
    kNandEccType_BCH8,  //!< BCH-ECC-8
    kNandEccType_BCH10, //!< BCH-ECC-10
    kNandEccType_BCH12, //!< BCH-ECC-12
    kNandEccType_BCH14, //!< BCH-ECC-14
    kNandEccType_BCH16, //!< BCH-ECC-16
    kNandEccType_BCH18, //!< BCH-ECC-18
    kNandEccType_BCH20, //!< BCH-ECC-20
#endif
    kNandEccType_None,  //!< ECC disabled.
    kNandEccType_Count  //!< Number of different ECC types.
} NandEccType_t;

// Forward declarations.
struct EccTypeInfo;
const EccTypeInfo * ddi_gpmi_get_ecc_type_info(NandEccType_t eccType);

/*!
 * \brief ECC parameters descriptor.
 *
 * This structure contains all the information required to describe an ECC configuration for
 * either the Reed-Solomon or, on systems that support it, the BCH ECC engines. The #eccType
 * member specifies the overall ECC type described by an instance of this struct. On systems that
 * support both Reed-Solomon and BCH ECC types, the BCH layout parameter members are only used if
 * #eccType is set to a BCH type. For BCH, #eccType also serves as the block N ECC level layout
 * parameter. On systems that don't support BCH, the BCH layout members are excluded from the
 * struct definition.
 *
 * In addition to describing Reed-Solomon and BCH ECC configurations, this struct can also
 * specify that ECC is disabled. This will be the case when #eccType is set to #kNandEccType_None.
 * To identify this situation, you can use the isEnabled() member function.
 *
 * Instances of this struct can be compared against each other for equality and inequality using
 * the provided comparison operators. You can also query the struct to see what type of ECC it
 * describes by using the isXXX() member functions. And the getTypeInfo() member returns a
 * pointer to the ECC type descriptor struct, allowing you to get more detailed information
 * about the ECC type.
 */
struct NandEccDescriptor
{
    NandEccType_t   eccType;            //!< For 378x, block N ECC type (RS or BCH). For chips without BCH, the RS ECC type.
    
#if defined(STMP378x)
    //! \name BCH layout parameters
    //!
    //! These parameters only apply to BCH ECC.
    //@{
    NandEccType_t   eccTypeBlock0;      //!< Block 0 ECC type. Must always be a BCH type if used.
    uint32_t        u32SizeBlockN;      //!< Block N data size.
    uint32_t        u32SizeBlock0;      //!< Block 0 data size.
    uint32_t        u32NumEccBlocksN;   //!< Number of ECC blocks not including block 0.
    uint32_t        u32MetadataBytes;   //!< Number of meta data bytes.
    uint32_t        u32EraseThreshold;  //!< Erase threshold.
    //@}
#endif

    //! \name Operators
    //@{
    inline bool operator == (const NandEccDescriptor & other) const;
    inline bool operator != (const NandEccDescriptor & other) const;
    //@}
    
    //! \name Type queries
    //@{
    inline bool isEnabled() const;
    inline bool isECC8() const;
    inline bool isBCH() const;
    //@}
    
    //! \brief Returns the info struct for this ECC type.
    inline const EccTypeInfo * getTypeInfo() const { return ddi_gpmi_get_ecc_type_info(eccType); }
    
    //! \brief Calculates the ECC mask value.
    inline uint32_t computeMask(uint32_t byteCount, uint32_t pageTotalSize, bool isWrite, bool readOnly2k, uint32_t * dataCount, uint32_t * auxCount) const;

};

typedef struct NandEccDescriptor NandEccDescriptor_t;

/*!
 * \brief ECC correction information.
 */
struct NandEccCorrectionInfo
{
    enum _ecc_correction_constants
    {
        //! Maximum number of payloads supported by all ECC engines.
        kMaxPayloadCount = 16,
        
        //! Sentinel value used to indicate that an ECC payload had too many errors to correct.
        kUncorrectable = 0xffffffff,
        
        //! Sentinel value that indicates that a payload contained all ones.
        kAllOnes = 0xfffffffe
    };
    
    
    unsigned maxCorrections;        //!< Overall maximum number of corrections for all payloads and the metadata.
    unsigned payloadCount;          //!< Number of valid entries in \a payloadCorrections.
    bool isMetadataValid;           //!< True if \a metadataCorrections contains valid data.
    unsigned metadataCorrections;   //!< Number of bit errors in the metadata, or #kUncorrectable.
    unsigned payloadCorrections[kMaxPayloadCount];    //!< Number of bit errors for each payload, or #kUncorrectable.
};

typedef struct NandEccCorrectionInfo NandEccCorrectionInfo_t;

//! \name ECC and Metadata Constants
//!
//! The 37xx hardware imposes a specific structure on how data is laid out both
//! on the NAND hardware and in system memory. The following constants describe
//! some aspects of this structure. See the data sheet for details.
//!
//! Note that all these constants are misnamed. They represent data structures
//! associated with Reed-Solomon four-symbol and eight-symbol ECC, but they are
//! named \c "4BIT" and \c "8BIT".
//!
//! @{

    //! \brief Size of an ECC data block in bytes.
    #define NAND_ECC_BLOCK_SIZE (512)

    //! \brief The number of ECC bytes associated with each of the four 512-byte
    //! data blocks when using four-symbol Reed-Solomon ECC on a 2KiB page.
    #define NAND_ECC_BYTES_4BIT      (9)

    //! \brief The number of metadata bytes available when using four-symbol
    //! Reed-Solomon ECC on a 2KiB page.
    #define NAND_METADATA_SIZE_4BIT  (19)

    //! \brief The number of 4-byte words that are required to store meta-data
    //! when using four-symbol Reed-Solomon ECC on a 2KiB page.
    #define NAND_METADATA_SIZE_4BIT_IN_WORDS (5)
    
    //! \brief The number of ECC bytes associated with each of the eight
    //! 512-byte data blocks when using eight-symbol Reed-Solomon ECC on a 4KiB
    //! page.
    #define NAND_ECC_BYTES_8BIT      (18)

    //! \brief The number of metadata bytes available when using eight-symbol
    //! Reed-Solomon ECC on a 4KiB page.
    #define NAND_METADATA_SIZE_8BIT  (65)

    //! Max BCH ECC level supported by the hardware.
    #define NAND_MAX_BCH_ECC_LEVEL 20

    //! \brief BCH Parity symbol size in bits.
    #define NAND_BCH_PARITY_SIZE_BITS   (13)

    //! \brief The number of ECC bytes associated with each 512-byte data block
    //!        for BCH.
    //!
    //! <pre>
    //! Level       Bytes Per Block
    //! -----       ---------------
    //! 8           13
    //! 12          20
    //! 16          26
    //! </pre>
    #define NAND_ECC_BYTES_BCH(level)   ((((level)*NAND_BCH_PARITY_SIZE_BITS)+(8-1))/8)

    //! \brief The number of metadata bytes available when using BCH.
    #define NAND_METADATA_SIZE_BCH      (10)

    //! \brief The block N count for a 2K page used by the ROM.
    //!
    //! This value is always 3, at least when using 512-byte block sizes. However,
    //! the ROM cannot use other block sizes because they don't come out to
    //! exactly 2K.
    #define NAND_BCH_2K_PAGE_BLOCKN_COUNT (3)

//! @}

//! \brief Constants for use when calling ecc mask computation functions.
enum _ecc_operation
{
    //! Reading from NAND.
    kEccOperationRead = false,

    //! Writing to NAND.
    kEccOperationWrite = true
};

//! \brief Constants for use when calling ddi_bch_SetFlashLayout().
enum _ecc_transfer_size
{
    //! Transfer full page.
    kEccTransferFullPage = false,
    
    //! Transfer 2k page.
    kEccTransfer2kPage = true
};

/*!
 * \brief Abstract interface to ECC types.
 *
 * This is an abstract class that presents the common interface to all supported ECC types.
 * The global function ddi_gpmi_get_ecc_type_info() is used to get an instance of this class
 * for a given type of ECC. You can also use the NandEccDescriptor::getTypeInfo() member as
 * a handy way to get the ECC type instance from an ECC descriptor structure.
 */
struct EccTypeInfo
{
    NandEccType_t eccType;      //!< Duplicate ECC type value.
    uint32_t decodeCommand;     //!< ECC engine command for decoding ECC.
    uint32_t encodeCommand;     //!< ECC engine command for encoding ECC.
    uint32_t parityBytes;       //!< Number of parity bytes per ECC chunk.
    uint32_t metadataSize;      //!< Number of bytes of metadata.
    uint32_t threshold;         //!< Number of bit errors that causes a rewrite.
    bool readGeneratesInterrupt;    //!< True if the ECC engine fires an interrupt after a read completes.
    bool writeGeneratesInterrupt;   //!< True if the ECC engine generates an interrupt after completing a write operation.
    
    //! \brief Abstract function to read ECC correction information.
    //!
    //! This function is the single entry point that external callers should use to
    //! get the results of ECC bit error correction. The appropriate function will be
    //! called for the given ECC type. You can use this function either by simply
    //! examining the return code to see if there was an uncorrectable error. Or,
    //! you can pass a pointer to a #NandEccCorrectionInfo_t structure that will
    //! be filled in, if you want more detailed information.
    //!
    //! \param pAuxBuffer Buffer containing ECC status bytes.
    //! \param correctionInfo Optional data about bit errors that were correction.
    //!
    //! \retval SUCCESS No errors detected.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIXED Errors detected and fixed.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIX_FAILED Uncorrectable errors detected.
    //! \retval ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR Errors detected and
    //!     fixed, but the number of bit errors for one or more payloads was above
    //!     the threshold.
    //!
    //! \note Once correction data is read once, it cannot be read again.
    virtual RtStatus_t correctEcc(SECTOR_BUFFER * pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo) const=0;

    //! \brief Abstract function to compute the number of payloads given a size of data and ECC type.
    //!
    //! \param dataSize The size of data that will be protected by ECC, not including metadata.
    //! \param[out] payloadCount On return, the number of ECC payloads.
    //!
    //! \retval SUCCESS
    virtual RtStatus_t computePayloads(unsigned dataSize, unsigned * payloadCount) const=0;
    
    //! \brief Returns the offset and length of metadata given a page size.
    //!
    //! \param dataSize The size of data that will be protected by ECC. Does not include metadata.
    //! \param[out] metadataOffset Offset into the page that the metadata starts.
    //! \param[out] metadataLength Number of bytes long the metadata plus its ECC is.
    //!
    //! \retval SUCCESS
    virtual RtStatus_t getMetadataInfo(unsigned dataSize, unsigned * metadataOffset, unsigned * metadataLength) const=0;

    //! \brief Calculates the ECC mask.
    //!
    //! \param[in] byteCount The number of bytes being read or written through the ECC engine.
    //! \param[in] bIsWrite TRUE if writing to NAND, FALSE if reading from NAND.
    //! \param[in] pEccDescriptor ECC Descriptor.
    //! \param[out] dataCount On return, contains the number of bytes of
    //!     non-auxiliary buffers. May be NULL.
    //! \param[out] auxCount On return this will contain the number of bytes of
    //!     auxiliary data. May be NULL.
    //!
    //! \return A mask value suitable for use in the BUFFER_MASK field of the
    //!     GPMI_ECCCTRL register of the GPMI peripheral.
    virtual uint32_t computeMask(uint32_t byteCount, uint32_t pageTotalSize, bool bIsWrite, bool readOnly2k, const NandEccDescriptor_t *pEccDescriptor, uint32_t * dataCount, uint32_t * auxCount) const=0;
    
    //! \brief Setup the ECC block to handle a transaction of the given type.
    //!
    //! \retval SUCCESS
    virtual RtStatus_t preTransaction(uint32_t u32NandDeviceNumber, bool isWrite, const NandEccDescriptor_t * pEccDescriptor, bool bTransfer2k, uint32_t pageTotalSize) const=0;
    
    //! \brief Perform any work needed by the ECC block after a transaction completes.
    //!
    //! \retval SUCCESS
    virtual RtStatus_t postTransaction(uint32_t u32NandDeviceNumber, bool isWrite) const=0;
    
    /*!
     * \brief Helper class for ensuring the ECC driver is called appropriately for transactions.
     *
     * Use a stack allocated instance of this class to make certain that the pre- and post-
     * transaction ECC driver methods are called when the code leaves the instance's scope.
     *
     * Example usage:
     * \code
     *      {
     *          EccTypeInfo::TransactionWrapper transaction(eccDesc,
     *                  chipSelect,
     *                  pageTotalSize,
     *                  kEccOperationRead);
     *
     *          // ... perform DMA transaction ...
     *      }
     * \endcode
     *
     * In the example code above, the EccTypeInfo::preTransaction() method is called when the
     * \a transaction object is constructed. Then when the code exits that object's scope, the
     * destructor of \a transaction ensures that EccTypeInfo::postTransaction() is called. 
     */
    class TransactionWrapper
    {
    public:
        
        //! \brief Calls ECC driver pre-transaction handler.
        //!
        //! If the ECC descriptor \a ecc specifies an ECC type of #kNandEccType_None, then
        //! this constructor does nothing.
        inline TransactionWrapper(const NandEccDescriptor_t & ecc, uint32_t chipSelect, uint32_t pageTotalSize, bool isWrite, bool bTransfer2k=kEccTransferFullPage)
        :   m_ecc(ecc),
            m_eccType(ecc.getTypeInfo()),
            m_chipSelect(chipSelect),
            m_isWrite(isWrite)
        {
            if (m_eccType)
            {
                m_eccType->preTransaction(chipSelect, isWrite, &ecc, bTransfer2k, pageTotalSize);
            }
        }
        
        //! \brief Invokes ECC driver post-transaction handler.
        //!
        //! Similar to the constructor, an ECC type of #kNandEccType_None will result in the
        //! destructor becoming a no-op.
        ~TransactionWrapper()
        {
            if (m_eccType)
            {
                m_eccType->postTransaction(m_chipSelect, m_isWrite);
            }
        }
    
    protected:
        const NandEccDescriptor_t & m_ecc;  //!< The ECC descriptor.
        const EccTypeInfo * m_eccType;      //!< The ECC driver object.
        uint32_t m_chipSelect;              //!< The chip select for the transaction.
        bool m_isWrite;                     //!< Whether the transaction is a read (false) or write (true).
    };
};

typedef struct EccTypeInfo EccTypeInfo_t;

/*!
 * \brief Reed-Solomon ECC type (ECC8 block).
 */
struct ReedSolomonEccType : public EccTypeInfo
{
    //! \brief Constructor.
    ReedSolomonEccType(NandEccType_t theEccType, uint32_t theDecodeCommand, uint32_t theEncodeCommand, uint32_t theParityBytes, uint32_t theMetadataSize, uint32_t theThreshold);
    
    //! \copydoc EccTypeInfo::correctEcc()
    virtual RtStatus_t correctEcc(SECTOR_BUFFER * pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo) const;

    //! \copydoc EccTypeInfo::computePayloads()
    virtual RtStatus_t computePayloads(unsigned dataSize, unsigned * payloadCount) const;
    
    //! \copydoc EccTypeInfo::getMetadataInfo()
    virtual RtStatus_t getMetadataInfo(unsigned dataSize, unsigned * metadataOffset, unsigned * metadataLength) const;

    //! \copydoc EccTypeInfo::computeMask()
    virtual uint32_t computeMask(uint32_t byteCount, uint32_t pageTotalSize, bool bIsWrite, bool readOnly2k, const NandEccDescriptor_t *pEccDescriptor, uint32_t * dataCount, uint32_t * auxCount) const;

    //! \copydoc EccTypeInfo::preTransaction()
    virtual RtStatus_t preTransaction(uint32_t u32NandDeviceNumber, bool isWrite, const NandEccDescriptor_t * pEccDescriptor, bool bTransfer2k, uint32_t pageTotalSize) const;
    
    //! \copydoc EccTypeInfo::postTransaction()
    virtual RtStatus_t postTransaction(uint32_t u32NandDeviceNumber, bool isWrite) const;
    
protected:
    void readCorrectionStatus(unsigned * maxBitErrors, unsigned * metadataBitErrors, NandEccCorrectionInfo_t * correctionInfo) const;
};

#if defined(STMP378x)
/*!
 * \brief BCH ECC type.
 */
struct BchEccType : public EccTypeInfo
{
    //! \brief Constructor.
    BchEccType(NandEccType_t theEccType, uint32_t theThreshold);
    
    //! \copydoc EccTypeInfo::correctEcc()
    virtual RtStatus_t correctEcc(SECTOR_BUFFER * pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo) const;

    //! \copydoc EccTypeInfo::computePayloads()
    virtual RtStatus_t computePayloads(unsigned dataSize, unsigned * payloadCount) const;
    
    //! \copydoc EccTypeInfo::getMetadataInfo()
    virtual RtStatus_t getMetadataInfo(unsigned dataSize, unsigned * metadataOffset, unsigned * metadataLength) const;

    //! \copydoc EccTypeInfo::computeMask()
    virtual uint32_t computeMask(uint32_t byteCount, uint32_t pageTotalSize, bool bIsWrite, bool readOnly2k, const NandEccDescriptor_t *pEccDescriptor, uint32_t * dataCount, uint32_t * auxCount) const;

    //! \copydoc EccTypeInfo::preTransaction()
    virtual RtStatus_t preTransaction(uint32_t u32NandDeviceNumber, bool isWrite, const NandEccDescriptor_t * pEccDescriptor, bool bTransfer2k, uint32_t pageTotalSize) const;
    
    //! \copydoc EccTypeInfo::postTransaction()
    virtual RtStatus_t postTransaction(uint32_t u32NandDeviceNumber, bool isWrite) const;
    
protected:
    void readCorrectionStatus(unsigned * maxBitErrors, unsigned * metadataBitErrors, SECTOR_BUFFER *pAuxBuffer, NandEccCorrectionInfo_t * correctionInfo) const;
};
#endif // STMP378x

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

//! \name Abstract ECC interface
//@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Accessor function for information about each ECC type.
    //!
    //! Use this function to obtain the ECC info structure for a given ECC type.
    //! The returned structure is the abstracted interface to the various types of
    //! ECC.
    //!
    //! \param eccType Unique ECC type constant.
    //! \return The value returned is a pointer to the global structure containing
    //!     information about the ECC type specified by \a eccType.
    ////////////////////////////////////////////////////////////////////////////////
    const EccTypeInfo_t * ddi_gpmi_get_ecc_type_info(NandEccType_t eccType);

//@}

//! \name ECC8 specific functions
//@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Initializes ECC8 driver.
    //!
    //! Performs ECC driver initialization. Removes ECC reset and ungates the clock.
    //!
    //! \retval SUCCESS
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_ecc8_init(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Resets the ECC8 block
    //!
    //! A soft reset can take multiple clocks to complete, so do NOT gate the
    //! clock when setting soft reset. The reset process will gate the clock
    //! automatically. Poll until this has happened before subsequently
    //! clearing soft reset and clock gate.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_ecc8_soft_reset(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Enable the ECC8 block.
    //!
    //! This function enables the ECC8 block.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_ecc8_enable(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Disable the ECC8 block.
    //!
    //! This function disables the ECC8 block.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_ecc8_disable(void);

//@}

//! \name BCH specific functions
//@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Initializes BCH driver.
    //!
    //! Performs BCH driver initialization.
    //! 1. Removes BCH reset, ungates BCH clock, and resets BCH DMA channel.
    //! 2. Initializes physical address pointers and links the DMA chain if needed.
    //!
    //! \pre  ppEccDmaCmds = NULL if DMA chains have not been linked
    //! \post HWECC is ready to start new computation
    //! \post ppEccDmaCmds and ppEccResults are initialized
    //! \post DMA chains in eccDmaCmds are linked
    //!
    //! \retval SUCCESS
    //!
    //! \todo Dynamically allocate DMA command and ECC results buffers?
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_bch_init(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Enable the BCH block.
    //!
    //! This function enables the BCH block.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_bch_enable(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Disable the BCH block.
    //!
    //! This function disables the BCH block.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_bch_disable(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Updates GPMI_CTRL1_BCH_MODE and the BCH registers.
    //!
    //! Also saves the total size of the NAND page for later use by the BCH driver.
    //! Thus, this function must be called as part of the BCH driver initialization
    //! process before using any other routines.
    //!
    //! \param[in] u32NandDeviceNumber NAND device number.
    //! \param[in] pEccDescriptor ECC Descriptor.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_bch_update_parameters(uint32_t u32NandDeviceNumber, const NandEccDescriptor_t *pEccDescriptor, uint32_t pageTotalSize);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Sets the BCH Flash Layout registers.
    //!
    //! \param[in] u32NandDeviceNumber NAND device number.
    //! \param[in] pEccDescriptor ECC Descriptor.
    //! \param[in] bTransfer2k TRUE to transfer only 2k, otherwise transfer full page.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_bch_SetFlashLayout(uint32_t u32NandDeviceNumber, const NandEccDescriptor_t * pEccDescriptor, bool bTransfer2k, uint32_t pageTotalSize);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Gets the BCH type code given the ECC level.
    //!
    //! \param[in] u32Level ECC Level
    //! \return BCH type
    ////////////////////////////////////////////////////////////////////////////////
    inline NandEccType_t ddi_bch_GetType(uint32_t u32Level)
    {
#if defined(STMP378x)
        return (NandEccType_t)((u32Level / 2) + kNandEccType_BCH0);
#else
        return kNandEccType_None;
#endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Gets the BCH level given the type code.
    //!
    //! \param[in] NandEccType_t Type code.
    //! \return BCH Level
    ////////////////////////////////////////////////////////////////////////////////
    inline uint32_t ddi_bch_GetLevel(NandEccType_t type)
    {
#if defined(STMP378x)
        return ((uint32_t)type - kNandEccType_BCH0) * 2;
#else
        return 0;
#endif
    }

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Determines the highest BCH ECC level that will fit in a page.
    //!
    //! Given a page with a certain data and metadata size, this function will
    //! compute the highest level of BCH ECC that will fit. It assumes that there
    //! will be 10 bytes of user metadata not reserved for ECC parity, and that
    //! the block size will always be 512 bytes.
    //!
    //! \param pageDataSize Number of bytes for data in the page.
    //! \param pageMetadataSize Number of bytes for user metadata and ECC parity.
    //! \param[out] resultEcc ECC descriptor filled in with the BCH ECC of
    //!     the highest level that will fit in the page.
    //! \retval SUCCESS The \a resultEcc parameter has been filled in.
    //! \retval ERROR_GENERIC There is not enough room in the page for any ECC.
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_bch_calculate_highest_level(uint32_t pageDataSize, uint32_t pageMetadataSize, NandEccDescriptor_t * resultEcc);
//@}

////////////////////////////////////////////////////////////////////////////////
//! \brief Clear the ECC Complete IRQ flag.
//!
//! This function clears the ECC Complete flag.  This should be done before each
//! transaction that uses the ECC.
////////////////////////////////////////////////////////////////////////////////
inline void ddi_gpmi_clear_ecc_complete_flag()
{
    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_COMPLETE_IRQ);
#if defined(STMP378x)
    HW_BCH_CTRL_CLR(BM_BCH_CTRL_COMPLETE_IRQ);
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Equality operator for ECC descriptors.
////////////////////////////////////////////////////////////////////////////////
inline bool NandEccDescriptor::operator == (const NandEccDescriptor & other) const
{
#if defined(STMP378x)
    return (eccType == other.eccType && eccTypeBlock0 == other.eccTypeBlock0 && u32SizeBlockN == other.u32SizeBlockN && u32SizeBlock0 == other.u32SizeBlock0 && u32NumEccBlocksN == other.u32NumEccBlocksN && u32MetadataBytes == other.u32MetadataBytes && u32EraseThreshold == other.u32EraseThreshold);
#else
    return (eccType == other.eccType);
#endif
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Inequality operator for ECC descriptors.
////////////////////////////////////////////////////////////////////////////////
inline bool NandEccDescriptor::operator != (const NandEccDescriptor & other) const
{
    return !(*this == other);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Query if ECC is enabled.
//!
//! \retval True The given ECC type is a valid ECC type where ECC is enabled.
//! \retval False The ECC type represents disabled ECC (no ECC).
////////////////////////////////////////////////////////////////////////////////
inline bool NandEccDescriptor::isEnabled() const
{
    return (eccType != kNandEccType_None);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Query if ECC is ECC8 (Reed-Solomon).
//!
//! The term "ECC8" here refers to the name of the Reed-Solomon ECC peripheral
//! block, not a specific ECC protection level.
//!
//! \retval True if ECC is Reed-Solomon
//! \retval False if ECC is either BCH or disabled.
////////////////////////////////////////////////////////////////////////////////
inline bool NandEccDescriptor::isECC8() const
{
    return (eccType == kNandEccType_RS4) || (eccType == kNandEccType_RS8);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Query if ECC is BCH.
//!
//! \retval True if ECC is BCH
//! \retval False if ECC is Reed-Solomon or disabled.
////////////////////////////////////////////////////////////////////////////////
inline bool NandEccDescriptor::isBCH() const
{
#if defined(STMP37xx) || defined(STMP377x)
    return false;
#elif defined(STMP378x)
    return (eccType >= kNandEccType_BCH0) && (eccType <= kNandEccType_BCH20);
#else
#error Must define STMP37xx, STMP377x or STMP378x
#endif
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Shorthand for calling EccTypeInfo::computeMask().
////////////////////////////////////////////////////////////////////////////////
inline uint32_t NandEccDescriptor::computeMask(uint32_t byteCount, uint32_t pageTotalSize, bool isWrite, bool readOnly2k, uint32_t * dataCount, uint32_t * auxCount) const
{
    return isEnabled() ? getTypeInfo()->computeMask(byteCount, pageTotalSize, isWrite, readOnly2k, this, dataCount, auxCount) : 0;
}

#endif // _DDI_ECC_H
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
