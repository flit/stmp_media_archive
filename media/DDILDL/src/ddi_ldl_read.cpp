///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor. All rights reserved.
// 
// Freescale Semiconductor
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute confidential
// information and may comprise trade secrets of Freescale Semiconductor or its
// associates, and any use thereof is subject to the terms and conditions of the
// Confidential Disclosure Agreement pursual to which this source code was
// originally received.
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media
//! @{
//! \file ddi_ldl_read.c
//! \brief Hardware independent read functions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "error.h"
#include "ddi_media_internal.h"
#include "drivers/media/ddi_media.h"
#include "hw/profile/hw_profile.h"
#include "hw/core/vmemory.h"

extern "C" {
#include "os/thi/os_thi_stack_context.h"
}

#if DEBUG
int32_t iCallsMediaRead = 0, iCallsMediaWrite = 0; 
uint64_t iMicrosecondsMediaRead = 0, iMicrosecondsMediaWrite = 0;
#endif // DEBUG

#if (defined(USE_NAND_STACK) && defined(NO_SDRAM))

#include "os/threadx/tx_api.h"

extern TX_MUTEX g_NANDThreadSafeMutex;

uint32_t g_u32NandStack[NAND_STACK_SIZE/4];
StackContext_t g_NewNandStackContext = { 0 , g_u32NandStack, 0, NAND_STACK_SIZE };
StackContext_t g_OldNandStackContext;

#endif // USE_NAND_STACK && NO_SDRAM

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t DriveReadSector(DriveTag_t tag, uint32_t u32SectorNumber, P_SECTOR_BUFFER pSectorData)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }
    else if (!drive->isInitialized())
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }

#if (defined(USE_NAND_STACK) && defined(NO_SDRAM))
    if (drive->getMedia()->getPhysicalType() != kMediaTypeMMC)
    {
        // We must use a static for the return value because of the stack switching.
        static RtStatus_t s_RetValue;
        TX_THREAD *pCurrentThread;
        
        tx_mutex_get(&g_NANDThreadSafeMutex, TX_WAIT_FOREVER);
        pCurrentThread = tx_thread_identify();
        if (pCurrentThread != NULL)
        {
            os_thi_SaveStackContext(&g_NewNandStackContext, pCurrentThread, &g_OldNandStackContext, 40);
        }

        s_RetValue = drive->readSector(u32SectorNumber, pSectorData);

        if (pCurrentThread != NULL)
        {
            os_thi_RestoreStackContext(&g_OldNandStackContext, pCurrentThread);
        }
        tx_mutex_put(&g_NANDThreadSafeMutex);

        return s_RetValue;
    }
#endif // (defined(USE_NAND_STACK) && defined(NO_SDRAM))
    
    // For SDRAM builds and MMC we don't need to deal with a special NAND stack.
    return drive->readSector(u32SectorNumber, pSectorData);
}

RtStatus_t DriveOpenMultisectorTransaction(DriveTag_t tag, uint32_t startSector, uint32_t sectorCount, bool isRead)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }
    else if (!drive->isInitialized())
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
    
#if (defined(USE_NAND_STACK) && defined(NO_SDRAM))
    if (drive->getMedia()->getPhysicalType() != kMediaTypeMMC)
    {
        // We must use a static for the return value because of the stack switching.
        static RtStatus_t s_RetValue;
        TX_THREAD *pCurrentThread;
        
        tx_mutex_get(&g_NANDThreadSafeMutex, TX_WAIT_FOREVER);
        pCurrentThread = tx_thread_identify();
        if (pCurrentThread != NULL)
        {
            os_thi_SaveStackContext(&g_NewNandStackContext, pCurrentThread, &g_OldNandStackContext, 40);
        }

        s_RetValue = drive->openMultisectorTransaction(startSector, sectorCount, isRead);

        if (pCurrentThread != NULL)
        {
            os_thi_RestoreStackContext(&g_OldNandStackContext, pCurrentThread);
        }
        tx_mutex_put(&g_NANDThreadSafeMutex);

        return s_RetValue;
    }
#endif // (defined(USE_NAND_STACK) && defined(NO_SDRAM))
    
    return drive->openMultisectorTransaction(startSector, sectorCount, isRead);
}

RtStatus_t DriveCommitMultisectorTransaction(DriveTag_t tag)
{
    LogicalDrive * drive = DriveGetDriveFromTag(tag);
    if (!drive)
    {
        return ERROR_DDI_LDL_LDRIVE_INVALID_DRIVE_TAG;
    }
    else if (!drive->isInitialized())
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
    
#if (defined(USE_NAND_STACK) && defined(NO_SDRAM))
    if (drive->getMedia()->getPhysicalType() != kMediaTypeMMC)
    {
        // We must use a static for the return value because of the stack switching.
        static RtStatus_t s_RetValue;
        TX_THREAD *pCurrentThread;
        
        tx_mutex_get(&g_NANDThreadSafeMutex, TX_WAIT_FOREVER);
        pCurrentThread = tx_thread_identify();
        if (pCurrentThread != NULL)
        {
            os_thi_SaveStackContext(&g_NewNandStackContext, pCurrentThread, &g_OldNandStackContext, 40);
        }

        s_RetValue = drive->commitMultisectorTransaction();

        if (pCurrentThread != NULL)
        {
            os_thi_RestoreStackContext(&g_OldNandStackContext, pCurrentThread);
        }
        tx_mutex_put(&g_NANDThreadSafeMutex);

        return s_RetValue;
    }
#endif // (defined(USE_NAND_STACK) && defined(NO_SDRAM))
    
    return drive->commitMultisectorTransaction();
}

//! @}
