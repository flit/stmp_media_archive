////////////////////////////////////////////////////////////////////////////////
//! \addtogroup os_resource
//! @{
//
// Copyright (c) 2004-2008 SigmaTel, Inc.
//
//! \file    os_resource_internal.h
//! \brief   Resource Manager Internal Header File
//! \version 1.0
//! \date    23-June 2005
//!
//! This file contains definitions needed by the internal (unexposed) resource 
//! manager APIs
////////////////////////////////////////////////////////////////////////////////

#ifndef __RSC_INTERNAL_H
#define __RSC_INTERNAL_H

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "errordefs.h"
#include "os\os_resource_api.h"
#include "components/lru/lru.h"
#include "drivers/media/ddi_media.h"
#include "os/threadx/tx_api.h"
#include "os/filesystem/fsapi.h"
#include "os/filesystem/include/fs_steering.h"
#include "hw/core/vmemory.h"
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//There is always one sector of heading in the resource file
#define RSRC_PADDING_SIZE     (RSRC_SECTOR_SIZE)   //!< Size of padding at head of resource file.

#define RSRC_SIZE_TYPE_SIZE 4       //!< # bytes of size field in resource
#define NUM_RSRC_CACHES 4   //!< should never be more than 4

#define TI1_INDEX_MASK 0x000000FF
#define INDEX_MASK 0x3FFFFFFF

#define RSRC_FILE_NUM_OFFSET RESOURCE_HANDLE_MIN    //!<    First _io_channel value for resources
#define RSRC_LAST_FILE_NUM_OFFSET (RSRC_FILE_NUM_OFFSET + MAX_RESOURCES_OPEN - 1)

//! Resource Type Enumeration
enum RESOURCE_TYPES_ENUM{	//these enums must match those in resourcebuilder
	//! Indicates the table entry points to a sub-table
	RESOURCE_TYPE_NESTED = 0x1,
	//! Indicates the table entry points to an image resource
	RESOURCE_TYPE_IMAGE,	//0x2
	//! Indicates the table entry contains a 28 bit piece of data
	RESOURCE_TYPE_VALUE,	//0x3
	//! Currently an unused resource type
	RESOURCE_TYPE_AUDIO,	//0x4
	//! Currently an unused resource type
	RESOURCE_TYPE_DATA		//0x5
};

//! Resource Table entry structure
typedef union _ResourceTableEntry_t{
    struct{
        uint32_t FileOffset : 28;
	    uint32_t ResourceType : 4;
    };
    uint32_t U;
}ResourceTableEntry_t;

//enum for cache types
typedef enum _ResourceCache{
	PRT_CACHE = 0,
	SRT_CACHE = 1,
	TRT_CACHE = 2,
	QRT_CACHE = 3,
    MAX_OFFSETS_TO_CACHE
} ResourceCache_t;

typedef struct _ResourceCacheLevel{
    uint32_t* Cache;
    uint8_t* Ages;
    ResourceTableEntry_t* Offsets;
    uint8_t CacheSize;
}ResourceCacheLevel_t;

typedef struct _ResourceHandle{
    FILE FileHandle;
    int curPos;
    int begPos;
    int32_t size : 29;
    uint32_t Allocated : 1;
} ResourceHandle_t;

