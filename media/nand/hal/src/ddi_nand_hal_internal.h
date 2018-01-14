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
//! \file   ddi_nand_hal_internal.h
//! \brief  Declarations internal to the NAND HAL.
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDI_NAND_HAL_INTERNAL_H
#define _DDI_NAND_HAL_INTERNAL_H

#include "errordefs.h"
#include "types.h"
#include "drivers/media/sectordef.h"
#include "components/telemetry/tss_logtext.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand_hal_types.h"
#include "drivers/media/nand/gpmi/ddi_nand_gpmi_dma.h"
#include "simple_mutex.h"
#include "device_name_table.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! Set to 1 to turn on verification of physical contiguity of all DMA buffers. Even when
//! this is enabled, it will only apply to debug builds.
#define NAND_HAL_VERIFY_PHYSICAL_CONTIGUITY 0

//! \brief Timeout Constants
//!
//! The following constants describe how much patience we have when waiting for
//! particular operations to finish.
//!
enum _nand_hal_timeouts
{
    kNandResetTimeout = 2000000,       //!< The time, in microseconds, to wait for a reset to finish. (2 sec)
    kNandReadPageTimeout = 500000,     //!< The time, in microseconds, to wait for a page read to finish. (0.5 sec)
    kNandWritePageTimeout = 1000000,   //!< The time, in microseconds, to wait for a page write to finish. (1 sec)
    kNandEraseBlockTimeout = 2000000   //!< The time, in microseconds, to wait for block erase to finish. (2 sec)
};

//! \brief Type 2 status byte masks.
enum _nand_hal_type_2_status
{
    kType2StatusPassMask = 0x01,        //!< 0=Page N Program Pass, 1=Fail
    kType2StatusCachePassMask = 0x02,   //!< 0=Page N-1 Program Pass, 1=Fail
    kType2StatusReadyMask = 0x20,       //!< 1=Ready, 0=Busy
    kType2StatusCacheReadyMask = 0x40,  //!< 1=Cache Ready, 0=Cache Busy
    kType2StatusWriteProtectMask = 0x80 //!< 0=Protected, 1=Unprotected
};

//! \brief Type 6 status byte masks.
enum _nand_hal_type_6_status
{
    kType6StatusPassMask = 0x01,        //!< 0=Program Pass, 1=Fail
    kType6StatusReadyMask = 0x40,       //!< 1=Ready, 0=Busy
    kType6StatusWriteProtectMask = 0x80 //!< 0=Protected, 1=Unprotected
};

//! \brief Toshiba PBA-NAND status byte masks.
enum _nand_hal_type_16_status
{
    kType16StatusPassMask = 0x01,           //!< 0=Page N Program Pass, 1=Fail
    kType16StatusCachePassMask = 0x02,      //!< 0=Page N-1 Program Pass, 1=Fail
    kType16StatusDistrict0PassMask = 0x02,  //!< For 0xf1 command. 0=District 0 Pass, 1=Fail
    kType16StatusDistrict1PassMask = 0x04,  //!< For 0xf1 command. 0=District 1 Pass, 1=Fail
    kType16StatusReadReclaimMask = 0x10,    //!< 1=Need Reclaim, 0=No Reclaim Needed
    kType16StatusDistrict0ReadReclaimMask = 0x10,   //!< For 0xf1 command. 1=Need Reclaim, 0=No Reclaim Needed
    kType16StatusDistrict1ReadReclaimMask = 0x20,   //!< For 0xf1 command. 1=Need Reclaim, 0=No Reclaim Needed
    kType16StatusReadyMask = 0x20,          //!< 1=Ready, 0=Busy
    kType16StatusCacheReadyMask = 0x40      //!< 1=Cache Ready, 0=Cache Busy
};

//! \brief Manufacturer ID Constants
//!
//! The first byte in the response to a Read ID command always identifies the
//! manufacturer. The following constants help interpret the manufacturer ID.
//!
//! \sa NandReadIdResponse_t
enum _nand_hal_manufacturer_ids
{
    kMakerIDMask = 0x0000ff,
    kSTMakerID = 0x20, //!< ST Microelectronics
    kHynixMakerID = 0xAD,
    kSamsungMakerID = 0xEC,
    kMicronMakerID = 0x2C,
    kIntelMakerID = 0x89,  //!< Intel uses Micron's fabs and ID sometimes. 2006
    kToshibaMakerID = 0x98,
    kMSystemsMakerID = 0x98,
    kRenesasMakerID = 0x07,
    kSandiskMakerID = 0x45
};

