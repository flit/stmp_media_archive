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
//! \file
//! \brief Implementation of NonsequentialSectorsMap and related classes.
////////////////////////////////////////////////////////////////////////////////

#include "NonsequentialSectorsMap.h"
#include "types.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include <string.h>
#include <stdlib.h>
#include "Mapper.h"
#include "hw/core/vmemory.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "NssmManager.h"
#include "VirtualBlock.h"

using namespace nand;

/////////////////////////////////////////////////////////////////////////////////
// Definitions
/////////////////////////////////////////////////////////////////////////////////

#ifndef NDD_LBA_DEBUG_ENABLE
    //! Enable this define to turn on debug messages.
//    #define NDD_LBA_DEBUG_ENABLE
#endif

#define LOG_NSSM_METADATA_ECC_LEVELS 0

#ifndef NSSM_INDUCE_ONE_PAGE_FAILURE
    // #define NSSM_INDUCE_ONE_PAGE_FAILURE 1
#endif

#define     MAX_BUILD_NSSM_READ_TRIES     2

/////////////////////////////////////////////////////////////////////////////////
// Variables
/////////////////////////////////////////////////////////////////////////////////

#if DEBUG && NSSM_INDUCE_ONE_PAGE_FAILURE
// A flag to cause one sector to be omitted from the NSSM.
static bool stc_bNSSMInduceOnePageFailure = false;
#endif

/////////////////////////////////////////////////////////////////////////////////
//  Code
/////////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark --NonsequentialSectorsMap--
#endif

NonsequentialSectorsMap::NonsequentialSectorsMap()
:   RedBlackTree::Node(),
    WeightedLRUList::Node(),
    m_referenceCount(0),
    m_map(),
    m_backupMap(),
    m_virtualBlock(),
    m_backupBlock(),
    m_isVirtualBlockValid(false),
    m_hasBackups(false),
    m_currentPageCount(0)
{
}

NonsequentialSectorsMap::~NonsequentialSectorsMap()
{
    // Invalidate to make sure we are removed from the NSSM index.
    invalidate();
}

void NonsequentialSectorsMap::init(NssmManager * manager)
{
    // Save our manager object.
    m_manager = manager;
    
    // Set the mapper in our virtual block instance.
    m_virtualBlock.setMapper(getMapper());
    m_backupBlock.setMapper(getMapper());
    
    // Init page order maps with the virtual pages per block.
    unsigned pagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    // Don't allocate memory for LSI Table
    m_backupMap.init(pagesPerBlock,0,false);
    m_map.init(pagesPerBlock,0,false);
    // Assign internal array pointer
    m_map.setMapArray(m_manager->getPOBlock());
    invalidate();
}

RtStatus_t NonsequentialSectorsMap::prepareForBlock(uint32_t blockNumber)
{
    // Start from a known state.
    invalidate();
    
    // Set the main virtual block number. Setting this number will clear any cached physical
    // addresses. The backup also has the same virtual block number, but we explicitly set its
    // physical block addresses.
    m_virtualBlock = blockNumber;
    m_backupBlock = blockNumber;

    // Build the sector order map by reading metadata from every page.
    RtStatus_t status = buildMapFromMetadata(m_map, &m_currentPageCount);
    
    // If we were able to build the map then mark us as valid.
    if (status == SUCCESS)
    {
        m_isVirtualBlockValid = true;
        
        // Insert ourself into the NSSM index now that we have a valid block number.
        m_manager->m_index.insert(this);
    }
    
    return status;
}

RtStatus_t NonsequentialSectorsMap::flush()
{
    // If a NSSM has a back-up block, it and primary block have to be merged together.        
    if (m_isVirtualBlockValid && m_hasBackups)
    {
        return mergeBlocks();
    }
    
    return SUCCESS;
}

void NonsequentialSectorsMap::invalidate()
{
    // Remove ourself from the NSSM index before our virtual block number becomes invalid.
    // But only remove if we were in the index to begin with.
    if (m_isVirtualBlockValid)
    {
        m_manager->m_index.remove(this);
    }
    
    // Set initial values for map entries
    m_virtualBlock.clearCachedPhysicalAddresses();
    m_backupBlock.clearCachedPhysicalAddresses();
    m_isVirtualBlockValid = false;
    m_hasBackups = false;
    m_currentPageCount  = 0;
    removeFromLRU();
    
    // Reset the page map.
    m_map.clear();
    m_backupMap.clear();
}

Region * NonsequentialSectorsMap::getRegion()
{
    // Skip already invalid entries.
    if (!m_isVirtualBlockValid)
    {
        return NULL;
    }
    
    return getMedia()->getRegionForBlock(m_virtualBlock);
}

