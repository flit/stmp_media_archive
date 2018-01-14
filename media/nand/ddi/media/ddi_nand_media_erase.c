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
//! \file ddi_nand_media_erase.c
//! \brief This file erases the media, skipping Bad Blocks, hidden drive blocks and
//!  related hidden drive zone map blocks.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/include/ddi_media_internal.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "DiscoveredBadBlockTable.h"
#include "ddi_nand_media.h"
#include "Mapper.h"
#include "NonsequentialSectorsMap.h"
#include "os/threadx/tx_api.h"
#include "drivers/media/include/ddi_media_timers.h"
#include <stdlib.h>
#include "auto_free.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

// Comment this line out unless actively profiling this function.
#define PROFILE_NAND_MEDIA_ERASE

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

//! \brief Object used to track average block erase times.
static AverageTime s_eraseAverage;

////////////////////////////////////////////////////////////////////////////////
//! \brief Erase the NAND media.
//!
//! This function will erase the media, which typically occurs when an update
//! occurs.  In order to preserve the DRM data, the Hidden Data Drive needs
//! to be preserved.  Additionally, bad blocks are not erased.
//! This function cycles through all the blocks skipping the hidden DD and
//! bad blocks but erasing all the others.
//!
//! Before this function proceeds to erase the media, a global bad-block
//! table is allocated to hold bad blocks for the entire media.
//! Any blocks that are currently marked bad or that fail to erase
//! are added to this bad-block table.  This table
//! exists until the media is allocated with NANDMediaAllocate(), after
//! which the table is freed.
//!
//! On the 37xx series, this function tests to see whether the NAND has been
//! used with a 37xx before. If it has not, then in addition to erasing the
//! media, bad block markings are converted from the factory marking position
//! to the 37xx marking position. The marking position is shifted on the
//! 37xx as a side effect of the flythrough ECC engine.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//!
//! \note Currently sets aside NCB1 and NCB2.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::erase()
{
    int iNandNumber;
    NandPhysicalMedia * pNandPhysicalMediaDesc;
    bool convertMarkings = false;
    RtStatus_t rtStatus = SUCCESS;

    // Make sure we're initialized
    if (m_bInitialized != true)
    {
        return ERROR_DDI_LDL_LMEDIA_MEDIA_NOT_INITIALIZED;
    }

    // Make sure we're not write protected
    if (m_bWriteProtected)
    {
        return ERROR_DDI_NAND_LMEDIA_MEDIA_WRITE_PROTECTED;
    }

    DdiNandLocker locker;

    // Invalidate sector map entries before a media erase.
    assert(m_nssmManager);
    m_nssmManager->invalidateAll();
    
    // Shutdown the mapper before erasing. This flushes the maps, but we'll immediately erase
    // them during the erase loop. Then when the mapper is re-inited after allocation and
    // discovery, it will have to scan to rebuild maps.
    if (m_mapper)
    {
        m_mapper->shutdown();
    }
    
    // Delete each of the valid Region objects.
    deleteRegions();

    // Dispose of any bad previous global block table before we reallocate it.
    m_globalBadBlockTable.release();

    // Switch the bad block table mode.
    m_badBlockTableMode = kNandBadBlockTableAllocationMode;

    // Allocate a global bad block table. This table is used only until the media is allocated
    // after this erase, and will be freed once it is no longer needed.
    uint32_t maxBadBlocks = m_iTotalBlksInMedia * m_params->maxBadBlockPercentage / 100;
    m_globalBadBlockTable.allocate(maxBadBlocks);
    
    // Determine if these NANDs have ever been used before. If not, we
    // must convert the factory bad block markings to our own while erasing
    // the media. This function also records the location of the NCBs,
    // which we need below in order to skip them in the case that the NAND
    // is not new. However, some NANDs such as those that have built-in ECC engine,
    // do not require bad block conversion, so we ask the HAL about this.
    convertMarkings = areNandsFresh() && m_params->requiresBadBlockConversion;

#if DEBUG
    if (convertMarkings)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "NANDMediaErase is converting bad block markings\n");
    }
