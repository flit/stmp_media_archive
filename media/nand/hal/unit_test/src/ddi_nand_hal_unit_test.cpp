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
#include <types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arm_ghs.h>
#include "components/telemetry/tss_logtext.h"
#include "os/pmi/os_pmi_api.h"
#include "hw/core/vmemory.h"
#include "os/threadx/tx_api.h"
#include "drivers/media/sectordef.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/nand/hal/src/ddi_nand_hal_internal.h"
#include "os/dmi/os_dmi_api.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/nand/include/ddi_nand.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "auto_free.h"
#include "drivers/media/common/media_unit_test_helpers.h"

extern "C" {
#include "hw/lradc/hw_lradc.h"
#include "drivers/rtc/ddi_rtc.h"
#include "drivers/clocks/ddi_clocks.h"
}

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#define PERFORM_READBACK_VERIFY 0
#define TEST_ALL_CHIP_ENABLES 0
#define ERASE_ENTIRE_NAND 0
#define IGNORE_BAD_BLOCKS 0

enum
{
    kTestSectorCount = 16
};

const RtStatus_t kCompareError = 0x12341234; 

//! true = Check factory bad block markers.
//! false = Check SGTL bad block markers.
bool g_checkFactoryBadBlockMarker = true;

const uint32_t kBadBlockTableChunkSize = 128;

uint32_t g_badBlockCount;
uint32_t * g_badBlockTable;
bool g_isBadBlockTableValid = false;
bool g_useBadBlockTableIfValid = true;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

bool is_bad_block(NandPhysicalMedia * nand, unsigned block);
void count_bad_blocks(NandPhysicalMedia * nand, unsigned maxBlocks, AverageTime & averageScan);
bool is_block_in_bad_table(uint32_t block);
void print_bad_blocks();
RtStatus_t test_erase(NandPhysicalMedia * nand, unsigned start, unsigned blockCount, AverageTime & averageErase);
RtStatus_t test_one_chip(NandPhysicalMedia * nand);
RtStatus_t test_read_meta(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, AverageTime & averageRead);
RtStatus_t test_multi_write(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, AverageTime & averageWrite);
RtStatus_t test_multi_read(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, AverageTime & averageRead);
RtStatus_t test_read_write(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, bool writeIt, bool readIt, AverageTime & averageWrite, AverageTime & averageRead, bool doFirmware, bool doRaw);
RtStatus_t fill_blocks(NandPhysicalMedia * nand, unsigned start, unsigned count);
RtStatus_t test_erase_multiple(NandPhysicalMedia * nand, unsigned start, unsigned blockCount, AverageTime & averageErase);

RtStatus_t test_hal();

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////
    
bool is_bad_block(NandPhysicalMedia * nand, unsigned block)
{
#if IGNORE_BAD_BLOCKS
    return false;
#elif 1
    if (g_useBadBlockTableIfValid && g_isBadBlockTableValid)
    {
        return is_block_in_bad_table(block);
    }
    AuxiliaryBuffer auxBuffer;
    auxBuffer.acquire();
    return nand->isBlockBad(block, auxBuffer, g_checkFactoryBadBlockMarker);
#else
    RtStatus_t status;
    const int pagesPerBlock = nand->pNANDParams->wPagesPerBlock;
    int offsets[6] = { 0, 1, 2, pagesPerBlock - 3, pagesPerBlock - 2, pagesPerBlock - 1 };
    int i;
    
    for (i=0; i < 6; ++i)
    {
        uint32_t pageNumber = nand->blockAndOffsetToPage(block, offsets[i]);
        
        if (g_checkFactoryBadBlockMarker)
        {
            // Check factory marker position, first byte after all data bytes.
            status = nand->readRawData(pageNumber, nand->pNANDParams->pageDataSize, 1, g_aux_buffer);
        }
        else
        {
            // Check SGTL marker position.
            // For Reed-Solomon ECC, first byte of all data + parity for data.
            // For BCH, first byte of page.
            status = nand->readMetadata(pageNumber, g_aux_buffer, NULL);
        }
        
        if (status != SUCCESS || ((uint8_t *)g_aux_buffer)[0] != 0xff)
        {
            if (status != SUCCESS)
            {
                FASTPRINT("is_bad_block: read metadata (block %u, offset %u) returned 0x%08x on line %d\n", block, offsets[i], status, __LINE__);
            }
            
            return true;
        }
    }
    
    return false;
#endif
}

void count_bad_blocks(NandPhysicalMedia * nand, unsigned maxBlocks, AverageTime & averageScan)
{
    // Allocate bad block table.
    if (g_badBlockTable)
    {
        free(g_badBlockTable);
    }
    g_badBlockTable = (uint32_t *)malloc(sizeof(uint32_t) * kBadBlockTableChunkSize);
    g_badBlockCount = 0;
    
    unsigned block = 0;
    unsigned badCount = 0;
    
    AuxiliaryBuffer auxBuffer;
    auxBuffer.acquire();
    
    for (; block < maxBlocks; ++block)
    {
        SimpleTimer t;
        bool isBad = nand->isBlockBad(block, auxBuffer, g_checkFactoryBadBlockMarker);
        uint64_t elapsed = t;

        if (isBad)
        {
//             FASTPRINT("Block #%u is bad\n", block);
            badCount++;
            
            // Add table entry.
            if (g_badBlockCount == kBadBlockTableChunkSize)
            {
                FASTPRINT("Bad block table is full!\n");
            }
            else
            {
                g_badBlockTable[g_badBlockCount++] = block;
            }
        }
        else
        {
            // Only average in good blocks, since the scan of a bad block may have
            // stopped at any page during the scan.
            averageScan += elapsed;
        }
    }
    
    g_isBadBlockTableValid = true;
    
    FASTPRINT("Total %u bad blocks on CE %u\n", badCount, nand->wChipNumber);
}

