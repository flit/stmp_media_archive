////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_mapper
//! @{
//!
//  Copyright (c) 2006-2007 SigmaTel, Inc.
//!
//! \file    ddi_nand_mapper_audit.c
//! \brief   NAND mapper audit functions.
//!
//! This file contains the local NAND write wear leveling mapping functions
//! which are not exposed to the external world via an API.
//!
////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////
//  Includes
/////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_mapper_internal.h"
#include "ddi_nand_mapper_audit.h"
#include "ddi_nand_ddi.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_media.h"
#include <string.h>
#include "hw/core/vmemory.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"

#define DEBUG_MAPPER

#define ENABLE_MAPPER_AUDIT 0

/////////////////////////////////////////////////////////////////////////////////
//  Code
/////////////////////////////////////////////////////////////////////////////////

#if ENABLE_MAPPER_AUDIT

////////////////////////////////////////////////////////////////////////////////
//! \brief   Count the number of bits which are clear in given 16-bit word.
//!
//! \param[in]  u16Value  The word whose clear bits this function will count.
//!
//! \return Number of bits which are clear in u16Value.
//!
////////////////////////////////////////////////////////////////////////////////
uint32_t ddi_nand_mapper_CountZeroes16(uint32_t u16Value)
{
    uint32_t u32NumSetBits;

    u32NumSetBits = ddi_nand_mapper_CountOnes16(u16Value);

    return (16-u32NumSetBits);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   This function counts the number of unused blocks in Nand.
//!
//! \return Number of used blocks in Nand.
//!
////////////////////////////////////////////////////////////////////////////////
uint32_t ddi_nand_mapper_PhymapUsedCount(void)
{
    int32_t  i;
    uint32_t u32NumCoarseBlocks;
    uint32_t u32UsedCount;

    u32UsedCount = 0;

    u32NumCoarseBlocks = MAPPER_PHYMAP_TOTAL_ENTRIES(MAPPER_NUM_ENTRIES);

    for(i=0; i<u32NumCoarseBlocks; i++)
    {
        u32UsedCount += ddi_nand_mapper_CountZeroes16((*g_MapperDescriptor.physMap)[i]);
    }

    return u32UsedCount;
} 

////////////////////////////////////////////////////////////////////////////////
//! \brief Perform Audit0 on Drive 0.
//!
//! This function compares
//! physical blocks assigned to each LBA to all other
//! physical blocks assigned to data-drive as well as
//! to physical blocks assigned to hidden drive.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_mapper_Audit0Drive0(void)
{
    int32_t    i;
    int32_t    j;
    RtStatus_t retCode;
    uint32_t   u32PhysicalBlockNum;
    uint32_t   u32PhysicalBlockNum2;

    for(i=((MAPPER_NUM_ENTRIES)-1);i>0;i--)
    {

        retCode = ddi_nand_mapper_GetBlockInfo(i,
                                               &u32PhysicalBlockNum);

        if (retCode)
        {
#ifdef DEBUG_MAPPER
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                    "Audit 0.  GetBlockInfo failed.  Drive 0, LBA %d\r\n", i);
#endif
            return ERROR_GENERIC;
        }

        if (!ddi_nand_mapper_IsBlockUnallocated(u32PhysicalBlockNum))
        {

            for(j=(i-1);j>=0;j--)
            {
                retCode = ddi_nand_mapper_GetBlockInfo(j,
                                                       &u32PhysicalBlockNum2);
                if (retCode)
                {
#ifdef DEBUG_MAPPER
                    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                            "Audit 0.  GetBlockInfo failed.  Drive 0, LBA %d\r\n", j);
#endif
                    return ERROR_GENERIC;
                }

                if (u32PhysicalBlockNum==u32PhysicalBlockNum2)
                {
#ifdef DEBUG_MAPPER
                    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                       "Audit0 has failed for drive number 0, %d and %d point to same physical block number %d\r\n", i, j, u32PhysicalBlockNum);
#endif
                    return ERROR_GENERIC;
                }

            } /* for */

//             for(j=0;j<MAPPER_MAX_TOTAL_HIDDEN_DRIVE_BLOCKS;j++)
//             {

//                 retCode = ddi_nand_mapper_GetBlockInfo(1,
//                                                        j,
//                                                        &u32PhysicalBlockNum2,
//                                                        &u32TrueLBA
//                                                        );
//                 if (retCode)
//                 {
// #ifdef DEBUG_MAPPER
//                     tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
//                                              "Audit 0.  GetBlockInfo failed.  Drive 1, LBA %d\r\n", j);
// #endif
//                     return ERROR_GENERIC;
//                 }

//                 if (u32PhysicalBlockNum==u32PhysicalBlockNum2)
//                 {
// #ifdef DEBUG_MAPPER
//                     tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
//                        "Audit0 has failed data drive block %d and hidden drive block %d point to same physical block number %d\r\n", i, j, u32PhysicalBlockNum);
// #endif
//                     return ERROR_GENERIC;
//                 }

//             }

        } /* if */

    }   /* for */

    return SUCCESS;

} /* Audit0 */