#endif // DEBUG
    
    AuxiliaryBuffer auxBuffer;
    if ((rtStatus = auxBuffer.acquire()) != SUCCESS)
    {
        return rtStatus;
    }

    // Reset the average time info.
    s_eraseAverage.reset();
    
    {
        // Disallow any sleeping during the media erase.
        NandHal::SleepHelper disableSleep(false);
        
        // Time to entire media erase.
        SimpleTimer timer;

        // For each NAND in the system
        for (iNandNumber = 0; iNandNumber < NandHal::getChipSelectCount(); iNandNumber++)
        {
            // Setup some pointers to use later
            pNandPhysicalMediaDesc = NandHal::getNand(iNandNumber);

            // Reset bad block count for this chip.
//            m_chipBadBlockCount[iNandNumber] = 0;

            // For every block of the individual NAND chip...
            int iBlockCounter;
            int remainingBlocks = pNandPhysicalMediaDesc->wTotalBlocks;
            for (iBlockCounter = 0; iBlockCounter < pNandPhysicalMediaDesc->wTotalBlocks;)
            {
                // Scan to find the next bad or unerasable block.
                bool wasBad = false;
                uint32_t goodBlockCount = eraseScan(iNandNumber, iBlockCounter, remainingBlocks, pNandPhysicalMediaDesc, convertMarkings, auxBuffer, &wasBad);
                
                // Erase any good blocks.
                if (goodBlockCount > 0)
                {
                    assert(goodBlockCount <= remainingBlocks);
                    eraseBlockRange(iNandNumber, iBlockCounter, goodBlockCount, pNandPhysicalMediaDesc);
                }
                
                if (wasBad)
                {
                    uint32_t badBlockAddress = iBlockCounter + goodBlockCount;
                    assert(badBlockAddress < pNandPhysicalMediaDesc->wTotalBlocks);
                    eraseHandleBadBlock(iNandNumber, badBlockAddress, pNandPhysicalMediaDesc, convertMarkings);
                }
                
                // Advance block counter.
                iBlockCounter += goodBlockCount + 1;
                remainingBlocks -= goodBlockCount + 1;
            }
        }

        // Update the MediaState_t to Erased
        m_eState = kMediaStateErased;
        
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP,
            "Erasing media took %d ms (average block erase took %d µs)\n", uint32_t(timer.getElapsed() / 1000), s_eraseAverage.getAverage());
    }

    return rtStatus;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Counts good blocks, stopping at the first bad or unerasable block.