bool is_block_in_bad_table(uint32_t block)
{
    unsigned i;
    for (i = 0; i < g_badBlockCount; ++i)
    {
        if (block == g_badBlockTable[i])
        {
            return true;
        }
    }
    
    return false;
}

void print_bad_blocks()
{
    const unsigned kBlocksPerLine = 8;
    char buf[128];
    unsigned i = 0;
    unsigned k = 0;
    for (; i < g_badBlockCount; ++i)
    {
        unsigned block = g_badBlockTable[i];
        bool isLast = (i == g_badBlockCount - 1);
        bool isLastOnLine = isLast || (((i + 1) % kBlocksPerLine) == 0);
        snprintf(&buf[k], sizeof(buf) - k, "%6u%s", block, isLast ? "" : ", ");
        k += 8;
        
        if (isLastOnLine)
        {
            FASTPRINT("%s\n", buf);
            k = 0;
        }
    }
}

RtStatus_t test_read_meta(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, AverageTime & averageRead)
{
    RtStatus_t status = SUCCESS;
    unsigned block = start;
    unsigned offset;
    NandEccCorrectionInfo_t ecc;
    
    FASTPRINT("Verifying metadata from %d pages over %u blocks...\n", count * pagesPerBlock, count);
    
    clear_buffer(s_readBuffer);
    clear_buffer(s_dataBuffer);
    
    for (block = start; block < start+count; ++block)
    {
        if (is_bad_block(nand, block))
        {
            continue;
        }

        for (offset=0; offset < pagesPerBlock; ++offset)
        {
            unsigned page = nand->blockAndOffsetToPage(block, offset);
            
            // Setup buffers for this block.
            fill_data_buffer(s_dataBuffer, page, nand);
            fill_aux(g_aux_buffer, page);
            
            // Read.
            SimpleTimer readTimer;
            status = nand->readMetadata(page, g_read_aux_buffer, &ecc);
            averageRead += readTimer;
            
            if (!nand::is_read_status_success_or_ecc_fixed(status))
            {
                FASTPRINT("Failed while reading metadata of block %u (page %u) with error 0x%08x on line %d\n", block, page, status, __LINE__);
                break;
            }
            
            // Compare aux buffers.
            if (!compare_buffers(g_read_aux_buffer, g_aux_buffer, 10))
            {
                status = kCompareError;
                FASTPRINT("Aux readback verification failed for block %u (line %d)\n", block, __LINE__);
                break;
            }
        }
    }
    
    return status;
}

RtStatus_t test_multi_write(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, AverageTime & averageWrite)
{
    RtStatus_t status = SUCCESS;
    unsigned block = start;
    unsigned offset;
    unsigned badBlockCount = 0;
    unsigned compareSize = g_actualBufferBytes;
    uint64_t totalElapsedWriteTime = 0;
    
    FASTPRINT("Multi writing %d pages over %u blocks...\n", count * pagesPerBlock, count);
    
    // For now this code only supports 2 planes.
    unsigned planeCount = nand->pNANDParams->planesPerDie;
    assert(planeCount == 2);
    
    auto_free<SECTOR_BUFFER> buf1 = (SECTOR_BUFFER *)malloc(g_actualBufferBytes);
    auto_free<SECTOR_BUFFER> buf2 = (SECTOR_BUFFER *)malloc(g_actualBufferBytes);
    auto_free<SECTOR_BUFFER> auxBuf1 = (SECTOR_BUFFER *)malloc(NOMINAL_AUXILIARY_SECTOR_SIZE);
    auto_free<SECTOR_BUFFER> auxBuf2 = (SECTOR_BUFFER *)malloc(NOMINAL_AUXILIARY_SECTOR_SIZE);
    NandPhysicalMedia::MultiplaneParamBlock pb[2] =
        {
            {
                .m_buffer = buf1,
                .m_auxiliaryBuffer = auxBuf1
            },
            {
                .m_buffer = buf2,
                .m_auxiliaryBuffer = auxBuf2
            }
        };
    
    for (block = start; block < start+count; block += planeCount)
    {
        // Handle bad blocks specially.
        bool isFirstBad = is_bad_block(nand, block);
        bool isSecondBad = is_bad_block(nand, block+1);
        if (isFirstBad || isSecondBad)
        {
            ++badBlockCount;
            if (isFirstBad && isSecondBad)
            {
                ++badBlockCount;
                continue;
            }
            
            // Fill the good block of the pair with the expected pattern.
            uint32_t goodBlock = isFirstBad ? block+1 : block;
            AverageTime dummyReadAverage;
            status = test_read_write(nand, goodBlock, 1, pagesPerBlock, true, false, averageWrite, dummyReadAverage, false, false);
            if (status != SUCCESS)
            {
                FASTPRINT("Failed write with 0x%08x (line %d)\n", status, __LINE__);
                return status;
            }
            
            continue;
        }
    
        for (offset=0; offset < pagesPerBlock; ++offset)
        {
            unsigned page0 = nand->blockAndOffsetToPage(block, offset);
            unsigned page1 = nand->blockAndOffsetToPage(block+1, offset);
            
            // Fill in addresses.
            pb[0].m_address = page0;
            pb[1].m_address = page1;
            
            // Setup buffers for this block..
            fill_data_buffer(pb[0].m_buffer, page0, nand);
            fill_data_buffer(pb[1].m_buffer, page1, nand);
            
            // Init aux buffer before writing page (it is used by is_bad_block() above).
            fill_aux(pb[0].m_auxiliaryBuffer, page0);
            fill_aux(pb[1].m_auxiliaryBuffer, page1);
    
            SimpleTimer writeTimer;
            status = nand->writeMultiplePages(pb, planeCount);
            averageWrite += writeTimer;
            totalElapsedWriteTime += writeTimer;

            // Check overall status.
            if (status != SUCCESS)
            {
                FASTPRINT("Failed while multiwriting pages (%u,%u) with error 0x%08x on line %d\n", page0, page1, status, __LINE__);
                break;
            }
            
            // Check each page's status.
            unsigned p;
            for (p=0; p < planeCount; ++p)
            {
                if (pb[p].m_resultStatus != SUCCESS)
                {
                    FASTPRINT("Failed page %u in multiwrite of blocks (%u,%u) with 0x%08x (line %d)\n", pb[p].m_address, block, block+1, pb[p].m_resultStatus, __LINE__);
                }
            }
        }
    }
    
    uint64_t totalWrittenDataBytes = (count - badBlockCount) * pagesPerBlock * compareSize;
    double w_mb_s = get_mb_s(totalWrittenDataBytes, totalElapsedWriteTime);
    FASTPRINT("Write speed = %g MB/s (%s in %s)\n", w_mb_s, bytes_to_pretty_string(totalWrittenDataBytes), microseconds_to_pretty_string(totalElapsedWriteTime));
    
    return SUCCESS;
}

