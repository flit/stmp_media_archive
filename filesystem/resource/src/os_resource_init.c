////////////////////////////////////////////////////////////////////////////////
//! \addtogroup os_resource
//! @{
//
// Copyright (c) 2004-2008 SigmaTel, Inc.
//
//! \file    os_resource_init.c
//! \brief   Resource Manager Initialization
//!
//! This file implements the initialization code for the Resource Manager.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////

#include "os_resource_internal.h"
#include "components/sb_info/cmp_sb_info.h"

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

extern int32_t FindDriveWithTag(uint32_t wTagForDrive);

////////////////////////////////////////////////////////////////////////////////
// Global variables
////////////////////////////////////////////////////////////////////////////////

rsc_Globals_t g_rsc_Globals;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

//! \brief Initializes the resource subsystem.
//!
//! \param wTag The section tag value within the firmware system drive.
//!
//! \retval RESOURCE_SUCCESS
//! \retval RESOURCE_ERROR
//!
//! \note   This function clear out the resource handle table and test to make
//!         sure the resource file exists
int32_t os_resource_Init(uint32_t wTag)
{
    uint8_t i;
    RtStatus_t retVal;
    
    // Check if already inited. Don't need to do it twice.
    if (g_rsc_Globals.g_bResourceFileOpen)
    {
        return SUCCESS;
    }
    
    for(i=0;i<MAX_RESOURCES_OPEN ;i++){
        g_rsc_Globals.ResourceHandleTable[i].Allocated = false;
    }

    //setup the resource caches
    retVal = util_lru_InitializeCache(&g_rsc_Globals.Caches[PRT_CACHE], 
                                    PRT_CACHE_SIZE,
                                    sizeof(uint32_t) , &g_rsc_Globals.g_PRTCache , &g_rsc_Globals.PRTTempKey,
                                    sizeof(ResourceTableEntry_t), &g_rsc_Globals.g_PRTCacheOffsets, &g_rsc_Globals.PRTTempItem);        
    if(retVal < 0){
        return ERROR_OS_FILESYSTEM_RESOURCE_INIT_FAILED;
    }
    retVal = util_lru_InitializeCache(&g_rsc_Globals.Caches[SRT_CACHE], 
                                    SRT_CACHE_SIZE,
                                    sizeof(uint32_t) , &g_rsc_Globals.g_SRTCache , &g_rsc_Globals.SRTTempKey,
                                    sizeof(ResourceTableEntry_t), &g_rsc_Globals.g_SRTCacheOffsets, &g_rsc_Globals.SRTTempItem);    
    
    if(retVal < 0){
        return ERROR_OS_FILESYSTEM_RESOURCE_INIT_FAILED;
    }

    retVal = util_lru_InitializeCache(&g_rsc_Globals.Caches[TRT_CACHE], 
                                    TRT_CACHE_SIZE,
                                    sizeof(uint32_t) , &g_rsc_Globals.g_TRTCache , &g_rsc_Globals.TRTTempKey,
                                    sizeof(ResourceTableEntry_t), &g_rsc_Globals.g_TRTCacheOffsets, &g_rsc_Globals.TRTTempItem);    
    
    if(retVal < 0){
        return ERROR_OS_FILESYSTEM_RESOURCE_INIT_FAILED;
    }

    retVal = util_lru_InitializeCache(&g_rsc_Globals.Caches[QRT_CACHE], 
                                    QRT_CACHE_SIZE,
                                    sizeof(uint32_t) , &g_rsc_Globals.g_QRTCache , &g_rsc_Globals.QRTTempKey,
                                    sizeof(ResourceTableEntry_t), &g_rsc_Globals.g_QRTCacheOffsets, &g_rsc_Globals.QRTTempItem);    
    
    if(retVal < 0){
        return ERROR_OS_FILESYSTEM_RESOURCE_INIT_FAILED;
    }
    
    tx_mutex_create(&g_rsc_Globals.ResourceCacheMutex, "RscMutex", TX_NO_INHERIT);

    // Search for the system drive containing the resource file. The resource file
    // now lives in the single firmware drive, as a section of the .sb file structure.
    g_rsc_Globals.g_uResourceSystemDrive = DRIVE_TAG_BOOTMANAGER_S;
    
    {
        sb_section_info_t info;
        
        // Get the sector size in bytes for the resource system drive.
        retVal = DriveGetInfo(g_rsc_Globals.g_uResourceSystemDrive, kDriveInfoSectorSizeInBytes, &g_rsc_Globals.bytesPerSector);
        if (retVal != SUCCESS)
        {
            return retVal;
        }

        // Get the offset to the resource section in the .sb file.
        retVal = cmp_sb_info_GetSectionInfo(g_rsc_Globals.g_uResourceSystemDrive, wTag, &info);
        if (retVal != SUCCESS)
        {
            return retVal;
        }
        
        // Convert byte offset to sector offset.
        assert(info.m_offset % g_rsc_Globals.bytesPerSector == 0);

        {   // Compute shift required to avoid division
            int iShift;
            for (iShift=0; iShift<31; iShift++)
            {
                if (g_rsc_Globals.bytesPerSector == (1<<iShift))
                    break;
            }
            g_rsc_Globals.bytesPerSectorShift = iShift;
            g_rsc_Globals.bytesPerSectorMask  = g_rsc_Globals.bytesPerSector - 1;
        }

        g_rsc_Globals.resourceSectionSectorOffset = (info.m_offset >> g_rsc_Globals.bytesPerSectorShift);

        g_rsc_Globals.g_bResourceFileOpen = true;    
    }

    if (g_rsc_Globals.g_bResourceFileOpen)
    {
        g_rsc_Globals.currentResourceSector = 0;
        return SUCCESS;
    }

    return ERROR_OS_FILESYSTEM_RESOURCE_INIT_FAILED;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
