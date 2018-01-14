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
//! \addtogroup ddi_lba_nand_media
//! @{
//! \file ddi_lba_nand_mbr.h
//! \brief Declaration of MBR type partition types.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__ddi_lba_nand_mbr_h__)
#define __ddi_lba_nand_mbr_h__

#include "types.h"
#include "errordefs.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \name MBR
//@{

//! Size of Partition Table header
const int k_iMbrPartTable = 0x01BE;

//! Number of partition table entries.
const int k_iPtblMaxNumEntries = 4;

//! Partition IDs
enum { k_ePartBootIdBootable = 0x80 };

//! File System IDs
enum { k_ePartSysIdFat12 = 0x01,
       k_ePartSysIdFat16 = 0x06,
       k_ePartSysIdFat32 = 0x0B };

//! Partition signature
const int k_iPartSignature = 0xAA55;

//! 1 MB in bytes
const int k_iOneMb = (1 * 1024 * 1024);

// Put these MBR structure in a namespace to group them together.
namespace Mbr {

#pragma pack(1)
//! CHS Packed.
typedef struct _ChsPacked_t
{
	uint8_t		u8Head;
	uint8_t		u8Sector;
	uint8_t		u8Cylinder;
} ChsPacked_t;

//! Partition Entry.
typedef struct _PartitionEntry_t
{
    uint8_t     u8BootDescriptor;       //!< 0=nonboot, 0x80=bootable
    ChsPacked_t StartCHSPacked;
    uint8_t     u8FileSystem;           //!< 1=fat12, 6=fat16
    ChsPacked_t EndCHSPacked;
    uint32_t    u32FirstSectorNumber;   //!< relative to beginning of device
    uint32_t    u32SectorCount;
} PartitionEntry_t;

//! Partition Table.
typedef struct _PartitionTable_t
{
	uint8_t             u8ConsistencyCheck[k_iMbrPartTable];    //!< not used
	PartitionEntry_t    Partitions[k_iPtblMaxNumEntries];
	uint16_t            u16Signature;                           //!< 0xaa55
} PartitionTable_t;
#pragma pack()

} // namespace

//! @}

//! \name Sector Offsets
//! @{

//! Sector number of the config block on the VFP of the first device.
const uint32_t k_u32ConfigBlockSectorNumber = 0;

//! Sector number of the MBR on the MDP of the first device.
const uint32_t k_u32MbrSectorNumber = 0;

//! @}

#endif // __ddi_lba_nand_mbr_h__
// EOF
//! @}