RtStatus_t test_multi_read(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, AverageTime & averageRead)
{
    RtStatus_t status = SUCCESS;
    unsigned block = start;
    unsigned offset;
    unsigned badBlockCount = 0;
    unsigned compareSize = g_actualBufferBytes;
    uint64_t totalElapsedReadTime = 0;
    
    FASTPRINT("Multi reading %d pages over %u blocks...\n", count * pagesPerBlock, count);
    
    // For now this code only supports 2 planes.
    unsigned planeCount = nand->pNANDParams->planesPerDie;
    assert(planeCount == 2);
    
    auto_free<SECTOR_BUFFER> buf1 = (SECTOR_BUFFER *)malloc(g_actualBufferBytes);
    auto_free<SECTOR_BUFFER> buf2 = (SECTOR_BUFFER *)malloc(g_actualBufferBytes);
    auto_free<SECTOR_BUFFER> auxBuf1 = (SECTOR_BUFFER *)malloc(NOMINAL_AUXILIARY_SECTOR_SIZE);
    auto_free<SECTOR_BUFFER> auxBuf2 = (SECTOR_BUFFER *)malloc(NOMINAL_AUXILIARY_SECTOR_SIZE);
    NandPhysicalMedia::MultiplaneParamBlock pb[2] =
        {
            {
                .m_buffer = buf1,
                .m_auxiliaryBuffer = auxBuf1
            },
            {
                .m_buffer = buf2,
                .m_auxiliaryBuffer = auxBuf2
            }
        };
    
    for (block = start; block < start+count; block += planeCount)
    {
        // Handle bad blocks specially.
        bool isFirstBad = is_bad_block(nand, block);
        bool isSecondBad = is_bad_block(nand, block+1);
        if (isFirstBad || isSecondBad)
        {
            ++badBlockCount;
            if (isFirstBad && isSecondBad)
            {
                ++badBlockCount;
                continue;
            }
            
            // Fill the good block of the pair with the expected pattern.
//             uint32_t goodBlock = isFirstBad ? block+1 : block;
//             AverageTime dummyReadAverage;
//             status = test_read_write(nand, goodBlock, 1, pagesPerBlock, true, false, averageWrite, dummyReadAverage, false, false);
//             if (status != SUCCESS)
//             {
//                 FASTPRINT("Failed write with 0x%08x (line %d)\n", status, __LINE__);
//                 return status;
//             }
            
            continue;
        }
    
        for (offset=0; offset < pagesPerBlock; ++offset)
        {
            unsigned page0 = nand->blockAndOffsetToPage(block, offset);
            unsigned page1 = nand->blockAndOffsetToPage(block+1, offset);
            
            // Fill in addresses.
            pb[0].m_address = page0;
            pb[1].m_address = page1;
            
            SimpleTimer readTimer;
            status = nand->readMultiplePages(pb, planeCount);
            averageRead += readTimer;
            totalElapsedReadTime += readTimer;

            // Check overall status.
            if (status != SUCCESS)
            {
                FASTPRINT("Failed while multiwriting pages (%u,%u) with error 0x%08x on line %d\n", page0, page1, status, __LINE__);
                break;
            }
            
            // Check each page's status.
            unsigned p;
            for (p=0; p < planeCount; ++p)
            {
                if (nand::is_read_status_success_or_ecc_fixed(pb[p].m_resultStatus))
                {
                    // Fill the compare buffers with the expected pattern for this sector.
                    fill_data_buffer(s_dataBuffer, pb[p].m_address, nand);
                    fill_aux(g_aux_buffer, pb[p].m_address);
                    
                    // Compare sector buffers.
                    if (!compare_buffers(pb[p].m_buffer, s_dataBuffer, compareSize))
                    {
                        status = kCompareError;
                        FASTPRINT("Readback verification failed for block %u (line %d)\n", block, __LINE__);
                        break;
                    }
                
                    // Compare aux buffers unless testing raw r/w.
                    if (!compare_buffers(pb[p].m_auxiliaryBuffer, g_aux_buffer, 10))
                    {
                        status = kCompareError;
                        FASTPRINT("Aux readback verification failed for block %u (line %d)\n", block, __LINE__);
                        break;
                    }
                }
                else
                {
                    FASTPRINT("Failed page %u in multiwrite of blocks (%u,%u) with 0x%08x (line %d)\n", pb[p].m_address, block, block+1, pb[p].m_resultStatus, __LINE__);
                }
            }
        }
    }
    
    uint64_t totalReadDataBytes = (count - badBlockCount) * pagesPerBlock * compareSize;
    double w_mb_s = get_mb_s(totalReadDataBytes, totalElapsedReadTime);
    FASTPRINT("Read speed = %g MB/s (%s in %s)\n", w_mb_s, bytes_to_pretty_string(totalReadDataBytes), microseconds_to_pretty_string(totalElapsedReadTime));
    
    return SUCCESS;
}

