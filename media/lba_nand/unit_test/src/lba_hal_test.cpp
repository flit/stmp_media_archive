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

const unsigned kModeSwitchTestSectorCount = 16;

const RtStatus_t kCompareError = 0x12341234; 

//! \name Read write test
//@{

struct sWriteReadTest_t
{
    LbaNandPhysicalMedia::LbaPartition  * pPartition;
    unsigned sectorCount;   //!< Sectors to read and write during the test.
    char                                * pLabel;
};

sWriteReadTest_t sWriteReadTest[] = 
{
    { 0, 1024, "\nTesting Data Partition\n" },
    { 0, 1024, "\nTesting Firmware Partition\n" },
    { 0, 128, "\nTesting Boot Partition\n" },
    { 0 }
};

//@}

//! \name Partition size test
//@{


struct sPartitionSizeTest_t
{
    uint32_t    vfpSetSize;
    uint32_t    vfpExpectedSize;
    char *      pLabel;
};

sPartitionSizeTest_t sPartitionSizeTestData[] = 
{
    { 1, 1, "\nTesting Firmware Partition - Max EX_ size\n" },
    { 0x4001, 0x6000, "\nTesting Firmware Partition - Min EX_ size\n" },        // set = 32MB + 1, expected 48MB
    { 0x4000, 0x4000, "\nTesting Firmware Partition - Max standard size\n" },   // set = 32MB, expected 32MB
    { 1, 1, "\nTesting Firmware Partition - Min size\n" },
    { 0, 0, "\nTesting Firmware Partition - Zero size\n" },
    { 1, 1, "\nTesting Firmware Partition - Original size\n" }
};
const unsigned kTestPartitionSizeCount = sizeof(sPartitionSizeTestData) / sizeof(sPartitionSizeTest_t);

//@}

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

RtStatus_t TestFirmwarePartitionSize(LbaNandPhysicalMedia * nand)
{
    RtStatus_t status = SUCCESS;

    // Dump partition info.
    LbaNandPhysicalMedia::LbaPartition * partition;

    partition = nand->getBootPartition();
    printf("Boot partition: %u sectors @ %u bytes\n", partition->getSectorCount(), partition->getSectorSize());

    // Build/fill out the test data to be used
    sPartitionSizeTestData[0].vfpSetSize = nand->getVfpMaxSize();                               // Max EX_ size
    sPartitionSizeTestData[0].vfpExpectedSize = sPartitionSizeTestData[0].vfpSetSize;

    sPartitionSizeTestData[3].vfpExpectedSize = nand->getVfpMinSize();                          // Min size

    sPartitionSizeTestData[5].vfpSetSize = (nand->getFirmwarePartition())->getSectorCount();    // Original size
    if (0 == sPartitionSizeTestData[5].vfpSetSize)
    {
        sPartitionSizeTestData[5].vfpSetSize = 0x4000;
        printf("Original VFP partition was zero.  Using 0x4000 \n");
    }
    sPartitionSizeTestData[5].vfpExpectedSize = sPartitionSizeTestData[5].vfpSetSize;


    unsigned i;
    for (i=0; (i < kTestPartitionSizeCount) && (status == SUCCESS); ++i)
    {
        printf(sPartitionSizeTestData[i].pLabel);

        status = nand->setVfpSize(sPartitionSizeTestData[i].vfpSetSize);

        if (status != SUCCESS)
        {
            printf("Failure while adjusting firmware partition #%u size 0x%x: 0x%08x (line %d)\n", i, sPartitionSizeTestData[i].vfpSetSize, status, __LINE__);
            break;
        }

        partition = nand->getFirmwarePartition();
        printf("Firmware partition: %u sectors @ %u bytes\n", partition->getSectorCount(), partition->getSectorSize());

        // Compare expected value with actual.  Skip 
        // for the min case, because mismatch is expected
        if (partition->getSectorCount() != sPartitionSizeTestData[i].vfpExpectedSize)
        {
            status = ERROR_GENERIC;
            printf("Failure while adjusting firmware partition size.  Results do not match what was expected.\n Partition #%u, Expected 0x%x, Actual 0x%x: 0x%08x (line %d)\n", 
                    i, sPartitionSizeTestData[i].vfpExpectedSize, partition->getSectorCount(),status, __LINE__);
            break;
        }

        partition = nand->getDataPartition();
        printf("Data partition: %u sectors @ %u bytes\n", partition->getSectorCount(), partition->getSectorSize());
        
    }

    return status;
}

