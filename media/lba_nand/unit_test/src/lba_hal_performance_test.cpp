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

#include "types.h"
#include "drivers/media/lba_nand/src/ddi_lba_nand_hal.h"
#include "drivers/media/lba_nand/src/ddi_lba_nand_hal_internal.h"
#include "drivers/media/sectordef.h"
#include "drivers/media/include/ddi_media_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "os/thi/os_thi_api.h"
#include "hw/lradc/hw_lradc.h"
#include "os/eoi/os_eoi_api.h"
#include "drivers/rtc/ddi_rtc.h"
}

const unsigned kBufferBytes = kLbaNandSectorSize;
const unsigned kBufferWords = kBufferBytes / sizeof(uint32_t);

const unsigned kTestSectorCount = 10240;

const RtStatus_t kCompareError = 0x12341234; 

//! \name Buffers
//@{
#pragma alignvar(32)
static SECTOR_BUFFER s_dataBuffer[CACHED_BUFFER_SIZE_IN_WORDS(kBufferBytes)];

#pragma alignvar(32)
static SECTOR_BUFFER s_readBuffer[CACHED_BUFFER_SIZE_IN_WORDS(kBufferBytes)];
//@}

//! \brief Fill the given buffer with a pattern based on the sector number.
void fill_data_buffer(SECTOR_BUFFER * buffer, uint32_t sectorNumber, LbaNandPhysicalMedia::LbaPartition * partition)
{
    uint32_t i;
    
    for (i=0; i < kBufferWords; ++i)
    {
        buffer[i] = (sectorNumber ^ ((~sectorNumber) << 8) ^ (sectorNumber << 16) ^ ((~sectorNumber) << 24)) ^ (uint32_t)partition;
    }
}

void clear_buffer(SECTOR_BUFFER * buffer)
{
    memset(buffer, 0xff, kBufferBytes);
}

RtStatus_t test_read_write(LbaNandPhysicalMedia::LbaPartition * partition, unsigned count, bool writeIt, AverageTime & averageWrite, AverageTime & averageRead)
{
    RtStatus_t status;
    unsigned sector = 123;
    
    if (writeIt)
    {
        clear_buffer(s_readBuffer);
        clear_buffer(s_dataBuffer);

        // Write.
        fill_data_buffer(s_dataBuffer, sector, partition);
        
        for (sector = 0; sector < count; ++sector)
        {
            SimpleTimer writeTimer;
            status = partition->writeSector(sector, s_dataBuffer);
            averageWrite += writeTimer;
            
            if (status != SUCCESS)
            {
                printf("Failed while writing sector %u with error 0x%08x on line %d\n", sector, status, __LINE__);
                break;
            }
    
            // Read back immediately.
//            SimpleTimer readTimer;
//            status = partition->readSector(sector, s_readBuffer);
//            averageRead += readTimer;
//            
//            if (status != SUCCESS)
//            {
//                printf("Failed while reading sector %u with error 0x%08x on line %d\n", sector, status, __LINE__);
//                break;
//            }
//    
//            // Compare buffers.
//            if (memcmp(s_readBuffer, s_dataBuffer, kBufferBytes) != 0)
//            {
//                status = kCompareError;
//                printf("Readback verification failed for sector %u (line %d)\n", sector, __LINE__);
//                break;
//            }
        }
    }

    // Now read back all the sectors again.
    if (status == SUCCESS)
    {
        sector = 123;
        clear_buffer(s_readBuffer);
        clear_buffer(s_dataBuffer);
        fill_data_buffer(s_dataBuffer, sector, partition);
    
        for (sector = 0; sector < count; ++sector)
        {
            // Read.
            SimpleTimer readTimer;
            status = partition->readSector(sector, s_readBuffer);
            averageRead += readTimer;
            if (status != SUCCESS)
            {
                printf("Failed while reading sector %u with error 0x%08x on line %d\n", sector, status, __LINE__);
                break;
            }
    
            // Compare buffers.
            if (memcmp(s_readBuffer, s_dataBuffer, kBufferBytes) != 0)
            {
                status = kCompareError;
                printf("Readback verification failed for sector %u (line %d)\n", sector, __LINE__);
                break;
            }
        }
    }
    
    return status;
}

const char * get_lba_mode_string(LbaTypeNand::LbaNandMode_t mode)
{
    switch (mode)
    {
        case LbaTypeNand::kPnpMode:
            return "PNP";
        case LbaTypeNand::kBcmMode:
            return "BCM";
        case LbaTypeNand::kMdpMode:
            return "MDP";
        case LbaTypeNand::kVfpMode:
            return "VFP";
        default:
            return "<unknown>";
    }
}

const char * get_on_off(bool onOrOff)
{
    return onOrOff ? "on" : "off";
}