RtStatus_t test_read_write(NandPhysicalMedia * nand, unsigned start, unsigned count, unsigned pagesPerBlock, bool writeIt, bool readIt, AverageTime & averageWrite, AverageTime & averageRead, bool doFirmware, bool doRaw)
{
    RtStatus_t status = SUCCESS;
    unsigned block = start;
    unsigned offset;
    NandEccCorrectionInfo_t ecc;
    unsigned badBlockCount = 0;
    const char * typeName = doFirmware ? "firmware " : doRaw ? "raw " : "";
    unsigned mismatches = 0;
    
    unsigned compareSize = g_actualBufferBytes;
    if (doFirmware)
    {
        compareSize = nand->pNANDParams->firmwarePageDataSize;
    }
    else if (doRaw)
    {
        // Raw r/w over the entire NAND page.
        compareSize = nand->pNANDParams->pageTotalSize;
    }
    
    if (writeIt)
    {
        FASTPRINT("Writing %d %spages over %u blocks...\n", count * pagesPerBlock, typeName, count);
        
        clear_buffer(s_readBuffer);
        clear_buffer(s_dataBuffer);
        
        uint64_t totalElapsedWriteTime = 0;
        badBlockCount = 0;
        
        for (block = start; block < start+count; ++block)
        {
            if (is_bad_block(nand, block))
            {
//                 FASTPRINT("Block #%d is bad\n", block);
                badBlockCount++;
                continue;
            }
        
            for (offset=0; offset < pagesPerBlock; ++offset)
            {
                unsigned page = nand->blockAndOffsetToPage(block, offset);
                
                // Setup buffers for this block..
                fill_data_buffer(s_dataBuffer, page, nand);
                
                // Init aux buffer before writing page (it is used by is_bad_block() above).
                fill_aux(g_aux_buffer, page);
        
                SimpleTimer writeTimer;
                if (doFirmware)
                {
                    status = nand->writeFirmwarePage(page, s_dataBuffer, g_aux_buffer);
                }
                else if (doRaw)
                {
                    status = nand->writeRawData(page, 0, compareSize, s_dataBuffer);
                }
                else
                {
                    status = nand->writePage(page, s_dataBuffer, g_aux_buffer);
                }
                averageWrite += writeTimer;
                totalElapsedWriteTime += writeTimer;
                
                if (status != SUCCESS)
                {
                    FASTPRINT("Failed while writing block %u (page %u) with error 0x%08x on line %d\n", block, page, status, __LINE__);
                    break;
                }

#if PERFORM_READBACK_VERIFY
                // Readback verification of what we just wrote.
                if (status == SUCCESS)
                {
                    // Reset the read buffer.
                    clear_buffer(s_readBuffer);
    
                    // Read.
                    if (doFirmware)
                    {
                        status = nand->readFirmwarePage(page, s_readBuffer, g_read_aux_buffer, &ecc);
                    }
                    else if (doRaw)
                    {
                        status = nand->readRawData(page, 0, compareSize, s_readBuffer);
                    }
                    else
                    {
                        status = nand->readPage(page, s_readBuffer, g_read_aux_buffer, &ecc);
                    }
                    
                    if (!(nand::is_read_status_success_or_ecc_fixed(status)))
                    {
                        FASTPRINT("Failed while reading block %u (page %u) with error 0x%08x on line %d\n", block, page, status, __LINE__);
                        break;
                    }
                    
                    // Clear any ECC related statuses.
                    status = SUCCESS;
            
                    if (doRaw)
                    {
                        uint32_t byteErrors = count_buffer_mismatches(s_readBuffer, s_dataBuffer, compareSize);
                        if (byteErrors > (compareSize / 10))
                        {
                            FASTPRINT("Readback verification failed for block %u (page %u) with %u mismatching bytes [line %d]\n", block, page, byteErrors, __LINE__);
                        }
                    }
                    else
                    {
                        // Compare buffers.
                        if (!compare_buffers(s_readBuffer, s_dataBuffer, compareSize))
                        {
                            status = kCompareError;
                            FASTPRINT("Readback verification failed for block %u (line %d)\n", block, __LINE__);
                            break;
                        }
                
                        // Compare aux buffers.
                        if (!compare_buffers(g_read_aux_buffer, g_aux_buffer, 10))
                        {
                            status = kCompareError;
                            FASTPRINT("Aux readback verification failed for block %u (line %d)\n", block, __LINE__);
                            break;
                        }
                    }
                }
#endif // PERFORM_READBACK_VERIFY
            }
        }
        
        uint64_t totalWrittenDataBytes = (count - badBlockCount) * pagesPerBlock * compareSize;
        double w_mb_s = get_mb_s(totalWrittenDataBytes, totalElapsedWriteTime);
        FASTPRINT("Write speed = %g MB/s (%s in %s)\n", w_mb_s, bytes_to_pretty_string(totalWrittenDataBytes), microseconds_to_pretty_string(totalElapsedWriteTime));
    }
    
    // Now read back all the sectors again.
    if (readIt && status == SUCCESS)
    {
        FASTPRINT("Verifying %d %spages over %u blocks...\n", count * pagesPerBlock, typeName, count);
        
        block = start;
        clear_buffer(s_readBuffer);
        clear_buffer(s_dataBuffer);
    
        uint64_t totalElapsedReadTime = 0;
        badBlockCount = 0;
        
        for (block = start; block < start+count; ++block)
        {
            if (is_bad_block(nand, block))
            {
//                 FASTPRINT("Block #%d is bad\n", block);
                badBlockCount++;
                continue;
            }
    
            for (offset=0; offset < pagesPerBlock; ++offset)
            {
                unsigned page = nand->blockAndOffsetToPage(block, offset);
                
                // Setup buffers for this block.
                fill_data_buffer(s_dataBuffer, page, nand);
                fill_aux(g_aux_buffer, page);
                
                // Read.
                SimpleTimer readTimer;
                if (doFirmware)
                {
                    status = nand->readFirmwarePage(page, s_readBuffer, g_read_aux_buffer, &ecc);
                }
                else if (doRaw)
                {
                    status = nand->readRawData(page, 0, compareSize, s_readBuffer);
                }
                else
                {
                    status = nand->readPage(page, s_readBuffer, g_read_aux_buffer, &ecc);
                }
                averageRead += readTimer;
                totalElapsedReadTime += readTimer;
                
                if (!nand::is_read_status_success_or_ecc_fixed(status))
                {
                    FASTPRINT("Failed while reading block %u (page %u) with error 0x%08x on line %d\n", block, page, status, __LINE__);
                    break;
                }
                
                // Clear any ECC related statuses.
                status = SUCCESS;
                
                if (doRaw)
                {
                    mismatches += count_buffer_mismatches(s_readBuffer, s_dataBuffer, compareSize);
//                     if (mismatches)
//                     {
//                         FASTPRINT("%u bytes mismatch for block %u (page %u)\n", mismatches, block, page);
//                     }
                }
                else
                {
                    // Compare buffers.
                    if (!compare_buffers(s_readBuffer, s_dataBuffer, compareSize))
                    {
                        status = kCompareError;
                        FASTPRINT("Readback verification failed for block %u (line %d)\n", block, __LINE__);
                        break;
                    }
                
                    // Compare aux buffers unless testing raw r/w.
                    if (!compare_buffers(g_read_aux_buffer, g_aux_buffer, 10))
                    {
                        status = kCompareError;
                        FASTPRINT("Aux readback verification failed for block %u (line %d)\n", block, __LINE__);
                        break;
                    }
                }
            }
        }

        if (mismatches)
        {
            FASTPRINT("%u byte mismatches over %u pages\n", mismatches, block * pagesPerBlock);
        }
        
        uint64_t totalReadDataBytes = (count - badBlockCount) * pagesPerBlock * compareSize;
        double r_mb_s = get_mb_s(totalReadDataBytes, totalElapsedReadTime);
        FASTPRINT("Read speed = %g MB/s (%s in %s)\n", r_mb_s, bytes_to_pretty_string(totalReadDataBytes), microseconds_to_pretty_string(totalElapsedReadTime));
    }
    
    return status;
}

