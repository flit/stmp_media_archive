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
//! \file ddi_nand_media_boot_block_utils.c
//! \brief This file contains the functions for manipulating the Boot Control
//!        blocks including saving and retrieving the bad block table on
//!        the NAND.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_nand_media.h"

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! \brief NCB Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! first page of the block to indicate this is a NCB.
extern const FingerPrintValues_t nand::zNCBFingerPrints =
    {
        NCB_FINGERPRINT1,
        NCB_FINGERPRINT2,
        NCB_FINGERPRINT3
    };

//! \brief LDLB Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! first page of the block to indicate this is a LDLB.
extern const FingerPrintValues_t nand::zLDLBFingerPrints =
    {
        LDLB_FINGERPRINT1,
        LDLB_FINGERPRINT2,
        LDLB_FINGERPRINT3
    };

//! \brief DBBT Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! first page of the block to indicate this is a DBBT.
extern const FingerPrintValues_t nand::zDBBTFingerPrints =
    {
        DBBT_FINGERPRINT1,
        DBBT_FINGERPRINT2,
        DBBT_FINGERPRINT3
    };

//! \brief BBRC Fingerprint values.
//!
//! These are the fingerprints that are spaced at defined values in the
//! first page of the block to indicate this is a BBRC.
extern const FingerPrintValues_t nand::zBBRCFingerPrints =
    {
        BBRC_FINGERPRINT1,
        BBRC_FINGERPRINT2,
        BBRC_FINGERPRINT3
    };

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Compare the Boot Block fingerprints to see if they match.
//!
//! This function will compare the values stored at the fingerprints area
//! in a given sector to an expected set of fingerprints.  If they match
//! we return TRUE, otherwise, we return FALSE.
//!
//! \param[in]  pBootBlock Pointer to the NAND sector that was read.
//! \param[in]  pFingerPrintValues Pointer to the expected Fingerprint values.
//!
//! \return True or False.
//! \retval TRUE  If fingerprints match.
//! \retval FALSE If fingerprints do not match.
////////////////////////////////////////////////////////////////////////////////
bool nand::ddi_nand_media_doFingerprintsMatch(BootBlockStruct_t * pBootBlock, const FingerPrintValues_t * pFingerPrintValues)
{
    bool bFoundBootBlock = false;

    // Match fingerprints/signatures to determine if this was successful.
    if(pBootBlock->m_u32FingerPrint1 == pFingerPrintValues->m_u32FingerPrint1 &&
       pBootBlock->m_u32FingerPrint2 == pFingerPrintValues->m_u32FingerPrint2 &&
       pBootBlock->m_u32FingerPrint3 == pFingerPrintValues->m_u32FingerPrint3)
    {
        bFoundBootBlock = true;
    }
    return bFoundBootBlock;
}