//! \brief Device ID Constants
//!
//! The second byte in the response to a Read ID command always identifies the
//! device (more or less - there is some ambiguity with some manufacturers).
//! The following constants help interpret the device ID.
//!
//! \sa NandReadIdResponse_t
enum _nand_hal_device_ids
{
    //! Device code for a Samsung 1 Gigabit SLC device.
    kSamsung1GbDeviceID = 0xF1,
    
    //! Device code for a Samsung 2 Gigabit SLC device.
    kSamsung2GbDeviceID = 0xDA,
    
    //! Device codes for Hynix ECC12 devices.
    kHynixD5DeviceID = 0xd5,

    //! Device codes for Hynix ECC12 devices.
    kHynixD7DeviceID = 0xd7,

    //! Device code for a Hynix ECC12 large (8GB/CE) device.
    kHynixLargeDeviceID = 0xde,
    
    //! Device code for a Micron ECC12 device.
    kMicronECC12DeviceID = 0xd7,
    
    //! Device code for a Micron ECC12 large (8GB/CE) device.
    kMicronECC12LargeDeviceID = 0xd9,
    
    //! Device code for Micron L63B 2GB/CE device
    kMicron2GBperCEDeviceID = 0x48,
    
    //! Device code for Micron L63B and L73A 4GB/CE device
    kMicron4GBperCEDeviceID = 0x68,
    
    //! Device code for Micron L63B and L74A 8GB/CE device
    kMicron8GBperCEDeviceID = 0x88,

    //! Device code for Micron L74A 16GB/CE device
    kMicron16GBperCEDeviceID = 0xA8
};

//! \brief Page Size Constants
//!
//! The fourth byte in the response to a Read ID command contains bits that
//! describe the device's page size. Note that for some 8K page NANDs (notably
//! Toshiba), the page size field value for 8K pages is the same as the value
//! for 4K pages on most 4K page NANDs. The #kPageSize4K value is the value
//! that 4K page devices use (thus, it can mean 8K on the 8K page devices).
//! Confusing enough?
//!
//! \sa NandReadIdResponse_t
enum _nand_hal_page_size_decode
{
    kPageSize1K = 0x00, //!< 1K pages.
    kPageSize2K = 0x01, //!< 2K pages.
    kPageSize4K = 0x02, //!< 4K pages.
    kPageSize8K = 0x03  //!< 8K pages.
};

//! \brief Type of NAND constants.
//!
//! These constants are for the _nand_hal_id_decode::TypeOfNAND field. They only apply to
//! Toshiba PBA-NAND devices.
enum _nand_hal_type_of_nand
{
    kRawNAND = 0,   //!< Device is a raw NAND.
    kPBANAND = 1    //!< Device is a PBA-NAND.
};