RtStatus_t fill_blocks(NandPhysicalMedia * nand, unsigned start, unsigned count)
{
    RtStatus_t status;
    unsigned block;
    unsigned offset;
    unsigned pagesPerBlock = nand->pNANDParams->wPagesPerBlock;
    
    FASTPRINT("Filling %d blocks (%d -> %d)...\n", count, start, start + count);
    
    for (block = start; block < start+count; ++block)
    {
        if (is_bad_block(nand, block))
        {
            continue;
        }
    
        for (offset=0; offset < pagesPerBlock; ++offset)
        {
            unsigned page = nand->blockAndOffsetToPage(block, offset);
            
            // Setup buffers for this block..
            fill_data_buffer(s_dataBuffer, page, nand);
            
            // Init aux buffer before writing page (it is used by is_bad_block() above).
            fill_aux(g_aux_buffer, page);
    
            status = nand->writePage(page, s_dataBuffer, g_aux_buffer);
            
            if (status != SUCCESS)
            {
                FASTPRINT("Failed while writing block %u (page %u) with error 0x%08x on line %d\n", block, page, status, __LINE__);
//                 break;
            }
        }
    }
                
    return SUCCESS;
}

RtStatus_t test_erase(NandPhysicalMedia * nand, unsigned start, unsigned blockCount, AverageTime & averageErase)
{
    RtStatus_t status;
    unsigned blockNumber;
//     unsigned badBlockCount = 0;
    
    FASTPRINT("Erasing %d blocks (%d -> %d)...\n", blockCount, start, start + blockCount);
    
    for (blockNumber=start; blockNumber < start+blockCount; blockNumber++)
    {
        bool isBad = is_bad_block(nand, blockNumber);
        if (isBad)
        {
//             FASTPRINT("Block #%d is bad\n", blockNumber);
//             badBlockCount++;
            continue;
        }
        
        SimpleTimer eraseTimer;
        status = nand->eraseBlock(blockNumber);
        averageErase += eraseTimer;
        
//         FASTPRINT("Erase block #%d took %d 탎\n", blockNumber, eraseTimer.getElapsed());
        
        if (status != SUCCESS)
        {
            FASTPRINT("Block %d erase returned 0x%08x\n", blockNumber, status);
//             break;
        }
    }
    
//     FASTPRINT("Done erasing (%d bad blocks).\n", badBlockCount);
    
    return SUCCESS; //status;
}

