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
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_media
//! @{
//! \file    ddi_nand_media.h
//! \brief   Internal declarations for the NAND media layer.
////////////////////////////////////////////////////////////////////////////////
#ifndef _NAND_MEDIA_H
#define _NAND_MEDIA_H

#include "ddi_nand_ddi.h"
#include "ddi_nand_boot_blocks.h"
#include "drivers/media/nand/rom_support/rom_nand_boot_blocks.h"
#include "drivers/media/sectordef.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "Block.h"
#include "Page.h"
#include "BadBlockTable.h"
#include "Region.h"
#include "drivers/media/buffer_manager/media_buffer.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand {

//! \brief Load parameter flag constants
//!
//! These constants tell Media::findNCB() and Media::findLDLB() to
//! either load and save the values in the respective boot control block or to
//! simply find the BCB and ignore its contents.
enum _load_parameters_constants
{
    kLoadParameters = true,      //!< Load BCB contents.
    kDontLoadParameters = false  //!< Only find the BCB; do not load contents.
};

//! \brief Erase block flag constants
//!
//! These constants are used for the \a eraseGoodBlock argument of 
//! Media::findFirstGoodBlock(). They tell the function whether
//! to erase the next good block that is found within the search window.
enum _erase_blocks_constants
{
    kEraseFoundBlock = true,      //!< Erase the block.
    kDontEraseFoundBlock = false  //!< Don't erase the block.
};

//! \brief Constants for use when calling Media::findBootControlBlocks().
enum _allow_recovery_constants {
    kAllowRecovery = true,      //!< Allow boot blocks to be recovered.
    kDontAllowRecovery = false  //!< Don't llow boot blocks to be recovered.
};

//! \name Config block constants
//@{
//! Offset of sector within Block ( 0 based) - 1 is the 2nd sector of the block.
#define CONFIG_BLOCK_SECTOR_OFFSET  1

#define NAND_CONFIG_BLOCK_MAGIC_COOKIE  0x010203
#define NAND_CONFIG_BLOCK_VERSION       0x00000b
#define NAND_MAGIC_COOKIE_WORD_POS      0
#define NAND_VERSION_WORD_POS           1
//@}

///////////////////////////////////////////////////////////////////////////////
// Typedefs
///////////////////////////////////////////////////////////////////////////////

//! \brief Config block region info
typedef struct _NandConfigBlockRegionInfo {

    //! \brief Constants for region tag values.
    //!
    //! These constants define special values for the \a wTag field of a config block region
    //! info structure. In addition to these values, the normal drive tag values are valid.
    enum _region_tag_constants
    {
        //! Tag value for a boot region in the config block.
        kBootRegionTag = 0x7fffffff
    };

    LogicalDriveType_t eDriveType;       //!< Some System Drive, or Data Drive
    uint32_t wTag;              //!< Drive Tag
    int iNumBlks;         //!< Size, in blocks, of whole Region. Size includes embedded Bad Blocks
    int iChip;            //!< Chip number that region is located on.
    int iStartBlock;      //!< Region's start block relative to chip.
} NandConfigBlockRegionInfo_t;

//! \brief Configuration block info sector
typedef struct _NandConfigBlockInfo {
    int iMagicCookie;       //!< #NAND_CONFIG_BLOCK_MAGIC_COOKIE
    int iVersionNum;        //!< #NAND_CONFIG_BLOCK_VERSION
    int iNumBadBlks;        //!< Number Bad Blocks on this Chip
    int iNumRegions;        //!< Number of regions in the region array.
    int iNumReservedBlocks; //!< Total number of reserved blocks on this chip enable.
    NandConfigBlockRegionInfo_t Regions[1]; //!< Information about the regions on this chip enable.
} NandConfigBlockInfo_t;

//! \brief The set of bad block table modes.
enum _nand_bad_block_table_mode
{
    //! No bad block table fields are valid.
    kNandBadBlockTableInvalid,
    
    //! \brief Allocation mode.
    //!
    //! The global per-chip tables and counts in #NandMediaInfo are valid.
    kNandBadBlockTableAllocationMode,
    
    //! \brief Discovery mode.
    //!
    //! Per-chip tables and counts are invalid, but the regions'
    //! bad block tables are valid.
    kNandBadBlockTableDiscoveryMode
};

//! \brief Typedef for the bad block table mode enumeration.
typedef enum _nand_bad_block_table_mode NandBadBlockTableMode_t;

// Forward declarations.
typedef struct _FingerPrintValues FingerPrintValues_t;
typedef struct _NandConfigBlockInfo NandConfigBlockInfo_t;
typedef struct _NandConfigBlockRegionInfo NandConfigBlockRegionInfo_t;
typedef struct _NandZipConfigBlockInfo NandZipConfigBlockInfo_t;
class PhyMap;
class DiscoveredBadBlockTable;
class Mapper;
class DeferredTaskQueue;
class SystemDriveRecoveryManager;
class NssmManager;

