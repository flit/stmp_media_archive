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
//! \addtogroup ddi_nand_mapper
//! @{
//! \file    ddi_nand_mapper_get_info.c
//! \brief   Common NAND Logical Block Address Mapper functions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "ddi_nand_media.h"
#include "drivers/media/ddi_media.h"
#include "Mapper.h"
#include <string.h>
#include "drivers/rtc/ddi_rtc.h"
#include "hw/core/vmemory.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include <stdlib.h>
#include "ZoneMapCache.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief   Get Physical block address of an LBA.
//!
//! This function returns the Physical block address of an LBA
//!
//! \param[in]  u32DriveNumber Which Drive to inquire about.
//! \param[in]  u32Lba Logical Block Address to inquire about.
//! \param[out]  pu32PhysAddr Physical Block address corresponding to LBA.
//! \param[out]  pu32TrueLBA True LBA, may be offset if Hidden Data Drive.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
//! \post  If an LBA has been allocated, then a its associated physical address will be
//! returned. Other wise an undefined address will be returned.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ZoneMapCache::getBlockInfo(uint32_t u32Lba, uint32_t * pu32PhysAddr)
{
    int32_t i32SelectedEntryNum;
    RtStatus_t ret;

    assert(m_block.isValid());
    assert(m_topPageIndex);

    ret = lookupCacheEntry(u32Lba, &i32SelectedEntryNum);
    if (ret)
    {
        return ret;
    }

    ret = evictAndLoad(u32Lba, i32SelectedEntryNum);
    if (ret)
    {
        return ret;
    }

    // Construct the entry's address from the 24-bit value.
    *pu32PhysAddr = readMapEntry(&m_descriptors[i32SelectedEntryNum], u32Lba);
    
    // Update the timestamp.
    m_descriptors[i32SelectedEntryNum].m_timestamp = hw_profile_GetMicroseconds();

    return SUCCESS;
}

RtStatus_t Mapper::getBlockInfo(uint32_t u32Lba, uint32_t * pu32PhysAddr)
{
    // Make sure that we are initialized
    if( !m_isInitialized )
    {
        return ERROR_DDI_NAND_MAPPER_UNITIALIZED;
    }

    return m_zoneMap->getBlockInfo(u32Lba, pu32PhysAddr);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Read a value from a zone map section.
//!
//! \param zoneMapSection The section of the zone map containing the entry for \a lba.
//! \param lba The \em true LBA number being modified. \a zoneMapSection must
//!     contain this LBA.
//! \return The physical address associated with \a lba is returned.
////////////////////////////////////////////////////////////////////////////////
uint32_t ZoneMapCache::readMapEntry(CacheEntry * zoneMapSection, uint32_t lba)
{
    uint32_t u32StartingEntry = zoneMapSection->m_firstLBA;    
    uint8_t * entries = zoneMapSection->m_entries;
    uint32_t entryIndex = lba - u32StartingEntry;
    uint32_t physicalAddress;
    
    // Handle the different entry sizes.
    switch (m_entrySize)
    {
        // 16-bit entries
        case kNandZoneMapSmallEntry:
            // We can simply read the value directly from the array.
            physicalAddress = ((uint16_t *)entries)[entryIndex];
            break;

        // 24-bit entries
        case kNandZoneMapLargeEntry:
            // Advance the entry pointer to the entry we are reading.
            entries += entryIndex * kNandZoneMapLargeEntry;

            // Construct the entry's physical address from the 24-bit value.
            physicalAddress = entries[0] | (entries[1] << 8) | (entries[2] << 16);
            
            break;
    }
    
    return physicalAddress;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Get Physical Page information for a logical page address.
//!
//! This function returns the Physical block address and the logical page offset
//! of the logical sector address to the caller.
//!
//! \param[in]  u32PageLogicalAddr Logical page Address to inquire about.
//! \param[out]  pu32LogicalBlkAddr Logical Block address for this logical page address.
//! \param[out]  pu32PhysBlkAddr Physical Block address corresponding to a logical page address.
//! \param[out]  pu32LbaPageOffset Logical page offset within the logical blk address.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
//! \post  If an LBA has been allocated, then a its associated physical address will be
//! returned. Other wise an undefined address will be returned.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Mapper::getPageInfo(
    uint32_t u32PageLogicalAddr,  // Logical page logical address
    uint32_t *pu32LogicalBlkAddr, // Logical Block address
    uint32_t *pu32PhysBlkAddr,    // Physical blk address
    uint32_t *pu32LbaPageOffset)   // Logical page offset
{
    RtStatus_t ret;

    // Make sure that we are initialized
    if (!m_isInitialized)
    {
        return ERROR_DDI_NAND_MAPPER_UNITIALIZED;
    }

    // Convert logical sector address to logical block number and logical page offset.
    NandHal::getFirstNand()->pageToBlockAndOffset(u32PageLogicalAddr, pu32LogicalBlkAddr, pu32LbaPageOffset);

    // Make sure that we are not go out of bound
    if (*pu32LogicalBlkAddr > m_media->getTotalBlockCount())
    {
        return ERROR_DDI_NAND_MAPPER_PAGE_OUTOFBOUND;  // logical page address is out of range
    }

    // Get the True LBA blk addr from the logical lba (may be offset by Total Blocks for HDD)
    ret = getBlockInfo(*pu32LogicalBlkAddr, pu32PhysBlkAddr);
    if (ret)
    {
        return ret;
    }
    
    if (*pu32PhysBlkAddr >= m_media->getTotalBlockCount())
    {
        // This also catches unallocated blocks, i.e. g_MapperDescriptor.unallocatedBlockAddress.
        return ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
