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
//! \defgroup ddi_media_nand_hal_internals
//! @{
//! \file   onfi_param_page.h
//! \brief  Definition of the ONFI parameter page structure.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__onfi_param_page_h__)
#define __onfi_param_page_h__

#include "types.h"
#include "drivers/media/nand/gpmi/ddi_nand_gpmi.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! \brief Value for the ONFI parameter page signature.
//!
//! The characters in this value are in reverse order on little endian systems because it is
//! assumed that the signature will be read as a whole word. If the system is big endian, then
//! the characters are in the natural order.
const uint32_t kOnfiSignature = 
#if __LITTLE_ENDIAN__
    'IFNO';
#else
    'ONFI';
#endif

//! \brief Minimum number of copies of the param page required by the ONFI specification.
//!
//! The param page copies comes one after the other when reading out the results of the
//! param page command.
const unsigned kMinOnfiParamPageCopies = 4;

#pragma pack(1)
/*!
 * \brief ONFI 2.2 parameter page.
 *
 * The parameter page is broken into several blocks, with enough room for future expansion
 * in each block. The total size is 256 bytes. This includes 88 bytes of vendor-defined fields
 * that are included simply as a byte array in this struct.
 */
struct OnfiParamPage
{
    //! \brief String length constants.
    enum
    {
        kManufacturerNameLength = 12,   //!< \brief Maximum number of characters in the manufacturer name field.
        kModelNameLength = 20   //!< \brief Maximum number of characters in the model name field.
    };
    
    /*!
     * \brief Bitfield describing the supported ONFI timing modes.
     *
     * This structure is used for several parameter page fields, so it is declared here and
     * referenced by name below.
     */
    struct TimingModeSupport
    {
        uint16_t timingMode0:1;
        uint16_t timingMode1:1;
        uint16_t timingMode2:1;
        uint16_t timingMode3:1;
        uint16_t timingMode4:1;
        uint16_t timingMode5:1;
        uint16_t _reserved:10;
    };
    
    //! \name Revision and Features Block
    //@{
    uint32_t signature; //!< Parameter page signature. 'ONFI' for big-endian, or 'IFNO' in little-endian.
    struct {
        uint16_t _reserved0:1;
        uint16_t supportsOnfi1_0:1; //!< Supports ONFI version 1.0.
        uint16_t supportsOnfi2_0:1; //!< Supports ONFI version 2.0.
        uint16_t supportsOnfi2_1:1; //!< Supports ONFI version 2.1.
        uint16_t supportsOnfi2_2:1; //!< Supports ONFI version 2.2.
        uint16_t _reserved1:11;
    } revision; //!< Supported versions of the ONFI spec.
    struct {
        uint16_t x16BusWidth:1;         //!< Supports 16-bit data bus width.
        uint16_t multiLunOperations:1;  //!< Supports multiple LUN operations.
        uint16_t nonsequentialPageProgramming:1;    //!< Supports non-sequential page programming.
        uint16_t interleavedWrite:1;    //!< Supports interleaved program and erase operations.
        uint16_t oddToEvenCopyback:1;   //!< Supports odd to even page Copyback.
        uint16_t noSynchronousInterface:1;  //!< Supports source synchronous.
        uint16_t interleavedRead:1;     //!< Supports interleaved read operations.
        uint16_t extendedParamPage:1;   //!< Supports extended parameter page.
        uint16_t pageRegisterClear:1;   //!< Supports program page register clear enhancement.
        uint16_t _reserved1:7;
    } featuresSupported;
    struct {
        uint16_t programPageCacheMode:1;
        uint16_t readCacheCommands:1;
        uint16_t getAndSetFeatures:1;
        uint16_t readStatusEnhanced:1;
        uint16_t copyback:1;
        uint16_t readUniqueID:1;
        uint16_t changeReadColumnEnhanced:1;
        uint16_t changeRowAddress:1;
        uint16_t smallDataMove:1;
        uint16_t resetLun:1;
        uint16_t _reserved:6;
    } optionalCommandsSupported;
    uint8_t _reserved5[2];
    uint16_t extendedParameterPageLength;
    uint8_t parameterPageCount;
    uint8_t _reserved0[17];
    //@}
    
    //! \name Manufacturer Information Block
    //@{
    char manufacturerName[kManufacturerNameLength];
    char modelName[kModelNameLength];
    uint8_t jedecManufacturerID;
    uint16_t dateCode;
    uint8_t _reserved1[13];
    //@}
    
