////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_media
//! @{
//
// Copyright (c) 2003-2008 SigmaTel, Inc.
//
//! \file ddi_nand_hamming_code_ecc.c
//! \brief hamming code ecc functions.
//!
////////////////////////////////////////////////////////////////////////////////
 
#include "ddi_nand_hamming_code_ecc.h"
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

RtStatus_t TripleRedundancyCheck(uint8_t* pNCBCopy1, uint8_t* pNCBCopy2, uint8_t* pNCBCopy3, 
                            uint8_t * pP1, uint8_t * pP2, uint8_t * pP3, 
                            uint8_t * pu8HammingCopy);

void CalculateParity(uint16_t d, uint8_t * p);

RtStatus_t TableLookupSingleErrors(uint8_t u8Synd, uint8_t * pu8BitToFlip);

RtStatus_t HammingCheck(uint8_t * pNCB, uint8_t * pParityBlock);

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief compares three NCBs and return error if no two are identical else
//!        returns the one that matches with at least another copy.
//!
//! a local function
//!
//! This function compares the three NCB structures and their corresponding 
//! parity bits. If any two matches then search ends. If none matches then it 
//! returns an error.
//!
//! \param[in]  pNCBCopy1  pointer to first NCB structure
//! \param[in]  pNCBCopy2  pointer to second NCB structure
//! \param[in]  pNCBCopy3  pointer to third NCB structure
//! \param[in]  pP1 pointer to first set of Parity bits
//! \param[in]  pP2 pointer to second set of Parity bits
//! \param[in]  pP3 pointer to third set of Parity bits
//! \param[out] pu8HammingCopy to return either 1, 2 or 3 that qualified for
//!             Humming code analysis.
//!
//! \retval    SUCCESS 
//! \retval    ERROR_DDI_NAND_DRIVER_NCB_TRIPLE_RED_CHK_FAILED
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t TripleRedundancyCheck(uint8_t * pNCBCopy1, uint8_t* pNCBCopy2, uint8_t* pNCBCopy3, 
                            uint8_t * pP1, uint8_t * pP2, uint8_t * pP3, 
                            uint8_t * pu8HammingCopy)
{
    int nError;

    // compare 1 and 2 copies of NCB
    nError = 0;
    nError = memcmp((void*)pNCBCopy1, (void*)pNCBCopy2, NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES);
    if( nError == 0 )
    {
        nError = memcmp((void*)pP1, (void*)pP2, NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES);
    }

    if( nError == 0 )
    {
        // 1 and 2 are identical so lets go with 1
        *pu8HammingCopy = 1;
        return SUCCESS;
    }

    // compare 1 and 3 copies of NCB
    nError = 0;
    nError = memcmp((void*)pNCBCopy1, (void*)pNCBCopy3, NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES);
    if( nError == 0 )
    {
        nError = memcmp((void*)pP1, (void*)pP3, NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES);
    }

    if( nError == 0 )
    {
        // 1 and 3 are identical so lets go with 1
        *pu8HammingCopy = 1;
        return SUCCESS;
    }

    // compare 2 and 3 copies of NCB
    nError = 0;
    nError = memcmp((void*)pNCBCopy2, (void*)pNCBCopy3, NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES);
    if( nError == 0 )
    {
        nError = memcmp((void*)pP2, (void*)pP3, NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES);
    }

    if( nError == 0 )
    {
        // 2 and 3 are identical so lets go with 2
        *pu8HammingCopy = 2;
        return SUCCESS;
    }

    return ERROR_DDI_NAND_DRIVER_NCB_TRIPLE_RED_CHK_FAILED;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief calculates parity using Hsiao Code and Hamming code
//!
//! \param[in]  d, given 16 bits integer
//! \param[out] p, pointer to uint8_t for parity
//!
//! \retval    none.
//!
////////////////////////////////////////////////////////////////////////////////
void CalculateParity(uint16_t d, uint8_t * p)
{
//  p[0] = d[15] ^ d[12] ^ d[11] ^ d[ 8] ^ d[ 5] ^ d[ 4] ^ d[ 3] ^ d[ 2];
//  p[1] = d[13] ^ d[12] ^ d[11] ^ d[10] ^ d[ 9] ^ d[ 7] ^ d[ 3] ^ d[ 1];
//  p[2] = d[15] ^ d[14] ^ d[13] ^ d[11] ^ d[10] ^ d[ 9] ^ d[ 6] ^ d[ 5];
//  p[3] = d[15] ^ d[14] ^ d[13] ^ d[ 8] ^ d[ 7] ^ d[ 6] ^ d[ 4] ^ d[ 0];
//  p[4] = d[12] ^ d[ 9] ^ d[ 8] ^ d[ 7] ^ d[ 6] ^ d[ 2] ^ d[ 1] ^ d[ 0];
//  p[5] = d[14] ^ d[10] ^ d[ 5] ^ d[ 4] ^ d[ 3] ^ d[ 2] ^ d[ 1] ^ d[ 0];

    uint8_t Bit0  = (d & (1 << 0))  ? 1 : 0; 
    uint8_t Bit1  = (d & (1 << 1))  ? 1 : 0;
    uint8_t Bit2  = (d & (1 << 2))  ? 1 : 0;
    uint8_t Bit3  = (d & (1 << 3))  ? 1 : 0;
    uint8_t Bit4  = (d & (1 << 4))  ? 1 : 0;
    uint8_t Bit5  = (d & (1 << 5))  ? 1 : 0;
    uint8_t Bit6  = (d & (1 << 6))  ? 1 : 0;
    uint8_t Bit7  = (d & (1 << 7))  ? 1 : 0;
    uint8_t Bit8  = (d & (1 << 8))  ? 1 : 0;
    uint8_t Bit9  = (d & (1 << 9))  ? 1 : 0;
    uint8_t Bit10 = (d & (1 << 10)) ? 1 : 0;
    uint8_t Bit11 = (d & (1 << 11)) ? 1 : 0;
    uint8_t Bit12 = (d & (1 << 12)) ? 1 : 0;
    uint8_t Bit13 = (d & (1 << 13)) ? 1 : 0;
    uint8_t Bit14 = (d & (1 << 14)) ? 1 : 0;
    uint8_t Bit15 = (d & (1 << 15)) ? 1 : 0;

    *p = 0;

    *p |= ((Bit15 ^ Bit12 ^ Bit11 ^ Bit8  ^ Bit5  ^ Bit4  ^ Bit3  ^ Bit2) << 0);
    *p |= ((Bit13 ^ Bit12 ^ Bit11 ^ Bit10 ^ Bit9  ^ Bit7  ^ Bit3  ^ Bit1) << 1);
    *p |= ((Bit15 ^ Bit14 ^ Bit13 ^ Bit11 ^ Bit10 ^ Bit9  ^ Bit6  ^ Bit5) << 2);
    *p |= ((Bit15 ^ Bit14 ^ Bit13 ^ Bit8  ^ Bit7  ^ Bit6  ^ Bit4  ^ Bit0) << 3);
    *p |= ((Bit12 ^ Bit9  ^ Bit8  ^ Bit7  ^ Bit6  ^ Bit2  ^ Bit1  ^ Bit0) << 4);
    *p |= ((Bit14 ^ Bit10 ^ Bit5  ^ Bit4  ^ Bit3  ^ Bit2  ^ Bit1  ^ Bit0) << 5);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief pre calculated array of syndromes using Hsiao code. 
//!
//! The table consists of 22 entries, first 16 entries for each bit of error in 
//! 16-bit data and the next 6 entries for 6-bit parity.
//!
//! The logic used to calculate this table is explained in the code below:
//! \code
//! 
//! for(j=0; j<22; j++) { // for each error location
//!
//!     // d is 16-bit data and p is 6-bit parity
//!     // initialize received vector   
//!     for(i=0;i<22;i++) {
//!         if(i<16) 
//!             r[i] = d[i];
//!         else
//!             r[i] = p[i-16];
//!     }
//!     // inject error
//!     r[j]=r[j]^0x1;
//!
//!     // compute syndrome
//!     s[0] = r[16] ^ r[15] ^ r[12] ^ r[11] ^ r[8]  ^ r[5]  ^ r[4] ^ r[3] ^ r[2];
//!     s[1] = r[17] ^ r[13] ^ r[12] ^ r[11] ^ r[10] ^ r[9]  ^ r[7] ^ r[3] ^ r[1];
//!     s[2] = r[18] ^ r[15] ^ r[14] ^ r[13] ^ r[11] ^ r[10] ^ r[9] ^ r[6] ^ r[5];
//!     s[3] = r[19] ^ r[15] ^ r[14] ^ r[13] ^ r[8]  ^ r[7]  ^ r[6] ^ r[4] ^ r[0];
//!     s[4] = r[20] ^ r[12] ^ r[9]  ^ r[8]  ^ r[7]  ^ r[6]  ^ r[2] ^ r[1] ^ r[0];
//!     s[5] = r[21] ^ r[14] ^ r[10] ^ r[5]  ^ r[4]  ^ r[3]  ^ r[2] ^ r[1] ^ r[0];
//! }
//! \endcode
////////////////////////////////////////////////////////////////////////////////
const uint8_t au8SyndTable[] = {
    0x38, 0x32, 0x31, 0x23, 0x29, 0x25, 0x1C, 0x1A, 0x19, 0x16, 0x26, 0x07, 0x13, 0x0E, 0x2C, 0x0D, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20 
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
RtStatus_t TableLookupSingleErrors(uint8_t u8Synd, uint8_t * pu8BitToFlip)
{
    uint8_t i;
    for(i=0; i<22; i++)
    {
        if( au8SyndTable[i] == u8Synd )
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
//! \param[in] pParityBlock, block of parity codes, every 6 bits for every 16 bits of 
//!            data in NCB block
//!
//! \retval    SUCCESS, if no error or 1 bit error that is fixed.
//! \retval    ERROR_DDI_NAND_DRIVER_NCB_HAMMING_DOUBLE_ERROR, double error occured 
//!            that cannot be fixed.
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t HammingCheck(uint8_t * pNCB, uint8_t * pParityBlock)
{
    uint16_t *p16Data = (uint16_t*)pNCB;
    uint8_t P;
    uint8_t NP;
    int i, j=0;
    int nBitPointer=0;
    uint8_t u8Syndrome, u8BitToFlip;
    RtStatus_t retStatus=SUCCESS;

    for(i=0; i<256; i++)
    {
        // the problem here is to read 6 bits from an 8-bit byte array for each parity code. 
        // The parity code is either present in 6 lsbs, or partial in one byte and remainder 
        // in the next byte or it can be 6 msbs. The code below tries to collect all 6 bits 
        // into one byte, leaving upper 2 bits 0.
        switch(nBitPointer)
        {
        case 0:
            // if nBitPointer is 0, that means, we are at the start of a new byte. 
            // we can straight away read 6 lower bits from the byte for the parity.
            P = (pParityBlock[j] & 0x3F);
            nBitPointer = 2;
            break;
        case 2:
            // if nBitPointer is 2, that means, we need to read 2 MSb from jth byte of 
            //parity block and remaining 4 from next byte. 
            P = ((pParityBlock[j] & 0xC0) >> 6); // read 2 MSbs and moved them to left 6 times
            j++; //go to next byte
            P |= ((pParityBlock[j] & 0x0F) << 2); // read 4 from LSb, move it up 2 times so that all 6 bits are aligned in a byte.
            nBitPointer = 4;
            break;
        case 4:
            // if nBitPointer is 4, that means, we need to read 4 MSbs from jth byte of 
            //parity block and remaining 2 from next byte. 
            P = ((pParityBlock[j] & 0xF0) >> 4); // read 4 MSbs and moved them to left 4 times
            j++; //goto the next byte
            P |= ((pParityBlock[j] & 0x03) << 4); // read 2 LSbs and moved them to right 4 times so that all 6 bits are aligned in a byte
            nBitPointer = 6;
            break;
        case 6:
            // if nBitPointer is 6, that means, we need to read 6 MSbs from jth byte of 
            // parity block 
            P = ((pParityBlock[j] & 0xFC) >> 2); // read 6 MSbs and moved them to left 2 times.
            nBitPointer = 0; 
            j++; // go to the next byte
            break;
        };

        // calculate new parity out of 16-bit data
        if((*p16Data == 0) || (*p16Data == 0xFFFF))
        {
            // this is for optimization purpose
            NP=0;
        }
        else
        {
            CalculateParity(*p16Data, &NP);
        }

        // calculate syndrome by XORing parity read from NAND and new parity NP just calculated.
        u8Syndrome = NP ^ P;

        // if syndrome is 0, that means the data is good.
        if( u8Syndrome == 0 )
        {
            // data is good. fetch next 16bits
            p16Data++;
            continue;
        }

        // check for double bit errors, which is the case when we have even number of 1s in the syndrome
        if( IsNumOf1sEven(u8Syndrome) )
        {
            // found a double error, can't fix it, return
            //DBG_PRINTF("Hamming: Found a double bit error in 16-bit data at byte offset %d\n", i);
            return ERROR_DDI_NAND_DRIVER_NCB_HAMMING_DOUBLE_ERROR;
        }
        else
        {
            // this is a single bit error and can be fixed
            retStatus = TableLookupSingleErrors(u8Syndrome, &u8BitToFlip);
            if( retStatus != SUCCESS )
            {
                return retStatus;
            }

            if( u8BitToFlip < 16 )
            {
                // error is in data bit u8BitToFlip, flip that bit to correct it
                *p16Data ^= (0x1 << u8BitToFlip);
                ////DBG_PRINTF("Hamming: Found a single bit error in 16-bit data at byte offset %d bit %d\n", i, u8BitToFlip);
            }
            else
            {
                ////DBG_PRINTF("Hamming: Found a single bit error in parity byte at offset %d bit %d\n", i, u8BitToFlip);

                // the error is a 1 bit error and is in parity so we do not worry fixing it.
            }
        }
        // fetch next 16bits
        p16Data++;
    }
    return retStatus;
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief encode 512 byte block with Hamming Codes and Triple Redundancy
//!
//! This function encodes 512 byte block of data with Hamming codes and makes 
//! three copies of data and parity.
//!
//! \param[in] pSector
//! \param[out] pOutBuffer
//!
//! \retval    none
////////////////////////////////////////////////////////////////////////////////
void EncodeHammingAndRedundancy(unsigned char* pSector, uint8_t *pOutBuffer)
{
    int i,j;
    uint16_t *pui16SectorData = (uint16_t*)pSector;
    int state=0;
    for(i=0; i<(NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES); i++)
    {
        pOutBuffer[i]=pSector[i];
        pOutBuffer[i+NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES]=pSector[i];
        pOutBuffer[i+(2*NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES)]=pSector[i];
    }

    i+=(2*NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES);

    for(j=0; j<256; j++)
    {
        uint8_t NP=0;

        CalculateParity(pui16SectorData[j], &NP);
        
        switch (state)
        {
        case 0:
            pOutBuffer[i] = 0;
            pOutBuffer[i] |= ((NP & 0x3F));
            state=2;
            break;
        case 2:
            pOutBuffer[i] |= (NP & 0x03)<<6;
            i++;
            pOutBuffer[i] = 0;
            pOutBuffer[i] |= (NP & 0x3C)>>2;
            state=4;
            break;
        case 4:
            pOutBuffer[i] |= (NP & 0x0F)<<4;
            i++;
            pOutBuffer[i] = 0;
            pOutBuffer[i] |= (NP & 0x30)>>4;
            state=6;
            break;
        case 6:
            pOutBuffer[i] |= (NP &0x3F) << 2;
            i++;
            state=0;
            break;
        }
    }
    // triple redundancy parity bits.
    for(j=0; j<NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES; j++)
    {
        pOutBuffer[i]=pOutBuffer[NAND_HC_ECC_OFFSET_FIRST_PARITY_COPY+j];
        pOutBuffer[i+NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES]=pOutBuffer[NAND_HC_ECC_OFFSET_FIRST_PARITY_COPY+j];
        i++;
    }
}

////////////////////////////////////////////////////////////////////////////////
//!
//! \brief Software ECC on a page of NCB/BCB data.
//!
//! \param[in] pBuffer Pointer to buffer to use for sector reads.
//! \param[out]ppBCBGoodCopy returns a copy of good bcb address in pBuffer
//!
//! \retval SUCCESS The BCB was found. 
//! \retval ERROR_ROM_NAND_DRIVER_NCB_TRIPLE_RED_CHK_FAILED  .
//!
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_nand_media_DecodeBCB(uint8_t * pBuffer, BootBlockStruct_t **ppBCBGoodCopy)
{
    RtStatus_t retStatus=SUCCESS;
    BootBlockStruct_t * pBCB;
    BootBlockStruct_t * pBCBCopy1;
    BootBlockStruct_t * pBCBCopy2;
    BootBlockStruct_t * pBCBCopy3;
    uint8_t * pParity;
    uint8_t * pParityCopy1;
    uint8_t * pParityCopy2;
    uint8_t * pParityCopy3;
    uint8_t u8ECCStatus; //! this will tell what copies of NCB are processed with HammingCheck.
    uint8_t u8HammingCopy; //!< this will tell what copy of NCB is currently in process

    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // ECC check for NCB/BCB block
    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // first copy of NCB data is stored at offset 0
    // second copy at offset 512 and third at offset 1024
    // the parity bits are also duplicated three times.
    // The parity for first copy of data are stored at offset 1536
    // for every 16 bits of data, we have 6 bits of parity, for 512 bytes (4096 bits) we have 256 16-bits data packets,
    // 256*6=1536 parity bits are required for one NCB. 1536/8=192 in bytes.
    // The parity for second copy of data are stored at offset 1536+192=1728
    // and third copy is stored at 1728+192=1920
    pBCBCopy1 = (BootBlockStruct_t *)pBuffer;
    pBCBCopy2 = (BootBlockStruct_t *)(pBuffer+NAND_HC_ECC_OFFSET_SECOND_DATA_COPY); 
    pBCBCopy3 = (BootBlockStruct_t *)(pBuffer+NAND_HC_ECC_OFFSET_THIRD_DATA_COPY); 
    pParityCopy1 = (pBuffer+NAND_HC_ECC_OFFSET_FIRST_PARITY_COPY); 
    pParityCopy2 = (pBuffer+NAND_HC_ECC_OFFSET_SECOND_PARITY_COPY); 
    pParityCopy3 = (pBuffer+NAND_HC_ECC_OFFSET_THIRD_PARITY_COPY); 

    // u8ECCStatus will be 0x1 if hamming code is run on first copy
    // u8ECCStatus will be 0x2 if hamming code is run on second copy
    // u8ECCStatus will be 0x3 if hamming code is run on first and second copies
    // u8ECCStatus will be 0x4 if hamming code is run on third copy
    // u8ECCStatus will be 0x7 if hamming code is run on all three copies
    u8HammingCopy = 0;
    // try triple redundancy check first
    //DBG_PRINTF("--->Running Triple redundancy check on NCB copies.\n");
    retStatus = TripleRedundancyCheck((uint8_t*)pBCBCopy1, (uint8_t*)pBCBCopy2, (uint8_t*)pBCBCopy3, pParityCopy1, pParityCopy2, 
        pParityCopy3, &u8HammingCopy);
    if( retStatus != SUCCESS )
    {
        //DBG_PRINTF("--->Triple redundancy failed to find two same copies.\n");
        // it couldn't resolve which copy is correct.
        // we need to try Hamming Code on first Copy
        u8HammingCopy = 1;
    }
    else
    {
        //DBG_PRINTF("--->Copy %d of NCB is found good with triple redundancy check.\n", u8HammingCopy);
    }

    // TripleRedundancyCheck() will return either 1 or 2 in u8HammingCopy, because at least 2 copies should be identical.
    // if 1 and 2 are same, then 1 is returned, if 2 and 3 are equal 2 is returned, and if 1 and 3 are equal then 1 is returned.
    if( u8HammingCopy == 1)
    {
        // first copy is not yet run with hamming code, lets run it
        pBCB=pBCBCopy1;
        pParity=pParityCopy1;
    }
    else 
    {
        // if u8HammingCopy is not 1 then it must be 2 as TripleRedundancyCheck does not return 3.
        pBCB=pBCBCopy2;
        pParity=pParityCopy2;
    }

    u8ECCStatus = 0;
    while (true)
    {
		//DBG_PRINTF("--->Hamming check on copy %d of NCB.\n", u8HammingCopy);
		retStatus = HammingCheck((uint8_t*)pBCB, pParity);

        // update ECC status bit mask, first bit for copy1, second bit for copy2 and third bit for copy3.

        u8ECCStatus |= (1 << (u8HammingCopy-1));
        
        if( retStatus == SUCCESS )
        {
            //DBG_PRINTF("--->Hamming check on copy %d of NCB is successful.\n", u8HammingCopy);
            break;
        }
        // try running other copies
        if( u8ECCStatus == BITMASK_HAMMINGCHECKED_ALL_THREE_COPIES )
        {
            // finished running ECC on all three copies
            //DBG_PRINTF("--->All three copies of NCB failed Hamming Check.\n");
            retStatus = ERROR_DDI_NAND_HAL_ECC_FIX_FAILED;

            break;
        }
        // try running other copies with hamming code
        if( !(u8ECCStatus & BITMASK_HAMMINGCHECKED_FIRST_COPY) )
        {
            // first copy is not yet run with hamming code, lets run it
            pBCB=pBCBCopy1;
            pParity=pParityCopy1;
            u8HammingCopy = 1;
        }
        else if( !(u8ECCStatus & BITMASK_HAMMINGCHECKED_SECOND_COPY) )
        {
            // second copy is not yet run with hamming code, lets run it
            pBCB=pBCBCopy2;
            pParity=pParityCopy2;
            u8HammingCopy = 2;
        }
        else if( !(u8ECCStatus & BITMASK_HAMMINGCHECKED_THIRD_COPY) )
        {
            // third copy is not yet run with hamming code, lets run it
            pBCB=pBCBCopy3;
            pParity=pParityCopy3;
            u8HammingCopy = 3;
        }
    }
    if( retStatus == SUCCESS )
    {
        *ppBCBGoodCopy = pBCB;
    }

    return retStatus;
}

// eof ddi_nand_hamming_code_ecc.c
//! @}
