////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_media
//! @{
//
// Copyright (c) 2003-2008 SigmaTel, Inc.
//
//! \file ddi_nand_hamming_code_bch_ecc.c
//! \brief hamming code bch ecc functions.
//!
////////////////////////////////////////////////////////////////////////////////
 
#include <stdlib.h>
#include <string.h>
#include "ddi_nand_hamming_code_ecc.h"

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

void CalculateParity_New(uint8_t d, uint8_t * p);
RtStatus_t TableLookupSingleErrors_New(uint8_t u8Synd, uint8_t * pu8BitToFlip);
RtStatus_t HammingCheck_New(uint8_t * pNCB, uint8_t * pParityBlock);

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief count number of 1s and return true if they occur even number of times
//!        in the given byte.
//!
//! xor all the bits of u8, if even number of 1s in u8, then the result is 0.
//!
//! \param[in]  u8 // input byte
//!
//! \retval    true, if even number of 1s in u8
//! \retval    false, if odd number of 1s in u8
//!
////////////////////////////////////////////////////////////////////////////////
bool IsNumOf1sEven(uint8_t u8)
{
    int i,  nCountOf1s=0;

    for(i=0; i<8; i++)
    {
        nCountOf1s ^= ((u8 & (1 << i)) >> i);
    }
    return !(nCountOf1s);
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief Software ECC on a for 378x TA3+ page NCB/BCB data.
//!
//! \param[in] pBuffer Pointer to buffer to use for sector reads.
//! \param[out]ppBCBGoodCopy returns a copy of good bcb address in pBuffer
//!
//! \retval SUCCESS The BCB was found. 
//! \retval ERROR_DDI_NAND_DRIVER_NCB_TRIPLE_RED_CHK_FAILED  .
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_media_decodeBCB_New(uint8_t * pBuffer, BootBlockStruct_t **pNCBGoodCopy)
{
    RtStatus_t retStatus=SUCCESS;
    BootBlockStruct_t * pNCB;
    uint8_t * pParity;
 
    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // ECC check for NCB block
    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // first copy of NCB data is stored at offset NAND_HC_ECC_OFFSET_DATA_COPY
    // The parity bytes are stored at offset NAND_HC_ECC_OFFSET_PARITY_COPY
    // for every 8 bits of data, we have 5 bits of parity, for 512 bytes (4096 bits) we have 512 8-bits data packets,
    // we will have each 5-bit parity in a byte so we need 512 bytes for parity data.
    pNCB = (BootBlockStruct_t *)(pBuffer+NAND_HC_ECC_OFFSET_DATA_COPY);
    pParity   = (pBuffer+NAND_HC_ECC_OFFSET_PARITY_COPY); 
 
    //DBG_PRINTF("--->Running Hamming check on NCB.\n");
    retStatus = HammingCheck_New((uint8_t*)pNCB, pParity);
    if( retStatus == SUCCESS )
    {
        //DBG_PRINTF("--->Hamming check on NCB is successful.\n");
        *pNCBGoodCopy = pNCB;
    }
    return retStatus;
}


////////////////////////////////////////////////////////////////////////////////
//!
//! \brief calculates parity using Hsiao Code and Hamming code
//!
//! \param[in]  d, given 8 bits integer
//! \param[out] p, pointer to uint8_t for parity
//!
//! \retval    none.
//!
////////////////////////////////////////////////////////////////////////////////
void CalculateParity_New(uint8_t d, uint8_t * p)
{
    uint8_t Bit0  = (d & (1 << 0))  ? 1 : 0; 
    uint8_t Bit1  = (d & (1 << 1))  ? 1 : 0;
    uint8_t Bit2  = (d & (1 << 2))  ? 1 : 0;
    uint8_t Bit3  = (d & (1 << 3))  ? 1 : 0;
    uint8_t Bit4  = (d & (1 << 4))  ? 1 : 0;
    uint8_t Bit5  = (d & (1 << 5))  ? 1 : 0;
    uint8_t Bit6  = (d & (1 << 6))  ? 1 : 0;
    uint8_t Bit7  = (d & (1 << 7))  ? 1 : 0;

    *p = 0;

    *p |= ((Bit6 ^ Bit5 ^ Bit3 ^ Bit2)               << 0);
    *p |= ((Bit7 ^ Bit5 ^ Bit4 ^ Bit2 ^ Bit1)        << 1);
    *p |= ((Bit7 ^ Bit6 ^ Bit5 ^ Bit1 ^ Bit0)        << 2);
    *p |= ((Bit7 ^ Bit4 ^ Bit3 ^ Bit0)               << 3);
    *p |= ((Bit6 ^ Bit4 ^ Bit3 ^ Bit2 ^ Bit1 ^ Bit0) << 4);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief pre calculated array of syndromes using Hsiao code. 
//!
//! The table consists of 13 entries, first 8 entries for each bit of error in 
//! 8-bit data and the next 5 entries for 5-bit parity.
//!
//! The logic used to calculate this table is explained in the code below:
//! \code
//! 
//! for(j=0; j<13; j++) { // for each error location
//!
//!     // d is 8-bit data and p is 5-bit parity
//!     // initialize received vector   
//!     for(i=0;i<13;i++) {
//!         if(i<8) 
//!             r[i] = d[i];
//!         else
//!             r[i] = p[i-8];
//!     }
//!     // inject error
//!     r[j]=r[j]^0x1;
//!
//!     // compute syndrome
//!     s[0] = r[8]  ^ r[6] ^ r[5] ^ r[3] ^ r[2];
//!     s[1] = r[9]  ^ r[7] ^ r[5] ^ r[4] ^ r[2] ^ r[1];
//!     s[2] = r[10] ^ r[7] ^ r[6] ^ r[5] ^ r[1] ^ r[0];
//!     s[3] = r[11] ^ r[7] ^ r[4] ^ r[3] ^ r[0];
//!     s[4] = r[12] ^ r[6] ^ r[4] ^ r[3] ^ r[2] ^ r[1] ^ r[0];
//!     
//! }
//! \endcode
////////////////////////////////////////////////////////////////////////////////

const uint8_t au8SyndTable_New[] = {
    0x1C, 0x16, 0x13, 0x19, 0x1A, 0x07, 0x15, 0x0E, 0x01, 0x02, 0x04, 0x08, 0x10 
};

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief looks up for a match in syndrome table array.
//!
//! \param[in]  u8Synd given syndrome to match in the table
//! \param[out] pu8BitToFlip pointer to return the index of array that matches
//!             with given syndrome
//!
//! \retval    SUCCESS if a match is found
//! \retval    ERROR_DDI_NAND_DRIVER_NCB_SYNDROME_TABLE_MISMATCH no match found
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t TableLookupSingleErrors_New(uint8_t u8Synd, uint8_t * pu8BitToFlip)
{
    uint8_t i;
    for(i=0; i<13; i++)
    {
        if( au8SyndTable_New[i] == u8Synd )
        {
            *pu8BitToFlip = i;
            return SUCCESS;
        }
    }
    return ERROR_DDI_NAND_DRIVER_NCB_SYNDROME_TABLE_MISMATCH;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief evaluate NCB block with Hamming Codes
//!
//! This function evaluates NCB Block with Hamming codes and if single bit error
//! occurs then it is fixed, if double error occurs then it returns an error
//!
//! \param[in] pNCB, NCB block
//! \param[in] pParityBlock, block of parity codes, every 6 bits for every 8 bits of 
//!            data in NCB block
//!
//! \retval    SUCCESS, if no error or 1 bit error that is fixed.
//! \retval    ERROR_DDI_NAND_DRIVER_NCB_HAMMING_DOUBLE_ERROR, double error occured 
//!            that cannot be fixed.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t HammingCheck_New(uint8_t * pNCB, uint8_t * pParityBlock)
{
    uint8_t *pu8Data = (uint8_t*)pNCB;
    uint8_t P;
    uint8_t NP;
    int i;
    uint8_t u8Syndrome, u8BitToFlip;
    RtStatus_t retStatus=SUCCESS;

    for(i=0; i<NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES; i++)
    {
        // Put parity for ith byte in P
        P = pParityBlock[i];

        // calculate new parity out of 8-bit data
        CalculateParity_New(*pu8Data, &NP);

        // calculate syndrome by XORing parity read from NAND and new parity NP just calculated.
        u8Syndrome = NP ^ P;

        // if syndrome is 0, that means the data is good.
        if( u8Syndrome == 0 )
        {
            // data is good. fetch next 8 bits
            pu8Data++;
            continue;
        }

        // Here we can check for only single and double bit errors. This method does not detect more than
        // two bit errors. If there are more than 2 bit errors they go undetected.
        // Check for double bit errors, which is the case when we have even number of 1s in the syndrome
        if( IsNumOf1sEven(u8Syndrome) )
        {
            // found a double error, can't fix it, return
            //DBG_PRINTF("Hamming: Found a double bit error in 8-bit data at byte offset %d\n", i);
            return ERROR_DDI_NAND_DRIVER_NCB_HAMMING_DOUBLE_ERROR;
        }
        else
        {
            // this is a single bit error and can be fixed
            retStatus = TableLookupSingleErrors_New(u8Syndrome, &u8BitToFlip);
            if( retStatus != SUCCESS )
            {
                retStatus = ERROR_DDI_NAND_DRIVER_NCB_HAMMING_DOUBLE_ERROR;
                return retStatus;
            }

            if( u8BitToFlip < 8 )
            {
                // error is in data bit u8BitToFlip, flip that bit to correct it
                *pu8Data ^= (0x1 << u8BitToFlip);
                //DBG_PRINTF("Hamming: Found a single bit error in 8-bit data at byte offset %d bit %d\n", i, u8BitToFlip);
            }
            else
            {
                //DBG_PRINTF("Hamming: Found a single bit error in parity byte at offset %d bit %d\n", i, u8BitToFlip);
            }
        }
        // fetch next 8 bits
        pu8Data++;
    }
    return retStatus;
}

void CalculateHammingForNCB_New(unsigned char* pSector, uint8_t *pOutBuffer)
{
    int i;
    uint8_t *pui8SectorData = (uint8_t*)pSector;
 
    for(i=0; i<NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES; i++ )
    {
        uint8_t NP=0;
 
        CalculateParity_New(pui8SectorData[i], &NP);
 
        pOutBuffer[i] = (NP & 0x1F);
    }
}

// eof rom_nand_hamming_code_bch_ecc.c
//! @}
