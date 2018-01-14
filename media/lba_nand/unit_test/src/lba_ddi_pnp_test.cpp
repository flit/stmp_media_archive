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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/lba_nand/src/ddi_lba_nand_hal.h"
#include "drivers/media/nand/rom_support/rom_nand_boot_blocks.h"

extern "C" {
#include "os/thi/os_thi_api.h"
}

//! Test pattern size in bytes.
// const int k_iPatternSizeInBytes = 2048;

// //! Test pattern short value.
// const uint16_t k_u16PatternVal = 0xAA55;

//! Media number of the internal media.
const uint32_t k_u32LogMediaNumber = 0;

extern MediaAllocationTable_t g_MediaAllocationTable[];
extern uint32_t g_wNumDrives;

bool validate_fingerprints(BootBlockStruct_t & block, uint32_t fp1, uint32_t fp2, uint32_t fp3);
RtStatus_t validate_ncb(SectorBuffer & buffer);
RtStatus_t validate_ldlb(SectorBuffer & buffer);
RtStatus_t validate_dbbt(SectorBuffer & buffer);
RtStatus_t test_pnp_boot_blocks();

///////////////////////////////////////////////////////////////////////////////
//! \brief Validate fingerprints in a boot block.
///////////////////////////////////////////////////////////////////////////////
bool validate_fingerprints(BootBlockStruct_t & block, uint32_t fp1, uint32_t fp2, uint32_t fp3)
{
    return (block.m_u32FingerPrint1 == fp1 && block.m_u32FingerPrint2 == fp2 && block.m_u32FingerPrint3 == fp3);
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Validate the NCB.
//! \todo Add more validation tests.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t validate_ncb(SectorBuffer & buffer)
{
    BootBlockStruct_t & ncb = *(BootBlockStruct_t *)buffer.getBuffer();
    
    if (!validate_fingerprints(ncb, NCB_FINGERPRINT1, NCB_FINGERPRINT2, NCB_FINGERPRINT3))
    {
        printf("Invalid NCB fingerprints (line %d)\n", __LINE__);
        return 0x10000001;
    }
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Validate the LDLB.
//! \todo Add more validation tests.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t validate_ldlb(SectorBuffer & buffer)
{
    BootBlockStruct_t & ldlb = *(BootBlockStruct_t *)buffer.getBuffer();
    
    if (!validate_fingerprints(ldlb, LDLB_FINGERPRINT1, LDLB_FINGERPRINT2, LDLB_FINGERPRINT3))
    {
        printf("Invalid LDLB fingerprints (line %d)\n", __LINE__);
        return 0x10000002;
    }
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Validate the DBBT.
//! \todo Add more validation tests.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t validate_dbbt(SectorBuffer & buffer)
{
    BootBlockStruct_t & dbbt = *(BootBlockStruct_t *)buffer.getBuffer();
    
    if (!validate_fingerprints(dbbt, DBBT_FINGERPRINT1, DBBT_FINGERPRINT2, DBBT_FINGERPRINT3))
    {
        printf("Invalid DBBT fingerprints (line %d)\n", __LINE__);
        return 0x10000003;
    }
    
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//! \brief Verify that the boot blocks were written correctly to the PNP.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t test_pnp_boot_blocks()
{
    RtStatus_t status;
    
    // Get the PNP of the first device.
    LbaNandPhysicalMedia * nand = ddi_lba_nand_hal_get_device(0);
    LbaNandPhysicalMedia::LbaPartition * pnp = nand->getBootPartition();
    
    printf("PNP partition is %u sectors @ %u bytes per sector\n", pnp->getSectorCount(), pnp->getSectorSize());
    
    // Get us a sector-sized buffer to work with.
    SectorBuffer buffer;
    
    // Read the NCB.
    printf("Validating NCB\n");
    status = pnp->readSector(0, buffer);
    if (status != SUCCESS)
    {
        printf("Failed to read NCB from PNP with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    
    status = validate_ncb(buffer);
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Read the LDLB.
    printf("Validating LDLB\n");
    status = pnp->readSector(1, buffer);
    if (status != SUCCESS)
    {
        printf("Failed to read LDLB from PNP with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    
    status = validate_ldlb(buffer);
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Read the DBBT.
    printf("Validating DBBT\n");
    status = pnp->readSector(2, buffer);
    if (status != SUCCESS)
    {
        printf("Failed to read DBBT from PNP with error 0x%08x on line %d\n", status, __LINE__);
        return status;
    }
    
    status = validate_dbbt(buffer);
    if (status != SUCCESS)
    {
        return status;
    }
    
    printf("Boot blocks are valid!\n\n");
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
    RtStatus_t Status;
//     uint32_t u32NumDrives;
    MediaAllocationTable_t *pMediaTable = &g_MediaAllocationTable[k_u32LogMediaNumber];

    // Initialize the internal media.
    Status = MediaInit(k_u32LogMediaNumber);
    printf("MediaInit returned 0x%08x\n", Status);

    // Now erase the media.
    if (Status == SUCCESS)
    {
        Status = MediaErase(k_u32LogMediaNumber, 0, true);
        printf("MediaErase returned 0x%08x\n", Status);
    }

    // Allocate drives.
    if (Status == SUCCESS)
    {
        Status = MediaAllocate(k_u32LogMediaNumber, pMediaTable);
        printf("MediaAllocate returned 0x%08x\n", Status);
    }

    // See if we can discover what we just allocated.
//    g_wNumDrives = 0;
//    if (Status == SUCCESS)
//    {
//        Status = MediaDiscoverAllocation(k_u32LogMediaNumber);
//        printf("MediaDiscoverAllocation returned 0x%08x\n", Status);
//    }
//
//    // Test Media Get Info.
//    if (Status == SUCCESS)
//    {
//        Status = MediaGetInfo(k_u32LogMediaNumber, kMediaInfoNumberOfDrives, &u32NumDrives);
//        printf("MediaGetInfo returned 0x%08x\n", Status);
//        printf("Media number of drives = %d\n", u32NumDrives);
//    }

    // Test all drives.
    if (Status == SUCCESS)
    {
        printf("\nTesting PNP boot blocks...\n");
        Status = test_pnp_boot_blocks();
    }

    if (Status == SUCCESS)
    {
        Status = MediaShutdown(k_u32LogMediaNumber);
        printf("MediaShutdown returned 0x%08x\n", Status);
    }

    // Done!
    if (Status == SUCCESS)
    {
        printf("Test passed!\r\n");
    }
    else
    {
        printf("Test failed with error: 0x%08x\n", (unsigned int)Status);
    }
    
    exit(Status);
    return Status;
}

