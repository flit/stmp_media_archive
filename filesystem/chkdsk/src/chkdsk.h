////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2000-2001
//
// Filename: chkdsk.h
// Description:
////////////////////////////////////////////////////////////////////////////////
#ifndef _CHKDSK_H
#define _CHKDSK_H

#include "types.h"
#include <error.h>
#include <os\fsapi.h>
#include "drivers\sectordef.h" //! \todo malinclusion

////////////////////////////////////////////////////////////////////////////////
//   Definitions
////////////////////////////////////////////////////////////////////////////////

#define BYTE_POS_SIGNATURE          0x1fe
#define BYTE_POS_BYTES_PER_SECTOR   0x0b
#define BYTE_POS_NUM_SECT_PER_CX    0x0d
#define BYTE_POS_NUM_RES_SECT       0x0e
#define BYTE_POS_NUM_FAT            0x10
#define BYTE_POS_NUM_ROOT_SECT      0x11
#define BYTE_POS_NUM_FAT_SECT       0x16
#define BYTE_POS_NUM_HIDDEN_SECT    0x1C
#define BYTE_POS_NUM_FAT_SECT_32	0x24
#define BYTE_POS_ROOT_DIR_CX     	0x2C
#define BYTE_POS_TOTAL_SECTS	    0x13
#define BYTE_POS_TOTAL_SECTS_32	    0x20

#define DIR_REC_ATT_POS             0x0b
#define DIR_REC_FIRST_CX_POS        0x1a
#define DIR_REC_SIZE_POS            0x1c
#define DIR_REC_FIRST_CX_HIGH_POS	0x14

#define SIGN_WORD_VALUE     0x00AA55
#define FAT_WORD            18
#define FAT_1ST_WORD        0x544146
#define FAT12_SIGN_VALUE    0x203231
#define FAT16_SIGN_VALUE    0x203631
#define FAT32_SIGN_VALUE    0x203233

//! Any long file name can be up to 255 bytes long and each entry can hold up to 13 characters
//! there can only be up to 20 entries of LFN per file.
#define MAX_ENTRIES_LONG_FILE_NAME  20

//! Number of bytes that a FAT directory entry occupies.
#define BYTES_PER_DIR_RECORD (32)

#define FILE_FREEENTRY_CODE (0x0)
#define FILE_DELETED_CODE (0xe5)
#define DOT_CHAR_CODE (0x2e)
#define PARENT_DIR_DOT_DOT (0x2e2e)

//! Number of sectors to cache in the FATBuffer.
#define NUM_CACHED_SECTORS (3)

#define MAX_CACHES             5   // 9
#define READCOUNTER           105
#define WRITECOUNTER          100

//! Value returned by GetNextCxFat16() and GetNextCxFat32() when an error occurs.
#define BAD_CLUSTER (0xffffffffL)

//! Number of bits in a 32-bit word.
#define BITS_PER_WORD (32)

#define BITS_SHIFT_FOR_UINT32   5
#define BITS_SHIFT_FOR_UINT8    3

////////////////////////////////////////////////////////////////////////////////
//   Types
////////////////////////////////////////////////////////////////////////////////

//! Constants passed to UpdateBit() to specify the operation to perform.
enum {
    GET_BIT = 0,    //!< Read and return the bit associated with the given cluster.
    SET_BIT = 1,    //!< Set the bit for the given cluster.
    FREE_BIT = 2    //!< Clear the bit for the given cluster.
};

//! \brief FAT filesystem types.
typedef enum {
    FS_FAT12 = 0,
    FS_FAT16,
	FS_FAT32,
    FATUNSUPPORTED
} FAT_TYPE;

//! \brief Dirty or clean enumeration.
typedef enum {
    CLEAN = 0,
    DIRTY
} SECT_CTRL;

//! \brief Partition header for FAT filesystem.
typedef struct {
    FAT_TYPE TypeFileSystem;
    uint8_t bSectPerCx;
    uint8_t bNumberFats;
	uint32_t wBytesPerSector;
    uint32_t wNumberRootDirEntries;
    uint32_t wNumberFatSectors;
    uint32_t wStartSectDataArea;
    uint32_t wStartSectPrimaryFat;
    uint32_t wStartSectSecondaryFat;
    uint32_t wStartSectRootDir;
    uint32_t wStartSectData;
    uint32_t dwNumHiddenSectors;
    uint32_t wNumberRootDirSectors;
	uint32_t Rootdirstartcx;
	uint32_t dwTotalsectors;
	uint32_t dwTotalclusters;
} PARTITION_BOOT_SECTOR;

//! \brief Directory control block.
typedef struct {
    uint8_t Device;                 //!< Logical device number
    uint32_t StartSectCurDir;		//!< Start Sector for the current directory
    uint32_t wStartCxCurDir;        //!< Start Cluster Number for current directory. 0 means Root Directory.
    SECT_CTRL Control;              //!< Whether the sector is dirty.
    uint32_t CurSect;               //!< Current Sector Number Loaded in Buffer
    uint32_t NumberFiles;           //!< Number of files in the current dir
    uint32_t * pwBuffer;            //!< Buffer to read device
} DIR_CTRL_BLK;

//! \brief File control block.
typedef struct {
    uint8_t StartNameCharacter;
    uint32_t Attribut;
    uint32_t StartCluster;
    uint32_t Size;
} FILE_CTRL_BLK;