RtStatus_t test_erase_multiple(NandPhysicalMedia * nand, unsigned start, unsigned blockCount, AverageTime & averageErase)
{
#if 0
    return test_erase(nand, start, blockCount, averageErase);
#else    
    RtStatus_t status;
    unsigned blockNumber;
    unsigned i;
//     unsigned badBlockCount = 0;
    
    FASTPRINT("Multi erasing %d blocks (%d -> %d)...\n", blockCount, start, start + blockCount);

    // Allocate param blocks for all the planes.
    unsigned planeCount = NandHal::getParameters().planesPerDie;
    auto_array_delete<NandPhysicalMedia::MultiplaneParamBlock> pb = new NandPhysicalMedia::MultiplaneParamBlock[planeCount];
    
    for (blockNumber=start; blockNumber < start+blockCount;)
    {
        // Fill in the param blocks with addresses to erase.
        for (i=0; i < planeCount; ++i)
        {
            pb[i].m_address = blockNumber + i;
        }
        
        SimpleTimer eraseTimer;
        status = nand->eraseMultipleBlocks(pb, planeCount);
        averageErase.add(eraseTimer, planeCount);
        
        if (status != SUCCESS)
        {
            FASTPRINT("Multi block erase of %d blocks returned 0x%08x\n", planeCount, status);
            return status;
        }
        
        // Skip over failed block.
        for (i=0; i < planeCount; ++i)
        {
            if (pb[i].m_resultStatus != SUCCESS)
            {
                FASTPRINT("Block %d erase returned 0x%08x\n", pb[i].m_address, pb[i].m_resultStatus);
    //             break;
            }
        }
        
        blockNumber += planeCount;
    }
    
//     FASTPRINT("Done erasing (%d bad blocks).\n", badBlockCount);
    
    return SUCCESS;
#endif //0
}

// extern AverageTime g_pbaAvgRead;
// extern AverageTime g_pbaAvgReadMeta;
// extern AverageTime g_pbaAvgWrite;
// extern AverageTime g_pbaAvgErase;
// extern AverageTime g_pbaAvgMultiwrite;
// extern AverageTime g_pbaAvgMultierase;

