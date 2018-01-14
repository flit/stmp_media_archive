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
//! \addtogroup ddi_mmc
//! @{
//! \file   mbr_types.h
//! \brief  Declarations for MBR structures.
////////////////////////////////////////////////////////////////////////////////
#ifndef _MBRTYPES_H
#define _MBRTYPES_H

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "types.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace mmc
{

//! \brief CHS definitions.
#define MAX_CYLINDERS   1024
#define MAX_HEADS       256
#define MAX_SECTORS     63

//! \brief MBR constants.
enum _mbr_constants
{
    kConsistencyCheckSizeBytes = 446,   //!< Size of consistencyCheck field
    kNumPartitionEntries = 4,           //!< Number of partition entries
    kPartSignature = 0xaa55,            //!< MBR signature
    kPartBootIdBootable = 0x80,         //!< Indicates partition is bootable
    kMbrBytesPerSector = 512,           //!< Bytes per FAT sector
    kMbrBootOffset = 4,                 //!< Known as MBR_BOOT_OFFSET in ROM code
    kMbrBootOffsetInBytes = kMbrBootOffset * kMbrBytesPerSector, //!< Boot offset in bytes
    kMbrLargeBlockSizeInBytes = 128 * 1024, //!< Matches NAND hidden drive allocation unit size
    kMbrMinDataDriveSizeInBytes = 8 * kMbrLargeBlockSizeInBytes, //!< Matches NAND MINIMUM_DATA_DRIVE_SIZE
    k1MByte = 1024 * 1024,              //!< 1MB
    kMbrBlockNumber = 0,                //!< Device block number of MBR

    //! \name File system IDs
    //@{
    kPartSysId_Fat12 = 0x01,            //!< File system ID for FAT12
    kPartSysId_Fat16 = 0x06,            //!< File system ID for FAT16
    kPartSysId_Fat32 = 0x0b,            //!< File system ID for FAT32
    kMbrSigmatelId = 'S'                //!< File system ID used for firmware partition
    //@}
};

#pragma pack(1)

//! \brief CHS.
typedef struct _chs_t
{
    uint16_t    head;
    uint8_t     sector;
    uint16_t    cylinder;
} Chs_t;

//! \brief CHS Packed.
typedef struct _chs_packed_t
{
    uint8_t     head;
    uint8_t     sector;
    uint8_t     cylinder;
} ChsPacked_t;

//! \brief Partition Entry.
typedef struct _part_entry_t
{
    uint8_t     bootDescriptor;         //!< 0=nonboot, 0x80=bootable
    ChsPacked_t startChsPacked;
    uint8_t     fileSystem;             //!< 1=fat12, 6=fat16
    ChsPacked_t endChsPacked;
    uint32_t    firstSectorNumber;      //!< relative to beginning of device
    uint32_t    sectorCount;
} PartEntry_t;

//! \brief Partition Table.
typedef struct _part_table_t
{
    uint8_t         consistencyCheck[kConsistencyCheckSizeBytes];
    PartEntry_t     partition[kNumPartitionEntries];
    uint16_t        signature;          //!< 0xaa55
} PartTable_t;

#pragma pack()

} // namespace mmc

#endif // _MBRTYPES_H

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
