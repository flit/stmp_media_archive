////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal_spy
//! @{
// Copyright (c) 2004-2007 SigmaTel, Inc.
//
//! \file ddi_nand_hal_spy.h
//! \brief     Telemetry for NAND hardware accesses.
//! \version   version_number
//! \date      5/30/2007
//!
////////////////////////////////////////////////////////////////////////////////

#ifndef __DDI_NAND_HAL_SPY_H__
#define __DDI_NAND_HAL_SPY_H__

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \brief Type must be at least able to represent DDI_NAND_HAL_SPY_DEFAULT_READ_WARNING_THRESHOLD.
//! See ddi_nand_hal_spy.c.
typedef uint16_t    ddi_nand_hal_spy_ReadsPerPage_t;

//! \brief Type must be able to represent DDI_NAND_HAL_SPY_MAX_PAGES_PER_BLOCK.
//! See ddi_nand_hal_spy.c.
typedef uint8_t     ddi_nand_hal_spy_PageWriteIndexPerBlock_t;

//! \brief Type must be at least able to represent DDI_NAND_HAL_SPY_DEFAULT_ERASE_WARNING_THRESHOLD.
//! See ddi_nand_hal_spy.c.
typedef uint16_t    ddi_nand_hal_spy_ErasuresPerBlock_t;

//! \brief Type must be at least able to represent maximum reads and maximum erasures for debugging/logging purposes
//! See ddi_nand_hal_spy.c.
typedef struct {
    uint32_t index;
    uint16_t value;
} ddi_nand_hal_spy_GetMax_t;

//! \brief This is used to store NAND page read, write and block erase timings and is used for debugging purposes
//! See ddi_nand_hal_spy.c.
typedef struct {
    uint64_t u32_sumofIter;
    uint32_t u32_numofIter;
} ddi_nand_hal_spy_TimeAnalysis_t;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

#pragma weak ddi_nand_hal_spy_Init
#pragma weak ddi_nand_hal_spy_Reset
#pragma weak ddi_nand_hal_spy_DeInit
#pragma weak ddi_nand_hal_spy_bIsLinked