#pragma pack(1)
//! \brief Describes the response to a Read ID command.
typedef union _nand_hal_id_decode {
    struct
    {
        //! \name Byte 1
        //! @{
        uint8_t MakerCode;  //!< Identifies the manufacturer.
        //! @}
        
        //! \name Byte 2
        //! @{
        uint8_t DeviceCode; //!< Identifies the device.
        //! @}
        
        //! \name Byte 3
        //! @{
        uint8_t InternalChipNumber     : 2; //!< Number of die = (1 << n)
        uint8_t CellType               : 2; //!< Number of bits per memory cell = ( 1 << (n+1) )
        uint8_t NumOfSimultProgPages   : 2; //! 1, 2, 4, 8
        uint8_t IntProgBetweenChips    : 1;	//!< 0 == Not supported
        uint8_t CacheProgram           : 1; //!< 0 == Not supported
        //! @}
        
        //! \name Byte 4
        //! @{
        uint8_t PageSize               : 2;    //!< Page size in bytes = (1 << n) * 1024
        uint8_t RedundantAreaSize      : 2;    //!< Redundant area bytes per 512 data bytes = 8 * (1 << n)
        uint8_t BlockSize              : 2;    //!< Block size in bytes = 64 * 1024 * (1 << n)
//         uint8_t RedundantAreaSize2     : 1;    //!< Additional bit for redundant area size. For some older NANDs, this bit was the organization field (0 == x8, 1 == x16).
//         uint8_t BlockSize2             : 1;    //!< Additional bit for block size. For some older Samsung NANDs, this bit is the Samsung High-speed Serial Access flag (0 == 50/30ns, 1 == 25ns).
       uint8_t Organization           : 1;    //!< 0 == x8, 1 == x16
       uint8_t SerialAccess  : 1;    //!< 0 == 35ns, 1 == 25ns
        //! @}
        
        //! \name Byte 5
        //! @{
        uint8_t Reserved2        : 2;           //!< Unspecified.
        uint8_t PlaneNumber            : 2;    //!< \brief # of planes total = (1 << n)
                                                //!
                                                //! This field will contain the
                                                //! number of planes per die * the
                                                //! number of die per chip enable.
                                                //! Examples;
                                                //!     - 1 plane, 1 die; value = 1
                                                //!     - 2 plane, 1 die; value = 2
                                                //!     - 2 plane, 2 die; value = 4
                                                //!     - 2 plane, 2 die, 2 chip enable; value = 2
                                                //!
                                                //! In case of multi-stacked device,
                                                //! the "plane number" will show the
                                                //! total number of planes of the
                                                //! packaged device and not the
                                                //! number of planes per die.
        uint8_t ECCLevel	            : 3;    //!< The minimum required ECC level for this device.
                                                //!     - 0 = 1 bit/512 byte
                                                //!     - 1 = 2 bit/512 byte
                                                //!     - 2 = 4 bit/512 byte
                                                //!     - 3 = 8 bit/512 byte
                                                //!     - 4 = 16 bit/512 byte
                                                //!     - 5 = reserved
                                                //!     - 6 = reserved
                                                //!     - 7 = vendor specific
                                                //!
                                                //! Some older NANDs used these bits to represent
                                                //! the plane size (# of bytes per plane =
                                                //! 64 * 1024 * 1024 * (1 << n)).
//        uint8_t PlaneSize              : 3;    //!< # of bytes per plane = 64 * 1024 * 1024 * (1 << n)
        uint8_t TypeOfNAND              : 1;    //!< For Toshiba PBA-NAND only:
                                                //!  - 0=Raw NAND
                                                //!  - 1=PBA-NAND
        //! @}
        
        //! \name Byte 6
        //! @{
        uint8_t DeviceVersion          : 3;    //!< Shows technology and process specific information
        uint8_t ToshibaHighSpeedMode   : 1;    //!< 0 == Not supported
        uint8_t Reserved4              : 2;
        uint8_t EDO					: 1;    //!< 0 == Not supported
        uint8_t Interface				: 1;    //!< 0 = SDR; 1 = DDR
        //! @}
    };
    uint8_t data[6];
} NandReadIdResponse_t;
#pragma pack()

//! \name Bit masks and constants for Samsung 6 byte ID Nands.
//@{

//! Page size = 8K
#define SAMSUNG_6BYTE_ID_PAGESIZE_8K 0x02

#define SAMSUNG_6BYTE_ID_ECCLEVEL_ECC8_MASK 0x70
#define SAMSUNG_6BYTE_ID_ECCLEVEL_ECC8 0x03

//! ECC = 24bit/1KB. The max we can support is 16bit/512B
#define SAMSUNG_6BYTE_ID_ECCLEVEL_ECC24 0x05

#define SAMSUNG_6BYTE_ID_DEVICEVERSION_40NM 0x01
//@}

//! \name Toshiba ID constants
//@{
//! Toshiba 8K page 6th byte value.
const uint8_t kToshiba8KPageIDByte6 = 0x54;

//! Toshiba PBA-NAND 6th byte value.
const uint8_t kToshiba32nmPBANANDIDByte6 = 0x55;

//! Toshiba second generation (24nm) PBA-NAND 6th byte value.
const uint8_t kToshiba24nmPBANANDIDByte6 = 0x56;
//@}

//! \name Hynix ID constants
//@{
//! Read ID Byte 4 for a Hynix ECC12 device.
#define HYNIX_ECC12_DEVICE_READ_ID_BYTE_4 (0x25)
//@}

//! \name Micron ID constants
//@{
//! Read ID Byte 5 for a Micron ECC12 device.
const uint8_t kMicronEcc12IDByte5 = 0x84;