////////////////////////////////////////////////////////////////////////////////
//! \brief Perform Audit0 on given Drive.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_mapper_Audit0Drive
(
    uint32_t u32DriveNumber,
    uint32_t u32NumBlocks
)
{
    int32_t    i;
    int32_t    j;
//     int32_t    k;
    RtStatus_t retCode;
    uint32_t   u32PhysicalBlockNum;
    uint32_t   u32PhysicalBlockNum2;


    if (u32DriveNumber)
    {
#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                    "Audit0.  Drive %d contents: ", u32DriveNumber);
#endif

//         for(k=0;k<MAPPER_MAX_TOTAL_HIDDEN_DRIVE_BLOCKS;k++)
//         {
//             ddi_nand_mapper_GetBlockInfo(k,
//                                          &u32PhysicalBlockNum2);

// #ifdef DEBUG_MAPPER
//             tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
//                                     "%d, ", u32PhysicalBlockNum2);
// #endif
//         }

#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "\r\n");
#endif

    } /* if */


    for(i=(u32NumBlocks-1);i>0;i--)
    {

        retCode = ddi_nand_mapper_GetBlockInfo(i,
                                               &u32PhysicalBlockNum);

        if (retCode)
        {
#ifdef DEBUG_MAPPER
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                          "Audit 0.  GetBlockInfo failed.  Drive %d, LBA %d\r\n", u32DriveNumber, i);
#endif

            return ERROR_GENERIC;
        }

        if (!ddi_nand_mapper_IsBlockUnallocated(u32PhysicalBlockNum))
        {

            for(j=(i-1);j>=0;j--)
            {
                retCode = ddi_nand_mapper_GetBlockInfo(j,
                                                       &u32PhysicalBlockNum2);
                if (retCode)
                {
#ifdef DEBUG_MAPPER
                    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                                "Audit 0.  GetBlockInfo failed.  Drive %d, LBA %d\r\n", u32DriveNumber, j);
#endif
                    return ERROR_GENERIC;
                }

                if (u32PhysicalBlockNum==u32PhysicalBlockNum2)
                {
#ifdef DEBUG_MAPPER
                    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP,
                       "Audit0 has failed for drive number %d, %d and %d point to same physical block number %d\r\n", u32DriveNumber, i, j, u32PhysicalBlockNum);
#endif
                    return ERROR_GENERIC;
                }

            } /* for */

        } /* if */

    }   /* for */

    return SUCCESS;

} /* Audit0 */