#if __cplusplus
extern "C" {
#endif // __cplusplus

//! \brief If the address of this variable is non-NULL, then the SPY code is linked in.
//! SPY code is weakly-linked.  Test for the presence of the SPY code like this:
//!  if ( NULL != &ddi_nand_hal_spy_bIsLinked )
extern bool ddi_nand_hal_spy_bIsLinked;

//! \brief The following defines are used for profiling purposes
#ifdef CMP_PROFILE_ENABLE
#define NAND_HAL_SPY_PROFILE_WRITE_FAILURE  0x3
#define NAND_HAL_SPY_PROFILE_ERASE_FAILURE  0x4
#define NAND_HAL_SPY_PROFILE_READ_FAILURE   0x5
#endif


////////////////////////////////////////////////////////////////////////////////
//! \brief      Initialization function for NAND HAL SPY.
//!
//! \fntype     Function
//!
//! Call this function to start NAND telemetry.  Actions:
//!     - Uses an initialized NandPhysicalMedia * to learn
//!       the sizes of the NANDs.
//!     - Inserts SPY telemetry functions in place of the HAL API
//!       for these NANDs.  (All NANDs in the system are assumed to be of the
//!       same size and type, so the same HAL API applies to all NANDs.)
//!       For each API call, SPY counts the NAND accesses and then invokes
//!       the original NAND HAL API function.
//!     - Clears all SPY buffers used to count reads, writes, and erasues.
//! 
//! SPY has been activated when this function returns.  Spy prints
//! warnings to tss_logtext_printf() when any of the following occurs:
//!     - Any NAND page is read more times than nReadWarningThreshold.
//!     - Any NAND block is erased more than nEraseWarningThreshold.
//!     - Any NAND block is written using an out-of-order page sequence.
//! 
//! Memory usage:
//! Memory to hold counters is statically-allocated in ddi_nand_hal_spy,
//! and is located in SDRAM.  The size of this memory is set by macros
//! in ddi_nand_hal_spy.c, and is nominally a little over 2Mbytes.
//!
//! \param[in]  pNANDDescriptor             An initialized NAND descriptor.
//! \param[in]  nReadWarningThreshold       Allowable quantity of reads per NAND page.
//!                                         Use 0 to cause SPY to use its internal default
//!                                         DDI_NAND_HAL_SPY_DEFAULT_READ_WARNING_THRESHOLD.
//! \param[out] nEraseWarningThreshold      Allowable quantity of erasures per NAND page.
//!                                         Use 0 to cause SPY to use its internal default
//!                                         DDI_NAND_HAL_SPY_DEFAULT_ERASE_WARNING_THRESHOLD.
//!
//! \retval SUCCESS                         If SPY has been activated.
//! \retval ERROR_DDI_NAND_GROUP_GENERAL    If the media descriptor or some referenced descriptor was NULL.
//!
//! \pre    pNANDDescriptor must have been initialized.
////////////////////////////////////////////////////////////////////////////////
extern RtStatus_t ddi_nand_hal_spy_Init( NandPhysicalMedia * pNANDDescriptor,
    ddi_nand_hal_spy_ReadsPerPage_t     nReadWarningThreshold,
    ddi_nand_hal_spy_ErasuresPerBlock_t nEraseWarningThreshold );

////////////////////////////////////////////////////////////////////////////////
//! \brief      Clear all counters in ddi_nand_hal_spy
//!
//! \fntype     Function
//!
//! Clear all counters in ddi_nand_hal_spy.
//!
//! \retval SUCCESS             All the time.
//!
////////////////////////////////////////////////////////////////////////////////
extern RtStatus_t ddi_nand_hal_spy_Reset(void);

////////////////////////////////////////////////////////////////////////////////
//! \brief      Stop using NAND HAL SPY.
//!
//! \fntype     Function
//!
//! Call this function to quit using NAND HAL SPY.  This function
//! stops SPY from counting NAND uses.  It restores the original
//! NAND HAL API defined for the NANDs before ddi_nand_hal_spy_Init()
//! was called.
//!
//! No memory is freed, because it is statically allocated in SPY.
//!
//! \retval SUCCESS                         If no error has occurred
//! \retval ERROR_DDI_NAND_GROUP_GENERAL    If SPY was not initialized in the first place.
//!
////////////////////////////////////////////////////////////////////////////////
extern RtStatus_t ddi_nand_hal_spy_DeInit(void);

////////////////////////////////////////////////////////////////////////////////
//! \brief      Get Max Reads and corresponding page indices.
//!
//! \fntype     Function
//!
//! To get the maximum reads and the corresponding page index of first nElements and
//! stores them in pBuffer passed by the user
//!
//! \param[in]      nElements   Number of elements in the array allocated by the user
//! \param[in,out]  pBuffer     Pointer to an array of structure to store the maximum page reads and the
//!                             corresponding page index. This array must be allocated to hold nElements.
//! \param[in,out]  pu32Pages   Pointer to an unsigned integer to receive the count of the total quantity of pages
//!                             in the desired drive type in the NAND(s).
//! \param[in,out]  pu64TotalReads   Pointer to an unsigned integer to receive the count of
//!                             all reads performed on all pages in desired drive type in the NAND(s).
//! \param[in]      bDriveTypeSystem   TRUE: Scan only system-drive pages.
//!                             FALSE: Scan only data-drive (incl. hidden data-drive) pages.
//!
//! \retval SUCCESS If the function finds at least one maximum.  ERROR_GENERIC otherwise.
//!
//! \pre    Size of the buffer passed by the user is at least large enough to hold the number of
//! elements requested by nElements.
////////////////////////////////////////////////////////////////////////////////
extern RtStatus_t ddi_nand_hal_spy_GetMaxReads(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Pages, uint64_t *pu64TotalReads, bool bDriveTypeSystem);

////////////////////////////////////////////////////////////////////////////////
//! \brief      Get Max Erasures and corresponding block indices.
//!
//! \fntype     Function
//!
//! To get the maximum erasures and the corresponding block index of first nElements and
//! stores them in pBuffer passed by the user
//!
//! \param[in]      nElements   Number of elements in the array allocated by the user
//! \param[in,out]  pBuffer     Pointer to an array of structure to store the maximum block erasures and the
//!                             corresponding block index. This array must be allocated to hold nElements.
//! \param[in,out]  pu32Pages   Pointer to an unsigned integer to receive the count of the total quantity of blocks
//!                             in the desired drive type in the NAND(s).
//! \param[in,out]  pu64TotalReads   Pointer to an unsigned integer to receive the count of
//!                             all erasures performed on all blocks in desired drive type in the NAND(s).
//! \param[in]      bDriveTypeSystem   TRUE: Scan only system-drive blocks.
//!                             FALSE: Scan only data-drive (incl. hidden data-drive) blocks.
//!
//! \retval SUCCESS If the function finds at least one maximum.  ERROR_GENERIC otherwise.
//!
//! \pre    Size of the buffer passed by the user is at least large enough to hold the number of
//! elements requested by nElements.
////////////////////////////////////////////////////////////////////////////////
extern RtStatus_t ddi_nand_hal_spy_GetMaxErasures(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Blocks, uint64_t *pu64TotalErasures, bool bDriveTypeSystem);

#if __cplusplus
}
#endif // __cplusplus


#endif /* __DDI_NAND_HAL_SPY_H__ */
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