void print_status_responses(LbaNandPhysicalMedia * nand)
{
    LbaTypeNand * lbaNand = (LbaTypeNand *)nand;
    
    LbaNandStatus1Response status1;
    lbaNand->readStatus1(&status1);
    printf("\nStatus 1 [0x%02x]\n  failure = %d\n  sector write transfer error = %d\n  new command start = %d\n  busy = %d\n", status1.m_response, (int)status1.failure(), (int)status1.sectorWriteTransferError(), (int)status1.newCommandStart(), (int)status1.busy());
    
    LbaNandStatus2Response status2;
    lbaNand->readStatus2(&status2);
    printf("\nStatus 2 [0x%02x]\n  power save = %d\n  high speed write = %d\n  current partition = %d (%s)\n  address out of range = %d\n  spare blocks exhausted = %d\n  command parameter error = %d\n", status2.m_response, (int)status2.powerSaveMode(), (int)status2.highSpeedWriteMode(), (int)status2.currentPartition(), get_lba_mode_string((LbaTypeNand::LbaNandMode_t)status2.currentPartition()), (int)status2.addressOutOfRange(), (int)status2.spareBlocksExhausted(), (int)status2.commandParameterError());
}

RtStatus_t test_performance(LbaTypeNand::LbaNandMode_t mode, LbaNandPhysicalMedia::LbaPartition * partition, bool enablePowerSave, bool enableHighSpeedWrites)
{
    RtStatus_t status;
    AverageTime averageWrite;
    AverageTime averageRead;

    partition->getDevice()->enablePowerSaveMode(enablePowerSave);
    partition->getDevice()->enableHighSpeedWrites(enableHighSpeedWrites);
    
    status = test_read_write(partition, kTestSectorCount, true, averageWrite, averageRead);
    if (status != SUCCESS)
    {
        return status;
    }
    
    printf("Average times for %s (power save %s, high speed writes %s)\n"
           "  read:  %d µs\n"
           "  write: %d µs\n",
           get_lba_mode_string(mode),
           get_on_off(enablePowerSave),
           get_on_off(enableHighSpeedWrites),
           averageRead.getAverage(),
           averageWrite.getAverage());
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Execute the unit test.
//! 
//! This function is the entry point for the program.
//! 
//! \param[in] param Unused.
//! 
//! \retval 0  SUCCESS (always return SUCCESS).
/////////////////////////////////////////////////////////////////////////////////
RtStatus_t test_main(ULONG param)
{
    RtStatus_t status;
    
    // Initialize the LBA HAL.
    status = ddi_lba_nand_hal_init();
    printf("ddi_lba_nand_init returned 0x%08x\n", status);
    
    if (status == SUCCESS)
    {
        unsigned count = ddi_lba_nand_hal_get_device_count();
        printf("%u device(s)\n", count);
        
        LbaNandPhysicalMedia * nand;
//        unsigned i;
//        for (i=0; i < count && status == SUCCESS ; ++i)
//        {
//            nand = ddi_lba_nand_hal_get_device(i);
//            printf("\nLbaNandPhysicalMedia #%u = 0x%08x\n", i, (uint32_t)nand);
//            
//            // Print out the Read ID response.
//            LbaNandId2Response idResults;
//            nand->getReadIdResults(&idResults);
//            printf("Device size: %uGB\n\n", idResults.getDeviceSizeInGB());
//        }
        
        // Just test the first device.
        nand = ddi_lba_nand_hal_get_device(0);
        
        // Get the partitions.
        LbaNandPhysicalMedia::LbaPartition * vfp = nand->getFirmwarePartition();
        LbaNandPhysicalMedia::LbaPartition * mdp = nand->getDataPartition();
            
            // Test the VFP 
//            if (status == SUCCESS)
//            {
//                status = test_performance(LbaTypeNand::kVfpMode, vfp, false, false);
//            }
//            if (status == SUCCESS)
//            {
//                status = test_performance(LbaTypeNand::kVfpMode, vfp, false, true);
//            }
//            if (status == SUCCESS)
//            {
//                status = test_performance(LbaTypeNand::kVfpMode, vfp, true, false);
//            }
            
        // Test the MDP 
        if (status == SUCCESS)
        {
            status = test_performance(LbaTypeNand::kMdpMode, mdp, false, false);
        }
        if (status == SUCCESS)
        {
            status = test_performance(LbaTypeNand::kMdpMode, mdp, false, true);
        }
        if (status == SUCCESS)
        {
            status = test_performance(LbaTypeNand::kMdpMode, mdp, true, false);
        }
        
        // Print status info when an error occurrs.
        if (status != SUCCESS)
        {
            print_status_responses(nand);
        }
    }

    // Done!
    if (status == SUCCESS)
    {
        printf("\nTest passed!\r\n");
    }
    else
    {
        printf("\nTest failed with error: 0x%08x\n", (unsigned int)status);
    }
    
    exit(status);
    return status;
}