////////////////////////////////////////////////////////////////////////////////
//! This function will read the redundant areas for a LBA to rebuild the
//! NonSequential Sector Map.  The result is placed in one of the SectorMaps
//! in RAM.
//!
//! \param[in]  map The page order map to be filled in from the page metadata.
//! \param[out] filledSectorCount  The number of pages holding data in the block.
//!
//! \retval SUCCESS             If any part of the NSSM was built successfully.
//! \retval SOME_ERR            If none of the NSSM was built successfully, then an
//!                             error code is propagated upward from lower layers.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::buildMapFromMetadata(
    nand::PageOrderMap & map,
    uint32_t * filledSectorCount)
{
    uint32_t virtualPagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    uint32_t thisVirtualOffset;
    RtStatus_t retCode;
	RtStatus_t retCodeLocal;
    uint32_t u32LogicalSectorIdx;
    uint32_t virtualBlockAddress;
    uint32_t u321stLBA = 0;
    uint8_t u8LastPageHandled = kNssmLastPageNotHandled;
    uint32_t topVirtualOffsetToRead;
    int iReads;
    RelocateVirtualBlockTask * relocateTask = NULL;
    PageAddress tempPageAddress;
    
    // Figure out how many planes we'll use. If the physical blocks don't all reside on
    // the same NAND then we cannot use multiplane reads.
    unsigned planeCount = VirtualBlock::getPlaneCount();
    if (!m_virtualBlock.isFullyAllocatedOnOneNand())
    {
        planeCount = 1;
    }
    
    // See if we can call the multiplane version.
    if (planeCount == 2)
    {
        return buildMapFromMetadataMultiplane(map, filledSectorCount);
    }
    
    // Time the building of the map.
    SimpleTimer buildTimer;

    // Create the page object and get a buffer to hold the metadata.
    Page thePage;
    retCode = thePage.allocateBuffers(false, true);
    if (retCode != SUCCESS)
    {
        return retCode;
    }
    
    // Go ahead and get our metadata instance since the buffer addresses won't change.
    Metadata & md = thePage.getMetadata();

    // First, clear the map before we fill it in.
    map.clear();
    
    // Read the RA of last page to check whether kIsInLogicalOrderFlag is set
    // if it is set, it means the pages of this block are written in logical order, 
    // we don't need to read all pages' metadata to build NSSM.
    //
    // We only perform this test if we know that all planes have physical blocks allocated
    // for them, since you cannot have fully logical order otherwise.
    if (m_virtualBlock.isFullyAllocated())
    {
        thisVirtualOffset = virtualPagesPerBlock - 1;
        
        m_virtualBlock.getPhysicalPageForVirtualOffset(thisVirtualOffset, tempPageAddress);
        thePage = tempPageAddress;
        
        iReads = 0;
        do
        {
        	// read Redundant Area of Sector
            retCodeLocal = thePage.readMetadata();
            
            if (retCodeLocal == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    ">>> Got ECC_FIXED_REWRITE_SECTOR reading metadata of vblock %u pblock %u voffset %u\n", m_virtualBlock.get(), thePage.getBlock().get(), thisVirtualOffset);
                
                // Post a deferred task to rewrite this virtual block since it is now marginal.
                if (!relocateTask)
                {
                    relocateTask = new RelocateVirtualBlockTask(m_manager, m_virtualBlock);
                    getMedia()->getDeferredQueue()->post(relocateTask);
                }
            }

            // Convert ECC_FIXED or ECC_FIXED_REWRITE_SECTOR to SUCCESS...
            if (is_read_status_success_or_ecc_fixed(retCodeLocal))
            {
                retCodeLocal = SUCCESS;
            }
        
            // ...and note other errors.
            if (retCodeLocal != SUCCESS)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    "buildMapFromMetadata: read %d failed on page 0x%x, status 0x%x\n", 
                    iReads, thePage.get(), retCodeLocal);
            }
        } while ( retCodeLocal != SUCCESS  &&  ++iReads < MAX_BUILD_NSSM_READ_TRIES );

        if (SUCCESS == retCodeLocal)
        {
            // Get Logical Block Address and Relative Sector Index from RA
            virtualBlockAddress = md.getLba();
            u32LogicalSectorIdx = md.getLsi();
            
            // if Erased 
            if (md.isErased())
            {
                u8LastPageHandled = kNssmLastPageErased;
            }
            else if (u32LogicalSectorIdx >= virtualPagesPerBlock)
            {
                //if LSI is invalid
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "buildMapFromMetadata: LSI out of range (%d >= %d)\n", u32LogicalSectorIdx, virtualPagesPerBlock);

                return ERROR_DDI_NAND_DATA_DRIVE_UBLOCK_HSECTORIDX_OUT_OF_RANGE;
            }
            else if (md.isFlagSet(Metadata::kIsInLogicalOrderFlag) && u32LogicalSectorIdx == thisVirtualOffset)
            {
                // The pages of this block are in logical order
                map.setSortedOrder();
                
                if (filledSectorCount)
                {
                    *filledSectorCount = virtualPagesPerBlock;
                }

                getStatistics().orderedBuildCount++;
                return SUCCESS;
            }
            else
            {
                map.setEntry(u32LogicalSectorIdx, thisVirtualOffset);
                
                if (filledSectorCount)
                {
                    *filledSectorCount = virtualPagesPerBlock;
                }
                
                u8LastPageHandled = kNssmLastPageOccupied;
            }
        }
    }

    // Figure out how many pages to read based on whether the last page was read above or not.
    if (u8LastPageHandled != kNssmLastPageNotHandled)
    {
        // RA of last page is read already, we don't need read it in the below loop
        topVirtualOffsetToRead = virtualPagesPerBlock - 1;
    }
    else
    {
        topVirtualOffsetToRead = virtualPagesPerBlock;
    }
    
    for (thisVirtualOffset=0; thisVirtualOffset < topVirtualOffsetToRead; thisVirtualOffset++)
    {
        // Exit the loop immediately if there is no physical block allocated for the plane.
        retCodeLocal = m_virtualBlock.getPhysicalPageForVirtualOffset(thisVirtualOffset, tempPageAddress);
        if (retCodeLocal == ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR)
        {
            // No physical block, so exit loop.
            break;
        }
        else if (retCodeLocal != SUCCESS)
        {
            // An unexpected error! Return immediately.
            return retCodeLocal;
        }
        thePage = tempPageAddress;

        // Reading this information is very important.  If there is
        // some kind of failure, we will re-try.
        iReads = 0;
        do
        {
			NandEccCorrectionInfo_t eccInfo;

        	// read Redundant Area of Sector
            retCodeLocal = thePage.readMetadata(&eccInfo);

#if DEBUG && NSSM_INDUCE_ONE_PAGE_FAILURE
            // A flag to cause one sector to be omitted from the NSSM.
            if ( stc_bNSSMInduceOnePageFailure )
            {
                retCodeLocal = ERROR_GENERIC;
            }
#endif
            
#if LOG_NSSM_METADATA_ECC_LEVELS
            if (retCodeLocal)
            {
                log_ecc_failures(pRegion, physicalBlock, u32NS_SectorIdx, &eccInfo);
            }
#endif // LOG_NSSM_METADATA_ECC_LEVELS
            
            if (retCodeLocal == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    ">>> Got ECC_FIXED_REWRITE_SECTOR reading metadata of vblock %u pblock %u voffset %u\n", m_virtualBlock.get(), thePage.getBlock().get(), thisVirtualOffset);
                
                // Post a deferred task to rewrite this virtual block since it is now marginal.
                if (!relocateTask)
                {
                    relocateTask = new RelocateVirtualBlockTask(m_manager, m_virtualBlock);
                    getMedia()->getDeferredQueue()->post(relocateTask);
                }
            }

            // Convert ECC_FIXED or ECC_FIXED_REWRITE_SECTOR to SUCCESS...
            if (is_read_status_success_or_ecc_fixed(retCodeLocal))
            {
                retCodeLocal = SUCCESS;
            }
        
            // ...and note other errors.
            if (retCodeLocal != SUCCESS)
            {
                // Print an advisory message that there was an error on one page.
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    "buildMapFromMetadata: read %d failed on page 0x%x, status 0x%x\n", 
                    iReads, thePage.get(), retCodeLocal);
            }
        } while ( retCodeLocal != SUCCESS  &&  ++iReads < MAX_BUILD_NSSM_READ_TRIES );

#if DEBUG && NSSM_INDUCE_ONE_PAGE_FAILURE
        // A flag to cause one sector to be omitted from the NSSM.
        stc_bNSSMInduceOnePageFailure = false;
#endif

        // Okay, did the reads work?
        if (SUCCESS != retCodeLocal)
        {
            // No, the reads did not work.
            // We still want to use any remaining sectors, so we will continue on.
            continue;
        }

        // If we got here, then we were successful reading the sector.
        // We set retCode accordingly, to indicate that SOMETHING worked.
        retCode = SUCCESS;

        // If erased, then exit the loop. Physical pages are written sequentially within a
        // block, so we know there's no more data beyond this.
        if (md.isErased())
        {
            break;
        }
        
        // Get the virtual block address and logical sector index from the page's metadata.
        virtualBlockAddress = m_virtualBlock.getVirtualBlockFromMapperKey(md.getLba());
        u32LogicalSectorIdx = md.getLsi();

        // Do a sanity check
        if (thisVirtualOffset == 0)
        {
            u321stLBA = virtualBlockAddress;
        }
        else if (u321stLBA != virtualBlockAddress)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "buildMapFromMetadata: LBA mismatch (%d != %d)\n", u321stLBA, virtualBlockAddress);

            return ERROR_DDI_NAND_DATA_DRIVE_UBLOCK_LBAS_INCONSISTENT;
        }

        // Another sanity check
        if (u32LogicalSectorIdx >= virtualPagesPerBlock)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "buildMapFromMetadata: LSI out of range (%d >= %d)\n", u32LogicalSectorIdx, virtualPagesPerBlock);

            return ERROR_DDI_NAND_DATA_DRIVE_UBLOCK_HSECTORIDX_OUT_OF_RANGE;
        }

        // Stuff the map bytes
        map.setEntry(u32LogicalSectorIdx, thisVirtualOffset);
    }
    
    if (filledSectorCount && (u8LastPageHandled == kNssmLastPageNotHandled || u8LastPageHandled == kNssmLastPageErased))
    {
        // The last page is not used, get the last used page here
        *filledSectorCount = thisVirtualOffset;
    }

    // Increment the count of instances in which the NSSM was built (used for performance
    // measurements).
    NssmManager::Statistics & stats = getStatistics();
    stats.buildCount++;
    stats.averageBuildTime += buildTimer;
    stats.blockDepthSum += thisVirtualOffset;
    stats.averageBlockDepth = stats.blockDepthSum / (stats.buildCount + stats.multiBuildCount);

    // The return-code is as follows:
    // If any of the reads worked, then retCode was set to SUCCESS, and that is what gets returned.
    // If none of the reads worked, then retCode is not SUCCESS, and retCodeLocal contains the
    // code from the last failure.
    return (SUCCESS == retCode ? retCode : retCodeLocal);
}