    //! \name Memory Organization Block
    //@{
    uint32_t dataBytesPerPage;
    uint16_t spareBytesPerPage;
    uint32_t dataBytesPerPartialPage;   //!< Obsolete in ONFI 2.2.
    uint16_t spareBytePerPartialPage;   //!< Obsolete in ONFI 2.2.
    uint32_t pagesPerBlock;
    uint32_t blocksPerLun;
    uint8_t lunsPerChipEnable;
    struct {
        uint8_t row:4;
        uint8_t column:4;
    } addressCycles;
    uint8_t bitsPerCell;
    uint16_t maxBadBlocksPerLun;
    struct {
        uint8_t value;
        uint8_t exponent;
    } blockEndurance;   //!< The total number of erase/write cycles per block = (value * 10^exponent).
    uint8_t validBlocksAtBeginning;
    uint16_t validBlockEndurance;
    uint8_t programsPerPage;
    uint8_t partialProgrammingAttributes;   //!< Obsolete in ONFI 2.2.
    uint8_t eccBitsCorrectability;
    uint8_t interleavedAddressBits;
    struct {
        uint8_t overlappedInterleavingSupport:1;
        uint8_t noBlockAddressRestrictions:1;
        uint8_t programCacheSupported:1;
        uint8_t addressRestrictionsForCacheOperations:1;
        uint8_t readCacheSupported:1;
        uint8_t lowerBitXNORBlockAddressRestriction:1;
        uint8_t _reserved:2;
    } interleavedOperationAttributes;
    uint8_t _reserved2[13];
    //@}
    
    //! \name Electrical Parameters Block
    //@{
    uint8_t maxIOPinCapacitance; //!< Maximum I/O pad capacitance per chip enable (pF).
    TimingModeSupport timingModeSupport;    //!< Asynchronous timing mode support.
    TimingModeSupport cacheTimingModeSupport;    //!< Obsolete in ONFI 2.2.
    uint16_t tPROG; //!< Maximum page program time (µs).
    uint16_t tBERS; //!< Maximum block erase time (µs).
    uint16_t tR;    //!< Maximum page read time (µs).
    uint16_t tCCS;  //!< Minimum change column setup time (ns).
    TimingModeSupport sourceSynchronousTimingModeSupport;
    struct {
        uint8_t whichtCADToUse:1;    //!< 0 = tCADs (slow), 1 = tCADf (fast)
        uint8_t typicalCapacitanceValuesPresent:1;
        uint8_t supportsCLKStoppedForInput:1;
        uint8_t _reserved:5;
    } sourceSynchronousFeatures;
    uint16_t typicalCLKInputPinCapacitance; //!< (0.1 pF units)
    uint16_t typicalIOPinCapacitance; //!< (0.1 pF units)
    uint16_t typicalInputPinCapacitance; //!< (0.1 pF units)
    uint8_t maxInputPinCapacitance; //!< (pF)
    struct {
        uint8_t driverStrengthSettings:1;
        uint8_t overdrive1DriveStrength:1;
        uint8_t overdrive2DriveStrength:1;
        uint8_t _reserved:5;
    } driverStrengthSupport;
    uint16_t maxInterleavedtR;  //!< Maximum interleaved page read time (µs).
    uint16_t tADL;  //!< Program page register clear enhancement tADL value (ns).
    uint8_t _reserved4[8];
    //@}
    
    //! \name Vendor Block
    //@{
    uint16_t vendorRevision;
    uint8_t vendor[88];
    //@}
    
    uint16_t crc;   //!< CRC computed over bytes 0-253.

    //! \name String helpers
    //@{
    //! \brief Copies a string and truncates trailing spaces.
    static unsigned copyOnfiString(char * dest, const char * src, unsigned maxSrcLen);
    
    //! \brief Utility to copy the manufacturer name and format it as a C string.
    //! \param dest Buffer where the manufacterer name will be copied. Must be at least one
    //!     byte larger than #kManufacturerNameLength to have room for the terminating NULL byte.
    //! \see copyOnfiString()
    inline unsigned copyManufacturerName(char * dest) const { return copyOnfiString(dest, manufacturerName, sizeof(manufacturerName)); }

    //! \brief Utility to copy the model name and format it as a C string.
    //! \param dest Buffer where the model name will be copied. Must be at least one
    //!     byte larger than #kModelNameLength to have room for the terminating NULL byte.
    //! \see copyOnfiString()
    inline unsigned copyModelName(char * dest) const { return copyOnfiString(dest, modelName, sizeof(modelName)); }
    //@}
    
    //! \name Timings helpers
    //@{
    //! \brief Determine the highest supported timing mode.
    const NAND_Timing2_struct_t * getFastestAsyncTimings() const;
    //@}
    
    //! \name CRC
    //@{
    //! \brief Compute actual CRC-16 value.
    uint16_t computeCRC();
    //@}

};
#pragma pack()

//! \brief ONFI asynchronous timing mode definitions.
extern const NAND_Timing2_struct_t kOnfiAsyncTimingModeTimings[];

#endif // __onfi_param_page_h__
//! @}
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