//! Read ID Byte 4 for a Micron L73A device.
const uint8_t kMicronL73AIdByte4 = 0x4a;

//! Read ID Byte 4 for a Micron L74A device.
const uint8_t kMicronL74AIdByte4 = 0x4b;
//@}

//! \brief NAND hardware command codes.
enum _nand_hal_command_codes
{
    eNandProgCmdReadID                    = 0x000090,
    eNandProgCmdReadID2                   = 0x000091,
    eNandProgCmdReadStatus                = 0x000070,
    eNandProgCmdReset                     = 0x0000ff,
    eNandProgCmdSerialDataInput           = 0x000080,   //!< Page Program/Cache Program
    eNandProgCmdRead1                     = 0x000000,   //!< Read or Read for CopyBack
    eNandProgCmdRead1_2ndCycle            = 0x000030,   //!< Second Cycle for Read (Type 2 NANDs)
    eNandProgCmdReadForCopyBack_2ndCycle  = 0x000035,   //!< Second Cycle for Read for Copy Back
    eNandProgCmdReadForCacheCopyback_2nd  = 0x00003A,
    eNandProgCmdPageProgram               = 0x000010,   //!< Second cycle for wSerialDataInput for Page Program
    eNandProgCmdPartialPageProgram        = 0x000011,   //!< Command to terminate partial page program.
    eNandProgCmdCacheProgram              = 0x000015,   //!< Second cycle for wSerialDataInput for Cache Program
    eNandProgCmdCopyBackProgram           = 0x000085,
    eNandProgCmdCopyBack2Program          = 0x00008C,
    eNandProgCmdCopyBackProgram_2ndCycle  = 0x000010,   //!< Second cycle for Copy Back Program
    eNandProgCmdAddressInput              = 0x000060,
    eNandProgCmdBlockErase                = 0x000060,
    eNandProgCmdBlockErase_2ndCycle       = 0x0000d0,
    eNandProgCmdRandomDataIn              = 0x000085,
    eNandProgCmdRandomDataOut             = 0x000005,
    eNandProgCmdRandomDataOut_2ndCycle    = 0x0000E0,
    eNandProgCmdReadMultiPlaneStatus      = 0x000071,   //!< MLC MultiPlane
    eNandProgCmdReadErrorStatus           = 0x000072,   //!< MLC Single Plane Error Status
    eNandProgCmdReadMultiPlaneErrorStatus = 0x000073,   //!< MLC MultiPlane Error Status.
    eNandProgCmdMultiPlaneWrite           = 0x000011,
    eNandProgCmdStatusModeReset           = 0x00007F,
    eNandProgCmdMultiPlaneRead_2ndCycle   = 0x000031,
    eNandProgCmdPageDataOutput            = 0x000006,
    eNandProgCmdPBAReliableMode           = 0x0000da,   //!< PBA-NAND command to enter reliable mode.
    eNandProgCmdPBANormalMode             = 0x0000df,   //!< PBA-NAND command to return to normal mode.
    eNandProgCmdPBAModeChange             = 0x000057,   //!< PBA-NAND command to change modes.
    eNandProgCmdPBAReadMode1              = 0x0000a1,   //!< PBA-NAND Read Mode 1 (Normal read)
    eNandProgCmdPBAReadMode2              = 0x0000a2,   //!< PBA-NAND Read Mode 2 (Faster read)
    eNandProgCmdPBAReadMode3              = 0x0000a3,   //!< PBA-NAND Read Mode 3 (Pre-read)
    eNandProgCmdPBAReadMode4              = 0x0000a4,   //!< PBA-NAND Read Mode 4 (Silent read)
    eNandProgCmdPBAEnableSleepMode        = 0x0000a5,   //!< PBA-NAND Enable Sleep Mode
    eNandProgCmdPBADisableSleepMode       = 0x0000b5,   //!< PBA-NAND Disable Sleep Mode
    eNandProgCmdPBACheckSleepModeState    = 0x0000b6,   //!< PBA-NAND command to read the current sleep mode state
    eNandProgCmdPBAStatusRead2            = 0x0000f1,   //!< PBA-NAND Multi-plane Status Read.
    eNandProgCmdPBAMultiPlaneDataInput    = 0x000081,   //!< PBA-NAND command to start data input for the second page of a multiplane page program sequence.
    eNandProgCmdReadONFIParamPage         = 0x0000ec,   //!< Read the parameter page from an ONFI NAND.
    eNandProgCmdMultiPlaneBlockErase      = 0x00ffff,
};

