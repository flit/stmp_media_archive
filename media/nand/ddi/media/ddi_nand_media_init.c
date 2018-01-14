////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_media
//! @{
//
// Copyright (c) 2003-2007 SigmaTel, Inc.
//
//! \file ddi_nand_media_init.c
//! \brief This file initializes the NAND Media.
//!
//!
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include <string.h>
#include "os/threadx/tx_api.h"
#include "os/threadx/os_tx_errordefs.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_nand_media.h"
#include "ddi_nand_ddi.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand_data_drive.h"
#include "hw/core/vmemory.h"
#include "hw/otp/hw_otp.h"
#include "Mapper.h"
#include "auto_free.h"
#include "drivers/rtc/ddi_rtc_persistent.h"
#include "DeferredTask.h"
#include "ddi_nand_system_drive_recover.h"
#include "NonsequentialSectorsMap.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "VirtualBlock.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! \brief Global information about the NAND media.
Media * g_nandMedia = NULL;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

uint32_t Nand_SetNandBootBlockSearchNumberAndWindow( uint32_t u32NandBootBlockSearchNumber );

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

LogicalMedia * nand_media_factory(const MediaDefinition_t * def)
{
    LogicalMedia * media = new Media();
    media->m_u32MediaNumber = def->m_mediaNumber;
    media->m_isRemovable = def->m_isRemovable;
    media->m_PhysicalType = def->m_mediaType;
    
    return media;
}

Media::Media()
:   LogicalMedia(),
    m_params(NULL),
    m_nssmManager(NULL),
    m_mapper(NULL),
    m_deferredTasks(NULL),
    m_iNumRegions(0),
    m_pRegionInfo(NULL),
    m_iTotalBlksInMedia(0),
    m_iNumBadBlks(0),
    m_iNumReservedBlocks(0),
    m_badBlockTableMode(kNandBadBlockTableInvalid),
    m_globalBadBlockTable(),
    m_bootBlockSearchNumber(0),
    m_bootBlockSearchWindow(0)
{
    memset(m_ConfigBlkAddr, 0, sizeof(m_ConfigBlkAddr));
}