RtStatus_t test_read_write(LbaNandPhysicalMedia::LbaPartition * partition, unsigned count, bool writeIt=false, bool logIt=false)
{
    RtStatus_t status;
    unsigned sector;
    
    if (writeIt)
    {
        if (logIt)
        {
            printf("Executing write test...\n");
        }
        
        for (sector = 0; sector < count; ++sector)
        {
            clear_buffer(s_readBuffer);
            clear_buffer(s_dataBuffer);
    
            // Write.
            fill_data_buffer(s_dataBuffer, sector, partition);
            status = partition->writeSector(sector, s_dataBuffer);
            if (status != SUCCESS)
            {
                printf("Failed while writing sector %u with error 0x%08x on line %d\n", sector, status, __LINE__);
                break;
            }
    
            // Read back immediately.
            status = partition->readSector(sector, s_readBuffer);
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

    // Now read back all the sectors again.
    if (status == SUCCESS)
    {
        if (logIt)
        {
            printf("Executing read test...\n");
        }
        
        for (sector = 0; sector < count; ++sector)
        {
            clear_buffer(s_readBuffer);
            clear_buffer(s_dataBuffer);
    
            // Read.
            status = partition->readSector(sector, s_readBuffer);
            if (status != SUCCESS)
            {
                printf("Failed while reading sector %u with error 0x%08x on line %d\n", sector, status, __LINE__);
                break;
            }
    
            // Compare buffers.
            fill_data_buffer(s_dataBuffer, sector, partition);
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

RtStatus_t TestSectorWriteRead(LbaNandPhysicalMedia * nand)
{
    RtStatus_t status = SUCCESS;

    // Build list of partitions to test then do the test
    sWriteReadTest[0].pPartition = nand->getDataPartition();
    sWriteReadTest[1].pPartition = nand->getFirmwarePartition();
    sWriteReadTest[2].pPartition = nand->getBootPartition();

    unsigned partitionIndex;
    for (partitionIndex = 0;; ++partitionIndex)
    {
        sWriteReadTest_t & testInfo = sWriteReadTest[partitionIndex];
        
        // Exit loop when we hit the terminating entry.
        if (testInfo.sectorCount == 0)
        {
            break;
        }
        
        // Try writing to the data partition and reading back.
        printf(testInfo.pLabel);
        status = test_read_write(testInfo.pPartition, testInfo.sectorCount, true, true);
        if (status != SUCCESS)
        {
            break;
        }
    }
    
    // Run test over the partitions again without writing.
    for (partitionIndex = 0;; ++partitionIndex)
    {
        sWriteReadTest_t & testInfo = sWriteReadTest[partitionIndex];
        
        // Exit loop when we hit the terminating entry.
        if (testInfo.sectorCount == 0)
        {
            break;
        }
        
        // Try only reading back.
        printf(testInfo.pLabel);
        status = test_read_write(testInfo.pPartition, testInfo.sectorCount, false, true);
        if (status != SUCCESS)
        {
            break;
        }
        
        // Now erase the sectors.
//        printf("Erasing...\n");
//        status = testInfo.pPartition->eraseSectors(0, testInfo.sectorCount);
//        if (status != SUCCESS)
//        {
//            break;
//        }
    }
    
    // Now come back and see if the sectors still contain any data after we erased them above.
//    printf("Reading back after erase...\n");
//    for (partitionIndex = 0;; ++partitionIndex)
//    {
//        sWriteReadTest_t & testInfo = sWriteReadTest[partitionIndex];
//        
//        // Exit loop when we hit the terminating entry.
//        if (testInfo.sectorCount == 0)
//        {
//            break;
//        }
//        
//        // Try reading back, but we expect this to fail.
//        printf(testInfo.pLabel);
//        test_read_write(testInfo.pPartition, testInfo.sectorCount, false, true);
//    }

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

void compare_current_mode(LbaNandPhysicalMedia * nand, LbaTypeNand::LbaNandMode_t expectedMode)
{
    LbaTypeNand * lbaNand = (LbaTypeNand *)nand;
    
    LbaNandStatus2Response status2;
    lbaNand->readStatus2(&status2);
    LbaTypeNand::LbaNandMode_t currentMode = (LbaTypeNand::LbaNandMode_t)status2.currentPartition();
        
    printf("Mode %s, expected %s\n", get_lba_mode_string(currentMode), get_lba_mode_string(expectedMode));
}

//! Does not write, so a previous test must have written the test pattern to each partition.
RtStatus_t test_mode_switching(LbaNandPhysicalMedia * nand)
{
    RtStatus_t status;
    
    printf("\nTesting mode switches...\n");
    
    // Start off in MDP mode.
    status = test_read_write(nand->getDataPartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("Failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kMdpMode);

    // MDP -> VFP
    status = test_read_write(nand->getFirmwarePartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("MDP -> VFP failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kVfpMode);

    // VFP -> MDP
    status = test_read_write(nand->getDataPartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("VFP -> MDP failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kMdpMode);
    
    // MDP -> BCM
    status = test_read_write(nand->getBootPartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("MDP -> BCM failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kBcmMode);
    
    // BCM -> MDP
    status = test_read_write(nand->getDataPartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("BCM -> MDP failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kMdpMode);
    
    // MDP -> VFP (again)
    status = test_read_write(nand->getFirmwarePartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("MDP -> VFP failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kVfpMode);
    
    // VFP -> BCM
    status = test_read_write(nand->getBootPartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("VFP -> BCM failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kBcmMode);
    
    // BCM -> VFP
    status = test_read_write(nand->getFirmwarePartition(), kModeSwitchTestSectorCount);
    if (status != SUCCESS)
    {
        printf("BCM -> VFP failed with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    compare_current_mode(nand, LbaTypeNand::kVfpMode);

    printf("Passed mode switch tests!\n");
    return SUCCESS;
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

void print_device_attributes(LbaNandPhysicalMedia * nand)
{
    uint8_t * buffer;
    unsigned actualLength;
    RtStatus_t status;
    
    // Unique ID
    status = nand->readDeviceAttribute(LbaNandPhysicalMedia::kUniqueId, NULL, 0, &actualLength);
    if (status == SUCCESS)
    {
        buffer = (uint8_t *)malloc(actualLength + 1);
        status = nand->readDeviceAttribute(LbaNandPhysicalMedia::kUniqueId, buffer, actualLength, &actualLength);
        if (status == SUCCESS)
        {
            buffer[actualLength] = '\0';
            printf("Unique ID: %s (%d bytes)\n", (char *)&buffer[0], actualLength);
        }
        else
        {
            printf("Failed to read Unique ID with error 0x%08x\n", status);
        }
        free(buffer);
    }
    
    // Controller firmware version
    status = nand->readDeviceAttribute(LbaNandPhysicalMedia::kControllerFirmwareVersion, NULL, 0, &actualLength);
    if (status == SUCCESS)
    {
        buffer = (uint8_t *)malloc(actualLength + 1);
        status = nand->readDeviceAttribute(LbaNandPhysicalMedia::kControllerFirmwareVersion, buffer, actualLength, &actualLength);
        if (status == SUCCESS)
        {
            buffer[actualLength] = '\0';
            printf("Controller firmware version: %s (%d bytes)\n", (char *)&buffer[0], actualLength);
        }
        else
        {
            printf("Failed to read controller version with error 0x%08x\n", status);
        }
        free(buffer);
    }
    
    // Device hardware version
    status = nand->readDeviceAttribute(LbaNandPhysicalMedia::kDeviceHardwareVersion, NULL, 0, &actualLength);
    if (status == SUCCESS)
    {
        buffer = (uint8_t *)malloc(actualLength + 1);
        status = nand->readDeviceAttribute(LbaNandPhysicalMedia::kDeviceHardwareVersion, buffer, actualLength, &actualLength);
        if (status == SUCCESS)
        {
            buffer[actualLength] = '\0';
            printf("Device hardware version: %s (%d bytes)\n", (char *)&buffer[0], actualLength);
        }
        else
        {
            printf("Failed to read hardware version with error 0x%08x\n", status);
        }
        free(buffer);
    }
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
        
        unsigned i;
        for (i=0; i < count && status == SUCCESS ; ++i)
        {
            LbaNandPhysicalMedia * nand = ddi_lba_nand_hal_get_device(i);
            printf("\nLbaNandPhysicalMedia #%u = 0x%08x\n", i, (uint32_t)nand);
            
            // Print out the Read ID response.
            LbaNandId2Response idResults;
            nand->getReadIdResults(&idResults);
            printf("Read ID response: %02x-%02x-%02x-%02x-%02x\n", idResults.m_data[0], idResults.m_data[1], idResults.m_data[2], idResults.m_data[3], idResults.m_data[4]);
            printf("Device size: %uGB\n", idResults.getDeviceSizeInGB());
            print_device_attributes(nand);
            
            nand->enablePowerSaveMode(false);
            nand->enableHighSpeedWrites(false);
            nand->enableHighSpeedWrites(true);
            nand->enableHighSpeedWrites(false);
            
            nand->enablePowerSaveMode(true);
            nand->enableHighSpeedWrites(false);
            nand->enableHighSpeedWrites(true);
            nand->enableHighSpeedWrites(false);
            
            // Perform the firmware partition size test
            if (status == SUCCESS)
            {
                status = TestFirmwarePartitionSize(nand);
            }

            // Perform the Write/Read test 
            if (status == SUCCESS)
            {
                status = TestSectorWriteRead(nand);
            }
            
            // Perform mode switch testing
            if (status == SUCCESS)
            {
                status = test_mode_switching(nand);
            }
            
            // Print status info when an error occurrs.
            if (status != SUCCESS)
            {
                print_status_responses(nand);
            }
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