//! A collection of all the global variables that the Resource Manager uses.
typedef struct _rsc_Globals
{
    DriveTag_t g_uResourceSystemDrive;    //!< Drive tag of the resource system drive
    TX_MUTEX ResourceCacheMutex;    //!< Mutex to add Thread Safety to the resource manager
    ResourceTableEntry_t PRTTempItem;   //!< Temp item used in the LRU cache for primary resource table
    ResourceTableEntry_t SRTTempItem;   //!< Temp item used in the LRU cache for secondary resource tables
    ResourceTableEntry_t TRTTempItem;   //!< Temp item used in the LRU cache for tertiary resource tables
    ResourceTableEntry_t QRTTempItem;   //!< Temp item used in the LRU cache for quaternary resource tables
    uint32_t PRTTempKey;    //!< Temp key used in the LRU cache for primary resource table
    uint32_t SRTTempKey;    //!< Temp key used in the LRU cache for secondary resource table 
    uint32_t TRTTempKey;    //!< Temp key used in the LRU cache for tertiary resource table 
    uint32_t QRTTempKey;    //!< Temp key used in the LRU cache for quaternary resource table
    uint32_t g_PRTCache[PRT_CACHE_SIZE];    //!< Cache for Primary Resource Table
    uint32_t g_SRTCache[SRT_CACHE_SIZE];    //!< Cache for Secondary Resource Tables
    uint32_t g_TRTCache[TRT_CACHE_SIZE];    //!< Cache for Tertiary Resource Tables
    uint32_t g_QRTCache[QRT_CACHE_SIZE];    //!< Cache for Quaternary Resource Tables
    ResourceTableEntry_t g_PRTCacheOffsets[PRT_CACHE_SIZE]; //!< Key Cache for Primary Resource Table
    ResourceTableEntry_t g_SRTCacheOffsets[SRT_CACHE_SIZE]; //!< Key Cache for Secondary Resource Tables
    ResourceTableEntry_t g_TRTCacheOffsets[TRT_CACHE_SIZE]; //!< Key Cache for Tertiary Resource Tables
    ResourceTableEntry_t g_QRTCacheOffsets[QRT_CACHE_SIZE]; //!< Key Cache for Quaternary Resource Tables
    util_lru_Cache_t Caches[NUM_RSRC_CACHES];   //!< Container for each of the caches
    uint8_t g_bResourceFileOpen;    //!< Bool value to tell if the Resource File is found or not
    ResourceHandle_t ResourceHandleTable[MAX_RESOURCES_OPEN];   //!< Resource Handle Table (info about open resources)
    uint32_t currentResourceSector; //!< The currently loaded sector of the resource system drive
    uint16_t currentResourceByteOffset; //!< Current Byte offset within the currently loaded sector
    uint32_t resourceSectionSectorOffset; //!< Offset in sectors to the start of the resource section in the .sb file.

    uint32_t bytesPerSector;    //!< Bytes per sector for the resource system drive.
    uint32_t bytesPerSectorShift;   //!< Bytes per sector shift to simplify division by arithmetic SHIFT operation.
    uint32_t bytesPerSectorMask;    //!< Bytes per sector mask to simplify remainder by arithmetic AND operation.

#ifdef _RSRC_CACHE_PROFILING
    int g_NumDirectHits;    //!< vars used for profiling cache hits
    int g_NumPartialHits;   //!< vars used for profiling cahce hits
#endif

} rsc_Globals_t;


////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

extern rsc_Globals_t g_rsc_Globals;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

int32_t os_resource_OpenInternal(uint32_t ResourceID, uint32_t* ResourceSize, uint16_t* ResourceValue);
int32_t os_resource_ReadInternal(int32_t fno, uint8_t *buf, int32_t size);
uint32_t FindCachedResource(os_resource_ResourceId_t ResourceID, uint32_t* indexFound);
void CacheResource(uint32_t ResourceID, ResourceTableEntry_t* OffsetArray, int8_t PathResolved);
RtStatus_t os_resource_Close(int32_t fno);
int32_t os_resource_Read(int32_t fno, uint8_t *buf, int32_t size);
RtStatus_t os_resource_Seek(int32_t fno, int32_t offset, int32_t end);
uint32_t ReadResourceSize(uint32_t ResourcePosition);
RtStatus_t ReadTableEntry(ResourceTableEntry_t *TableEntry, uint32_t TablePos, uint16_t TableEntryNumber);
uint8_t * ResourceSeekToPos(uint32_t ResourcePosition, uint32_t * token);

#endif  // __RSC_INTERNAL_H
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
