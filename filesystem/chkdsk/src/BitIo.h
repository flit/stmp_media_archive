////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2000-2001
//
// Filename: BitIo.h
// Description: 
////////////////////////////////////////////////////////////////////////////////
#include "types.h"


#ifndef _BITIO_H
#define _BITIO_H


uint32_t UpdateBit(uint32_t wBitNumber, uint32_t *pwBuffer, uint32_t bufferWordCount, uint8_t bLogDevNumber, FAT_TYPE TypeFileSystem, uint32_t Bittype);

#endif 
