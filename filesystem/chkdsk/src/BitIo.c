////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2002
//
//! \file BitIo.c
//! \brief Utilities operating on bits for CheckDisk.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include "chkdsk.h"
#include "types.h"
#include "bitio.h"
#include "fatutils.h"

/////////////////////////////////////////////////////////////////////////////////
//! \brief Get the bit and set it to 0 or 1 according to the passing parameter.
//!
//! \param[in] wBitNumber Bit number in the buffer.
//! \param[in] pwBuffer Pointer to buffer.
//! \param[in] bufferWordCount Number of words long the buffer pointed to by pwBuffer is.
//! \param[in] bLogDevNumber Device number.
//! \param[in] TypeFileSystem FAT12, FAT16, or FAT32.
//! \param[in] Bittype The operation to perform. One of:
//!		- GET_BIT: Just return the bit value and don't modify the buffer.
//!		- SET_BIT: Set the bit in the buffer.
//!		- FREE_BIT: Clear the bit in the buffer.
//!
//! \return Bit value as it was before being set.
//!
//! \see FAT32_UpdateBit
/////////////////////////////////////////////////////////////////////////////////
uint32_t UpdateBit(uint32_t wBitNumber, uint32_t * pwBuffer, uint32_t bufferWordCount, uint8_t bLogDevNumber, FAT_TYPE TypeFileSystem, uint32_t Bittype)
{
    uint32_t wOffsetToWord;
	uint32_t woffset;
    uint8_t wOffsetBit;
    uint32_t sector;
    uint32_t temp;

    if (TypeFileSystem == FS_FAT32)
    {
	    sector = wBitNumber >> (g_checkdisk_context->cachedClusterEntryPerSectorShift);
		woffset = wBitNumber & g_checkdisk_context->cachedClusterEntryPerSectorMask;

	    wOffsetToWord = woffset >> BITS_SHIFT_FOR_UINT32;
	    wOffsetBit = woffset - (wOffsetToWord * BITS_PER_WORD);
	    sector = sector + g_checkdisk_context->stFat.FirstSecondaryFatSect;
        return FAT32_UpdateBit(bLogDevNumber, sector, wOffsetToWord, wOffsetBit, Bittype);
	}
	else
    {
		uint32_t bitMask;
		
		wOffsetToWord = wBitNumber / BITS_PER_WORD;
		wOffsetBit = wBitNumber - (wOffsetToWord * BITS_PER_WORD);
		bitMask = 1 << wOffsetBit;
        
        assert(wOffsetToWord < bufferWordCount);
        
		switch(Bittype)
		{
			case GET_BIT:
				return (pwBuffer[wOffsetToWord] & bitMask);
			case SET_BIT:
				temp = (pwBuffer[wOffsetToWord] & bitMask);
	            pwBuffer[wOffsetToWord] = pwBuffer[wOffsetToWord] | bitMask;
				return temp;
			case FREE_BIT:
				temp = 0xffffffff ^ bitMask;
                pwBuffer[wOffsetToWord] = pwBuffer[wOffsetToWord] & temp;
 				return 0;
 		}
	}
	
    return 0;
}