////////////////////////////////////////////////////////////////////////////////
//! \brief This function checks that No more than one LBA points
//!                  to a given Physical block number.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_mapper_Audit0(void)
{
    RtStatus_t retCode;

    retCode = ddi_nand_mapper_Audit0Drive0();
    if (retCode)
    {
#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit 0.  Drive 0 Audit failed\r\n");
#endif

        return retCode;
    }

//     retCode = ddi_nand_mapper_Audit0Drive(DRIVE_TAG_DATA_HIDDEN, MAPPER_MAX_TOTAL_HIDDEN_DRIVE_BLOCKS);
//     if (retCode)
//     {
// #ifdef DEBUG_MAPPER
//         tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audi 0.  Drive 1 Audit failed\r\n");
// #endif
//         return retCode;
//     }

//     retCode = ddi_nand_mapper_Audit0Drive(DRIVE_TAG_DATA_HIDDEN_2, MAPPER_MAX_TOTAL_HIDDEN_DRIVE_BLOCKS);
//     if (retCode)
//     {
// #ifdef DEBUG_MAPPER
//         tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audi 0.  Drive 1 Audit failed\r\n");
// #endif
//         return retCode;
//     }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Perform Audit1 on given Drive.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_mapper_Audit1Drive
(
    uint32_t u32DriveNumber,
    uint32_t u32NumBlocks,
    uint32_t *pu32NumUsed
)
{
    int32_t    i;
    RtStatus_t retCode;
    uint32_t   u32PhysicalBlockNum;
    uint32_t   bPhymapBlockUsed;

    for(i=(u32NumBlocks-1);i>=0;i--)
    {

        retCode = ddi_nand_mapper_GetBlockInfo(i,
                                               &u32PhysicalBlockNum);

        if (retCode)
        {
#ifdef DEBUG_MAPPER
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit1 has failed\r\n");
#endif
            return ERROR_GENERIC;
        }

        if (!ddi_nand_mapper_IsBlockUnallocated(u32PhysicalBlockNum))
        {
            (*pu32NumUsed)++;

            bPhymapBlockUsed = g_MapperDescriptor.physMap->isBlockUsed(u32PhysicalBlockNum);

            if (FALSE==bPhymapBlockUsed)
            {
#ifdef DEBUG_MAPPER
                tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit1 has failed for Drive Number %d.  Physical block %d is assigned to LBA %d but is marked as unused in Phymap\r\n", u32DriveNumber, u32PhysicalBlockNum, i);
#endif
                return ERROR_GENERIC;
            }
        }

    } /* for */

    return SUCCESS;

} /* Audit1Drive */

////////////////////////////////////////////////////////////////////////////////
//! \brief This function checks that No more than one LBA points
//!                  to a given Physical block number.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_mapper_Audit1(void)
{
    RtStatus_t retCode;
    uint32_t   u32UsedCount;
    uint32_t   u32UsedCount2;

    u32UsedCount = 0;

    retCode = ddi_nand_mapper_Audit1Drive(0,MAPPER_NUM_ENTRIES, &u32UsedCount);
    if (retCode)
    {
        return retCode;
    }

//     retCode = ddi_nand_mapper_Audit1Drive(DRIVE_TAG_DATA_HIDDEN, MAPPER_MAX_TOTAL_HIDDEN_DRIVE_BLOCKS, &u32UsedCount);
//     if (retCode)
//     {
//         return retCode;
//     }

//     retCode = ddi_nand_mapper_Audit1Drive(DRIVE_TAG_DATA_HIDDEN_2, MAPPER_MAX_TOTAL_HIDDEN_DRIVE_BLOCKS, &u32UsedCount);
//     if (retCode)
//     {
//         return retCode;
//     }

    u32UsedCount2 = ddi_nand_mapper_PhymapUsedCount();

    if (u32UsedCount>=u32UsedCount2)
    {
#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit1 has failed.  The number of blocks used in LBA, %d, is not smaller than number of blocks used in Phymap, %d\r\n", u32UsedCount, u32UsedCount2);
#endif
        return ERROR_DDI_NAND_MAPPER_AUDIT_PHYMAP_FAIL;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief This function verifies that all blocks marked as
//!    unused in Physmap are in erased state.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_mapper_Audit2(void)
{
    uint16_t   u16Value;
    uint16_t   u16Mask;
    uint32_t   u32BlockNum;
    uint32_t   u32PagesPerBlock;
    uint32_t   u32NumCoarseBlocks;
    int32_t    i;
    int32_t    j;
    uint32_t   u32PagePhysAddr;
    uint32_t   u32LogicalBlockAddr;
    uint32_t   u32RelativeHSectorIdx;
    NandPhysicalMedia * pPhyMediaDescriptor = NULL;
    RtStatus_t status;

    // Get a buffer to hold the redundant area.
    AuxiliaryBuffer auxBuffer;
    if ((status = auxBuffer.acquire()) != SUCCESS)
    {
        return status;
    }

    u32PagesPerBlock = NandHal::getParameters()->wPagesPerBlock;

    u32NumCoarseBlocks = MAPPER_PHYMAP_TOTAL_ENTRIES(MAPPER_NUM_ENTRIES);

    for(i=0; i<u32NumCoarseBlocks; i++)
    {

        u16Value = (*g_MapperDescriptor.physMap)[i];

        u16Mask = 1;

        for(j=0; j<MAPPER_PHYMAP_ENTRY_SIZE; j++, (u16Mask<<=1))
        {

            if (u16Value & u16Mask)
            {
                // Check that the block is erased.
                u32BlockNum = i * MAPPER_PHYMAP_ENTRY_SIZE + j;

                pPhyMediaDescriptor = NandHal::getNandForAbsoluteBlock(blockPhysicalAddress);
                u32BlockNum = pPhyMediaDescriptor->blockToRelative(u32BlockNum);
                u32PagePhysAddr = pPhyMediaDescriptor->blockToPage(u32BlockNum);

                if (ddi_nand_mapper_readMetadata(pPhyMediaDescriptor, u32PagePhysAddr, auxBuffer) != SUCCESS)
                {
                    SystemHalt();
                }

                nand::Metadata md(auxBuffer);
                u32LogicalBlockAddr = md.getLba();
                u32RelativeHSectorIdx = md.getLsi();

                if (u32LogicalBlockAddr != (uint32_t)LBA_VALUE_ERASED)
                {
#ifdef DEBUG_MAPPER
                    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "A Block which is marked as unused, block number %d, is not erased.\r\n", u32BlockNum);
#endif

                    return ERROR_DDI_NAND_MAPPER_AUDIT_PHYMAP_FAIL;
                } /* if */

            } /* if */

        } /* for */

    } /* for */

    return SUCCESS;

} /* ddi_nand_mapper_Audit2 */

////////////////////////////////////////////////////////////////////////////////
//! \brief Determines whether a block belongs to a system drive.
//!
//! The list of NAND regions is scanned to find the region to which
//! \a absolutePhysicalBlock belongs. If that region is a system drive then
//! true is returned.
//!
//! \param absolutePhysicalBlock The physical address of the block being
//!     queried. This address must be absolute from the beginning of the
//!     first NAND.
//!
//! \retval true The block belongs to a system drive.
//! \retval false Either the block is invalid, it belongs to a drive type
//!     other than a system drive, or the block is not part of a drive
//!     (i.e., a config block).
////////////////////////////////////////////////////////////////////////////////
bool IsSystemDriveBlock(uint32_t absolutePhysicalBlock)
{
    // first loop to fill in phy-map
    nand::Region::Iterator it = g_nandMedia->createRegionIterator();
    nand:Region * region;
    while ((region = it.getNext()))
    {
        int numBlocksInRegion = region->m_iNumBlks;
        LogicalDriveType_t eDriveType = region->m_eDriveType;
        uint32_t absoluteOffset = region->m_u32AbPhyStartBlkAddr;

        if ((kDriveTypeSystem == eDriveType) && (absolutePhysicalBlock >= absoluteOffset) && (absolutePhysicalBlock < absoluteOffset + numBlocksInRegion))
        {
            return true;
        }
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief   Audit the integrity of a zone map
//!
//! This function verifies that LBA map and Phys map are consistent with
//! Redundant areas of blocks in Nand.
//!
//! \return Status of call or error.
//! \retval 0            If no error has occurred.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t  ddi_nand_mapper_Audit3(void)
{
    uint32_t    u32ChipAbsoluteOffset;
    uint32_t    u32BlockPhysAddr;
    uint32_t    u32BlockPhysAddrAudit;
    uint32_t    u32PagePhysAddr;
    uint32_t    u32StmpCode;
    uint32_t    u32LogicalBlockAddr;
    uint32_t    u32RelativeHSectorIdx;
    uint32_t    u32BadBlockCounter = 0;
    bool        bBlockStat;
    uint16_t    wPagesPerBlock = NandHal::getParameters().wPagesPerBlock;
    NandPhysicalMedia * pPhyMediaDescriptor;
    uint32_t u32NumberOfBlocks = g_nandMedia->getTotalBlockCount();
    int iChipCounter;
    RtStatus_t status;

    // Get a buffer to hold the redundant area.
    AuxiliaryBuffer auxBuffer;
    if ((status = auxBuffer.acquire()) != SUCCESS)
    {
        return status;
    }

    u32ChipAbsoluteOffset = 0;

    for (iChipCounter=0; iChipCounter<NandHal::getChipSelectCount(); iChipCounter++)
    {
        pPhyMediaDescriptor = NandHal::getNand(iChipCounter);

        // Get the total number of blocks for this chip.
        u32NumberOfBlocks = pPhyMediaDescriptor->wTotalBlocks;

        // Read all the blocks in the nand
        for(u32BlockPhysAddr = g_nandMedia->getRegion(0)->iStartPhysAddr;
            u32BlockPhysAddr < u32NumberOfBlocks;
            u32BlockPhysAddr++)
        {
            // Ignore system drive blocks. If they are erased then the code
            // below will think there is a problem, which is not actually the case.
            if (IsSystemDriveBlock(u32BlockPhysAddr + u32ChipAbsoluteOffset))
            {
                continue;
            }

            //Check to make sure that we are not going beyond the limit
            if( u32BlockPhysAddr >= u32NumberOfBlocks )
            {
                break;
            }

            // Convert this to page addressing scheme
            u32PagePhysAddr = u32BlockPhysAddr * wPagesPerBlock;

            // Check to see if the block is bad or not
            // Using a Data Drive type will mean that System Drives and Bad Blocks are
            // interpreted as bad blocks which is what we want.
//            if( IsBlockBad(u32PagePhysAddr, pPhyMediaDescriptor, auxBuffer) )
            if (nand::Block(PageAddress(pPhyMediaDescriptor, u32PagePhysAddr)).isMarkedBad(auxBuffer))
            {
                // mark the block bad in phys map
                // g_MapperDescriptor.physMap->markBlockUsed(u32BlockPhysAddr);

                // Increment Bad Block Counter
                u32BadBlockCounter++;
            }
            else  // It's good then what kind of block is this
            {
                // read Redundant Area of Sector
                if (ddi_nand_mapper_readMetadata(pPhyMediaDescriptor, u32PagePhysAddr, auxBuffer) != SUCCESS)
                {
                    SystemHalt();
                }

                // Get Logical Block Address and Relative Sector Index from RA
                nand::Metadata md(auxBuffer);
                u32LogicalBlockAddr = md.getLba();
                u32RelativeHSectorIdx = md.getLsi();

                if (u32LogicalBlockAddr == (uint32_t)LBA_VALUE_ERASED)
                {
                    bBlockStat = g_MapperDescriptor.physMap->isBlockUsed(u32BlockPhysAddr + u32ChipAbsoluteOffset);

                    if (bBlockStat)
                    {
#ifdef DEBUG_MAPPER
                        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, " Audit3 - Physical Block %d was erased but is marked as used.\r\n",
                               u32BlockPhysAddr + u32ChipAbsoluteOffset);
#endif
                        // Block is erased unused but was mark used
                        return(ERROR_DDI_NAND_MAPPER_AUDIT_PHYMAP_FAIL);
                    }
                }
                else
                {
                    // Check to see if this is a system block or not. If it is then ignore the LBA

                    nand::Metadata md(auxBuffer);
                    u32StmpCode = md.getSignature();
                    u32StmpCode &= 0xffff;  // only 4 bytes

                    bBlockStat = g_MapperDescriptor.physMap->isBlockUsed(u32BlockPhysAddr + u32ChipAbsoluteOffset);

                    if (!bBlockStat)
                    {
                        // Block is used but was mark as unused
                        return(ERROR_DDI_NAND_MAPPER_AUDIT_PHYMAP_FAIL);
                    }

                    if( !u32StmpCode )
                    {
                        // Allocated this block in the zone map
                        // TODO:::Should we check for the validity of the extracted LBA ???????????????

                        if (u32LogicalBlockAddr>(MAPPER_NUM_ENTRIES))
                        {
                            // Something is seriously wrong with what was in
                            // redundant area.  Ignore for now and continue.
                            SystemHalt();
                            continue;
                        }

                        ddi_nand_mapper_GetBlockInfo(u32LogicalBlockAddr,
                                                     &u32BlockPhysAddrAudit);
                        if( u32BlockPhysAddrAudit != (u32BlockPhysAddr + u32ChipAbsoluteOffset) )
                        {
#ifdef DEBUG_MAPPER
                            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, " Audit3 - LBA %d, Physical Block %d doesn't match expected %d.\r\n",
                                   u32LogicalBlockAddr, u32BlockPhysAddrAudit, u32BlockPhysAddr );
#endif

                            return(ERROR_DDI_NAND_MAPPER_AUDIT_ZONEMAP_FAIL);
                        }

                    } /* if */

                } /* else */

            } /* else */

        } /* for # blocks per chip*/
        // To get the absolute offset, we need to add the blocks in the previous NANDs
        // to the current offset.
        u32ChipAbsoluteOffset += u32NumberOfBlocks;

    } /* for # chips */

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! Calls the mapper audit functions one after another.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_mapper_DoAudits(void)
{
    RtStatus_t retCode;

#ifdef DEBUG_MAPPER
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit\r\n");
#endif

#if 0
    retCode = ddi_nand_mapper_Audit0();

    if (retCode)
    {
#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit, Audit0 failed\r\n");
#endif
        return retCode;
    }
#endif

    retCode = ddi_nand_mapper_Audit1();

    if (retCode)
    {
#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit, Audit1 failed\r\n");
#endif
        return retCode;
    }

    retCode = ddi_nand_mapper_Audit2();

    if (retCode)
    {
#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit, Audit2 failed\r\n");
#endif
        return retCode;
    }

    retCode = ddi_nand_mapper_Audit3();

    if (retCode)
    {
#ifdef DEBUG_MAPPER
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit, Audit3 failed\r\n");
#endif
        return retCode;
    }

#ifdef DEBUG_MAPPER
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Audit succeeded\r\n");
#endif

    return SUCCESS;

} /*  ddi_nand_mapper_DoAudits */

#endif // ENABLE_MAPPER_AUDIT

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
