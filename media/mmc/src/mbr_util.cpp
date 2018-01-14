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
//! \addtogroup ddi_mmc
//! @{
//! \file   mbr_util.cpp
//! \brief  Utility functions for MBR access.
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "mbr_types.h"
#include "errordefs.h"

using namespace mmc;

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

static void PackChs(Chs_t _chs, ChsPacked_t *_p_chsPacked);
static RtStatus_t SectorToChs(Chs_t *pchs, Chs_t *_p_chs, uint32_t _sector);
static RtStatus_t CalcStartEndChs(int iPartitionNum, Chs_t *pchs, PartTable_t *pMmcPartitionTable);
static RtStatus_t InitChs(int iPartitionNum, uint64_t u64TotalSectors, PartTable_t *pMmcPartitionTable);

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

//! \brief Populate partition table CHS fields.
void updateChsEntries(uint64_t u64TotalSectors, PartTable_t *pMmcPartitionTable)
{
    int i;
    for(i=0; i<kNumPartitionEntries; i++)
    {
        InitChs(i, u64TotalSectors, pMmcPartitionTable);
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Initializes CHS parameters for given sectors
////////////////////////////////////////////////////////////////////////////////
static RtStatus_t InitChs(int iPartitionNum, uint64_t u64TotalSectors, PartTable_t *pMmcPartitionTable)
{
    uint32_t ulSize, ulWastedSectors;
    uint8_t ucSectors, ucSectors2, ucOptimalSectors=0;
    uint16_t usHeads, usHeads2, usOptimalHeads=0, usCylinders, usCylinders2, usOptimalCylinders=0;
    bool not_done=TRUE;
    Chs_t chs;
    // Number of bits available for CHS:
    //
    // Standard      Cylinders   Heads   Sectors   Total
    // --------------------------------------------------
    //  IDE/ATA        16          4        8       28
    //  Int13/MBR      10          8        6       24
    //  Combination    10          4        6       20
    //
    // In decimal we get
    //
    // Standard      Cylinders   Heads   Sectors            Total
    // ----------------------------------------------------------------
    //  IDE/ATA        65536      16       256       268435456 =  128GB
    //  Int13/MBR      1024       256       63*       16515072 = 8064MB
    //  Combination    1024       16        63         1032192 =  504MB
    //
    // * There is no sector "0" in CHS (there is in LBA, though)
    //
    // All drives with more than 16,515,072 sectors will get bogus CHS
    //  parameters.

    if(u64TotalSectors >= (uint32_t)16515072)
    {
        // Create bogus, non-zero parameters.  Params are non-zero because
        //  some 3rd party media readers may fail to recognize the media.
        chs.cylinder    = 1;
        chs.head    = 1;
        chs.sector  = 16;
                CalcStartEndChs(iPartitionNum, &chs, pMmcPartitionTable);
        return SUCCESS;
    }

    usCylinders = 1;
    usHeads = 1;
    ucSectors = 1;
    ulWastedSectors = 0x7FFFFFFF;

    while(not_done)
    {
        ulSize = (uint32_t)usCylinders * (uint32_t)usHeads * (uint32_t)ucSectors;
        if(ulSize < u64TotalSectors)
        {
            // Not enough
            ucSectors++;
            if(ucSectors > MAX_SECTORS)
            {
                ucSectors = 1;
                usHeads++;
                if(usHeads > MAX_HEADS)
                {
                    usHeads = 1;
                    usCylinders++;
                    if(usCylinders > MAX_CYLINDERS)
                    {
                        break;
                    }
                }
            }
        }
        else
        {
            if(ulSize == u64TotalSectors)
            {
                // Found an exact solution so it's time to stop
                usOptimalCylinders = usCylinders;
                usOptimalHeads = usHeads;
                ucOptimalSectors = ucSectors;
                break;
            }
            else
            {
                // Found a solution.  We're over by some amount so we need
                //  to back up
                usCylinders2 = usCylinders;
                usHeads2 = usHeads;
                ucSectors2 = ucSectors;

                ucSectors2--;
                if(ucSectors2 == 0)
                {
                    ucSectors2 = MAX_SECTORS;
                    usHeads2--;
                    if(usHeads2 == 0)
                    {
                        usHeads2 = MAX_HEADS;
                        usCylinders2--;
                        if(usCylinders2 == 0)
                        {
                            //! \todo return some error
                            return ERROR_GENERIC;
                        }
                    }
                }

                // Only keep it if it's optimal
                if((u64TotalSectors - ((uint32_t)usCylinders2 *
                    (uint32_t)usHeads2 * (uint32_t)ucSectors2)) < ulWastedSectors)
                {
                    ulWastedSectors = u64TotalSectors -
                        ((uint32_t)usCylinders2 * (uint32_t)usHeads2 *
                        (uint32_t)ucSectors2);
                    usOptimalCylinders = usCylinders2;
                    usOptimalHeads = usHeads2;
                    ucOptimalSectors = ucSectors2;
                }

                // Keep searching
                ucSectors++;
                if(ucSectors > MAX_SECTORS)
                {
                    ucSectors = 1;
                    usHeads++;
                    if(usHeads > MAX_HEADS)
                    {
                        usHeads = 1;
                        usCylinders++;
                        if(usCylinders > MAX_CYLINDERS)
                        {
                            break;
                        }
                    }
                }
            }
        }
    }

    chs.cylinder = usOptimalCylinders;
    chs.head = usOptimalHeads;
    chs.sector = ucOptimalSectors;

    CalcStartEndChs(iPartitionNum, &chs, pMmcPartitionTable);
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Calculate Start and END CHS values
////////////////////////////////////////////////////////////////////////////////
static RtStatus_t CalcStartEndChs(int iPartitionNum, Chs_t *pchs, PartTable_t *pMmcPartitionTable)
{
    Chs_t StartChs;
    Chs_t EndChs;

    // Calculate the start CHS for the MBR partition entry.  Use the MBR
    //  start sector number adjusted from LBA 0-based to a 1-based address.

    SectorToChs(pchs, &StartChs, (pMmcPartitionTable->partition[iPartitionNum].firstSectorNumber+1));
    PackChs(StartChs, &(pMmcPartitionTable->partition[iPartitionNum].startChsPacked));

    // Calculate the end CHS for the MBR partition entry.  Use the MBR
    //  start sector number, the SectorCount will adjust for LBA 0-based.
    SectorToChs(pchs, &EndChs, (pMmcPartitionTable->partition[iPartitionNum].firstSectorNumber+
        pMmcPartitionTable->partition[iPartitionNum].sectorCount));
    PackChs(EndChs, &(pMmcPartitionTable->partition[iPartitionNum].endChsPacked));

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Converts sector to chs
////////////////////////////////////////////////////////////////////////////////
static RtStatus_t SectorToChs(Chs_t *pchs, Chs_t *_p_chs, uint32_t _sector)
{
    uint32_t ulTemp=0;
    bool not_done = TRUE;
    _p_chs->cylinder = 0;
    _p_chs->head = 0;
    _p_chs->sector = 1;

    while(not_done)
    {
        ulTemp = (_p_chs->cylinder * pchs->head * pchs->sector)
            + (_p_chs->head * pchs->sector)
            + _p_chs->sector;

        if( ulTemp == _sector)
        {
            break;
        }

        _p_chs->sector++;
        if(_p_chs->sector > pchs->sector)
        {
            _p_chs->sector = 1;
            _p_chs->head++;
            if(_p_chs->head == pchs->head)
            {
                _p_chs->head = 0;
                _p_chs->cylinder++;
                if(_p_chs->cylinder == pchs->cylinder)
                {
                    return ERROR_GENERIC;
                }
            }
        }

    }
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      packs chs values into CHS_PACKED data structure
////////////////////////////////////////////////////////////////////////////////
static void PackChs(Chs_t _chs, ChsPacked_t *_p_chsPacked)
{
    _p_chsPacked->cylinder  = (uint8_t)(_chs.cylinder & 0x00FF);
    _p_chsPacked->head      = (uint8_t)_chs.head;
    _p_chsPacked->sector    = _chs.sector | ((uint8_t)((_chs.cylinder & 0x0300)>>2));
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
