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
////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal_internals
//! @{
//! \file ddi_nand_hal_tables.cpp
//! \brief specific timings, sizes, and api's per flash ID.
//!
//! This file includes the structures created for each
//! supported NAND ID.
////////////////////////////////////////////////////////////////

#include "ddi_nand_hal_tables.h"

#if !defined(__ghs__)
#pragma mark -Sector Descriptors-
#endif
///////////////////////////////////////////////////////////////////////////////
//! \name Sector Descriptors
//@{
///////////////////////////////////////////////////////////////////////////////

//! \brief Standard 2112 byte sector.
const NandPageDescriptor_t  Type2_SectorDescriptor = {
    LARGE_SECTOR_TOTAL_SIZE,    // wTotalSize
    LARGE_SECTOR_DATA_SIZE,     // wDataSize
    LARGE_SECTOR_REDUNDANT_SIZE // pageMetadataSize
};

//! \brief Samsung 4K page + 128 RA (4224 bytes).
const NandPageDescriptor_t  Type8_SectorDescriptor = {
    SAMSUNG_XL_SECTOR_TOTAL_SIZE,       // wTotalSize
    XL_SECTOR_DATA_SIZE,                // wDataSize
    SAMSUNG_XL_SECTOR_REDUNDANT_SIZE    // pageMetadataSize
};

//! \brief Toshiba 4K page + 218 RA (4314 bytes).
const NandPageDescriptor_t  Type9_SectorDescriptor = {
    XL_SECTOR_TOTAL_SIZE,       // wTotalSize
    XL_SECTOR_DATA_SIZE,        // wDataSize
    XL_SECTOR_REDUNDANT_SIZE    // pageMetadataSize
};

//! \brief Toshiba 8K page + 376 RA (8568 bytes).
const NandPageDescriptor_t  Type11_SectorDescriptor = {
    8568,       // wTotalSize
    8192,        // wDataSize
    376    // pageMetadataSize
};

#if defined(STMP378x)
//! \brief Hynix and Micron 4K page + 224 RA (4320 bytes).
const NandPageDescriptor_t  Type12_SectorDescriptor = {
    4320,       // wTotalSize
    4096,       // wDataSize
    224         // pageMetadataSize
};

//! \brief Samsung 8K page + 436 RA (8628 bytes).
const NandPageDescriptor_t  Type15_SectorDescriptor = {
    8628,       // wTotalSize
    8192,       // wDataSize
    436         // pageMetadataSize
};
#elif !defined(STMP37xx) && !defined(STMP377x)
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

//! \brief Toshiba 8K page + 32 RA (8224 bytes).
const NandPageDescriptor_t  Type16_SectorDescriptor = {
    8224,       // wTotalSize
    8192,       // wDataSize
    32         // pageMetadataSize
};
//@}

#if !defined(__ghs__)
#pragma mark -ECC Descriptors-
#endif
///////////////////////////////////////////////////////////////////////////////
//! \name Ecc Descriptors
//@{
///////////////////////////////////////////////////////////////////////////////

//! 2K + 64
const NandEccDescriptor_t   EccDescriptor_D2k_RA64_ECC4 = {
#if defined(STMP37xx) || defined(STMP377x)
    kNandEccType_RS4            // eccType
#elif defined(STMP378x)
    kNandEccType_RS4,           // eccType (BlockN)
    kNandEccType_RS4,           // eccTypeBlock0
    0,                          // u32SizeBlockN
    0,                          // u32SizeBlock0
    0,                          // u32NumEccBlocksN
    0,                          // u32MetadataBytes
    0                           // u32EraseThreshold
#else
    #error Must define STMP37xx, STMP377x or STMP378x
#endif
};

//! 2K + 64 / 4K + 128
//!
//! Special descriptor to Type 8 and 10 NANDs. These devices have 4224 byte pages and
//! require 4-bit ECC. But on chips without BCH we have to use two 2112 byte RS4 subpages
//! to get the hardware to comply. When BCH is available, there is enough metadata space
//! to use BCH8.
const NandEccDescriptor_t   EccDescriptor_D4k_RA128_ECC4_BCH8 = {
#if defined(STMP37xx) || defined(STMP377x)
    kNandEccType_RS4            // eccType
#elif defined(STMP378x)
    kNandEccType_BCH8,           // eccType (BlockN)
    kNandEccType_BCH8,           // eccTypeBlock0
    512,                          // u32SizeBlockN
    512,                          // u32SizeBlock0
    7,                          // u32NumEccBlocksN
    10,                          // u32MetadataBytes
    0                           // u32EraseThreshold
#else
    #error Must define STMP37xx, STMP377x or STMP378x
#endif
};

//! 4K + 218
const NandEccDescriptor_t   EccDescriptor_D4k_RA218_ECC8 = {
#if defined(STMP37xx) || defined(STMP377x)
    kNandEccType_RS8            // eccType
#elif defined(STMP378x)
    kNandEccType_RS8,           // eccType (BlockN)
    kNandEccType_RS8,           // eccTypeBlock0
    0,                          // u32SizeBlockN
    0,                          // u32SizeBlock0
    0,                          // u32NumEccBlocksN
    0,                          // u32MetadataBytes
    0                           // u32EraseThreshold
#else
    #error Must define STMP37xx, STMP377x or STMP378x
#endif
};

//! ECC14 8K + 376
const NandEccDescriptor_t   EccDescriptor_D8k_RA376_ECC14 = {
#if defined(STMP37xx) || defined(STMP377x)
    kNandEccType_None           // eccType
#elif defined(STMP378x)
    kNandEccType_BCH14,         // eccType (BlockN)
    kNandEccType_BCH14,         // eccTypeBlock0
    512,                        // u32SizeBlockN
    512,                        // u32SizeBlock0
    15,                         // u32NumEccBlocksN
    10,                         // u32MetadataBytes
    0                           // u32EraseThreshold
#else
    #error Must define STMP37xx, STMP377x or STMP378x
#endif
};