RtStatus_t test_one_chip(NandPhysicalMedia * nand)
{
    RtStatus_t status;
    
    NandHal::SleepHelper disableSleep(false);
    
    g_isBadBlockTableValid = false;
    
    // Reset the nand.
    status = nand->reset();
    if (status != SUCCESS)
    {
        FASTPRINT("NAND reset failed: 0x%08x\n", status);
        return status;
    }
    
    AverageTime averageBadBlockScan;
    AverageTime averageErase;
    AverageTime averageEraseMultiple;
    AverageTime averageWrite;
    AverageTime averageRead;
    AverageTime averageFirmwareWrite;
    AverageTime averageFirmwareRead;
    AverageTime averageMetadataRead;
    AverageTime averageRawWrite;
    AverageTime averageRawRead;
    AverageTime averageMultiwrite;
    AverageTime averageMultiread;
    
    const unsigned pagesPerBlock = /*32; */nand->pNANDParams->wPagesPerBlock;

    unsigned blockCount;
    
    g_isBadBlockTableValid = true;
    g_badBlockCount = 0;
    
    blockCount = nand->wTotalBlocks;
//     blockCount = kTestSectorCount * 2;
    FASTPRINT("Count bad blocks (0 -> %u)\n", blockCount);
    count_bad_blocks(nand, blockCount, averageBadBlockScan);
    print_bad_blocks();
    
    // Erase and fill for testing.
    blockCount = kTestSectorCount * 2;
//     test_erase(nand, 0, blockCount, averageErase);
//     fill_blocks(nand, 0, blockCount);
    
    // Erase blocks to be tested.
    averageErase.reset();
    blockCount = kTestSectorCount;
    status = test_erase(nand, 0, blockCount, averageErase);
    if (status != SUCCESS)
    {
        FASTPRINT("Block erase test failed: 0x%08x\n", status);
        return status;
    }
    
    // Test erase multiple.
    blockCount = kTestSectorCount;
    status = test_erase_multiple(nand, kTestSectorCount, blockCount, averageEraseMultiple);
    if (status != SUCCESS)
    {
        FASTPRINT("Block multi erase test failed: 0x%08x\n", status);
        return status;
    }
    
    // Read/write test.
    status = test_read_write(nand, 0, kTestSectorCount, pagesPerBlock, true, true, averageWrite, averageRead, false, false);
    if (status != SUCCESS)
    {
        FASTPRINT("Read write test failed: 0x%08x\n", status);
        return status;
    }
    
    // Metadata read test.
    status = test_read_meta(nand, 0, kTestSectorCount, pagesPerBlock, averageMetadataRead);
    if (status != SUCCESS)
    {
        FASTPRINT("Metadata read test failed: 0x%08x\n", status);
        return status;
    }

    // Verify multi read test 1.
    status = test_multi_read(nand, 0, kTestSectorCount, pagesPerBlock, averageMultiread);
    if (status != SUCCESS)
    {
        FASTPRINT("Multiread test 1 failed: 0x%08x\n", status);
        return status;
    }

    // Multi write test.
    test_erase_multiple(nand, 0, blockCount, averageEraseMultiple);
    status = test_multi_write(nand, 0, blockCount, pagesPerBlock, averageMultiwrite);
    if (status != SUCCESS)
    {
        FASTPRINT("Multiwrite test failed: 0x%08x\n", status);
        return status;
    }

    // Verify multiwrite with regular reads (for now).
    status = test_read_write(nand, 0, blockCount, pagesPerBlock, false, true, averageWrite, averageRead, false, false);
    if (status != SUCCESS)
    {
        FASTPRINT("Verify multiwrite test failed: 0x%08x\n", status);
        return status;
    }

    // Verify multi read test 2.
    status = test_multi_read(nand, 0, blockCount, pagesPerBlock, averageMultiread);
    if (status != SUCCESS)
    {
        FASTPRINT("Multiread test 2 failed: 0x%08x\n", status);
        return status;
    }
    
    // Firmware read/write test.
//     test_erase_multiple(nand, 0, blockCount, averageEraseMultiple);
//     status = test_read_write(nand, 0, kTestSectorCount, pagesPerBlock, true, true, averageFirmwareWrite, averageFirmwareRead, true, false);
//     if (status != SUCCESS)
//     {
//         FASTPRINT("Firmware read write test failed: 0x%08x\n", status);
//         return status;
//     }
//     
//     // Firmware metadata read test.
//     status = test_read_meta(nand, 0, kTestSectorCount, pagesPerBlock, averageMetadataRead);
//     if (status != SUCCESS)
//     {
//         FASTPRINT("Firmware metadata read test failed: 0x%08x\n", status);
//         return status;
//     }
//     
//     // Raw read/write test.
//     test_erase_multiple(nand, 0, blockCount, averageEraseMultiple);
//     status = test_read_write(nand, 0, kTestSectorCount, pagesPerBlock, true, true, averageRawWrite, averageRawRead, false, true);
//     if (status != SUCCESS)
//     {
//         FASTPRINT("Raw read write test failed: 0x%08x\n", status);
//         return status;
//     }

    // Erase blocks that were written to, to clean up.
//     blockCount = kTestSectorCount * 2;
    test_erase_multiple(nand, 0, blockCount*2, averageEraseMultiple);
    
    FASTPRINT("Average times:\n"
           "  bad block:   %d 탎\n"
           "  erase:       %d 탎\n"
           "  multi erase: %d 탎\n"
           "  read:        %d 탎 (%g MB/s)\n"
           "  write:       %d 탎 (%g MB/s)\n"
           "  multiread:   %d 탎 (%g MB/s)\n"
           "  multiwrite:  %d 탎 (%g MB/s)\n"
           "  read fw:     %d 탎 (%g MB/s)\n"
           "  write fw:    %d 탎 (%g MB/s)\n"
           "  raw read:    %d 탎 (%g MB/s)\n"
           "  raw write:   %d 탎 (%g MB/s)\n"
           "  read meta:   %d 탎\n",
           averageBadBlockScan.getAverage(),
           averageErase.getAverage(),
           averageEraseMultiple.getAverage(),
           averageRead.getAverage(), get_mb_s(nand->pNANDParams->pageDataSize, averageRead.getAverage()),
           averageWrite.getAverage(), get_mb_s(nand->pNANDParams->pageDataSize, averageWrite.getAverage()),
           averageMultiread.getAverage(), get_mb_s(nand->pNANDParams->planesPerDie * nand->pNANDParams->pageDataSize, averageMultiread.getAverage()),
           averageMultiwrite.getAverage(), get_mb_s(nand->pNANDParams->planesPerDie * nand->pNANDParams->pageDataSize, averageMultiwrite.getAverage()),
           averageFirmwareRead.getAverage(), get_mb_s(nand->pNANDParams->firmwarePageDataSize, averageFirmwareRead.getAverage()),
           averageFirmwareWrite.getAverage(), get_mb_s(nand->pNANDParams->firmwarePageDataSize, averageFirmwareWrite.getAverage()),
           averageRawRead.getAverage(), get_mb_s(nand->pNANDParams->pageDataSize, averageRawRead.getAverage()),
           averageRawWrite.getAverage(), get_mb_s(nand->pNANDParams->pageDataSize, averageRawWrite.getAverage()),
           averageMetadataRead.getAverage());
    
// extern AverageTime g_pbaAvgRead;
// extern AverageTime g_pbaAvgReadMeta;
// extern AverageTime g_pbaAvgWrite;
// extern AverageTime g_pbaAvgErase;
// extern AverageTime g_pbaAvgMultiwrite;
// extern AverageTime g_pbaAvgMultierase;
//     FASTPRINT("Average prep times:\n"
//             "  read:        %d 탎 (min=%d 탎, max=%d 탎)\n"
//             "  read meta:   %d 탎 (min=%d 탎, max=%d 탎)\n"
//             "  write:       %d 탎 (min=%d 탎, max=%d 탎)\n"
//             "  erase:       %d 탎 (min=%d 탎, max=%d 탎)\n"
//             "  multiwrite:  %d 탎 (min=%d 탎, max=%d 탎)\n"
//             "  multierase:  %d 탎 (min=%d 탎, max=%d 탎)\n",
//             g_pbaAvgRead.getAverage(), g_pbaAvgRead.getMin(), g_pbaAvgRead.getMax(),
//             g_pbaAvgReadMeta.getAverage(), g_pbaAvgReadMeta.getMin(), g_pbaAvgReadMeta.getMax(),
//             g_pbaAvgWrite.getAverage(), g_pbaAvgWrite.getMin(), g_pbaAvgWrite.getMax(),
//             g_pbaAvgErase.getAverage(), g_pbaAvgErase.getMin(), g_pbaAvgErase.getMax(),
//             g_pbaAvgMultiwrite.getAverage(), g_pbaAvgMultiwrite.getMin(), g_pbaAvgMultiwrite.getMax(),
//             g_pbaAvgMultierase.getAverage(), g_pbaAvgMultierase.getMin(), g_pbaAvgMultierase.getMax()
//             );
    return status;
}