/*!
 * \brief NAND logical media class.
 *
 * This structure contains all of the information about a (the) NAND Media.
 * It has a table of all of the chips' NANDDescriptors, the addresses of the
 * Configuration Block for each chip, a table of all of the Regions
 * on those chips, and a table of all of the Bad Blocks on those chips.
 */
class Media : public ::LogicalMedia
{
public:
    
    //! \brief Default constructor.
    Media();
    
    //! \brief Destructor.
    virtual ~Media();

    //! \name Logical media API
    //@{
    virtual RtStatus_t init();
    virtual RtStatus_t allocate(MediaAllocationTable_t * table);
    virtual RtStatus_t discover();
    virtual RtStatus_t getMediaTable(MediaAllocationTable_t ** table);
    virtual RtStatus_t freeMediaTable(MediaAllocationTable_t * table);
    virtual RtStatus_t getInfo(uint32_t infoSelector, void * value);
    virtual RtStatus_t setInfo(uint32_t infoSelector, const void * value);
    virtual RtStatus_t erase();
    virtual RtStatus_t shutdown();
    virtual RtStatus_t flushDrives();
    virtual RtStatus_t setBootDrive(DriveTag_t tag);
    RtStatus_t getConfigBlock1stSector(NandPhysicalMedia * pNandPhysicalMediaDesc, int * piConfigBlockPhysAdd, bool bConfirmConfigBlock, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer);
    RtStatus_t layoutBootBlocks(int iNumFirmwareBlocks, uint32_t * p32NumBlocksUsedForBootBlocks);
    RtStatus_t writeNCB(const NAND_Timing2_struct_t * pNandTiming, SECTOR_BUFFER * pu8Page, SECTOR_BUFFER * auxBuffer);
    //@}

    RtStatus_t discover(bool bWriteToTheDevice);
    
    //! \name Regions
    //@{
    unsigned getRegionCount() const { return m_iNumRegions; }
    Region * getRegion(unsigned index) { return m_pRegionInfo[index]; }
    Region * getRegionForBlock(const BlockAddress & physicalBlock);
    
    //! \brief Quick way to create a iterator for this media object's regions.
    Region::Iterator createRegionIterator() { return Region::Iterator(m_pRegionInfo, m_iNumRegions); }
    //@}
    
    //! \name Block counts
    //@{
    uint32_t getTotalBlockCount() const { return m_iTotalBlksInMedia; }
    uint32_t getBadBlockCount() const { return m_iNumBadBlks; }
    NandBadBlockTableMode_t getBadBlockTableMode() const { return m_badBlockTableMode; }
    uint32_t getReservedBlockCount() const { return m_iNumReservedBlocks; }
    //@}

#pragma ghs section text=".static.text"
    //! \name Accessors
    //@{
    BootBlocks & getBootBlocks() { return m_bootBlocks; }
    NssmManager * getNssmManager() { return m_nssmManager; }
    Mapper * getMapper() { return m_mapper; }
    DeferredTaskQueue * getDeferredQueue() { return m_deferredTasks; }
    SystemDriveRecoveryManager * getRecoveryManager() { return m_recoveryManager; }
    //@}
#pragma ghs section text=default

    //! \name Boot blocks
    //@{
    //! \brief
    RtStatus_t recoverBootControlBlocks(bool force, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer);

    //! \brief Scan a search window for a boot block matching the given fingerprints.
    RtStatus_t bootBlockSearch(uint32_t u32NandDeviceNumber, const FingerPrintValues_t * pFingerPrintValues, uint32_t * p32SearchSector, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, bool bDecode, BootBlockStruct_t **ppBCB);

    //! \brief Given a starting block number, find the next good block.
    RtStatus_t findFirstGoodBlock(uint32_t u32NAND, uint32_t * pu32StartingBlock, uint32_t u32SearchSize, SECTOR_BUFFER * auxBuffer, bool eraseGoodBlock);
    
    //! \brief Returns the number of pages in the search window.
    uint32_t getBootBlockSearchWindow() { return m_bootBlockSearchWindow; }
    
    //! \brief Returns the number of blocks in the search window.
    uint32_t getBootBlockSearchWindowInBlocks() { return m_bootBlockSearchWindow / m_params->wPagesPerBlock; }
    
    //! \brief Set the span of pages over which the NAND driver searches for BCBs.
    uint32_t setBootBlockSearchNumberAndWindow(uint32_t newSearchNumber);
    //@}

protected:

    NandParameters_t * m_params;  //!< Parameters shared between all chip selects.
    NssmManager * m_nssmManager;  //!< \brief The manager object for all NSSMs.
    Mapper * m_mapper;  //!< The virtual to physical block mapper.
    DeferredTaskQueue * m_deferredTasks;    //!< Queue to handle deferred tasks.
    SystemDriveRecoveryManager * m_recoveryManager; //!< Object to handle recovery from failed reads of system drives.
    