#if defined(STMP378x)
//! ECC16 8K + 436
const NandEccDescriptor_t   EccDescriptor_D8k_RA436_ECC16 = {
    kNandEccType_BCH16,         // eccType (BlockN)
    kNandEccType_BCH16,         // eccTypeBlock0
    512,                        // u32SizeBlockN
    512,                        // u32SizeBlock0
    15,                          // u32NumEccBlocksN
    10,                         // u32MetadataBytes
    0                           // u32EraseThreshold
};
//! ECC16 4K + 224
const NandEccDescriptor_t   EccDescriptor_D4k_RA224_ECC16 = {
    kNandEccType_BCH16,         // eccType (BlockN)
    kNandEccType_BCH16,         // eccTypeBlock0
    512,                        // u32SizeBlockN
    512,                        // u32SizeBlock0
    7,                          // u32NumEccBlocksN
    10,                         // u32MetadataBytes
    0                           // u32EraseThreshold
};
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

//! 4K + 218 page - BCH12 (378x), RS8 (37xx, 377x)
const NandEccDescriptor_t   EccDescriptor_D4k_RA218_BCH12_RS8 = {
#if defined(STMP37xx) || defined(STMP377x)
    kNandEccType_RS8            // eccType
#elif defined(STMP378x)
    kNandEccType_BCH12,         // eccType (BlockN)
    kNandEccType_BCH12,         // eccTypeBlock0
    512,                        // u32SizeBlockN
    512,                        // u32SizeBlock0
    7,                          // u32NumEccBlocksN
    10,                         // u32MetadataBytes
    0                           // u32EraseThreshold
#else
    #error Must define STMP37xx, STMP377x or STMP378x
#endif
};
//@}

#if !defined(__ghs__)
#pragma mark -Type Descriptors-
#endif
///////////////////////////////////////////////////////////////////////////////
//! \name Type Descriptors
//@{
///////////////////////////////////////////////////////////////////////////////

//! Type 2 - Small Addressing - Large Page SLC
//!    - Reed-Solomon ECC4
const NandTypeDescriptor_t   Type2SmallSub_NAND = {
    &Type2_SectorDescriptor,        // *pSectorDescriptor
    64,                             // pagesPerBlock
    2,                              // columnAddressBytes
    2,                              // rowAddressBytes
    kNandType2,                     // NandType
    kNandSLC,                       // cellType
    1                               // planesPerDie
};

//! Type 2 - Large Addressing - Large Page SLC
//!    - Reed-Solomon ECC4
const NandTypeDescriptor_t   Type2LargeSub_NAND = {
    &Type2_SectorDescriptor,        // *pSectorDescriptor
    64,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType2,                     // NandType
    kNandSLC,                       // cellType
    1                               // planesPerDie
};

