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
//! \file rom_nand_boot_blocks.h
//! \brief Type definitions for structure used by the boot ROM.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_rom_nand_boot_blocks_h_)
#define _rom_nand_boot_blocks_h_

#include "drivers/media/nand/gpmi/ddi_nand_gpmi.h"
#include "drivers/media/sectordef.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

// Locate Bad Block table after the Config Block in the NAND.
#define DBBT_DATA_START_PAGE_OFFSET             4

//! \name 37xx DBBT fingerprint constants
//@{
#define DBBT_FINGERPRINT1   0x504d5453   //!< 'STMP'
#define DBBT_FINGERPRINT2   0x54424244   //!< 'DBBT' - Discovered Bad Block Table.
#define DBBT_FINGERPRINT3   0x44494252   //!< 'RBID' - ROM Boot Image Block - D
//@}

//! \name 37xx NCB fingerprint constants
//@{
#define NCB_FINGERPRINT1    0x504d5453    //!< 'STMP'
#define NCB_FINGERPRINT2    0x2042434E    //!< 'NCB<space>' - NAND Control Block
#define NCB_FINGERPRINT3    0x4E494252    //!< 'RBIN' - ROM Boot Image Block - N
//@}

//! \name 37xx LDLB fingerprint constants
//@{
#define LDLB_FINGERPRINT1   0x504d5453   //!< 'STMP'
#define LDLB_FINGERPRINT2   0x424C444C   //!< 'LDLB' - Logical Device Layout Block
#define LDLB_FINGERPRINT3   0x4C494252   //!< 'RBIL' - ROM Boot Image Block - L
//@}

//! \name 37xx BBRC (BadBlocksPerRegionCounts_t) fingerprint constants
//@{
#define BBRC_FINGERPRINT1   0x504d5453   //!< 'STMP'
#define BBRC_FINGERPRINT2   0x52434242   //!< 'BBRC' - Bad Block per Region Counts
#define BBRC_FINGERPRINT3   0x42494252   //!< 'RBIB' - ROM Boot Image Block - B
//@}

//! \name 37xx NCB FirmwareBlock version constants
//@{
#define NCB_FIRMWAREBLOCK_VERSION_MAJOR  0x0001
#define NCB_FIRMWAREBLOCK_VERSION_MINOR  0x0000
//@}

//! \name 37xx LDLB version constants
//@{
#define LDLB_VERSION_MAJOR  0x0001
#define LDLB_VERSION_MINOR  0x0000
#define LDLB_VERSION_SUB    0x0000
//@}

//! \name NAND bitmap constants
//!
//! These bitmap constants are used for the bitmap of present NAND
//! devices that is located in the LDLB boot block.
//@{
#define NAND_1_BITMAP       1
#define NAND_2_BITMAP       2
#define NAND_3_BITMAP       4
#define NAND_4_BITMAP       8
//@}

//! \brief Number of bad block entries per page in the DBBT.
//!
//! Used in the #BadBlockTableNand_t structure. Each entry is a 32-bit word.
//! Subtract two because of the extra header fields.
#define NAND_DBBT_ENTRIES_PER_PAGE (SIZE_IN_WORDS(LARGE_SECTOR_DATA_SIZE) - 2)

//! \brief Number of pages per NAND in the DBBT.
#define NAND_MAX_DBBT_PAGES_PER_NAND (1)

#define BOOTBLOCKSTRUCT_RESERVED1_SIZE_U32          (10)
#define BOOTBLOCKSTRUCT_RESERVED2_SIZE_U32          (19)
#define BOOTBLOCKSTRUCT_FIRMWAREBLOCKDATA_SIZE_U32  (128)

#define MAX_BBRC_REGIONS (32)

///////////////////////////////////////////////////////////////////////////////
// Typedefs
///////////////////////////////////////////////////////////////////////////////

//! \brief Number of Bad Blocks in NAND.
//!
//! This structure defines the number of BB on each NAND and the number of 2K pages
//! that must be read to fill in the Bad Block Table from the data saved on
//! the NAND.
typedef struct _DiscoveredBadBlockStruct_t
{
    union
    {
        struct 
        {
            uint32_t        m_u32NumberBB_NAND0;		//!< # Bad Blocks stored in this table for NAND0.
            uint32_t        m_u32NumberBB_NAND1;		//!< # Bad Blocks stored in this table for NAND1.
            uint32_t        m_u32NumberBB_NAND2;		//!< # Bad Blocks stored in this table for NAND2.
            uint32_t        m_u32NumberBB_NAND3;		//!< # Bad Blocks stored in this table for NAND3.  
        };
        uint32_t    m_u32NumberBB_NAND[4];          
    };
    union
    {
        struct 
        {
            uint32_t        m_u32Number2KPagesBB_NAND0; //!< Bad Blocks for NAND0 consume this # of 2K pages.   
            uint32_t        m_u32Number2KPagesBB_NAND1;	//!< Bad Blocks for NAND1 consume this # of 2K pages.  
            uint32_t        m_u32Number2KPagesBB_NAND2;	//!< Bad Blocks for NAND2 consume this # of 2K pages.
            uint32_t        m_u32Number2KPagesBB_NAND3;	//!< Bad Blocks for NAND3 consume this # of 2K pages.
        };
        uint32_t    m_u32Number2KPagesBB_NAND[4];
    };
} DiscoveredBadBlockStruct_t;