RtStatus_t test_hal()
{
    RtStatus_t status;
    
    status = NandHal::init();
//     FASTPRINT("HAL init returned 0x%08x\n", status);
    
    if (status == SUCCESS)
    {
        NandPhysicalMedia * nand = NandHal::getNand(0);
        unsigned chipSelectCount = NandHal::getChipSelectCount();
        
        auto_free<char> devName = nand->getDeviceName();
        FASTPRINT("[%s%stype %d, %uCE x (%u blocks, %u %s), %u pages/block, %u+%u pages]\n", 
            bool(devName) ? devName.get() : "",
            bool(devName) ? ", " : "",
            (int)nand->pNANDParams->NandType,
            chipSelectCount,
            nand->wTotalBlocks,
            nand->wTotalInternalDice,
            (nand->wTotalInternalDice > 1 ? "dice" : "die"),
            nand->pNANDParams->wPagesPerBlock,
            nand->pNANDParams->pageDataSize,
            nand->pNANDParams->pageMetadataSize);

        // Scan for bad blocks.
//         FASTPRINT("Counting bad blocks...\n");
//         count_bad_blocks(ddi_nand_hal_get_nand(0));
//         count_bad_blocks(ddi_nand_hal_get_nand(1));
        
#if ERASE_ENTIRE_NAND
        AverageTime averageEraseMultiple;
        uint32_t blockCount = nand->wTotalBlocks;
#endif // ERASE_ENTIRE_NAND

        unsigned cs = 0;
#if TEST_ALL_CHIP_ENABLES || ERASE_ENTIRE_NAND
        for (; cs < chipSelectCount; ++cs)
#endif // TEST_ALL_CHIP_ENABLES || ERASE_ENTIRE_NAND
        {
            nand = NandHal::getNand(cs);
            
            // Save size of buffers.
            g_actualBufferBytes = nand->pNANDParams->pageDataSize;

#if ERASE_ENTIRE_NAND
            FASTPRINT(">>>Erasing CE%u<<<\n", cs);
            status = test_erase_multiple(nand, 0, blockCount, averageEraseMultiple);
            if (status != SUCCESS)
            {
                FASTPRINT("Block multi erase test failed: 0x%08x\n", status);
                return status;
            }
#else
            FASTPRINT(">>>Testing CE%u<<<\n", cs);
            status = test_one_chip(nand);
#endif // ERASE_ENTIRE_NAND

        }

#if ERASE_ENTIRE_NAND
        FASTPRINT("Average erase time per block: %u 탎\n", averageEraseMultiple.getAverage());
#endif // TEST_ALL_CHIP_ENABLES
        
        NandHal::shutdown();
    }
    
    return status;
}

RtStatus_t test_main(ULONG param)
{
    RtStatus_t status;
    
    // Initialize the Media
    status = SDKInitialization();

    if (status == SUCCESS)
    {
        status = test_hal();
    }
    
    if (status == SUCCESS)
    {
        FASTPRINT("unit test passed!\n");
    }
    else
    {
        FASTPRINT("unit test failed: 0x%08x\n", status);
    }
    
    exit(status);
    return status;
}

uint32_t MediaGetMaximumSectorSize(void)
{
    return 8192;
}
