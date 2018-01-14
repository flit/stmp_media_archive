////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_nand_media
//! @{
//
// Copyright (c) 2003-2007 SigmaTel, Inc.
//
//! \file ddi_nand_hamming_code_ecc.h
//! \brief This file provides header info for hamming code ecc.
//!
////////////////////////////////////////////////////////////////////////////////
#ifndef _DDI_NAND_HAMMING_CODE_ECC_H
#define _DDI_NAND_HAMMING_CODE_ECC_H

//#include "ddi_nand_media.h"
#include "types.h"
#include "drivers/media/ddi_media_errordefs.h"
#include "rom_nand_boot_blocks.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////
//! Bytes per NCB data block
#define NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES        (512) 
//! Size of a parity block in bytes for all 16-bit data blocks present inside one 512 byte NCB block.
#define NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES      ((((512*8)/16)*6)/8) 
//! Offset to first copy of NCB in a NAND page
#define NAND_HC_ECC_OFFSET_FIRST_DATA_COPY            (0) 
//! Offset to second copy of NCB in a NAND page
#define NAND_HC_ECC_OFFSET_SECOND_DATA_COPY           (NAND_HC_ECC_OFFSET_FIRST_DATA_COPY+NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES) 
//! Offset to third copy of NCB in a NAND page
#define NAND_HC_ECC_OFFSET_THIRD_DATA_COPY            (NAND_HC_ECC_OFFSET_SECOND_DATA_COPY+NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES)
//! Offset to first copy of Parity block in a NAND page
#define NAND_HC_ECC_OFFSET_FIRST_PARITY_COPY          (NAND_HC_ECC_OFFSET_THIRD_DATA_COPY+NAND_HC_ECC_SIZEOF_DATA_BLOCK_IN_BYTES)
//! Offset to second copy of Parity block in a NAND page
#define NAND_HC_ECC_OFFSET_SECOND_PARITY_COPY         (NAND_HC_ECC_OFFSET_FIRST_PARITY_COPY+NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES)
//! Offset to third copy of Parity block in a NAND page
#define NAND_HC_ECC_OFFSET_THIRD_PARITY_COPY          (NAND_HC_ECC_OFFSET_SECOND_PARITY_COPY+NAND_HC_ECC_SIZEOF_PARITY_BLOCK_IN_BYTES)

#define BITMASK_HAMMINGCHECKED_ALL_THREE_COPIES 0x7 //!< to indicate all three copies of NCB in first page are processed with Hamming codes.
#define BITMASK_HAMMINGCHECKED_FIRST_COPY       0x1 //!< to indicate first copy of NCB is processed with Hamming codes.
#define BITMASK_HAMMINGCHECKED_SECOND_COPY      0x2 //!< to indicate second copy of NCB is processed with Hamming codes.
#define BITMASK_HAMMINGCHECKED_THIRD_COPY       0x4 //!< to indicate third copy of NCB is processed with Hamming codes.

// These defines are for the new TA3 boot block storage
#define NAND_HC_ECC_OFFSET_DATA_COPY	12
#define NAND_HC_ECC_OFFSET_PARITY_COPY	((NAND_HC_ECC_OFFSET_DATA_COPY) + 512)

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

extern const uint8_t au8SyndTable[];

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

//! \name Shared utilities
//@{
bool IsNumOf1sEven(uint8_t u8);
//@}

//! \name TA1-2 implementation
//@{
void EncodeHammingAndRedundancy(unsigned char* pSector, uint8_t *pOutBuffer);

RtStatus_t ddi_nand_media_DecodeBCB(uint8_t * pBuffer, BootBlockStruct_t **ppBCBGoodCopy);
//@}

//! \name TA3 implementation
//@{
RtStatus_t ddi_nand_media_decodeBCB_New(uint8_t * pBuffer, BootBlockStruct_t **ppBCBGoodCopy);

void CalculateHammingForNCB_New(unsigned char* pSector, uint8_t *pOutBuffer);
//@}

#endif
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
// @}