//! \brief Structure used to archive the counts of bad blocks in each NAND region.
typedef struct BadBlocksPerRegionCounts_t   // i.e. the "BBRC"
{
    //! \brief Quantity of valid entries in the u32BadBlocks array.
    uint32_t    u32Entries;
    //! \brief An array of quantities of bad blocks, one quantity per region.
    uint32_t    u32NumBadBlksInRegion[MAX_BBRC_REGIONS];
} BadBlocksPerRegionCounts_t;

//! \brief Structure defining where NCB and LDLB parameters are located.
//!
//! This structure defines the basic fingerprint template for both the Nand
//! Control Block (NCB) and the Logical Drive Layout Block (LDLB).  This
//! template is used to determine if the sector read is a Boot Control Block.
//! This structure defines the NAND Control Block (NCB).  This block
//! contains information describing the timing for the NAND, the number of
//! NANDs in the system, the block size of the NAND, the page size of the NAND,
//! and other criteria for the NAND.  This is information that is
//! required just to successfully communicate with the NAND.
//!
//! This structure also defines the Logical Drive Layout Block (LDLB).  This
//! block contains information describing the version as well as the layout of
//! the code and data on the NAND Media.  For the ROM, we're only concerned
//! with the boot firmware start.  Additional information may be stored in
//! the Reserved3 area.  This area will be of interest to the SDK.
//!
//! This structure also defines the Discovered Bad Block Table (DBBT) header.  
//! This block contains the information used for parsing the bad block tables
//! which are stored in subsequent 2K sectors.  The DBBT header is 8K, followed
//! by the first NANDs entries, then the 2nd NANDs entries on a subsequent 2K 
//! page (determined by how many 2K pages the first nand requires), and so on.
typedef struct _BootBlockStruct_t
{
    uint32_t    m_u32FingerPrint1;      //!< First fingerprint in first byte.
    union
    {
        struct
        {            
            NAND_Timing_t   m_NANDTiming;           //!< Optimum timing parameters for Tas, Tds, Tdh in nsec.
            uint32_t        m_u32DataPageSize;      //!< 2048 for 2K pages, 4096 for 4K pages.
            uint32_t        m_u32TotalPageSize;     //!< 2112 for 2K pages, 4314 for 4K pages.
            uint32_t        m_u32SectorsPerBlock;   //!< Number of 2K sections per block.
            uint32_t        m_u32SectorInPageMask;  //!< Mask for handling pages > 2K.
            uint32_t        m_u32SectorToPageShift; //!< Address shift for handling pages > 2K.
            uint32_t        m_u32NumberOfNANDs;     //!< Total Number of NANDs - not used by ROM.
        } NCB_Block1;
        struct
        {
            struct  
            {
                uint16_t    m_u16Major;             
                uint16_t    m_u16Minor;
                uint16_t    m_u16Sub;
                uint16_t    m_u16Reserved;
            } LDLB_Version;                     //!< LDLB version - not used by ROM.
            uint32_t    m_u32NANDBitmap;        //!< bit 0 == NAND 0, bit 1 == NAND 1, bit 2 = NAND 2, bit 3 = NAND3
        } LDLB_Block1;
        DiscoveredBadBlockStruct_t zDBBT1;
        // This one just forces the spacing.
        uint32_t    m_Reserved1[BOOTBLOCKSTRUCT_RESERVED1_SIZE_U32];
    };
    uint32_t    m_u32FingerPrint2;      //!< 2nd fingerprint at word 10.
    union
    {
        struct
        {
            uint32_t        m_u32NumRowBytes;   //!< Number of row bytes in read/write transactions.
            uint32_t        m_u32NumColumnBytes;//!< Number of row bytes in read/write transactions.
            uint32_t        m_u32TotalInternalDie;  //!< Number of separate chips in this NAND.
            uint32_t        m_u32InternalPlanesPerDie;  //!< Number of internal planes - treat like separate chips.
            uint32_t        m_u32CellType;      //!< MLC or SLC.
            uint32_t        m_u32ECCType;       //!< 4 symbol or 8 symbol ECC?
#if defined(STMP37xx) || defined(STMP377x)
            uint32_t        m_u32Read1stCode;   //!< First value sent to initiate a NAND Read sequence.
            uint32_t        m_u32Read2ndCode;   //!< Second value sent to initiate a NAND Read sequence.
#elif defined(STMP378x)
            uint32_t        m_u32EccBlock0Size;         //!< Number of bytes for Block0 - BCH
            uint32_t        m_u32EccBlockNSize;         //!< Block size in bytes for all blocks other than Block0 - BCH
            uint32_t        m_u32EccBlock0EccLevel;     //!< Ecc level for Block 0 - BCH
            uint32_t        m_u32NumEccBlocksPerPage;   //!< Number of blocks per page - BCH
            uint32_t        m_u32MetadataBytes;         //!< Metadata size - BCH
            uint32_t        m_u32EraseThreshold;        //!< To set into BCH_MODE register.
            uint32_t        m_u32Read1stCode;           //!< First value sent to initiate a NAND Read sequence.
            uint32_t        m_u32Read2ndCode;           //!< Second value sent to initiate a NAND Read sequence.
            uint32_t        m_u32BootPatch;             //!< 0 for normal boot and 1 to load patch starting next to NCB.
            uint32_t        m_u32PatchSectors;          //!< Size of patch in sectors.
            uint32_t    m_u32Firmware_startingNAND2;    //!< duplicate required for patch boot.
#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

        } NCB_Block2;
        struct
        {
            uint32_t    m_u32Firmware_startingNAND;     //!< Firmware image starts on this NAND.
            uint32_t    m_u32Firmware_startingSector;   //!< Firmware image starts on this sector.
            uint32_t    m_u32Firmware_sectorStride;     //!< Amount to jump between sectors - unused in ROM.
            uint32_t    m_uSectorsInFirmware;           //!< Number of sectors in firmware image.
            uint32_t    m_u32Firmware_startingNAND2;    //!< Secondary FW Image starting NAND.
            uint32_t    m_u32Firmware_startingSector2;  //!< Secondary FW Image starting Sector.
            uint32_t    m_u32Firmware_sectorStride2;    //!< Secondary FW Image stride - unused in ROM.
            uint32_t    m_uSectorsInFirmware2;          //!< Number of sector in secondary FW image.
            struct  
            {
                uint16_t    m_u16Major;
                uint16_t    m_u16Minor;
                uint16_t    m_u16Sub;
                uint16_t    m_u16Reserved;
            } FirmwareVersion;
            uint32_t    m_u32DiscoveredBBTableSector;   //!< Location of Discovered Bad Block Table (DBBT).
            uint32_t    m_u32DiscoveredBBTableSector2;  //!< Location of backup DBBT 
        } LDLB_Block2;
        // This one just forces the spacing.
        uint32_t    m_Reserved2[BOOTBLOCKSTRUCT_RESERVED2_SIZE_U32];    
    };

    uint16_t    m_u16Major;         //!< Major version of BootBlockStruct_t
    uint16_t    m_u16Minor;         //!< Minor version of BootBlockStruct_t

    uint32_t    m_u32FingerPrint3;    //!< 3rd fingerprint at word 30.

    //! \brief Contains values used by firmware, not by ROM.
    struct {
        uint16_t                m_u16Major;             //!< Major version of BootBlockStruct_t.FirmwareBlock
        uint16_t                m_u16Minor;             //!< Minor version of BootBlockStruct_t.FirmwareBlock

        union
        {
            uint32_t                    m_u32FirmwareBlockData[BOOTBLOCKSTRUCT_FIRMWAREBLOCKDATA_SIZE_U32]; 
                                                            //!< Minimum size of BootBlockStruct_t.FirmwareBlock.
                                                            //!< Also provides a place for miscellaneous data storage.
            NAND_Timing2_struct_t       NAND_Timing2_struct;//!< Timing values for the GPMI interface to the NAND.

            BadBlocksPerRegionCounts_t  BadBlocksPerRegionCounts;
                                                            //!< Contains counts of bad-blocks in all regions.
        };
    
    } FirmwareBlock;

} BootBlockStruct_t;

//! \brief Structure of the Bad Block Entry Table in NAND.
//!
//! This structure defines the Discovered Bad Block Table (DBBT) entries.  This 
//! block contains a word holding the NAND number then a word describing the number 
//! of Bad Blocks on the NAND and an array containing these bad blocks.  The ROM 
//! will use these entries in the Bad Block table to correctly index to the next 
//! sector (skip over bad blocks) while reading from the NAND. 
//! Blocks are not guaranteed to be sorted in this table.
typedef struct _BadBlockTableNand_t
{
    uint32_t      uNAND;		        //!< Which NAND this table is for.
    uint32_t      uNumberBB;		    //!< Number of Bad Blocks in this NAND.
    uint32_t      u32BadBlock[NAND_DBBT_ENTRIES_PER_PAGE];		//!< Table of the Bad Blocks.  
} BadBlockTableNand_t;

#endif // _rom_nand_boot_blocks_h_
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