////////////////////////////////////////////////////////////////////////////////
//! This function will determine where the next good block is.  It requires
//! advanced knowledge of the Bad Block table which should have been retained
//! even during the NAND Media Erase.
//!
//! \param[in]  pNANDMediaInfo Pointer to the NAND media info descriptor
//! \param[in]  u32NAND Which NAND are we concerned with?
//! \param[out]  pu32StartingBlock Pointer to starting block.  This gets
//!              overwritten with the next good block and returned to caller.
//! \param[in]  u32SearchSize Number of blocks to search before returning an
//!              error
//! \param[in] auxBuffer Optional buffer to hold the auxiliary data when checking if
//!     a block is bad. This parameter may be NULL.
//! \param[in] eraseGoodBlock A flag indicating if the caller wants the next
//!     good block that is found to be erased before this function returns.
//!     Pass #kEraseFoundBlock to erase the found block, #kDontEraseFoundBlock
//!     to not erase.
//!
//! \return Status of call or error.
//! \retval SUCCESS If no error has occurred.
//! \retval ERROR_DDI_NAND_MEDIA_FINDING_NEXT_VALID_BLOCK  if can't be found.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t Media::findFirstGoodBlock(uint32_t u32NAND, uint32_t * pu32StartingBlock, uint32_t u32SearchSize, SECTOR_BUFFER * auxBuffer, bool eraseGoodBlock)
{
    int iCounter;
    RtStatus_t retCode = ERROR_DDI_NAND_MEDIA_FINDING_NEXT_VALID_BLOCK;
    bool bBlockIsBad;
    uint32_t u32NextGoodBlock = 0xffffffff;

#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL, "\nNAND %x \nStartingBlock %x \nSearchSize %x \n", u32NAND, *pu32StartingBlock, u32SearchSize);
#endif
    
    Block testBlock(BlockAddress(u32NAND, *pu32StartingBlock));
    
    for (iCounter=*pu32StartingBlock;
        ((iCounter < (*pu32StartingBlock+u32SearchSize)) && (retCode != SUCCESS));
        iCounter++, ++testBlock)
    {
        RtStatus_t  ReadFailErrorVal;
        
        // Test the current block.
        bBlockIsBad = testBlock.isMarkedBad(auxBuffer, &ReadFailErrorVal);

        if (eraseGoodBlock)
        {
			// We are planning to erase any usable blocks.

            //  The block just had ECC errors, which means it is usable after erasure...
            if ( ReadFailErrorVal == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED ||
                // ...or the block was not marked bad at all.
                 !bBlockIsBad )
            {
                // In either case, we can erase it.
                RtStatus_t status = testBlock.eraseAndMarkOnFailure();
			    if (status == SUCCESS)
                {
                    // We erased the block, so we don't need to consider the block
                    // bad anymore.
                    bBlockIsBad = false;
                }
                else if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
                {
                    // Add the bad block to its owning region.
                    Region * region = getRegionForBlock(testBlock);
                    if (region)
                    {
                        region->addNewBadBlock(testBlock);
                    }
                }
                else
                {
                    // Unexpected error occurred.
                    return status;
                }
            }
		}

        // If the block is not bad, then we can use it.  Return the address.
        if (!bBlockIsBad)
        {
            u32NextGoodBlock = iCounter;
            
            // Set the return-code, and cause the loop to exit.
            // (Plesae refer to the loop test conditions above.)
            retCode = SUCCESS;
        }
#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
        else
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL, "Bad Block %x \n", iCounter);
        }
#endif
    }

    // Save off the next good block.
    *pu32StartingBlock = u32NextGoodBlock;
    
#ifdef DEBUG_BOOT_BLOCK_ALLOCATION_DISCOVER
    tss_logtext_Print(LOGTEXT_VERBOSITY_ALL, "NextGoodBlock %x \nretCode %x \n\n", u32NextGoodBlock, retCode);
#endif

    return retCode;
}

////////////////////////////////////////////////////////////////////////////////
//! A set of NANDs is considered to be new from the factory if there is no
//! valid NCB1 or NCB2 that can be found. It is assumed that all NANDs in a
//! multi-NAND configuration have never been used apart from one another.
//!
//! \retval true The NAND has never been used by the SDK before and is
//!     fresh from the factory. Factory bad block markings are still intact.
//! \retval false This NAND has been modified by the 37xx SDK. The factory
//!     bad block markings are no longer valid.
//!
//! \post The addresses of the NCBs are recorded if they
//!     were found successfully.
//!
//! \todo This search loop is very similar to the one in
//!     ddi_nand_media_FindBootControlBlocks(), so find a way to merge them.
////////////////////////////////////////////////////////////////////////////////
bool Media::areNandsFresh()
{
    // Allocate temporary sector and auxiliary buffers.
    SectorBuffer sectorBuffer;
    if (sectorBuffer.acquire() != SUCCESS)
    {
        return false;
    }

    // Get an auxiliary buffer.
    AuxiliaryBuffer auxBuffer;
    if (auxBuffer.acquire() != SUCCESS)
    {
        return false;
    }

    findBootControlBlocks(sectorBuffer, auxBuffer, kDontAllowRecovery);

    return !m_bootBlocks.hasValidNCB();
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