////////////////////////////////////////////////////////////////////////////////
//! This function will read the redundant areas for a LBA to rebuild the
//! NonSequential Sector Map.  The result is placed in one of the SectorMaps
//! in RAM.
//!
//! \param[in]  map The page order map to be filled in from the page metadata.
//! \param[out] filledSectorCount  The number of pages holding data in the block.
//!
//! \retval SUCCESS             If any part of the NSSM was built successfully.
//! \retval SOME_ERR            If none of the NSSM was built successfully, then an
//!                             error code is propagated upward from lower layers.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::buildMapFromMetadataMultiplane(
    nand::PageOrderMap & map,
    uint32_t * filledSectorCount)
{
    uint32_t virtualPagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    uint32_t thisVirtualOffset;
    RtStatus_t retCode;
	RtStatus_t retCodeLocal;
    uint32_t u32LogicalSectorIdx;
    uint8_t u8LastPageHandled = kNssmLastPageNotHandled;
    uint32_t topVirtualOffsetToRead;
    RelocateVirtualBlockTask * relocateTask = NULL;
    PageAddress tempPageAddress;
    NandPhysicalMedia::MultiplaneParamBlock pb[VirtualBlock::kMaxPlanes] = {0};
    unsigned planeNumber;
    bool bErasedPageFound = false;
    
    // Figure out how many planes we'll use. If the physical blocks don't all reside on
    // the same NAND then we cannot use multiplane reads.
    unsigned planeCount = VirtualBlock::getPlaneCount();
    assert(planeCount == 2 && m_virtualBlock.isFullyAllocatedOnOneNand());
    
    // Time the building of the map.
    SimpleTimer buildTimer;

    // Acquire our buffers to hold the metadata.
    AuxiliaryBuffer auxBuffers[2];
    retCode = auxBuffers[0].acquire();
    if (retCode != SUCCESS)
    {
        return retCode;
    }
    retCode = auxBuffers[1].acquire();
    if (retCode != SUCCESS)
    {
        return retCode;
    }
    
    // Fill in the buffers in the param blocks.
    pb[0].m_auxiliaryBuffer = auxBuffers[0];
    pb[1].m_auxiliaryBuffer = auxBuffers[1];
    
    // Get the NAND object we're reading from.
    m_virtualBlock.getPhysicalPageForVirtualOffset(0, tempPageAddress);
    NandPhysicalMedia * theNand = tempPageAddress.getNand();

    // First, clear the map before we fill it in.
    map.clear();
    
    // Read the RA of last page to check whether kIsInLogicalOrderFlag is set
    // if it is set, it means the pages of this block are written in logical order, 
    // we don't need to read all pages' metadata to build NSSM.
    //
    // We only perform this test if we know that all planes have physical blocks allocated
    // for them, since you cannot have fully logical order otherwise.
    if (m_virtualBlock.isFullyAllocated())
    {
        thisVirtualOffset = virtualPagesPerBlock - planeCount;
        
        // Fill in page addresses.
        for (planeNumber = 0; planeNumber < planeCount; ++planeNumber)
        {
            m_virtualBlock.getPhysicalPageForVirtualOffset(thisVirtualOffset + planeNumber, tempPageAddress);
            pb[planeNumber].m_address = tempPageAddress.getRelativePage();
        }
        
        // Read multiple metadata at once.
        retCodeLocal = theNand->readMultipleMetadata(pb, planeCount);
        if (retCodeLocal != SUCCESS)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                "buildMapFromMetadata: read multi failed status 0x%x\n", retCodeLocal);
            return retCodeLocal;
        }

        // Examine results.
        for (planeNumber = 0; planeNumber < planeCount; ++planeNumber)
        {
            retCodeLocal = pb[planeNumber].m_resultStatus;
            
            if (retCodeLocal == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    ">>> Got ECC_FIXED_REWRITE_SECTOR reading metadata of vblock %u nand %u page %u voffset %u\n", m_virtualBlock.get(), theNand->wChipNumber, pb[planeNumber].m_address, thisVirtualOffset + planeNumber);
                
                // Post a deferred task to rewrite this virtual block since it is now marginal.
                if (!relocateTask)
                {
                    relocateTask = new RelocateVirtualBlockTask(m_manager, m_virtualBlock);
                    getMedia()->getDeferredQueue()->post(relocateTask);
                }
            }
            
            // Convert ECC_FIXED or ECC_FIXED_REWRITE_SECTOR to SUCCESS...
            if (is_read_status_success_or_ecc_fixed(retCodeLocal))
            {
                retCodeLocal = SUCCESS;
            }
        
            // ...and note other errors.
            if (retCodeLocal != SUCCESS)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    "buildMapFromMetadata: read multi failed on page 0x%x, status 0x%x\n", 
                    pb[planeNumber].m_address, retCodeLocal);
            }

            if (SUCCESS == retCodeLocal)
            {
                Metadata md(pb[planeNumber].m_auxiliaryBuffer);
                
                // Get Logical Block Address and Relative Sector Index from RA
                u32LogicalSectorIdx = md.getLsi();
                
                bool isLastPage = (thisVirtualOffset + planeNumber == virtualPagesPerBlock - 1);
                
                // if Erased 
                if (md.isErased())
                {
                    if (u8LastPageHandled != kNssmLastPageOccupied)
                    {
                        u8LastPageHandled = kNssmLastPageErased;
                    }

                    // There shouldn't be any more filled pages.
                    break;
                }
                
                if (u32LogicalSectorIdx >= virtualPagesPerBlock)
                {
                    //if LSI is invalid
                    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "buildMapFromMetadata: LSI out of range (%d >= %d)\n", u32LogicalSectorIdx, virtualPagesPerBlock);

                    return ERROR_DDI_NAND_DATA_DRIVE_UBLOCK_HSECTORIDX_OUT_OF_RANGE;
                }
                
                if (isLastPage && md.isFlagSet(Metadata::kIsInLogicalOrderFlag))
                {
                    // The pages of this block are in logical order
                    map.setSortedOrder();
                    
                    if (filledSectorCount)
                    {
                        *filledSectorCount = virtualPagesPerBlock;
                    }

                    getStatistics().orderedBuildCount++;
                    return SUCCESS;
                }

                // Set the map entry.
                map.setEntry(u32LogicalSectorIdx, thisVirtualOffset + planeNumber);
                
                // Set the number of filled sectors.
                if (filledSectorCount)
                {
                    *filledSectorCount = thisVirtualOffset + planeNumber + 1;
                }
                
                u8LastPageHandled = kNssmLastPageOccupied;
            }
        }
    }

    topVirtualOffsetToRead = virtualPagesPerBlock;
    
    for (thisVirtualOffset=0; thisVirtualOffset < topVirtualOffsetToRead ; thisVirtualOffset += planeCount)
    {
        // Fill in page addresses.
        for (planeNumber = 0; planeNumber < planeCount; ++planeNumber)
        {
            retCodeLocal = m_virtualBlock.getPhysicalPageForVirtualOffset(thisVirtualOffset + planeNumber, tempPageAddress);
            if (retCodeLocal != SUCCESS)
            {
                break;
            }
            
            pb[planeNumber].m_address = tempPageAddress.getRelativePage();
        }
        
        // Exit the loop immediately if there is no physical block allocated for the plane.
        if (retCodeLocal == ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR)
        {
            // No physical block, so exit loop.
            break;
        }
        else if (retCodeLocal != SUCCESS)
        {
            // An unexpected error! Return immediately.
            return retCodeLocal;
        }
        
        // Read multiple metadata at once.
        retCodeLocal = theNand->readMultipleMetadata(pb, planeCount);
        if (retCodeLocal != SUCCESS)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                "buildMapFromMetadata: read multi failed status 0x%x\n", retCodeLocal);
            return retCodeLocal;
        }

        // Examine results.
        for (planeNumber = 0; planeNumber < planeCount; ++planeNumber)
        {
            retCodeLocal = pb[planeNumber].m_resultStatus;
            
            if (retCodeLocal == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    ">>> Got ECC_FIXED_REWRITE_SECTOR reading metadata of vblock %u nand %u page %u voffset %u\n", m_virtualBlock.get(), theNand->wChipNumber, pb[planeNumber].m_address, thisVirtualOffset + planeNumber);
                
                // Post a deferred task to rewrite this virtual block since it is now marginal.
                if (!relocateTask)
                {
                    relocateTask = new RelocateVirtualBlockTask(m_manager, m_virtualBlock);
                    getMedia()->getDeferredQueue()->post(relocateTask);
                }
            }

            // Convert ECC_FIXED or ECC_FIXED_REWRITE_SECTOR to SUCCESS...
            if (is_read_status_success_or_ecc_fixed(retCodeLocal))
            {
                retCodeLocal = SUCCESS;
            }
        
            // ...and note other errors.
            if (retCodeLocal != SUCCESS)
            {
                // Print an advisory message that there was an error on one page.
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                    "buildMapFromMetadata: read multi failed on page 0x%x, status 0x%x\n", 
                    pb[planeNumber].m_address, retCodeLocal);
                break;
            }
            
            // If we got here, then we were successful reading the sector.
            // We set retCode accordingly, to indicate that SOMETHING worked.
            retCode = SUCCESS;

            Metadata md(pb[planeNumber].m_auxiliaryBuffer);
            
            // If erased, then exit the loop. Physical pages are written sequentially within a
            // block, so we know there's no more data beyond this.
            if (md.isErased())
            {
                if (filledSectorCount
                    && (u8LastPageHandled == kNssmLastPageNotHandled
                        || u8LastPageHandled == kNssmLastPageErased))
                {
                    // The last page is not used, get the last used page here.
                    // The planeNumber will still be valid from the loop above.
                    *filledSectorCount = thisVirtualOffset + planeNumber;
                }
                bErasedPageFound = true;
                break;
            }
            
            // Get the virtual block address and logical sector index from the page's metadata.
            u32LogicalSectorIdx = md.getLsi();

            // Another sanity check
            if (u32LogicalSectorIdx >= virtualPagesPerBlock)
            {
                tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "buildMapFromMetadata: LSI out of range (%d >= %d)\n", u32LogicalSectorIdx, virtualPagesPerBlock);

                return ERROR_DDI_NAND_DATA_DRIVE_UBLOCK_HSECTORIDX_OUT_OF_RANGE;
            }

            // Stuff the map bytes
            map.setEntry(u32LogicalSectorIdx, thisVirtualOffset + planeNumber);
        }
        if (retCodeLocal != SUCCESS)
        {
            // Check if 1st plane also resulted in crash.
            if (planeNumber == 0 && pb[1].m_resultStatus == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
            {
                thisVirtualOffset += planeCount; 
            }
            else
            {
                thisVirtualOffset += (planeNumber + 1); 
            }
            break;
        }
        else if (bErasedPageFound == true)
        {
            thisVirtualOffset += planeNumber; 
            break;
        }
    }
    
    // Set the filled count in the case where the read loop above completed.
    if (thisVirtualOffset >= topVirtualOffsetToRead
        && filledSectorCount
        && (u8LastPageHandled == kNssmLastPageNotHandled
            || u8LastPageHandled == kNssmLastPageErased))
    {
        // The last page is not used, get the last used page here.
        // The planeNumber will still be valid from the loop above.
        *filledSectorCount = thisVirtualOffset;
    }
    
    // Increment the count of instances in which the NSSM was built (used for performance
    // measurements).
    NssmManager::Statistics & stats = getStatistics();
    stats.multiBuildCount++;
    stats.averageMultiBuildTime += buildTimer;
    stats.blockDepthSum += thisVirtualOffset;
    stats.averageBlockDepth = stats.blockDepthSum / (stats.buildCount + stats.multiBuildCount);

    // The return-code is as follows:
    // If any of the reads worked, then retCode was set to SUCCESS, and that is what gets returned.
    // If none of the reads worked, then retCode is not SUCCESS, and retCodeLocal contains the
    // code from the last failure.
    return (SUCCESS == retCode ? retCode : retCodeLocal);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Add NS Sectors Map entry in RAM if currently in RAM NS SectorsMap.