    //! \name Block addresses
    //@{
    BootBlocks m_bootBlocks;
    int m_ConfigBlkAddr[MAX_NAND_DEVICES];     //!< On the STMP3700, the Config block is the LDLB block.
    //@}
    
    //! \name Regions
    //@{
    unsigned m_iNumRegions;         //!< Number of valid regions pointed to by #pRegionInfo.
    Region ** m_pRegionInfo;        //!< Pointer to the array of region structs.
    //@}
    
    //! \name Block counts
    //@{
    uint32_t m_iTotalBlksInMedia;  //!< Total number of blocks in this media.
    uint32_t m_iNumBadBlks;
    uint32_t m_iNumReservedBlocks;
    //@}
    
    //! \name Bad blocks
    //@{
    NandBadBlockTableMode_t m_badBlockTableMode;  //!< Current mode of the bad block tables.
    BadBlockTable m_globalBadBlockTable;
    //@}
    
    //! \name Boot block search window
    //@{
    //! \brief Number of search strides the BCB search window is composed of.
    uint32_t m_bootBlockSearchNumber;

    //! \brief Number of pages within which a boot block must be found.
    //!
    //! The boot block search window size in pages is #kNandBootBlockSearchStride multiplied
    //! by #m_bootBlockSearchNumber. So if the search stride is 64 pages and the search number
    //! is 2, then the search window is 128 pages, scanned by skipping 64 pages each step.
    uint32_t m_bootBlockSearchWindow;
    //@}

protected:

    void deleteRegions();

    //! \brief Determine if the NANDs are fresh from the factory.
    bool areNandsFresh();

    //! \name Discover
    //@{
    RtStatus_t fillInNandBadBlocksInfo(SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, int attempt, bool bWriteToTheDevice);
    RtStatus_t createDrives();
    RtStatus_t fillInBadBlocksFromAllocationModeTable(SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer);
    //@}

    //! \name Nand media erase
    //@{
    uint32_t eraseScan(int iNandNumber, int iBlockPhysAddr, int remainingBlocks, NandPhysicalMedia * pNandPhysicalMediaDesc, bool convertMarkings, SECTOR_BUFFER * auxBuffer, bool * wasBad);
    void eraseBlockRange(int iNandNumber, int iBlockPhysAddr, uint32_t numberToErase, NandPhysicalMedia * pNandPhysicalMediaDesc);
    void eraseHandleBadBlock(int iNandNumber, int iBlockPhysAddr, NandPhysicalMedia * pNandPhysicalMediaDesc, bool convertMarkings);
    void eraseAddBadBlock(int iNandNumber, int iBlockPhysAddr);
    bool eraseShouldSkipBlock(int iNandNumber, int iBlockPhysAddr);
    //@}

    //! \name Allocate
    //@{
    void findConfigBlocks();
    int findNextRegionInChip(int iChip, int iLastBlockFound, NandZipConfigBlockInfo_t *pNandZipConfigBlockInfo);
    void prepareBlockDescriptor(int iChip, NandZipConfigBlockInfo_t * pNandZipConfigBlockInfo, SECTOR_BUFFER * pSectorBuffer, SECTOR_BUFFER * pAuxBuffer);
    RtStatus_t writeBootControlBlockDescriptor(NandZipConfigBlockInfo_t * pNandZipConfigBlockInfo, SECTOR_BUFFER * pSectorBuffer, SECTOR_BUFFER * pAuxBuffer);
    RtStatus_t updatePhymapWithBadBlocks(PhyMap * phymap);
    //@}

    //! \name Boot blocks
    //@{
    RtStatus_t findNCB(uint32_t u32CurrentNAND, uint32_t * pReadSector, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, bool loadParameters);
    RtStatus_t findLDLB(uint32_t u32CurrentNAND, uint32_t * pReadSector, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, bool b32UseSecondaryBoot, bool loadParameters);
    RtStatus_t ncbSearch(uint32_t u32CurrentNAND, uint32_t * pReadSector, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer);
    RtStatus_t findBootControlBlocks(SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxBuffer, bool allowRecovery);
    RtStatus_t writeLDLB(uint32_t u32BlocksInFirmware, SECTOR_BUFFER * pu8Page, SECTOR_BUFFER * auxBuffer);
    RtStatus_t writeBootBlockPair(BootBlockLocation_t * bootBlocks[2], SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer, bool doWriteRaw);
    RtStatus_t findNGoodBlocks(int nNand, int nBlocks, uint32_t u32BlockStart, uint32_t * p32NthBlock);
    //@}

};

} // namespace nand

extern nand::Media * g_nandMedia;

#endif // #ifndef _NAND_MEDIA_H
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