//! Type 5 - Toshiba/Sandisk Large Page MLC
//!    - 128 pages/block
//!    - Has cache
//!    - 2K pages
//!    - No partial writes
//!    - Internal copy-back
//!    - Reed-Solomon ECC4
const NandTypeDescriptor_t   Type5Sub_NAND = {
    &Type2_SectorDescriptor,        // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType5,                     // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

//! Type 6 - Samsung Large Page MLC
//!    - Like Type 5, but no cache
//!    - 128 pages/block
//!    - 2K pages
//!    - No internal copy-back
//!    - Reed-Solomon ECC4
const NandTypeDescriptor_t   Type6Sub_NAND = {
    &Type2_SectorDescriptor,        // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType6,                     // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

//! Type 7 - Samsung, Micron, and Intel SLC
//!    - Like Type 6, but SLC
//!    - Region split between odd/even
//!    - 128 pages/block
//!    - 2K pages
//!    - No internal copy-back
//!    - Reed-Solomon ECC4
const NandTypeDescriptor_t   Type7Sub_NAND = {
    &Type2_SectorDescriptor,        // *pSectorDescriptor
    64,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType7,                     // NandType
    kNandSLC,                       // cellType
    2                               // planesPerDie
};

//! Type 8 - Samsung MLC
//!    - Like Type 6, but 4K pages
//!    - Region split between odd/even
//!    - 128 pages/block
//!    - 4224 byte pages
//!    - Has internal copy-back
//!    - Reed-Solomon ECC4
const NandTypeDescriptor_t   Type8Sub_NAND = {
    &Type8_SectorDescriptor,        // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType8,                     // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

//! Type 9 - Toshiba Large Page MLC
//!    - 128 pages/block
//!    - Has cache
//!    - 4314 byte pages
//!    - No partial writes
//!    - Internal copy-back
//!    - Reed-Solomon ECC8
const NandTypeDescriptor_t   Type9Sub_NAND = {
    &Type9_SectorDescriptor,        // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType9,                     // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

//! Type 10 - Samsung SLC
//!    - Like Type 6, but 4K pages
//!    - Region split between odd/even
//!    - 64 pages/block
//!    - 4224 byte pages
//!    - Has internal copy-back
//!    - Reed-Solomon ECC4
const NandTypeDescriptor_t   Type10Sub_NAND = {
    &Type8_SectorDescriptor,        // *pSectorDescriptor
    64,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType10,                    // NandType
    kNandSLC,                       // cellType
    1                               // planesPerDie
};

//! Type 11 - Toshiba Large Page MLC
//!    - 128 pages/block
//!    - Has cache
//!    - 8568 byte pages
//!    - No partial writes
//!    - Internal copy-back
//!    - 8 bit/512 byte or 24 bit/1024 byte
const NandTypeDescriptor_t Type11Sub_NAND = {
    &Type11_SectorDescriptor,        // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType11,                     // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

//! Type 12 - Hynix MLC ECC12
//!    - 128 pages/block
//!    - Has cache
//!    - 4320 byte pages (224 RA)
//!    - No partial writes
//!    - Internal copy-back
//!    - nominally 12 bit/512 byte ECC (BCH), although sometimes issued by the manufacturer as an ECC8 device
const NandTypeDescriptor_t Type12Sub_NAND = {
#if defined(STMP378x)
    &Type12_SectorDescriptor,       // *pSectorDescriptor
#elif defined(STMP37xx) || defined(STMP377x)
    // For STMP chips with only R-S ECC engines, the sector descriptor
    // must be configured as 4k+218 (i.e. Type9 sector).
    &Type9_SectorDescriptor,       // *pSectorDescriptor
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType12,                    // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

//! Type 13 - Micron MLC ECC12
//!    - 128 pages/block
//!    - Has cache
//!    - 4314 byte pages (218 RA)
//!    - No partial writes
//!    - Internal copy-back
//!    - ideally 12 bit/512 byte ECC (BCH)
const NandTypeDescriptor_t Type13Sub_NAND = {
    &Type9_SectorDescriptor,       // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType13,                    // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

//! Type 14 - Micron MLC ECC12
//!    - 256 pages/block
//!    - Has cache
//!    - 4320 byte pages (224 RA), but for 377x and 37xx we treat it as 4k+218 (Type9 page)
//!    - No partial writes
//!    - Internal copy-back
//!    - nominally 12 bit/512 byte ECC (BCH), although sometimes issued by the manufacturer as an ECC8 device
const NandTypeDescriptor_t Type14Sub_NAND = {
#if defined(STMP378x)
    &Type12_SectorDescriptor,       // *pSectorDescriptor
#elif defined(STMP37xx) || defined(STMP377x)
    // For STMP chips with only R-S ECC engines, the sector descriptor
    // must be configured as 4k+218 (i.e. Type9 sector).
    &Type9_SectorDescriptor,       // *pSectorDescriptor
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif
    256,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType14,                    // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};

#if defined(STMP378x)
//! Type 15 - Samsung MLC ECC16
//!    - 128 pages/block
//!    - Has cache
//!    - 8628 byte pages (436 RA)
//!    - No partial writes
//!    - Internal copy-back
//!    - ideally 16 bit/512 byte ECC (BCH)
const NandTypeDescriptor_t Type15Sub_NAND = {
    &Type15_SectorDescriptor,       // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType15,                    // NandType
    kNandMLC,                       // cellType
    1                               // planesPerDie
};
#elif defined(STMP37xx) || defined(STMP377x)
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

//! Type 16 - Toshiba PBA-NAND
//!    - 128 pages/block
//!    - Has cache
//!    - 8224 byte pages (32 RA)
//!    - No partial writes
//!    - Internal copy-back
//!    - Built-in ECC
const NandTypeDescriptor_t Type16Sub_NAND = {
    &Type16_SectorDescriptor,       // *pSectorDescriptor
    128,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType16,                    // NandType
    kNandMLC,                       // cellType
    2                               // planesPerDie
};

//! Type 16 - 24nm Toshiba PBA-NAND
//!    - 256 pages/block
//!    - Has cache
//!    - 8224 byte pages (32 RA)
//!    - No partial writes
//!    - Internal copy-back
//!    - Built-in ECC
const NandTypeDescriptor_t Type16Sub_24nm_NAND = {
    &Type16_SectorDescriptor,       // *pSectorDescriptor
    256,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType16,                    // NandType
    kNandMLC,                       // cellType
    2                               // planesPerDie
};

#if defined(STMP378x)
//! Type 17 - Micron MLC 4K page ECC16
//!    - 256 pages/block
//!    - Has cache
//!    - 4320 byte pages (224 RA)
//!    - Internal copy-back
//!    - 16 bit/512 byte ECC (BCH)
const NandTypeDescriptor_t Type17Sub_NAND = {
    &Type12_SectorDescriptor,       // *pSectorDescriptor
    256,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType17,                    // NandType
    kNandMLC,                       // cellType
    2                               // planesPerDie
};

//! Type 18 - Micron MLC ECC16 8K page
//!    - 256 pages/block
//!    - Has cache
//!    - 8640 byte pages (448 RA)
//!      (using block descriptor for 8192+436)
//!    - Internal copy-back
//!    - 16 bit/512 byte ECC (BCH)
const NandTypeDescriptor_t Type18Sub_NAND = {
    &Type15_SectorDescriptor,       // *pSectorDescriptor
    256,                             // pagesPerBlock
    2,                              // columnAddressBytes
    3,                              // rowAddressBytes
    kNandType18,                    // NandType
    kNandMLC,                       // cellType
    2                               // planesPerDie
};
#elif defined(STMP37xx) || defined(STMP377x)
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif
//@}

#if !defined(__ghs__)
#pragma mark -Device Code Maps-
#endif
///////////////////////////////////////////////////////////////////////////////
//! \name Device Code to NAND Descriptor Maps
//!
//! These device code maps associate Device/Manufacturer IDs with NAND descriptors,
//! to specify their timing and size parameters. Because many NANDs have the same
//! device code values, we have a number of separate device code maps, typically
//! one for each NAND type. The HAL function nand_hal_select_device_code_map()
//! contains the logic to select which device code map to search based on the
//! Read ID command results.
//!
//! Typically there is as single table entry for a NAND family whose parts differ
//! only in the number of chip enables in the package. The NANDInitDescriptor contains
//! the TotalBlocks and TotalInternalDice for the 1CE part number. This assumes that
//! the number of blocks and dice per chip enable are the same for each device.
//!
//! \par Calculating table entry timings
//!
//! First grab the Tds (Data Setup), Tdh (Data Hold), Tas (Address Setup), and Tah
//! (Address Hold) times from the datasheet.  Although Tah isn't a parameter we can
//! adjust, we can adjust Tdh which is the hold time for everything.  Therefore, Tdh
//! must be the greater of Tdh and Tah. Plug the values into the spreadsheet named 
//! nand_analysis_template.xls. Trea, Trhoh and Trloh are also required from the
//! datasheet. The resulting calculation will result in a DSAMPLE_TIME which should
//! be the 2nd parameter of the structure. If Trhoh and Trloh are not available, use
//! Toh and Thrz.
//!
//! The HAL will automatically adjust TSU, TDS, and TDH values listed in the
//! #MK_NAND_TIMINGS_STATIC macros at runtime for NANDs with multiple chip-enable
//! lines. The amount of adjustment is shown in the table here:
//! <table>
//! <tr><th># Chip-enables</th><th>Runtime Adjustment</th></tr>
//! <tr><td>1</td><td>0 nsec</td></tr>
//! <tr><td>2</td><td>+5 nsec</td></tr>
//! <tr><td>4</td><td>+10 nsec</td></tr>
//! </table>
//!
//! Thus, for a family of NANDs with different parts with different quantities
//! of chip-enable lines, the timings are increased for the parts with
//! more chip-enables.  This potentially allows the timings to be set here for
//! the one-chip-enable part, and to be compatible at runtime with slower
//! two- and four- chip-enable parts.
//!
//! For some reason, when Tsample time is set to the mid point between its min and
//! max, some of the nands do not work properly. In these cases, Dsample time of 0
//! works just fine. One way to alleviate this problem is to make sure that at 96MHz,
//! the Dsample time is set to 1 and at slower speed (60Mhz), Dsample will have a
//! value of 0.
//////////////////////////////////////////////////////////////////////////////
//@{

//! \brief Name table for Samsung K9F1G08U0M.
NandDeviceNameTable::TableEntry_t kSamsungType2K9F1GNames[] = { DEVNAME_1CE_END("K9F1G08U0M") };

//! \brief Name table for Samsung K9F2G08U0M.
NandDeviceNameTable::TableEntry_t kSamsungType2K9F2GNames[] = { DEVNAME_1CE_END("K9F2G08U0M") };

//! \brief Type 2 Device Code Map
//! \note Devices with pages smaller than 2048 bytes are not supported.
const NandDeviceCodeMap_t Type2DescriptorIdList[] =
{
    // 128 MB NANDs    ST Micro NAND01GW3
    {0xf120, 1, 1024, &Type2SmallSub_NAND, MK_NAND_TIMINGS_STATIC(25, 6, 30, 20), &EccDescriptor_D2k_RA64_ECC4},
    
    // 128 MB NANDs    Hynix
    {0xf1ad, 1, 1024, &Type2SmallSub_NAND, MK_NAND_TIMINGS_STATIC(25, 6, 45, 30), &EccDescriptor_D2k_RA64_ECC4},
    
    // 128 MB NANDs    Micron
    {0xf12c, 1, 1024, &Type2SmallSub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 30, 20 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 128 MB NANDs    Samsung K9F1F08
    {0xf1ec, 1, 1024, &Type2SmallSub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 35, 25 ), &EccDescriptor_D2k_RA64_ECC4, kSamsungType2K9F1GNames},
    
    // 128 MB NANDs    Toshiba TC58NVG0S3
    {0xf198, 1, 1024, &Type2SmallSub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 30, 20 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 128 MB NANDs    SanDisk
    {0xf145, 1, 1024, &Type2SmallSub_NAND, NAND_FAILSAFE_TIMINGS, &EccDescriptor_D2k_RA64_ECC4},
    
    // 256 MB NANDs    ST Micro NAND02GW3
    {0xda20, 2, 2048, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 20, 30), &EccDescriptor_D2k_RA64_ECC4},
    
    // [2Gb] Hynix HY27UF082G2M, HY27UG082G2M, HY27UG082G1M
    {0xdaad, 2, 2048, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 30, 25), &EccDescriptor_D2k_RA64_ECC4},
    
    // 256 MB NANDs    Micron MT29F2G08
    {0xda2c, 2, 2048, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 20, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 256 MB NANDs    Samsung K9F2G08U0M
    {0xdaec, 2, 2048, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 20, 6, 20, 10 ), &EccDescriptor_D2k_RA64_ECC4, kSamsungType2K9F2GNames},
    
    // 256 MB NANDs    Toshiba TC58NVG1S3
    {0xda98, 2, 2048, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 20, 30 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 256 MB NANDs    SanDisk
    {0xda45, 2, 2048, &Type2LargeSub_NAND, NAND_FAILSAFE_TIMINGS, &EccDescriptor_D2k_RA64_ECC4},
    
    // 512 MB NANDs ST Micro
    // 4 districts
    {0xdc20, 2, 4096, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 45, 30), &EccDescriptor_D2k_RA64_ECC4},
    
    // [4Gb] Hynix HY27UH084G2M, HY27UG084G2M, HY27UH084G1M
    // 4 districts
    {0xdcad, 2, 4096, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 10, 10, 45, 30), &EccDescriptor_D2k_RA64_ECC4},
    
    // 512 MB NANDs Micron MT29F4G08
    {0xdc2c, 2, 4096, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 20, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 512 MB NANDs Samsung
    {0xdcec, 2, 4096, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 20, 6, 25, 25 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 512 MB NANDs Toshiba TH58NVG2S3
    // 4 districts
    {0xdc98, 2, 4096, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 25, 25 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 512 MB NANDs SanDisk
    {0xdc45, 2, 4096, &Type2LargeSub_NAND, NAND_FAILSAFE_TIMINGS, &EccDescriptor_D2k_RA64_ECC4},
    
    // [8Gb] Hynix HY27UH088G2M
    {0xd3ad, 4, 8192, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC(20, 6, 30, 25), &EccDescriptor_D2k_RA64_ECC4},
    
    // [8Gb] STMicro NAND08GW3BxANx
    {0xd320, 4, 8192, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC(10, 6, 45, 30), &EccDescriptor_D2k_RA64_ECC4},
    
    // [8Gb] Micron MT29F8G08FABWG
    // <15 gives 1 clock cycle which may be unstable. Datasheet 10 for last timing param
    // Fix for defect 8343.  Change the Micron timings to fit the datasheet
    {0xd32c, 4, 8192, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC(10, 6, 25, 15), &EccDescriptor_D2k_RA64_ECC4},
    
    // Toshiba
    {0xd398, 4, 8192, &Type2LargeSub_NAND, NAND_FAILSAFE_TIMINGS, &EccDescriptor_D2k_RA64_ECC4},
    
    // Prelim STMicro
    {0xd520, 4, 16384, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC(10, 6, 45, 30), &EccDescriptor_D2k_RA64_ECC4},
    
    // Prelim Hynix
    {0xd5ad, 4, 16384, &Type2LargeSub_NAND, MK_NAND_TIMINGS_STATIC(10, 6, 25, 30), &EccDescriptor_D2k_RA64_ECC4},
    
    // Micron
    {0xd52c, 4, 16384, &Type2LargeSub_NAND, NAND_FAILSAFE_TIMINGS, &EccDescriptor_D2k_RA64_ECC4},
    
    // Null list terminator
    {0}
};

//! \brief Types 5 and 6 Map.
const NandDeviceCodeMap_t LargeMLCDescriptorIdList[] =
{
    // 2  GBit Large MLC, Toshiba TC58NVG1D4BFT00
    {0xda98, 1, 1024, &Type5Sub_NAND, MK_NAND_TIMINGS_STATIC(  0,  6, 20, 30 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 2  GBit Large MLC, Sandisk
    {0xda45, 1, 1024, &Type5Sub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 20, 30 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 4  GBit Large MLC, Sandisk
    {0xdc45, 1, 2048, &Type5Sub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 20, 30 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 8  GBit Large MLC, Toshiba TH58NVG3D4xFT00
    {0xd398, 4, 4096, &Type5Sub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 35, 30 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 8  GBit Large MLC, Sandisk
    {0xd345, 4, 4096, &Type5Sub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 35, 20 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 16 GBit Large MLC, Toshiba TH58NVG4D4xFT00 Prelim.
    // TH58NVG5D4Cxxxx uses this for each of its 2 CEs.
    // Does it really need to be treated as 4 dice per CE as it is now?
    {0xd598, 4, 8192, &Type5Sub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 35, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 16 GBit Large MLC, Sandisk
    // Prelim
    {0xd545, 4, 8192, &Type5Sub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 35, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 4  GBit Large MLC, Toshiba TC58NVG2D4BFT00
    {0xdc98, 1, 2048, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 0, 6, 20, 30 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 4  GBit Large MLC, Samsung K9G4G08U0M
    {0xdcec, 1, 2048, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 25,  6, 25, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 4  GBit Large MLC, Hynix HY27UT084G2M, HY27UU088G5M
    {0xdcad, 1, 2048, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 50,  6, 45, 25 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 4  GBit Large MLC, STMicro NAND04GW3C2AN1E
    {0xdc20, 1, 2048, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 30,  6, 40, 20 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 8  GBit Large MLC, Samsung K9L8G08U0M, K9HAG08U1M
    {0xd3ec, 1, 4096, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 20,  6, 20, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 8  GBit Large MLC, Hynix HY27UV08AG5M
    {0xd3ad, 1, 4096, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 50,  6, 60, 30 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 8  GBit Large MLC, Intel JS29F08G08AAMB1 (aka Micron MT29F8G08MAA), 
    // JS29F08G08CAMB1 (aka Micron MT29F16G08QAA)
    {0xd32c, 1, 4096, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 15,  6, 15, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 16 GBit Large MLC, Samsung K9LAG08U0M K9HBG08U1M K9GAG08U0M[4K page]
    {0xd5ec, 2, 8192, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 20,  6, 20, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 16 GBit Large MLC, Intel JS29F32G08FAMB1 (aka Micron MT29F32G08TAA)
    {0xd52c, 2, 8192, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 15,  6, 15, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 512 MB NANDs Micron MT29F4G08
    {0xdc2c, 1, 2048, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 20,  6, 20, 20 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // Intel JS29F08G08AAMB2, JS29F08G08CAMB2
    {0xd389, 1, 4096, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 15,  6, 15, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // Intel JS29F32G08FAMB2
    {0xd589, 2, 8192, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 15,  6, 15, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // Hynix HY27UT088G2M, HY27UU08AG5M
    {0xd3ad, 1, 4096, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 20,  6, 20, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // Hynix HY27UW08CGFM
    {0xd5ad, 2, 8192, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 20,  6, 15, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // Hynix HY27UV08BG5M, HY27UV08BGDM
    // Timings nominally ==   ( 20, 6, 15, 10), but the software will bump the timings by 4ns/log2(qty CE's) at runtime.
    {0xd5ad, 2, 8192, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 15, 6, 10, 05), &EccDescriptor_D2k_RA64_ECC4},
    
    // Hynix HY27UV08BGFM
    {0xd3ad, 2, 8192, &Type6Sub_NAND, MK_NAND_TIMINGS_STATIC( 15, 6, 10, 05), &EccDescriptor_D2k_RA64_ECC4},
    
    // Null list terminator
    {0}
};

//! \brief Type 7 List - MultiPlane devices allow simultaneous programs.
const NandDeviceCodeMap_t Type7DescriptorIdList[] =
{
    // [8Gb] Micron MT29F8G08FABWG
    // <15 gives 1 clock cycle which may be unstable. Datasheet 10 for last timing param
    {0xd32c, 2, 8192, &Type7Sub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 25, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 512 MB NANDs Micron MT29F4G08AAA
    {0xdc2c, 1, 4096, &Type7Sub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 20, 10 ), &EccDescriptor_D2k_RA64_ECC4},

    // 512 MB Samsung K9F4G08
    {0xdcec, 1, 4096, &Type7Sub_NAND, MK_NAND_TIMINGS_STATIC( 25, 6, 15, 12 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 1 GB Samsung K9K8G08UXM, K9NBG08U5A, K9WAG08U1A
    {0xd3ec, 2, 8192, &Type7Sub_NAND, MK_NAND_TIMINGS_STATIC( 35, 6, 25, 15 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 2 GB Samsung K9WAG08UXM
    {0xd5ec, 2, 16384, &Type7Sub_NAND, MK_NAND_TIMINGS_STATIC( 25, 6, 15, 12 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 256 MB Samsung K9F2G08U0A
    {0xdaec, 1, 2048, &Type7Sub_NAND, MK_NAND_TIMINGS_STATIC( 20, 6, 20, 10 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // 128 MB NANDs Samsung K9F1F08
    {0xf1ec, 1, 1024, &Type7Sub_NAND, MK_NAND_TIMINGS_STATIC( 20, 6, 15, 12 ), &EccDescriptor_D2k_RA64_ECC4},
    
    // Null list terminator
    {0}
};

//! \name Samsung Type 8 Device Names
//@{
NandDeviceNameTable::TableEntry_t kSamsungType816GbNames[] = { DEVNAME_1CE_END("K9GAG08U0M") };
NandDeviceNameTable::TableEntry_t kSamsungType832GbNames[] = { DEVNAME_1CE("K9LBG08U0M"), DEVNAME_2CE("K9HCG08U1M"), DEVNAME_4CE_END("K9MDG08U5M") };
//@}

//! \brief Type 8 List - 4K page MLC devices with multi-plane operations.
const NandDeviceCodeMap_t Type8DescriptorIdList[] =
{    
    // Samsung K9GAG08U0M (16Gb)
    {0xd5ec, 1, 4096, &Type8Sub_NAND, MK_NAND_TIMINGS_STATIC( 20, 6, 15, 10 ), &EccDescriptor_D4k_RA128_ECC4_BCH8, kSamsungType816GbNames},

    // Samsung K9LBG08U0M (32Gb), K9HCG08U1M (64Gb), K9MDG08U5M (128Gb)
    {0xd7ec, 1, 8192, &Type8Sub_NAND, MK_NAND_TIMINGS_STATIC( 25, 6, 15, 15 ), &EccDescriptor_D4k_RA128_ECC4_BCH8, kSamsungType832GbNames},

    // Hynix H27UAG, H27UBG
    // The following timings are expanded from the minimum timings indicated by
    // the specification, i.e. "( 20, 0, 12, 20)".
    // The minimum timings generated failures in P4S test Section 11.
    {0xd5ad, 1, 4096, &Type8Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 0, 20, 20, 20, 5, 15), &EccDescriptor_D4k_RA128_ECC4_BCH8},

    // Hynix H27UCG
    // Note that the H27UCG has smaller timing values, but uses the same ID, so must use the H27UDG timings
    {0xd7ad, 1, 8192, &Type8Sub_NAND, MK_NAND_TIMINGS_STATIC( 25, 0, 23, 20), &EccDescriptor_D4k_RA128_ECC4_BCH8},
    
    // Null list terminator
    {0}
};

//! \brief Type 9 List - 4K page Toshiba, Intel or Micron devices with cache.
const NandDeviceCodeMap_t Type9DescriptorIdList[] =
{
    // Toshiba TC58NVG3D1DTG00 (8Gb)
    {0xd398, 1, 2048, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 15, 15 ), &EccDescriptor_D4k_RA218_ECC8},
    
    // Toshiba TC58NVG4D1DTG00 (16Gb)
    {0xd598, 1, 4096, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 15, 15 ), &EccDescriptor_D4k_RA218_ECC8},

    // Toshiba TH58NVG6D1DTG20 (32Gb)
    {0xd798, 1, 8192, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 10, 6, 15, 15 ), &EccDescriptor_D4k_RA218_ECC8},

    // Intel JS29F16G08AAMC1 and JS29F32G08CAMC1
    {0xd589, 1, 4096, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 15, 6, 10, 10 ), &EccDescriptor_D4k_RA218_ECC8},    

    // Micron MT29F16G08MAA and MT29F32G08QAA
    // Be advised that the MT29F16G08MAA needs (tDS >= 15nsec)
    {0xd52c, 1, 4096, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 15, 6, 15, 10 ), &EccDescriptor_D4k_RA218_ECC8},  
    
    // Micron MT29F64G08TAA (32Gb)
    {0xd72c, 1, 8192, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 15, 6, 15, 10 ), &EccDescriptor_D4k_RA218_ECC8},

    // Intel JSF64G08FAMC1 (32Gb)
    {0xd789, 1, 8192, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 15, 6, 10, 10 ), &EccDescriptor_D4k_RA218_ECC8},
            
    //Samsung K9LBG08U0D (32Gb)        
    {0xd7ec, 1, 8192, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 25, 6, 20, 10 ), &EccDescriptor_D4k_RA218_ECC8},

    //Samsung K9GAG08U0D (16Gb), K9LBG08U1D, K9HCG08U5D
    {0xd5ec, 1, 4096, &Type9Sub_NAND, MK_NAND_TIMINGS_STATIC( 20, 6, 20, 10 ), &EccDescriptor_D4k_RA218_ECC8},

    // Null list terminator
    {0}
};

//! \brief Type 10 List - 4K page SLC devices with multi-plane operations.
const NandDeviceCodeMap_t Type10DescriptorIdList[] =
{
    {0xd3ec, 1, 4096, &Type10Sub_NAND, MK_NAND_TIMINGS_STATIC( 20, 6, 15, 10 ),     &EccDescriptor_D4k_RA128_ECC4_BCH8},
    
    // K9NCG08U5M
    {0xd5ec, 1, 8192, &Type10Sub_NAND, MK_NAND_TIMINGS_STATIC( 30, 6, 25, 15 ),     &EccDescriptor_D4k_RA128_ECC4_BCH8},

    {0xd7ec, 1, 16384, &Type10Sub_NAND, MK_NAND_TIMINGS_STATIC( 25, 6, 15, 15 ),     &EccDescriptor_D4k_RA128_ECC4_BCH8},
    
    // Null list terminator
    {0}
};

//! \brief Type 11 List - 8K page MLC devices
const NandDeviceCodeMap_t Type11DescriptorIdList[] =
{
    // Toshiba TC58NVG5D2ELAM8 (4GB), TH58NVG6D2ELAM8 (8GB) - 4GB/CE
    {0xd798, 1, 4096, &Type11Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 8, 6, 15, 10, 20, 5, 25 ), &EccDescriptor_D8k_RA376_ECC14},
    
    // Toshiba TH58NVG7D2ELAM8 (16GB) - 8GB/CE
    {0xde98, 2, 8192, &Type11Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 8, 6, 15, 10, 20, 5, 25 ), &EccDescriptor_D8k_RA376_ECC14},
    
    // Null list terminator
    {0}
};

//! \brief BCH ECC12 List
//!
//! For the 3780 these NANDs use BCH12 ECC. For the 37xx or 377x, they use RS8 with lower
//! reliability specified by the manufacturer.
const NandDeviceCodeMap_t BchEcc12DescriptorIdList[] =
{
    // Hynix 1G/CE H27UAG8T2A 2GB (1CE), H27UBG8U5A 4GB (2CE)
    {0xd5ad, 1, 4096, &Type12Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 25, 6, 15, 15, 25, 5, 15 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },

    // Hynix 4G/CE H27UBG8T2M 4GB, H27UCG8UDM 8GB (2CE), H27UDG8VEM 16GB (4CE)
    {0xd7ad, 1, 8192, &Type12Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 6, 15, 15, 25, 5, 15 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },

    // Hynix 8G/CE H27UEG8YEM 32GB (4CE)
    {0xdead, 2, 16384, &Type12Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 6, 15, 10, 20, 5, 15 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },
    
    // Micron L63A 4G/CE MT29F32G08CBAAA 4GB, MT29F64G08CFAAA 8GB (2CE)
    {0xd72c, 1, 8192, &Type13Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 0, 15, 15, 21, 10, 20 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },

    // Micron L63A MT29F128G08CJAAA  - 128Gb, 4 dice, 8 planes, 2 CE
    {0xd92c, 2, 16384, &Type13Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 0, 15, 15, 21, 10, 20 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },
    
    // Micron 2G/CE - L62A
    // MT29F16G08CBABA 2GB (1CE)
    {0x482c, 1, 2048, &Type14Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 6, 15, 10, 20, 5, 15 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },

    // Micron 4G/CE - L63B
    // MT29F32G08CBABA 4GB (1CE, common I/O)
    // MT29F64G08CEABA 8GB (2CE, separate I/O)
    // MT29F64G08CFABA 8GB (2CE, common I/O)
    {0x682c, 1, 4096, &Type14Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 25, 0, 20, 15, 25, 10, 20 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },

    // Micron 8G/CE - L63B
    // MT29F128G08CJABA 16GB (2CE, common I/O)
    // MT29F128G08CKABA 16GB (2CE, separate I/O)
    // MT29F256G08CUABA 32GB (4CE, common I/O)
    {0x882c, 2, 8192, &Type14Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 25, 0, 20, 15, 25, 10, 20 ), &EccDescriptor_D4k_RA218_BCH12_RS8 },
    
    // Null list terminator
    {0}
};

#if defined(STMP378x)
NandDeviceNameTable::TableEntry_t kSamsungType15Names[] = { DEVNAME_1CE("K9GBG08U0M"), DEVNAME_2CE("K9LCG08U1M"), DEVNAME_4CE_END("K9HDG08U5M") };
#endif // defined(STMP378x)

//! \brief Type 15 List - 8K page Samsung MLC devices ECC 16 bit
const NandDeviceCodeMap_t Type15DescriptorIdList[] =
{
#if defined(STMP378x)
    //Samsung 4G/CE - 8K Page MLC
    //K9GBG08U0M 4GB ( 1 CE )
    //K9LCG08U1M 8GB ( 2 CE)
    //K9HDG08U5M 16GB ( 4 CE)
    { 0xd7ec, 1, 4096, &Type15Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 25, 6, 20, 10, 25, 5, 15 ), &EccDescriptor_D8k_RA436_ECC16, kSamsungType15Names },
#endif // defined(STMP378x)

    // Null list terminator
    {0}
};

NandDeviceNameTable::TableEntry_t kToshibaPBA4GBNames[] = { DEVNAME_1CE_END("THGVR0G5D1FTA00") };
NandDeviceNameTable::TableEntry_t kToshibaPBA8GBNames[] = { DEVNAME_2CE_END("THGVR0G6D2FTA00") };
NandDeviceNameTable::TableEntry_t kToshibaPBA16GBNames[] = { DEVNAME_2CE_END("THGVR0G7D4FLA09") };
NandDeviceNameTable::TableEntry_t kToshibaPBA32GBNames[] = { DEVNAME_2CE_END("THGVR0G8D8FLA09") };

//! \brief Type 16 List - Toshiba PBA-NAND
//!
//! The Toshiba PBA-NAND has a built-in ECC engine, meaning that normally we don't have
//! to use our own ECC engine. But our boot ROM does not support disabling ECC, so we have
//! to write boot image data with ECC for the ROM to read. To do this, we set the PBA-NAND's
//! ECC descriptor to RS4, but override the regular page read/write functions to use raw
//! r/w that don't take this ECC descriptor into account. All of this makes management
//! of the DMA descriptors much easier.
const NandDeviceCodeMap_t Type16DescriptorIdList[] =
{
    // THGVR0G5D1FTA00 - 4GB - 1CE x 1 die (1 die total)
    {0xd798, 1, 4096, &Type16Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 30, 6, 21, 19, 25, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshibaPBA4GBNames},

    // THGVR0G6D2FTA00 - 8GB - 2CE x 1 dice (2 dice total)
    {0xde98, 1, 4096, &Type16Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 30, 6, 21, 19, 25, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshibaPBA8GBNames},

    // THGVR0G7D4FLA09 - 16GB - 2CE x 2 dice (4 dice total)
    {0x3a98, 2, 8192, &Type16Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 30, 6, 21, 19, 25, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshibaPBA16GBNames},

    // THGVR0G8D8FLA09 - 32GB - 2CE x 4 dice (8 dice total)
    {0x3c98, 4, 16384, &Type16Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 30, 6, 21, 19, 25, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshibaPBA32GBNames},

    // Null list terminator
    {0}
};

NandDeviceNameTable::TableEntry_t kToshiba24nmPBA4GBNames[] = { DEVNAME_1CE_END("THGVR1G5D1HTA00") };
NandDeviceNameTable::TableEntry_t kToshiba24nmPBA8GBNames[] = { DEVNAME_1CE_END("THGVR1G6D1GTA00") };
NandDeviceNameTable::TableEntry_t kToshiba24nmPBA16GBNames[] = { DEVNAME_2CE_END("THGVR1G7D2GLA09") };
NandDeviceNameTable::TableEntry_t kToshiba24nmPBA32GBNames[] = { DEVNAME_2CE_END("THGVR1G8D4GLA09") };
NandDeviceNameTable::TableEntry_t kToshiba24nmPBA64GBNames[] = { DEVNAME_2CE_END("THGVR1G9D8GLA09") };

//! \brief Type 16 List - 24nm Toshiba PBA-NAND
//!
//! These Toshiba PBA-NAND devices are the second generation, 24nm geometry, devices. The only
//! major difference with the earlier 32nm generation is that the newer devices have 256 pages
//! per block instead of 128.
const NandDeviceCodeMap_t Type16DescriptorIdList_24nm[] =
{
    // THGVR1G5D1HTA00 - 4GB - 1CE x 1 die (1 die total)
    // This one uses the Type16Sub_NAND because, unlike the other 24nm parts, it has 128 pages per block.
    {0xd798, 1, 4096, &Type16Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 8, 6, 16, 14, 20, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshiba24nmPBA4GBNames},

    // THGVR1G6D1GTA00 - 8GB - 1CE x 2 dice (2 dice total)
    {0xde98, 1, 4096, &Type16Sub_24nm_NAND, MK_NAND_TIMINGS_DYNAMIC( 8, 6, 16, 14, 20, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshiba24nmPBA8GBNames},

    // THGVR1G7D2GLA09 - 16GB - 2CE x 2 dice (4 dice total)
    {0x3a98, 2, 4096, &Type16Sub_24nm_NAND, MK_NAND_TIMINGS_DYNAMIC( 8, 6, 16, 14, 20, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshiba24nmPBA16GBNames},

    // THGVR1G8D4GLA09 - 32GB - 2CE x 4 dice (8 dice total)
    {0x3c98, 4, 8192, &Type16Sub_24nm_NAND, MK_NAND_TIMINGS_DYNAMIC( 8, 6, 16, 14, 20, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshiba24nmPBA32GBNames},

    // THGVR1G9D8GLA09 - 64GB - 2CE x 8 dice (16 dice total)
    {0x3e98, 4, 16384, &Type16Sub_24nm_NAND, MK_NAND_TIMINGS_DYNAMIC( 8, 6, 16, 14, 20, 5, 25 ), &EccDescriptor_D4k_RA218_ECC8, kToshiba24nmPBA64GBNames},

    // Null list terminator
    {0}
};

//! \brief BCH ECC16 List
//!
//! This list contains a mix of NAND types that use BCH ECC16.
//! WARNING - the initial releases of L73A and L74A are not supported by the boot ROM due to bit 7 of byte 6 of
//! the Parameters Page being set. (The "BA-NAND" bit). If Micron releases new versions with this bit clear
//! then these NANDS will be supported.
const NandDeviceCodeMap_t BchEcc16DescriptorIdList[] =
{
#if defined(STMP378x)
    // Micron L73A - 4GB/CE - 4K page + 224 RA
    // MT29F32G08BACA - 4GB (1 CE, common I/O)
    // MT29F64G08CEACA - 8GB (2 CE, separate I/O)
    // MT29F64G08CFACA - 8GB (2 CE, common I/O)
    // MT29F128G08CXACA - 16GB (4 CE, separate I/O)
    // Note: timing relaxed from datasheet minimums. 5ns added to tsu; 10ns added to (tds+tdh)
    {0x682c, 1, 4096, &Type17Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 6, 20, 10, 16, 5, 15 ), &EccDescriptor_D4k_RA224_ECC16},

    // Micron L74A - 8GB/CE - 8K page + 448 RA
    // MT29F64G08CBAAA - 8GB (1 CE, common I/O)
    // MT29F128G08CEAAA - 16GB ( 2 CE, separate I/O)
    // MT29F128G08CFAAA - 16GB ( 2 CE, common I/O)
    // MT29F256G08CMAAA - 32GB ( 4 CE, separate I/O)
    // Note: timing relaxed from datasheet minimums. 5ns added to tsu; 10ns added to (tds+tdh)
    {0x882c, 1, 4096, &Type18Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 6, 20, 10, 16, 5, 15 ), &EccDescriptor_D8k_RA436_ECC16},

    // Micron L74A - 16GB/CE - 8K page + 448 RA
    // MT29F256G08CJAAA - 32GB ( 4 Die, 2 CE, common I/O)
    // MT29F256G08CKAAA - 32GB ( 4 Die, 2 CE, separate I/O)
    // MT29F512G08CUAAA - 64GB ( 8 Die, 4 CE, separate I/O)
    // Note: timing relaxed from datasheet minimums. 5ns added to tsu; 10ns added to (tds+tdh)
    {0xA82c, 2, 8192, &Type18Sub_NAND, MK_NAND_TIMINGS_DYNAMIC( 20, 6, 20, 10, 16, 5, 15 ), &EccDescriptor_D8k_RA436_ECC16},

#endif // defined(STMP378x)

    // Null list terminator
    {0}
};

//@}

//! @}
/////////////////////////////////////////////////////////////////////////////////
// EOF
/////////////////////////////////////////////////////////////////////////////////