//!
//! Starting at the block specified by \a iBlockPhysAddr, this function will
//! count up the number of good blocks in sequence. When it encounters a bad
//! block or a block that should not be erased (such as an NCB), or when it
//! hits the end of the chip, it will stop and return the count of good blocks
//! to the caller. This function does not erase or mark any blocks itself.
//!
//! \param[in] iNandNumber The index of the NAND on which the block resides.
//! \param[in] iBlockPhysAddr The index of the block from which to start
//!     scanning, starting at 0 for the first block in the given NAND number.
//! \param[in] remainingBlocks Number of blocks left in this chip. This is
//!     the maximum number of blocks that will be scanned, and as such is the
//!     maximum value that could be returned from this function.
//! \param[in] pNandMediaInfoDesc NAND media descriptor.
//! \param[in] pNandPhysicalMediaDesc NAND physical media descriptor.
//! \param[in] convertMarkings Whether bad block markings are being converted.
//! \param[in] doNotEraseHidden Whether to erase the hidden data drive
//!     (the Janus drive) or to preserve it. Only applies when \a
//!     convertMarkings is false.
//! \param[out] wasBad Returns true if the block after the last found good
//!     block was marked as bad. If true, the address of the bad block will
//!     be (iBlockPhysAddr + return_value).
////////////////////////////////////////////////////////////////////////////////
uint32_t Media::eraseScan(int iNandNumber, int iBlockPhysAddr, int remainingBlocks, NandPhysicalMedia * pNandPhysicalMediaDesc, bool convertMarkings, SECTOR_BUFFER * auxBuffer, bool * wasBad)
{
    assert(wasBad);
    
    *wasBad = false;
    
    // Scan until we run out of blocks or hit a bad one, or one that must not be erased.
    uint32_t goodCount = 0;
    while (remainingBlocks > 0)
    {
        // Test the current block to see if it is bad.
        RtStatus_t ReadFailErrorVal;
        bool bBlockIsBad = pNandPhysicalMediaDesc->isBlockBad(iBlockPhysAddr, auxBuffer, convertMarkings, &ReadFailErrorVal);

        // Exit the scan loop if we hit a bad block or we encounter a good block that cannot
        // be erased, such as an NCB.
        if ((bBlockIsBad && ReadFailErrorVal != ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
            || (!convertMarkings && eraseShouldSkipBlock(iNandNumber, iBlockPhysAddr)))
        {
            *wasBad = bBlockIsBad;
            break;
        }
        
        // Update counters.
        ++iBlockPhysAddr;
        --remainingBlocks;
        ++goodCount;
    }
    
    return goodCount;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Erases a number of blocks in sequence.
//!
//! This function iterates over the range of blocks specified by \a iBlockPhysAddr
//! and \a numberToErase, erasing as many at once as possible (determined by
//! the HAL).
//!
//! \param[in] iNandNumber The index of the NAND on which the block resides.
//! \param[in] iBlockPhysAddr The index of the first block to erase, starting at 0
//!     for the first block in the given NAND number.
//! \param[in] numberToErase The number of blocks to be erased, starting with
//!     \a iBlockPhysAddr.
//! \param[in] pNandMediaInfoDesc NAND media descriptor.
//! \param[in] pNandPhysicalMediaDesc NAND physical media descriptor.
////////////////////////////////////////////////////////////////////////////////
void Media::eraseBlockRange(int iNandNumber, int iBlockPhysAddr, uint32_t numberToErase, NandPhysicalMedia * pNandPhysicalMediaDesc)
{
    // Allocate param blocks for all the planes.
    unsigned planeCount = m_params->planesPerDie;
    auto_array_delete<NandPhysicalMedia::MultiplaneParamBlock> pb = new NandPhysicalMedia::MultiplaneParamBlock[planeCount];
    
    while (numberToErase)
    {
        RtStatus_t status;
        unsigned erasedBlockCount;
        unsigned i;
        
        // Use a multiblock erase if we can, otherwise we have to erase just one block.
        if (numberToErase >= planeCount)
        {
            // Fill in the param blocks with addresses to erase.
            for (i=0; i < planeCount; ++i)
            {
                pb[i].m_address = iBlockPhysAddr + i;
            }
            
            // Perform the erase.
            SimpleTimer timer;
            status = pNandPhysicalMediaDesc->eraseMultipleBlocks(pb, planeCount);
            s_eraseAverage.add(timer, planeCount);
            
            erasedBlockCount = planeCount;
        }
        else
        {
            // Fill in the param block for this block so the loop below will see it.
            (pb.get())[0].m_address = iBlockPhysAddr;
            status = SUCCESS;
            erasedBlockCount = 1;
            
            SimpleTimer singleBlockTimer;
            (pb.get())[0].m_resultStatus = pNandPhysicalMediaDesc->eraseBlock(iBlockPhysAddr);
            s_eraseAverage += singleBlockTimer;
        }
        
        // If the whole command failed then there is something seriously wrong.
        if (status != SUCCESS)
        {
            continue;
        }

        // Now review the results for each erased block.
        for (i=0; i < erasedBlockCount; ++i)
        {
            // Only mark the block bad for this specific error.
            if (pb[i].m_resultStatus == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                Block thisBlock(BlockAddress(iNandNumber, pb[i].m_address));
                
                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Erase failed: new bad block %u! ***\n", thisBlock.get());
            
                // Save the block address into the bad block table.
                eraseAddBadBlock(iNandNumber, pb[i].m_address);

                // Mark the block BAD, on the NAND
                thisBlock.markBad();
            }
        }
        
        // Adjust counters with the number of successful erasures.
        iBlockPhysAddr += erasedBlockCount;
        numberToErase -= erasedBlockCount;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Records and converts a single bad block during media erase.
//!
//! The bad block address is saved into the chipwide bad block table
//! in RAM and the global bad block count will be incremented. In addition,
//! if the \a convertMarkings flag is true, the bad block will be erased and
//! re-marked using SigmaTel markings.
//!
//! \param[in] iNandNumber The index of the NAND on which the block resides.
//! \param[in] iBlockPhysAddr The index of the bad block, starting at 0
//!     for the first block in the given NAND number.
//! \param[in] pNandMediaInfoDesc NAND media descriptor.
//! \param[in] pNandPhysicalMediaDesc NAND physical media descriptor.
//! \param[in] convertMarkings Whether bad block markings are being converted.
//!
//! \pre Writes must be enabled for the erase to take effect.
//! \post If the block is bad, it will be added to the appropriate
//!     per-chip-enable bad block table and the global bad block count will
//!     be incremented.
////////////////////////////////////////////////////////////////////////////////
void Media::eraseHandleBadBlock(int iNandNumber, int iBlockPhysAddr, NandPhysicalMedia * pNandPhysicalMediaDesc, bool convertMarkings)
{
    // If we're converting from the factory markings, we need to
    // re-mark bad blocks using the SigmaTel marking location.
    if (convertMarkings)
    {
        // Mark the block as bad using our bad block marker.
        Block(BlockAddress(iNandNumber, iBlockPhysAddr)).markBad();

        #ifdef PROFILE_NAND_MEDIA_ERASE
        tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP,
            " NandMediaErase converted bad block #%d\n", iBlockPhysAddr);
        #endif
    }

    // Always save the block address into the bad block table.
    eraseAddBadBlock(iNandNumber, iBlockPhysAddr);

    #ifdef PROFILE_NAND_MEDIA_ERASE
    if (!convertMarkings)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP,
            " NandMediaErase skipping bad block #%d\n", iBlockPhysAddr);
    }
    #endif
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Adds a bad block to the chip-specific bad block table.
//!
//! \param[in] iNandNumber The index of the NAND the block sits on.
//! \param[in] iBlockPhysAddr The address of the bad block relative
//!     to the beginning of the NAND.
//! \param[in] pNandMediaInfoDesc NAND media descriptor.
//!
//! \post Both the global and chip-specific bad block counts are incremented.
////////////////////////////////////////////////////////////////////////////////
void Media::eraseAddBadBlock(int iNandNumber, int iBlockPhysAddr)
{
    if (!m_globalBadBlockTable.insert(BlockAddress(iNandNumber, iBlockPhysAddr)))
    {
#if DEBUG
    // Keep track of the number of bad blocks we couldn't fit in the table. If the number is
    // very large then there is probably a serious problem and we should stop right away.
    static unsigned overflowCount = 0;
    assert(++overflowCount < 50);
    
    // Warn the user that the bad block table is too small.
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
        "Warning: could not fit bad block #%d into chip %d bad block table!\n", iBlockPhysAddr, iNandNumber);
    tss_logtext_Flush(10);  // allow message to be sent before sending next block.
#endif
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Determines whether a good block should be saved from erasure.
//!
//! When performing a NAND media erase, this function decides whether a given
//! block is a candidate for erasure or not. The rules for this decision are
//! quite simple.
//!
//! First, on the 37xx, the NCB and its backup copy are never erased.
//!
//! Second, the \a doNotEraseHidden flag controls whether two other groups
//! of blocks are erased. These are the hidden data drive, also called the
//! Janus drive, and the zone map blocks for the mapper. When the flag is
//! set, these blocks are saved, and when it is cleared the blocks are
//! erased.
//!
//! \param[in] iNandNumber The index of the NAND the block sits on.
//! \param[in] iBlockPhysAddr The address of the block in question, relative
//!     to the beginning of the NAND.
//! \param[in] pNandMediaInfoDesc NAND media descriptor.
//! \param[in] doNotEraseHidden Whether to erase the hidden data drive
//!     (the Janus drive) or to preserve it.
//!
//! \retval true The block should not be erased.
//! \retval false The block is not special and can be erased.
////////////////////////////////////////////////////////////////////////////////
bool Media::eraseShouldSkipBlock(int iNandNumber, int iBlockPhysAddr)
{
    // On the 37xx we normally do not erase either NCB1 or NCB2. We need these
    // boot control blocks to stay in place for the life of the NAND. Once
    // these boot blocks are written we attempt to never modify them again.
    // However, if the NCB is marked as having a problem, we go ahead and
    // erase it.
    {
        BootBlockLocation_t * ncb1 = &m_bootBlocks.m_ncb1;
        BootBlockLocation_t * ncb2 = &m_bootBlocks.m_ncb2;

        // Skip the block if it matches one of the NCB addresses.
        if (ncb1->doesAddressMatch(iNandNumber, iBlockPhysAddr) || ncb2->doesAddressMatch(iNandNumber, iBlockPhysAddr))
        {
            #ifdef PROFILE_NAND_MEDIA_ERASE
            tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP,
                " NandMediaErase skipping NCB at block #%d on NAND%d\n", iBlockPhysAddr, iNandNumber);
            #endif

            return true;
        }
    }

    // If doNotEraseHidden is TRUE, we need to keep hidden drive intact.
//    if (doNotEraseHidden)
//    {
//        uint32_t absoluteBlockNumber = (iNandNumber * pNandMediaInfoDesc->pNANDDesc[0]->wTotalBlocks) + iBlockPhysAddr;
//        
//        if (ddi_nand_mapper_IsBlockHidden(absoluteBlockNumber))
//        {
//            #ifdef PROFILE_NAND_MEDIA_ERASE
//            tss_logtext_Print(LOGTEXT_VERBOSITY_4 | LOGTEXT_EVENT_DDI_NAND_GROUP,
//                " NandMediaErase skipping hidden block #%d\n", absoluteBlockNumber);
//            #endif
//
//            return true;
//        }
//    }

    // This block should be erased.
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