//! \name Bad block marking constants
//@{

//! \brief Offset into the metadata for the bad block marker byte.
const unsigned kBadBlockMarkerMetadataOffset = 0;

//! \brief The value of the bad block marker byte for a good block.
const uint8_t kBadBlockMarkerValidValue = 0xff;

//@}

//! \brief The standard maximum percentage of blocks that may go bad.
const unsigned kDefaultMaxBadBlockPercentage = 5;

//! \name ONFI constants
//@{

//! \brief Address to read the ONFI ID from using the Read ID command.
const uint8_t kOnfiReadIDAddress = 0x20;

//@}

/*!
 * \brief Global context information for the HAL.
 *
 * This structure contains all of the important global information for the HAL,
 * such as the number of active chip selects. It also includes the all-important pointers to
 * the NAND object for each chip select. In addition, there is information about the
 * NANDs that is common to all chip selects, such as the Read ID command results and the
 * NAND parameters struct.
 */
typedef struct NandHalContext {
    TX_MUTEX serializationMutex;    //!< The mutex that serializes all access to the HAL.
    NandReadIdResponse_t readIdResponse;    //!< Read-ID response from the first chip select.
    unsigned chipSelectCount;   //!< Number of active chip selects.
    uint32_t totalBlockCount;   //!< Combined number of blocks from all chip selects.
    NandParameters_t parameters;    //!< Shared description of NAND properties.
    NandDeviceNameTable::TablePointer_t nameTable; //!< Pointer to optional device name table.
    NandPhysicalMedia * nands[MAX_NAND_DEVICES];    //!< Pointers to the individual NAND objects.
    NandDma::ReadEccData readDma;   //!< Regular page read DMA descriptor.
    NandDma::ReadEccData readMetadataDma;   //!< Metadata read DMA descriptor.
    NandDma::ReadEccData readFirmwareDma;   //!< Firmware page read DMA descriptor. Not used if the firmware page size is the same as the regular page size.
    NandDma::WriteEccData writeDma; //!< Page write DMA descriptor.
    NandDma::ReadStatus statusDma;  //!< Status read DMA descriptor. Chained onto several other DMAs, such as writes and erases.
    NandDma::BlockErase eraseDma;   //!< Block erase DMA descriptor.
} NandHalContext_t;

// Forward declaration of the context global for use in NandHalMutex.
extern NandHalContext_t g_nandHalContext;

/*!
 * \brief Automatic mutex locker for the NAND HAL serialization mutex.
 */
class NandHalMutex : public SimpleMutex
{
public:
    //! \brief Constructor.
    NandHalMutex() : SimpleMutex(g_nandHalContext.serializationMutex) {}
    
    //! \brief Destructor.
    ~NandHalMutex() {}
};

/*!
 * \brief Utility class to cleanly and safely enable and disable writes.
 */
class EnableNandWrites
{
public:
    //! \brief Constructor; enables writes.
    EnableNandWrites(CommonNandBase * nand)
    :   m_nand(nand)
    {
        assert(nand);
        m_nand->enableWrites();
    }
    
    //! \brief Destructor; disables writes.
    ~EnableNandWrites()
    {
        m_nand->disableWrites();
    }

private:
    //! The NAND object to enable writes on.
    CommonNandBase * m_nand;
};

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

#pragma alignvar(32)
extern uint8_t g_nandHalResultBuffer[];

#if DEBUG
//! This global is used to insert read errors for testing purposes while
//! debugging. Set it to the error code you want to be returned from the
//! next HAL read function call. After that error is returned once, this global
//! will be reset to 0. As long as the value is zero, this global has no
//! effect whatsoever.
extern RtStatus_t g_nand_hal_insertReadError;
#endif // DEBUG

#if DEBUG && NAND_HAL_VERIFY_PHYSICAL_CONTIGUITY
extern void _verifyPhysicalContiguity(const void * buffer, uint32_t len);
#else
inline void _verifyPhysicalContiguity(const void * buffer, uint32_t len) {}
#endif // DEBUG

#endif // #ifndef _DDI_NAND_HAL_INTERNAL_H
//! @}