//!
//! This function adds a sector to the NonSequential Sector Map.  If the
//! NS Sectors Map for a given LBA Block is in RAM, it gets updated, but it
//! doesn't try to refresh the map if it is not in RAM.  We don't worry about
//! it because it will be properly constructed, based on the Redundant Areas
//! in the LBA Block when it is needed.
//!
//! \param logicalOffset Index of the sector in the LBA.
//! \param virtualOffset Sector Index of the actual sector.
////////////////////////////////////////////////////////////////////////////////
void NonsequentialSectorsMap::addEntry(uint32_t logicalOffset, uint32_t virtualOffset)
{
    // Verify the sector indexes.
    assert(logicalOffset < VirtualBlock::getVirtualPagesPerBlock());
    assert(virtualOffset < VirtualBlock::getVirtualPagesPerBlock());

    // Update the page order map.
    m_map.setEntry(logicalOffset, virtualOffset);
    
    // Verify that we're writing into the correct location.
    assert(m_currentPageCount == virtualOffset);
    
    // Increment the next page offset.
    m_currentPageCount++;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Check whether the first N-1 pages are written in logical order
//!
//! \return TRUE or FALSE
////////////////////////////////////////////////////////////////////////////////
bool NonsequentialSectorsMap::isInLogicalOrder()
{
    return m_map.isInSortedOrder(m_map.getEntryCount() - 1);
}

////////////////////////////////////////////////////////////////////////////////
//! This function simply frees back-up blocks.  This function is called
//! when "new" block completely overwrites the back-up block and 
//! therefore merge is not necessary.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::shortCircuitMerge()
{
    // This value is used for performance analysis.
    getStatistics().mergeCountShortCircuit++;
    
    // Just free the backup physical blocks.
    RtStatus_t status = m_backupBlock.freeAndEraseAllPlanes();
    if (status != SUCCESS)
    {
        return status;
    }

    m_backupMap.clear();
    m_hasBackups = false;

    return status;
}

#define TRANSFER_SEQUENCE_UPDATE 1

// Function for quickMerge operation
int NonsequentialSectorsMap::scanPlane_quickMerge(VirtualPageRange_t &range)
{
    for (; range.start < range.end; range.start++)
    {
        int idx=range.start;

        // Validate if this page is in target plane
        if ( (m_map[idx] & range.planeMask) != range.uTargetPlane )
            continue;

        bool isOldOccupied = m_backupMap.isOccupied(idx);
        bool isNewOccupied = m_map.isOccupied(idx);
        
        // Get a page which is occupied in backup/old map and not present in primary map
        if (!isNewOccupied && isOldOccupied)
        {
            range.start ++;
            return idx;
        }        
    }
    return range.end;
}

int NonsequentialSectorsMap::scanPlane_mergeBlocksCore(
    VirtualPageRange_t &range, unsigned uOffsetToSkip, VirtualBlock **sourceBlock)
{
    *sourceBlock = NULL;
    for (; range.start < range.end; range.start++)
    {
        if ( uOffsetToSkip == range.start )
            continue;
        int idx=range.start;
        
        // After SDK-7146, plane comparison can be simplified as follows since memory is shared
        if ( (m_map[idx] & range.planeMask) != range.uTargetPlane )
             continue;
        
        bool isOldOccupied = m_backupMap.isOccupied(idx);
        bool isNewOccupied = m_map.isOccupied(idx);
        
        // Get a page which is occupied in backup/old map and not present in primary map
        if (isNewOccupied)
        {
            *sourceBlock = &m_virtualBlock;
            range.start++;
            return idx;
        }        
        else if (isOldOccupied)
        {
            *sourceBlock = &m_backupBlock;
            range.start++;
            return idx;
        }
    }
    return range.end;
}

////////////////////////////////////////////////////////////////////////////////
//! This function merges "old" block into "new" block in place.  This function
//! is only called when there is enough free space in "new" block to accommodate
//! all sectors in "old" block which are not overshadowed by sectors in "new"
//! block.
//!
//! As a result, when this function is finished, the "new" block should be 
//! completely full.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::quickMerge()
{
    uint32_t virtualPagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    int32_t i;
    RtStatus_t retCode;
    VirtualPageRange_t vpr[VirtualBlock::kMaxPlanes];

    // This value is used for performance analysis.
    getStatistics().mergeCountQuick++;
    
    // Get a sector buffer.
    SectorBuffer sectorBuffer;
    if ((retCode = sectorBuffer.acquire()) != SUCCESS)
    {
        return retCode;
    }

    AuxiliaryBuffer auxBuffer;
    if ((retCode = auxBuffer.acquire()) != SUCCESS)
    {
        return retCode;
    }
    
    // Create our filter.
    CopyPagesFlagFilter copyFilter;
    
    unsigned planeMask = VirtualBlock::getPlaneCount() - 1; 
    // Initialize iterators
    vpr[0].init(0);
    vpr[1].init(1);
    int virtualoffset;
    
    // For each sector, first look up the sector in both the old and new maps. If it is present
    // only in the old map, then we copy into the primary (new) block.
    for (i=0; i < virtualPagesPerBlock; i++)
    {
#if TRANSFER_SEQUENCE_UPDATE
        int TargetPlane = m_currentPageCount & planeMask;
        // Try to get a page from target plane
        virtualoffset = scanPlane_quickMerge(vpr[TargetPlane]);
        // Is this a valid page ?
        if ( virtualoffset == vpr[TargetPlane].end )
        {
            // If all pages from this plane are consumed then get page across the plane.
            TargetPlane++;
            TargetPlane &= planeMask;
            virtualoffset = scanPlane_quickMerge(vpr[TargetPlane]);            
            // Is this a valid page ?
            if ( virtualoffset == vpr[TargetPlane].end )
            {
                // If we do not find any page means we have traversed complete map.
                break;
            }
        }
        assert(virtualoffset < virtualPagesPerBlock);
        
        if ( virtualoffset < virtualPagesPerBlock )
#else         
        virtualoffset  = i; // Initially this was the case
        bool isOldOccupied = m_backupMap.isOccupied(virtualoffset);
        bool isNewOccupied = m_map.isOccupied(virtualoffset);
        
        if (!isNewOccupied && isOldOccupied)
#endif        
        {
            // After memory reduction there is single LSI table shared between 2 maps.
            uint32_t u32SourceSectorIdx = m_map[virtualoffset]; //m_backupMap[i];
            assert(u32SourceSectorIdx < virtualPagesPerBlock);
            
            PageAddress sourcePage;
            if (m_backupBlock.getPhysicalPageForVirtualOffset(u32SourceSectorIdx, sourcePage) != SUCCESS)
            {
                break;
            }

            PageAddress targetPage;
            if (m_virtualBlock.getPhysicalPageForVirtualOffset(m_currentPageCount, targetPage) != SUCCESS)
            {
                break;
            }
            
            NandPhysicalMedia * sourceNand = sourcePage.getNand();
            NandPhysicalMedia * targetNand = targetPage.getNand();
            
            // Copy one page.
            uint32_t successfulCopies;
            {
                // Initialize auxiliary buffer for movePage API
                Metadata md(auxBuffer);            
                md.prepare(m_virtualBlock.getMapperKeyFromVirtualOffset(m_currentPageCount),virtualoffset);
                if (i == virtualPagesPerBlock - 1 && m_map.isInSortedOrder(virtualPagesPerBlock - 1)) 
                { 
                    // In practice there are very less chances of reaching this place
                    // However, if it appeares, why not set logical order flag to improve buildMap time.
                    md.setFlag(Metadata::kIsInLogicalOrderFlag);                     
                    ++getStatistics().mergeSetOrderedCount; 
                } 
                else
                {
                    md.clearFlag(Metadata::kIsInLogicalOrderFlag);
                }
                // Initialize filter for copyPage API
                copyFilter.setLba(m_virtualBlock.getMapperKeyFromVirtualOffset(m_currentPageCount));
                // At any point in time either movePage or copyPage API will be called.
                successfulCopies = 0;
                // Retry if copypage operation fails
                retCode = sourceNand->copyPages(
                    targetNand,
                    sourcePage.getRelativePage(),
                    targetPage.getRelativePage(),
                    1,
                    sectorBuffer,
                    auxBuffer,
                    &copyFilter,
                    &successfulCopies);
            }
           
            if (retCode == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                // The write failed, so we need to copy all data into a new block.
                return recoverFromFailedWrite(i, kInvalidAddress);
            }
            else if (!is_read_status_success_or_ecc_fixed(retCode))
            {
                return retCode;
            }
	
            m_map.setEntry(virtualoffset, m_currentPageCount);
            m_currentPageCount += successfulCopies;
        }
    }
    
    assert(m_currentPageCount <= virtualPagesPerBlock);

    // Erase the backup block and mark it free in the phymap. The short circuit merge does
    // this for us.
    retCode = shortCircuitMerge();
    getStatistics().mergeCountShortCircuit--;   // Counter increment in shortCircuitMerge().

    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief This function combines paired blocks into a single new block.
//!
//! \param[in] u32NewSectorNumber  Number of new sector which needs to be written.
//!     This logical sector will be excluded when copying sectors from original
//!     block contents.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::mergeBlocksSkippingPage(uint32_t u32NewSectorNumber)
{
    RtStatus_t retCode;
    uint32_t virtualPagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();

    // Compute free pages remaining in primary block.
    int freePhysicalPages = virtualPagesPerBlock - m_currentPageCount;
    
    // Figure out how many logical sectors exist only in the backup block. If there are logical
    // sectors that are only in the backup block then we must copy them either into the primary
    // block or do a full merge into a new block.
    int entriesOnlyInBackup = m_backupMap.countEntriesNotInOtherMap(m_map);
    
    // If the number of unique sectors in "current" block is equal to total number of
    // sectors in block, the current block completely overwrites the back-up block.  
    // In that case, no merge is necessary.
    if (entriesOnlyInBackup == 0)
    {
        // The backup block can simply be disposed of.
        retCode = shortCircuitMerge();
    }
    // Find out if the number of sectors that exist only in the backup block will fit
    // in the room remaining in the primary block.
    else if (entriesOnlyInBackup <= freePhysicalPages)
    {
        // We can simply copy those sectors that exist unique in the backup block into
        // the primary block.
        retCode = quickMerge();
    }
    else
    {
        // No option but to do a full merge into a new block.
        retCode = mergeBlocksCore(u32NewSectorNumber);
    }
    
    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Combines the primary and backup blocks into a newly allocated block.
//!
//! 
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::mergeBlocksCore(uint32_t u32NewSectorNumber)
{
    uint32_t pagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();
    uint32_t u32RetryCount = 0;
    RtStatus_t status;
    bool hadBackup = hasBackup();
    
    // Time the whole merge.
    SimpleTimer mergeTimer;
                                       
    // This value is used for performance analysis.
    getStatistics().mergeCountCore++;

    // Get a sector buffer.
    SectorBuffer sectorBuffer;
    if ((status = sectorBuffer.acquire()) != SUCCESS)
    {
        return status;
    }

    AuxiliaryBuffer auxBuffer;
    if ((status = auxBuffer.acquire()) != SUCCESS)
    {
        return status;
    }
    
    // Allocate the order map for the new block we're merging into.
    PageOrderMap targetMap;
    status = targetMap.init(pagesPerBlock);
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Create a copy of our virtual block and allocate new physical blocks to merge into. The
    // source physical blocks will still be saved in m_virtualBlock.
    VirtualBlock targetBlock = m_virtualBlock;
    status = targetBlock.allocateAllPlanes();
    if (status != SUCCESS)
    {
        return status;
    }

    // Create our filter.
    CopyPagesFlagFilter copyFilter;

    // For each sector, first look up the sector in new Non-sequential sector map.
    // If entry in new non-sequential sector map is invalid, look up in old
    // non-sequential sector map.
copy_loop_start:
    int32_t logicalSector;
    uint32_t runPageCount = 0;
    int32_t runStartPage = -1;
    VirtualBlock * runSourceBlock = NULL;
    VirtualBlock * sourceBlock = NULL;
    uint32_t targetVirtualPageOffset = 0;
    uint32_t startEntry = 0;    // Logical sector offset for the start of the run.
    
    // Clear the set-logical-order flag in case we had to start the loop over due to a failed write.
    copyFilter.setLogicalOrderFlag(false);
    
#if TRANSFER_SEQUENCE_UPDATE
    unsigned virtualPagesPerBlock = pagesPerBlock;
    unsigned planeMask = VirtualBlock::getPlaneCount() - 1; 
    unsigned virtualoffset;
    VirtualPageRange_t vpr[VirtualBlock::kMaxPlanes];
    vpr[0].init(0);
    vpr[1].init(1);

    for (logicalSector=0; logicalSector < pagesPerBlock; logicalSector++)
    {
        int TargetPlane = targetVirtualPageOffset & planeMask;
        // Try to get a page from target plane
        virtualoffset = scanPlane_mergeBlocksCore(vpr[TargetPlane],u32NewSectorNumber,&sourceBlock);
        // Is this a valid page ?
        if ( virtualoffset == vpr[TargetPlane].end )
        {
            // If all pages from this plane are consumed then get page across the plane.
            TargetPlane++;
            TargetPlane &= planeMask;
            virtualoffset = scanPlane_mergeBlocksCore(vpr[TargetPlane],u32NewSectorNumber,&sourceBlock);            
            // Is this a valid page ?
            if ( virtualoffset == vpr[TargetPlane].end )
            {
                // If we do not find any page means we have traversed complete map.
                break;
            }
        }
        assert(virtualoffset < virtualPagesPerBlock);

        // Just to remove build warning in release build.
        if (virtualPagesPerBlock > virtualoffset) {}     

        runSourceBlock = sourceBlock;
        runStartPage = m_map[virtualoffset]; 
        startEntry = virtualoffset;
#else
    uint32_t sourceVirtualPageOffset;
    unsigned planeMask = VirtualBlock::getPlaneCount() - 1; 
    for (logicalSector=0; logicalSector < pagesPerBlock; logicalSector++)
    {
        bool hasSector = false;
        bool isPartOfRun = false;
        
        // If we are not replacing this sector
        if (logicalSector != u32NewSectorNumber)
        {
            bool isOldOccupied = hadBackup && m_backupMap.isOccupied(logicalSector);
            bool isNewOccupied = m_map.isOccupied(logicalSector);
            
            if (isNewOccupied || isOldOccupied)
            {
                if (isNewOccupied)
                {
                    sourceVirtualPageOffset = m_map[logicalSector];
                    assert(sourceVirtualPageOffset < pagesPerBlock);

                    sourceBlock = &m_virtualBlock;
                }
                else
                {
                    // After memory reduction there is single LSI table shared between 2 maps.
                    sourceVirtualPageOffset = m_map[logicalSector]; //m_backupMap[logicalSector];
                    assert(sourceVirtualPageOffset < pagesPerBlock);

                    sourceBlock = &m_backupBlock;
                }

                hasSector = true;
            }
        }
                
        // See if this physical page is the next one in our current run of pages.
        if (hasSector)
        {
            // First see if we didn't have a previous run and need to start a new one.
            if (!runPageCount)
            {
                runPageCount = 1;
                runSourceBlock = sourceBlock;
                runStartPage = sourceVirtualPageOffset;
                startEntry = logicalSector;
                isPartOfRun = true;
            }
            // Check if this is a continuation of the previous run.
            else if (sourceBlock == runSourceBlock
                    && sourceVirtualPageOffset == runStartPage + runPageCount)
            {
                ++runPageCount;
                isPartOfRun = true;
            }
            // Otherwise, this page is the start of a new run.
            else
            {
                isPartOfRun = false;
            }

            // If this is the last logical page then we need to copy, so don't loop.
            if (isPartOfRun && logicalSector < pagesPerBlock - 1)
            {
                continue;
            }
        }

        // Copy the current run if there is at least one page in it.
        // Even though we compute runs of sequential virtual page offsets to copy, we currently
        // only copy one page at a time.
        while (runPageCount)
#endif
        {
            PageAddress sourcePage;
            if (runSourceBlock->getPhysicalPageForVirtualOffset(runStartPage, sourcePage) != SUCCESS)
            {
                break;
            }

            PageAddress targetPage;
            if (targetBlock.getPhysicalPageForVirtualOffset(targetVirtualPageOffset, targetPage) != SUCCESS)
            {
                break;
            }
            
            NandPhysicalMedia * sourceNand = sourcePage.getNand();
            NandPhysicalMedia * targetNand = targetPage.getNand();
            
            // Copy a single page.
            uint32_t successfulCopies = 1;            
            status = -1;
            {
                // Initialize metadata for movePage operation
                Metadata md(auxBuffer);            
                md.prepare(m_virtualBlock.getMapperKeyFromVirtualOffset(targetVirtualPageOffset),startEntry); //runStartPage);
                md.clearFlag(Metadata::kIsInLogicalOrderFlag);
                if (startEntry == pagesPerBlock - 1 && targetMap.isInSortedOrder(pagesPerBlock - 1))
                {
                    md.setFlag(Metadata::kIsInLogicalOrderFlag);
                }
                else
                {
                    md.clearFlag(Metadata::kIsInLogicalOrderFlag);
                }
                
                // See if we need to set the logical order flag. We only want to do this when
                // copying the last logical page and all previous pages were in order.
                if (startEntry == pagesPerBlock - 1 && targetMap.isInSortedOrder(pagesPerBlock - 1))
                {
                    copyFilter.setLogicalOrderFlag(true);
                    ++getStatistics().mergeSetOrderedCount;
                }

                copyFilter.setLba(m_virtualBlock.getMapperKeyFromVirtualOffset(targetVirtualPageOffset));
                successfulCopies = 0;
                // Retry if copypage operation fails
                status = sourceNand->copyPages(
                    targetNand,
                    sourcePage.getRelativePage(),
                    targetPage.getRelativePage(),
                    1,
                    sectorBuffer,
                    auxBuffer,
                    &copyFilter,
                    &successfulCopies);
            }
            
            // Handle benign ECC stati. It doesn't matter if we get a rewrite sector status
            // because we are already copying into a new block.
            if (is_read_status_success_or_ecc_fixed(status))
            {
                status = SUCCESS;
            }
            
            // Update target map and page offset based on how many pages were copied.
            if (successfulCopies)
            {
#if TRANSFER_SEQUENCE_UPDATE
                targetMap.setEntry(startEntry, targetVirtualPageOffset);
#else
                targetMap.setSortedOrder(startEntry, successfulCopies, targetVirtualPageOffset);
#endif
                targetVirtualPageOffset += successfulCopies;
                runStartPage += successfulCopies;
                runPageCount -= successfulCopies;
                startEntry += successfulCopies;
            }

            // Deal with different error codes from the page copy.
            if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                // Writing to the third block failed, so mark the block as bad, pick a
                // new target block, and restart the merge sequence. We'll repeat this up
                // to 10 times.
                u32RetryCount++;
                if (u32RetryCount>10)
                {
                    return status;
                }
                
                unsigned failedPlane = targetBlock.getPlaneForVirtualOffset(targetVirtualPageOffset);

                BlockAddress physicalBlockAddress;
                
                // Handle the bad block and allocate a new block for the failed plane. Also,
                // we have to erase blocks for the other planes that are still good before
                // we can restart the merge. Unfortunately, since we are erasing, it's possible
                // for more blocks to go bad and we have to handle that!
                unsigned thePlane;
                for (thePlane=0; thePlane < VirtualBlock::getPlaneCount(); ++thePlane)
                {
                    // This address should already be cached, so we shouldn't be getting any
                    // errors here.
                    status = targetBlock.getPhysicalBlockForPlane(thePlane, physicalBlockAddress);
                    if (status != SUCCESS)
                    {
                        return status;
                    }
                    
                    // Reallocate the failed plane.
                    bool doReallocate = true;
                    
                    // For other planes we try to erase, and only reallocate if the erase fails.
                    if (thePlane != failedPlane)
                    {
                        // We cannot just pass a Block instance into the above call because the
                        // methods are not virtual.
                        Block thisBlock(physicalBlockAddress);
                        doReallocate = (thisBlock.erase() == ERROR_DDI_NAND_HAL_WRITE_FAILED);
                    }
                    
                    if (doReallocate)
                    {
                        // Deal with the new bad block.
                        getMapper()->handleNewBadBlock(physicalBlockAddress);
                        
                        // Now reallocate the phy block for this plane.
                        status = targetBlock.allocateBlockForPlane(thePlane, physicalBlockAddress);
                        if (status != SUCCESS)
                        {
                            return status;
                        }
                    }
                }

                // Reset the target block map.
                targetVirtualPageOffset = 0;
                targetMap.clear();
                
                // Restart the whole merge loop.
                goto copy_loop_start;
            }
            else if (status == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
            {
                //! \todo If we have a copy of this sector in the backup block, then we could use
                //! that as a replacement. This really isn't ideal, though, as data will still be
                //! lost. Also, there may be prior versions of the sector in the new block as well,
                //! and those would be more recent than any copy in the backup block.
                //!
                //! \todo We should probably finish the merge first so we don't lose even more data!
                return status;
            }
            else if (status)
            {
                // Got some other error while copying pages, so just return it.
                return status;
            }
        }
#if TRANSFER_SEQUENCE_UPDATE==0
        // Start a new run of source pages.
        if (hasSector)
        {
            runSourceBlock = sourceBlock;
            runStartPage = sourceVirtualPageOffset;
            runPageCount = 1;
            startEntry = logicalSector;
            
            // Handle when the last logical sector is not part of previous run.
            if (!isPartOfRun && logicalSector == pagesPerBlock - 1)
            {
                // Set the sector number to skip to be the last sector. This prevents trying
                // to process that sector again. We only want to loop again so that we can copy
                // this final run.
                u32NewSectorNumber = logicalSector;
                --logicalSector;
            }
        }
        else
        {
            runPageCount = 0;
        }
#endif
    }
    
    // Copy the target map into our primary map.
    m_map = targetMap;
    m_backupMap.clear();
    
    // Save the number of pages in the target block.
    m_currentPageCount = targetVirtualPageOffset;

    // Erase and free the old blocks.
    m_virtualBlock.freeAndEraseAllPlanes();
    m_virtualBlock = targetBlock;
    
    // Get rid of any backup physical blocks.
    if (hadBackup)
    {
        m_backupBlock.freeAndEraseAllPlanes();
        m_hasBackups = false;
    }
    
    // Update average merge elapsed time.
    getStatistics().averageCoreMergeTime += mergeTimer;

    return SUCCESS;
}

RtStatus_t NonsequentialSectorsMap::getNextOffset(uint32_t logicalSectorOffset, unsigned * offset)
{
    RtStatus_t status = SUCCESS;
    
    // If the block is full, we have to allocate a new block to write data into and make the
    // current block the backup. If we already have a backup, then we'll have to merge.
    if (m_currentPageCount >= VirtualBlock::getVirtualPagesPerBlock())
    {
        status = preventThrashing(logicalSectorOffset);
    }
    
    // Return the next virtual offset to the caller.
    assert(offset);
    *offset = m_currentPageCount;
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief This function allocates a new block to assist in thrashing prevention.
//!
//! Previous NAND architecture had a worst case write performance that was O(MN),
//! where M is the number of blocks per sector and N is the number of sectors
//! written.  Although, M can be though of as a constant in theory, in practice,
//! it cannot be so easily dismissed.  The old architecture had to resort to
//! nand pinning to get around worst case performance issue.  Nand pinning was 
//! just not compatible with LENA, and we thought of a smarter way of handling
//! worst case write performance issue.
//!
//! When a block is written to after it has been completely filled, we simply
//! allocate a new block and make that the new primary block.  All updates go
//! to the new block.  When new block is filled, we have choices.  If new block
//! completely overwrites old block, we simply release the old block.  If not,
//! we have to merge new block and old block.  In either case, worst case 
//! write performance is still O(N).
//!
//! \param[out] pu32PhysicalBlkAddr Physical Address.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::getNewBlock()
{
    RtStatus_t status;
    
    // Must not have any backups, since we overwrite the information about them.
    assert(!m_hasBackups);

    // First copy current sector map to backup sector map. Then clear the sector map
    // for the new block.
    m_backupMap = m_map;
    // Don't remove existing LSI Table
    m_map.clear(false);
    m_currentPageCount  = 0;
    
    // Save the original physical pages as the backup.
    m_backupBlock = m_virtualBlock;
    m_hasBackups = true;

    // Allocate new physical blocks for each plane of the primary virtual block.
    status = m_virtualBlock.allocateAllPlanes();
    if (status)
    {
        return status;
    }
    
    assert(m_virtualBlock.isFullyAllocated());

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief This function prevents the thrashing.
//!
//! This function prevents the thrashing that can occur when block is full
//! and each subsequent write triggers an erase and M writes where M is the
//! number of sectors per block.
//!
//! \param[in] u32NewSectorNumber  Number of new sector which needs to be written
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred & pu32NewBlkAddr filled.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::preventThrashing(uint32_t u32NewSectorNumber)
{
    RtStatus_t retCode;
    uint32_t virtualPagesPerBlock = VirtualBlock::getVirtualPagesPerBlock();

    // This function should only be called when the following condition is true.
    assert(virtualPagesPerBlock == m_currentPageCount);

    if (m_hasBackups)
    {
        // What we have here is the case where old block and new block are both completely full.
        // First figure out if the new block has N distinct sector entries where N is the number
        // of pages per block.  If there are no duplicates, the old block can simply be ignored.
        // If there are duplicates, the old block has to be used in reconstructing a complete block.
        if (m_map.countDistinctEntries() == virtualPagesPerBlock)
        {
            // New Block completely overwrites old block.  So,
            // simply erase old block, make the new block the old
            // block and get a new new block.
            m_backupBlock.freeAndEraseAllPlanes();
            m_hasBackups = false;

            // Make the current blocks the backups and allocate new blocks.
            retCode = getNewBlock();
            if (retCode)
            {
                return retCode;
            }
        }
        else
        {
            // In this case, we have two blocks which are completely full and the two have to be
            // merged together. A third block is needed to house the combination of old block and
            // new block.
            retCode = mergeBlocksSkippingPage(u32NewSectorNumber);
            if (retCode)
            {
                return retCode;
            }
            
            if (m_currentPageCount == virtualPagesPerBlock)
            {
                retCode = getNewBlock();
                if (retCode)
                {
                    return retCode;
                }
            }
        }
    }
    else
    {
        // There is no backup block yet, so we don't have to merge and can simply allocate
        // a new block to write into.
        retCode = getNewBlock();
        if (retCode)
        {
            return retCode;
        }
    }
    
    assert(virtualPagesPerBlock != m_currentPageCount);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! This function will recover from a bad write to an LBA.  First it must
//! get a new LBA block, then copy the written sectors from the old block to
//! the new block, then mark the old LBA block as bad.
//!
//! \param[in] failedVirtualOffset .
//! \param[in] logicalOffsetToSkip Logical sector in the block to leave out.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//!
//! \post If successful, the new block has been completely refreshed from old block.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::recoverFromFailedWrite(uint32_t failedVirtualOffset, uint32_t logicalOffsetToSkip)
{
    // Get the physical block address that failed.
    BlockAddress failedBlock;
    unsigned thePlane = m_virtualBlock.getPlaneForVirtualOffset(failedVirtualOffset);
    RtStatus_t status = m_virtualBlock.getPhysicalBlockForPlane(thePlane, failedBlock);
    if (status != SUCCESS)
    {
        return status;
    }

    // Merge blocks into a new block so we leave the newly bad block behind. If we do not
    // have backup blocks then this will just copy to a new location. We use the mergeBlocksCore()
    // to ensure that it always actually does copy into a new block.
    status = mergeBlocksCore(logicalOffsetToSkip);
    
    // Ask the mapper to help with this bad block. This is done even if the merge fails for
    // some reason.
    getMapper()->handleNewBadBlock(failedBlock);
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! All we have to do is merge since that copies the block contents to a new block
//! by its very nature.
//!
//! \retval SUCCESS
////////////////////////////////////////////////////////////////////////////////
RtStatus_t NonsequentialSectorsMap::relocateVirtualBlock()
{
    RtStatus_t status;

    // The merge works even if the block doesn't have a backup, in which case it just
    // copies the contents of the sole block into a new block. We use the mergeBlocksCore()
    // to ensure that it always actually does copy into a new block.
    status = mergeBlocksCore(kInvalidAddress);
    
    return status;
}

RtStatus_t NonsequentialSectorsMap::resolveConflict(uint32_t blockNumber, uint32_t physicalBlock1, uint32_t physicalBlock2)
{
    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
      "Entered Unimplemented Resolving conflict \r\n"); 
#if 0
    uint32_t newPhysicalBlock;
    uint32_t u32NumUsedSectors1;
    uint32_t u32NumUsedSectors2;
    uint32_t u32PagesPerBlock;
    RtStatus_t ret;
    RtStatus_t retEccFixFail1 = SUCCESS;
    RtStatus_t retEccFixFail2 = SUCCESS;
    
    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
      "Resolving conflict of virtual block %u between physical blocks %u and %u\n", 
      blockNumber, physicalBlock1, physicalBlock2);
    
    // Get some media info.
    u32PagesPerBlock = NandHal::getParameters().wPagesPerBlock;
    
    m_virtualBlock = blockNumber;
    
    // Create two map instances to hold the page indexes read from metadata.
    nand::PageOrderMap map1;
    ret = map1.init(u32PagesPerBlock);
    if (ret != SUCCESS)
    {
        return ret;
    }
    
    nand::PageOrderMap map2;
    ret = map2.init(u32PagesPerBlock);
    if (ret != SUCCESS)
    {
        return ret;
    }
    
    // Build temporary nonsequential sectors maps for the old and new blocks.
    ret = buildMapFromMetadata(map1, physicalBlock1, &u32NumUsedSectors1);
    if (ret == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
    {
        retEccFixFail1 = ret;
    }
    else if (ret != SUCCESS)
    {
        return ret;
    }
    
    ret = buildMapFromMetadata(map2, physicalBlock2, &u32NumUsedSectors2);
    if (ret == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
    {
        retEccFixFail2 = ret;
    }
    else if (ret != SUCCESS)
    {
        return ret;
    }
    
    Mapper * mapper = getMapper();

    // Handle cases where one or both of the blocks has uncorrectable ECC errors.
    if ((retEccFixFail1 != SUCCESS) && (retEccFixFail2 != SUCCESS))
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
            "-> uncorrectable ECC in both phy blocks\n");
                
        // Mark them as unused, which will also erase them.
        mapper->getPhymap()->markBlockFreeAndErase(physicalBlock2);
        
        // Update zonemap to mark this LBA as unallocated
        mapper->markBlock(blockNumber, physicalBlock1, kNandMapperBlockFree);
        
        return ret;
    }
    else if (retEccFixFail1 != SUCCESS)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
            "-> uncorrectable ECC in phy block %u\n", physicalBlock1);
                
        // Mark the erroneous block as unused, which will also erase it.
        mapper->getPhymap()->markBlockFreeAndErase(physicalBlock1);
        
        // Update zone map to map LBA to the right physical block
        mapper->setBlockInfo(blockNumber, physicalBlock2);
        
        return SUCCESS;
    }
    else if (retEccFixFail2 != SUCCESS)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
            "-> uncorrectable ECC in phy block %u\n", physicalBlock2);
                
        // Mark the erroneous block as unused, which will also erase it.
        mapper->getPhymap()->markBlockFreeAndErase(physicalBlock2);

        // Update zone map to map LBA to the right physical block
        mapper->setBlockInfo(blockNumber, physicalBlock1);
        
        return SUCCESS;
    }
    
    // Select the block which has more used sectors as the old block.
    bool block1IsOld;
    if (u32NumUsedSectors1 > u32NumUsedSectors2)
    {
        block1IsOld = true;
    }
    else if (u32NumUsedSectors2 > u32NumUsedSectors1)
    {
        block1IsOld = false;
    }
    else
    {
        // When the number of sectors in each block is the same, choose the highest
        // block number as the new block. This won't always be the case, but most times it is.
        block1IsOld = (physicalBlock2 > physicalBlock1);
    }
    
    // Now update fields to prepare for the merge based on the above decision about which
    // block is old and which is new.
    if (block1IsOld)
    {
        // Block 1 is the old block.
        newPhysicalBlock = physicalBlock2;
        
        m_backupPhysicalBlock = physicalBlock1;
        m_currentPageCount = u32NumUsedSectors2;
        m_backupMap = map1;
        m_map = map2;
    }
    else
    {
        // Block 2 is the old block.
        newPhysicalBlock = physicalBlock1;
        
        m_backupPhysicalBlock = physicalBlock2;
        m_currentPageCount = u32NumUsedSectors1;
        m_backupMap = map2;
        m_map = map1;
    }
    
    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
        "-> selected physical block %u as new block (#%u=%u pages, #%u=%u pages)\n", newPhysicalBlock, physicalBlock1, u32NumUsedSectors1, physicalBlock2, u32NumUsedSectors2);

    // Now merge the two blocks. This will also clean up everything.
    return mergeBlocksCore(kInvalidAddress, newPhysicalBlock);
#endif

    return SUCCESS;
}

RedBlackTree::Key_t NonsequentialSectorsMap::getKey() const
{
    return m_virtualBlock;
}

void NonsequentialSectorsMap::retain()
{
    // Increment references and remove ourself from the LRU list if this is the first one.
    if (m_referenceCount++ == 0)
    {
        removeFromLRU();
    }
}

void NonsequentialSectorsMap::release()
{
    if (m_referenceCount > 0)
    {
        // If this was the last reference, then put ourself back into the LRU list so
        // we can be reused if necessary.
        if (--m_referenceCount == 0)
        {
            insertToLRU();
        }
    }
    else
    {
        // Somebody is releasing the map an extra time!
        assert(false);
    }
}

RtStatus_t NonsequentialSectorsMap::getPhysicalPageForLogicalOffset(unsigned logicalOffset, PageAddress & physicalPage, bool * isOccupied, unsigned * virtualOffset)
{
    // Look up the logical offset in the primary block.
    unsigned virtualSectorOffset = m_map[logicalOffset];
    bool localIsOccupied = m_map.isOccupied(logicalOffset);
    VirtualBlock * whichBlock = &m_virtualBlock;

    // If the logical sector has not been written to the primary block yet, see if we have
    // a backup block that contains it.
    if (!localIsOccupied && hasBackup())
    {
        // We have a backup block, so return the sector info 
        // After memory reduction there is single LSI table shared between 2 msps
        //virtualSectorOffset = m_backupMap[logicalOffset];
        localIsOccupied = m_backupMap.isOccupied(logicalOffset);
        whichBlock = &m_backupBlock;
    }
    
    // Return virtual offset to caller.
    if (virtualOffset)
    {
        *virtualOffset = virtualSectorOffset;
    }
    if (isOccupied)
    {
        *isOccupied = localIsOccupied;
    }
    
    // Look up the physical block containing the sector, to see if the block has been
    // allocated yet. If the logical page has not been written to either the backup or primary
    // then we just return an error.
    if (localIsOccupied)
    {
        return whichBlock->getPhysicalPageForVirtualOffset(virtualSectorOffset, physicalPage);
    }
    else
    {
        return ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR;
    }
}

RtStatus_t NonsequentialSectorsMap::getNextPhysicalPage(unsigned logicalOffset, PageAddress & physicalPage, unsigned * virtualOffset)
{
    RtStatus_t status = SUCCESS;
    
    // If the block is full, we have to allocate a new block to write data into and make the
    // current block the backup. If we already have a backup, then we'll have to merge.
    if (m_currentPageCount >= VirtualBlock::getVirtualPagesPerBlock())
    {
        status = preventThrashing(logicalOffset);
        if (status != SUCCESS)
        {
            return status;
        }
    }
    
    assert(m_currentPageCount < VirtualBlock::getVirtualPagesPerBlock());
    
    // Return the next virtual offset to the caller.
    if (virtualOffset)
    {
        *virtualOffset = m_currentPageCount;
    }
    
    // Convert the virtual offset into a real physical page address. This will use the mapper
    // to look up the physical block, so we may get an error if this is the first time the
    // block is being written to.
    status = m_virtualBlock.getPhysicalPageForVirtualOffset(m_currentPageCount, physicalPage);

    // There is no physical block allocated for this virtual offset's plane, so we must
    // allocate one.
    if (status == ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR)
    {
        // Look up the plane that the virtual offset belongs to.
        unsigned thePlane = m_virtualBlock.getPlaneForVirtualOffset(m_currentPageCount);
        
        // Allocate a new physical block for this plane.
        BlockAddress newBlock;
        status = m_virtualBlock.allocateBlockForPlane(thePlane, newBlock);
        
        if (status == SUCCESS)
        {
            // Get the physical page address again. There should be no error this time.
            status = m_virtualBlock.getPhysicalPageForVirtualOffset(m_currentPageCount, physicalPage);
        }
    }

    return status;
}

unsigned NonsequentialSectorsMap::getFreePagesInBlock()
{
    return VirtualBlock::getVirtualPagesPerBlock() - m_currentPageCount;
}

#if !defined(__ghs__)
#pragma mark --CopyPagesFlagFilter--
#endif

NonsequentialSectorsMap::CopyPagesFlagFilter::CopyPagesFlagFilter()
:   NandCopyPagesFilter(), m_setLogicalOrder(false)
{
}

//! By default, the "in logical order" metadata flag is checked, and if it is set it will
//! be cleared. But if the setLogicalOrderFlag() method has been called with an argument of true,
//! then the logical order flag will be set on any copied pages.
//!
//! \param fromNand NAND object for the source page.
//! \param toNand NAND object for the destination page. May be the same as #fromNand.
//! \param fromPage Relative address of the source page.
//! \param toPage Relative address of the destination page.
//! \param sectorBuffer Buffer containing the page data.
//! \param auxBuffer Buffer holding the page's metadata.
//! \param[out] didModifyPage The filter method should set this parameter to true if it has
//!     modified the page in any way. This will let the HAL know that it cannot use copyback
//!     commands to write the destination page.
RtStatus_t NonsequentialSectorsMap::CopyPagesFlagFilter::filter(
    NandPhysicalMedia * fromNand,
    NandPhysicalMedia * toNand,
    uint32_t fromPage,
    uint32_t toPage,
    SECTOR_BUFFER * sectorBuffer,
    SECTOR_BUFFER * auxBuffer,
    bool * didModifyPage)
{
    // Create a metadata object so we can work with flags.
    Metadata md(auxBuffer);
    
    // Set the is-in-order flag if requested.
    if (m_setLogicalOrder)
    {
        md.setFlag(Metadata::kIsInLogicalOrderFlag);
        
        // Inform the HAL that we changed the page contents.
        assert(didModifyPage);
        *didModifyPage = true;
    }
    // Otherwise check if the is-in-order flag is set on this page, so we can clear it.
    else if (md.isFlagSet(Metadata::kIsInLogicalOrderFlag))
    {
        // Clear the flag.
        md.clearFlag(Metadata::kIsInLogicalOrderFlag);
        
        // Inform the HAL that we changed the page contents.
        assert(didModifyPage);
        *didModifyPage = true;
    }
    md.setLba(m_LBA);
    
    return SUCCESS;
}

#if !defined(__ghs__)
#pragma mark --RelocateVirtualBlockTask--
#endif

RelocateVirtualBlockTask::RelocateVirtualBlockTask(NssmManager * manager, uint32_t virtualBlockToRelocate)
:   DeferredTask(kTaskPriority),
    m_manager(manager),
    m_virtualBlock(virtualBlockToRelocate)
{
}

uint32_t RelocateVirtualBlockTask::getTaskTypeID() const
{
    return kTaskTypeID;
}

bool RelocateVirtualBlockTask::examineOne(DeferredTask * task)
{
    if (task->getTaskTypeID() == kTaskTypeID)
    {
        RelocateVirtualBlockTask * relocateTask = static_cast<RelocateVirtualBlockTask *>(task);
        if (relocateTask->getVirtualBlock() == m_virtualBlock)
        {
            // This task exactly matches me, so return true to indicate that I don't want to
            // be placed into the deferred queue. There's no reason to relocate the block more
            // than once.
            return true;
        }
    }
    
    return false;
}

void RelocateVirtualBlockTask::task()
{
    RtStatus_t status;
    
    // Get the NSSM instance for this virtual block.
    NonsequentialSectorsMap * map;
    status = m_manager->getMapForVirtualBlock(m_virtualBlock, &map);
    if (status != SUCCESS)
    {
        tss_logtext_Print(LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1, "Failed to get NSSM for virtual block %d, error 0x%08x\n", m_virtualBlock, status);
        return;
    }
    
//    map->retain();
    
//#if REPORT_ECC_REWRITES
    tss_logtext_Print(LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1, "NAND ECC reached threshold, rewriting virtual block=%d\n", m_virtualBlock);
//#endif
    
    // Now relocate the block contents.
    status = map->relocateVirtualBlock();
    if (status != SUCCESS)
    {
        tss_logtext_Print(LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1, "..failed to move virtual block %d to new physical block (0x%08x)\n", m_virtualBlock, status);
    }
    
//#if REPORT_ECC_REWRITES
    tss_logtext_Print(LOGTEXT_EVENT_DDI_NAND_GROUP|LOGTEXT_VERBOSITY_1, "..moved virtual block %d\n", m_virtualBlock);
//#endif

//    map->release();
    
    // Increment the number of relocate operations.
    m_manager->getStatistics().relocateBlockCount++;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////


