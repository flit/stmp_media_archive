////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2000-2004
//
// Filename: Fattest.h
// Description:
////////////////////////////////////////////////////////////////////////////////
#include "types.h"

#ifndef _CHKDSK_FATUTILS_H
#define _CHKDSK_FATUTILS_H

bool LoadFatSector(uint32_t wSect);
bool WriteFatSector(uint32_t wSect);

bool IsLastCx(uint32_t wCluster);

int32_t GetNextCx(uint32_t wCurCx);
int32_t GetNextCxFat12(uint32_t wCurCx);
int32_t GetNextCxFat16(uint32_t wCurCx);
int32_t GetNextCxFat32(uint32_t wCurCx);

int32_t FetchCxFat12(uint32_t wCurCx);

int32_t GetLengthCxChain(uint32_t wCluster);

bool FreeCxFat(uint32_t wCluster);
bool FreeCxFat12(uint32_t wCluster);
bool FreeCxFat16(uint32_t wCluster);
bool FreeCxFat32(uint32_t wCluster);

uint32_t Getsectorno(uint32_t wCluster);

uint32_t SearchmatchingSector(int32_t sectorNumber, int MAXCACHES, tCACHEDESCR_checkdisk * CacheDesc_chkdsk);

int FAT32_UpdateBit(int DeviceNum, int32_t sectorNumber, uint32_t wOffsetToWord, uint32_t wOffsetBit, uint32_t Bittype);
 
void IncrementCacheCounters_chkdsk(void);

#endif