Media::~Media()
{
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Initialize the NAND media.
//!
//! This function is responsible for initializing the pieces of the following
//! descriptors:
//!     - #LogicalMedia_t
//!     - #NandMediaInfo_t
//!     - #NandPhysicalMedia[] via NandHalInit
//!     - #NandParameters_t via NandHalInit
//!
//! This routine also initializes the NAND Hardware via NandHalInit.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//!
//! \post The NAND hardware has been setup and is ready for transfers.  The
//!       media descriptors have been initialized.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::init()
{
    RtStatus_t Status;
    
    // Set global NAND media object.
    g_nandMedia = this;

    // Initialize the NAND serial number to the same as the chip
    hw_otp_GetChipSerialNumber( &g_InternalMediaSerialNumber );

    // Grab the number of Boot blocks to search
    // and then set the NAND driver to use that search window.
    setBootBlockSearchNumberAndWindow( hw_otp_NandBootSearchCount() );

#if RTOS_THREADX
    // Initialize our synchronization objects.
    Status = os_thi_ConvertTxStatus(tx_mutex_create(&g_NANDThreadSafeMutex, "NAND_TS_MUTEX", TX_INHERIT));
    if (Status != SUCCESS)
    {
        return Status;
    }
#endif
    
    // Init the HAL library.
    Status = NandHal::init();
    if (Status != SUCCESS)
    {
        return Status;
    }
    
    // Save parameters pointer.
    m_params = NandHal::getFirstNand()->pNANDParams;

    m_iNumRegions = 0;
    m_pRegionInfo = new Region*[MAX_NAND_REGIONS];
    assert(m_pRegionInfo);
    
    memset(m_pRegionInfo, 0, sizeof(Region*) * MAX_NAND_REGIONS);
    
    m_iTotalBlksInMedia = NandHal::getTotalBlockCount();
    m_badBlockTableMode = kNandBadBlockTableInvalid;

    // Init boot block addresses (9 of them) so that their state is unknown. The
    // bfBlockProblem field of the block address structure has a value of
    // kNandBootBlockUnknown when all bits are set.
    memset(&m_bootBlocks, 0xff, sizeof(m_bootBlocks));
    m_bootBlocks.m_isNCBAddressValid = false;

    // Initialize the LogicalMedia fields
    m_PhysicalType = kMediaTypeNand;
    m_bWriteProtected = false;
    m_bInitialized = true;
    m_u32AllocationUnitSizeInBytes = m_params->pageDataSize;
    m_eState = kMediaStateUnknown;
    m_u64SizeInBytes = (uint64_t)(m_iTotalBlksInMedia << m_params->pageToBlockShift) * (uint64_t)m_u32AllocationUnitSizeInBytes;
    
    // Create the deferred task queue.
    m_deferredTasks = new DeferredTaskQueue;
    assert(m_deferredTasks);
    Status = m_deferredTasks->init();
    if (Status != SUCCESS)
    {
        return Status;
    }
    
    // Create the system drive recovery manager.
    m_recoveryManager = new SystemDriveRecoveryManager;
    assert(m_recoveryManager);
    
    // Create the mapper instance. It won't be inited until the first data drive is inited.
    m_mapper = new Mapper(this);
    assert(m_mapper);
    
    // Create the NSSM manager.
    m_nssmManager = new NssmManager(this);
    assert(m_nssmManager);
    
#if DEBUG
    // Print out the NAND device name.
    auto_free<char> name = NandHal::getFirstNand()->getDeviceName();
    if (name)
    {
        tss_logtext_Print(~0, "NAND: %s\n", name.get());
    }
#endif // DEBUG

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! This function is used to set the members m_bootBlockSearchNumber and
//! m_bootBlockSearchWindow.  These globals control where the NAND driver
//! looks in the NAND to find BCBs.  (i.e. NCBs, LDLBs).
//!
//! \param[in] newSearchNumber The value to set for m_bootBlockSearchNumber.
//!
//! \retval The old value of m_bootBlockSearchNumber is returned.
//!
//! \post m_bootBlockSearchNumber is changed to the given value.
//! \post m_bootBlockSearchWindow is calculated based on newSearchNumber.
////////////////////////////////////////////////////////////////////////////////
uint32_t Media::setBootBlockSearchNumberAndWindow(uint32_t newSearchNumber)
{
    uint32_t oldSearchNumber = m_bootBlockSearchNumber;

    // Re-calculate the search window.
    m_bootBlockSearchNumber = newSearchNumber;
    m_bootBlockSearchWindow = kBootBlockSearchStride * newSearchNumber;

    return oldSearchNumber;
}

uint32_t Nand_SetNandBootBlockSearchNumberAndWindow( uint32_t u32NandBootBlockSearchNumber )
{
    return g_nandMedia->setBootBlockSearchNumberAndWindow(u32NandBootBlockSearchNumber);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Set bootable drive to the one specified in u32Tag
//!
//! This function is responsible for setting the device to boot from primary or
//! Secondary firmware. It does this by setting persistent bit based on 
//! u32DriveTag value
//!
//! \param[in] u32DriveTag Tag for the drive to boot from.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::setBootDrive(DriveTag_t u32DriveTag)
{
    uint32_t u32PersistentValue;
    
    if (u32DriveTag == DRIVE_TAG_BOOTMANAGER_S)
    {
        // Set boot to primary firmware
        u32PersistentValue = 0;
    }
    else
    {
        // Set boot to secondary firmware
        u32PersistentValue = 1;
    }

    return ddi_rtc_WritePersistentField(RTC_NAND_SECONDARY_BOOT, u32PersistentValue);
}

Region::Region()
:   m_regionNumber(0),
    m_iChip(0),
    m_nand(NULL),
    m_pLogicalDrive(NULL),
    m_eDriveType(kDriveTypeUnknown),
    m_wTag(0),
    m_iStartPhysAddr(0),
    m_iNumBlks(0),
    m_u32AbPhyStartBlkAddr(0),
    m_bRegionInfoDirty(false)
{
}

SystemRegion::SystemRegion()
:   Region(),
    m_badBlocks()
{
    m_badBlocks.clear();
}

DataRegion::DataRegion()
:   Region(),
    m_badBlockCount(0),
    m_u32NumLBlks(0)
{
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