//! \brief TBD.
typedef struct {
    uint8_t Device;
    uint32_t FatSectorCached;    //!< Absolute sector cached in Fat buffer
    SECT_CTRL Control;
    uint32_t FirstPrimaryFatSect;
    uint32_t FirstSecondaryFatSect;
    uint32_t * pwBuffer;
} FAT_STRUCT;

//! \brief TBD.
typedef struct {
    uint8_t RecordNumber;
    uint32_t SectorNumber;
} DIR_REC_LOCATION;

//! \brief CheckDisk cache entry descriptor for a cached sector.
typedef struct {
	int32_t CacheValid;
	int32_t SectorNumber;
	int32_t WriteAttribute;
	int32_t CacheCounter;
} tCACHEDESCR_checkdisk;

//! \brief TBD.
typedef struct {
    bool bInstalled;
    int iPbsSectorOffset;   //!< From the beginning of the data drive
    uint32_t dwSize;
} DATA_DRIVE_PBS_LOC;

//! Type for the functions used to get the next cluster given a previous cluster.
typedef int32_t (*GetNextCxFromFat_t)(uint32_t wCurCx);

//! \brief Global context information for CheckDisk.
//!
//! This structure contains almost all of the global variables used by CheckDisk,
//! so the whole lot of them can be made dynamically allocated.
typedef struct {
    uint8_t * XScratchSpace; //!< Shared between FATEntryStatus and CacheMem_chkdsk, at different times.
    uint32_t * DirRecordBuffer; //!< stDirCtrlBlk.pwBuffer is set to this buffer.
    uint32_t * FATBuffer; //!< stFat.pwBuffer is set to this buffer.
    uint32_t * FATEntryStatus; //!< Points into XScratchSpace. This table holds the status for each FAT entry. Each FAT entry is one bit in this table. If bit set to 1, the entry is used and good. If bit set to 0, the entry is unknown. Only used for FAT12 and FAT16 filesystems.
    uint32_t FATEntryStatusLength; //!< The buffer that FATEntryStatus points to is this number of words long.
    uint32_t * CacheMem_chkdsk[MAX_CACHES]; //!< Points into XScratchSpace, just like FATEntryStatus. Used when initializing the secondary FAT for FAT32 and when copying the primary FAT over the secondary. Also used in place of FATEntryStatus to keep track of used clusters for FAT32.
    tCACHEDESCR_checkdisk CacheDesc_chkdsk[MAX_CACHES]; //!< Cache entry descriptors for cached sectors.
    uint32_t glb_wFileCorrupted; //!< Total number of corrupted files in the current device.
    uint8_t glb_bFailReadSect; //!< This flag indicates if a sector fail to read/write on the current device. This is a critical error and chkdsk activity should be suspended for the current device.
    uint32_t FlagNeedReadSector; //!< Set to 1 need read sector.
    uint32_t cachedSectorSize; //!< Size of a sector in bytes.
    uint32_t cachedSectorSizeInWords; //!< Size of a sector in 32-bit words.
    uint32_t cachedClusterEntryPerSectorShift;
    uint32_t cachedClusterEntryPerSectorMask;
    uint32_t cachedDirRecordsPerSector;
    PARTITION_BOOT_SECTOR stPartitionBootSector; //!< Cached information read from the partition boot sector.
    FAT_STRUCT stFat; //!< Cached information about the FAT.
    GetNextCxFromFat_t GetNextCxFromFat; //!< Pointer to filesystem specific implementation of GetNextCxFromFat.
} checkdisk_context_t;

////////////////////////////////////////////////////////////////////////////////
//   External references
////////////////////////////////////////////////////////////////////////////////

extern checkdisk_context_t * g_checkdisk_context;

////////////////////////////////////////////////////////////////////////////////
//   Prototypes
////////////////////////////////////////////////////////////////////////////////

RtStatus_t AllocateCheckDiskMemory(uint8_t bDiskNum);
void DeallocateCheckDiskMemory(void);
bool CopyPrimaryFatToSecondary(uint8_t bDiskNum, uint32_t TotalFatsectors);
bool InitPartitionBootSectorStruct(uint8_t bLogDevNumber);
bool ScanFilesAndSubDirs(DIR_CTRL_BLK *pstDirCtrlBlk);
void HandleFailReadSector(void);
bool DeleteFileRecord(uint8_t bRecordNumber, uint32_t wSectNumber, DIR_CTRL_BLK *pstDirCtrlBlk);
bool GetFileCtrlBlk(uint8_t bRecordNumber, uint32_t wSectNumber, DIR_CTRL_BLK *pstDirCtrlBlk, FILE_CTRL_BLK *pstFileCtrlBlk);
uint32_t CxToSect(uint32_t wCx);
bool ReadDirSector(uint32_t wSectNumber, DIR_CTRL_BLK *pstDirCtrlBlk);
int  CheckCrossLinkFile(uint32_t wStartCluster,uint8_t blogdevicenumber);
void ReserveCluster(uint32_t wStartCluster,uint8_t blogdevicenumber);
bool ScanDirectory(DIR_CTRL_BLK *pstDirCtrlBlk);
bool ScanAndUpdateFat(uint8_t blogdevicenumber,FAT_TYPE TypeFileSystem);
#if 0
bool CheckPartialFile(uint8_t Device, FILE_CTRL_BLK *file_blk, uint32_t file_cluster_cnt);
#endif

#endif
