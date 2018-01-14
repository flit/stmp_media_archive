////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal_spy
//! @{
// Copyright (c) 2007 SigmaTel, Inc.
//
//! \file      ddi_nand_hal_spy.c
//! \brief     Telemetry for NAND hardware accesses.
//! \version   version_number
//! \date      5/30/2007
//!
//! NAND HAL SPY collects usage information for NAND hardware.  Specifically,
//! it counts writes and reads to/from the NAND.  A big chunk of memory
//! is needed to record these counts, so this is intended as a diagnostic tool
//! and not a usual component of a deliverable application.
//!
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
//#include <error.h>
#include <string.h>
#include "components/tss_logtext.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/ddi_media_errordefs.h"
#include "components/profile/cmp_profile.h"   // profiling
#include "components/profile/src/cmp_profile_local.h"   // profiling
#include "drivers/ddi_subgroups.h"
#include "ddi_nand_hal_spy.h"
#include "drivers/media/nand/hal/src/ddi_nand_hal_internal.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "drivers/media/nand/ddi/common/ddi_nand_ddi.h"
#include "drivers/media/nand/ddi/media/ddi_nand_media.h"
#include <algorithm>

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#define DDI_NAND_HAL_SPY_MAX_NANDS                      4
#define DDI_NAND_HAL_SPY_MAX_PAGES                      (1 << 20)
#define DDI_NAND_HAL_SPY_MAX_PAGES_PER_BLOCK            128
#define DDI_NAND_HAL_SPY_MAX_BLOCKS                     (DDI_NAND_HAL_SPY_MAX_PAGES/DDI_NAND_HAL_SPY_MAX_PAGES_PER_BLOCK)

//! \brief Default quantity of reads per page allowed.
//! Beyond this quantity, spy prints warnings.
#define DDI_NAND_HAL_SPY_DEFAULT_READ_WARNING_THRESHOLD    50000

//! \brief Default quantity of writes per block allowed.
//! Beyond this quantity, spy prints warnings.
#define DDI_NAND_HAL_SPY_DEFAULT_ERASE_WARNING_THRESHOLD   5000

//! \brief Length of array which is used to store NAND Page or Block index to help analyze
//! NAND accesses with the debugger.
#define DDI_NAND_HAL_SPY_NAND_ANALYSIS_INDEX    10

#if __cplusplus
extern "C" {
#endif // __cplusplus
////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

 /* *******************************************************************
        Physical dimensions
    ******************************************************************* */
    
static uint32_t     stc_MaxPages        = 0;

static uint32_t     stc_MaxBlocks       = 0;

static uint32_t     stc_MaxQuantityChips   = 0;

static uint32_t     stc_PageOriginPerNand[ DDI_NAND_HAL_SPY_MAX_NANDS ];
static uint32_t     stc_BlockOriginPerNand[ DDI_NAND_HAL_SPY_MAX_NANDS ];

 /* *******************************************************************
        Thresholds
    ******************************************************************* */
    
//! \brief Quantity of reads per page allowed.
uint32_t    n_ddi_nand_hal_spy_ReadWarningThreshold = DDI_NAND_HAL_SPY_DEFAULT_READ_WARNING_THRESHOLD;

//! \brief Quantity of writes per block allowed.
uint32_t    n_ddi_nand_hal_spy_EraseWarningThreshold = DDI_NAND_HAL_SPY_DEFAULT_ERASE_WARNING_THRESHOLD;

 /* *******************************************************************
        Overridden descriptors and functions
    ******************************************************************* */
    
//! \brief used to restore the NAND HAL API when spy is finished.
static NandPhysicalMedia *     stc_pNANDDescriptor;

 /* *******************************************************************
        Initialization flags
    ******************************************************************* */
    
//! \brief TRUE indicates that spy has overridden the HAL API.
static bool                                 stc_bAPIOverridden = FALSE;

//! \brief TRUE indicates that spy is initialized.
static bool                                 stc_bInitialized = FALSE;

//! \brief TRUE indicates that spy debug is needed to analyse NAND accesses (reads/erasures)
static bool                                 stc_bSpyNandAnalysis = FALSE;

//! \brief NAND Page or Block index to be analysed. This array is filled using debugger to analyse
//! NAND accesses
uint32_t i_ddi_nand_hal_spy_NandAnalysis[DDI_NAND_HAL_SPY_NAND_ANALYSIS_INDEX];

bool                                        ddi_nand_hal_spy_bIsLinked = TRUE;

 /* *******************************************************************
        The following are big chunks of memory, destined for SDRAM.
    ******************************************************************* */
//! \brief Count of the times each page has been read.
ddi_nand_hal_spy_ReadsPerPage_t             n_ddi_nand_hal_spy_ReadsPerPage[DDI_NAND_HAL_SPY_MAX_PAGES];

//! \brief Index of the last page written in each block.
ddi_nand_hal_spy_PageWriteIndexPerBlock_t   n_ddi_nand_hal_spy_PageWriteIndexPerBlock[DDI_NAND_HAL_SPY_MAX_BLOCKS];

//! \brief Count of the times each block has been erased.
ddi_nand_hal_spy_ErasuresPerBlock_t         n_ddi_nand_hal_spy_ErasuresPerBlock[DDI_NAND_HAL_SPY_MAX_BLOCKS];
ddi_nand_hal_spy_ErasuresPerBlock_t         n_ddi_nand_hal_spy_ErasuresPerBlockMaxQty;
uint32_t                                    i_ddi_nand_hal_spy_ErasuresPerBlockMax;

ddi_nand_hal_spy_TimeAnalysis_t ddi_nand_hal_spy_readTime, ddi_nand_hal_spy_writeTime, ddi_nand_hal_spy_eraseTime;

#ifdef CMP_PROFILE_ENABLE
//! \brief Profiler function
extern uint32_t cmp_profile_capture(uint32_t ID, uint32_t Data, uint32_t StartStop);

//! brief Profiler log buffer is allocated by app framework using EnableProfiling call. But, this call is called after 
//! media initialization during which read, write or erase event can occur. So, before SPY code starts to capture
//! event, it has to check to see if buffer is already been allocated
extern cmp_profile_log_t *stc_cmp_profile_log_buffer;
#endif

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////


#define DDI_NAND_HAL_SPY_IS_SAME_DRIVE_TYPE( bDriveTypeSystem, eDriveType )\
               (    (   bDriveTypeSystem   && ( eDriveType==kDriveTypeSystem) )\
                    ||                                                        \
                    (   !bDriveTypeSystem  && ( eDriveType==kDriveTypeData  || eDriveType==kDriveTypeHidden ) )\
               )
    
RtStatus_t ddi_nand_hal_spy_Init( NandPhysicalMedia * pNANDDescriptor,
    ddi_nand_hal_spy_ReadsPerPage_t     nReadWarningThreshold,
    ddi_nand_hal_spy_ErasuresPerBlock_t nEraseWarningThreshold );
RtStatus_t ddi_nand_hal_spy_RestoreAPI( NandPhysicalMedia * pNANDDescriptor );
static RtStatus_t ddi_nand_hal_spy_DeInitPrivate(void);
RtStatus_t ddi_nand_hal_spy_CountBlockErase( NandPhysicalMedia * pNANDDescriptor, uint32_t wBlockNum );
RtStatus_t ddi_nand_hal_spy_CountPageRead( NandPhysicalMedia * pNANDDescriptor, 
    uint32_t wSectorNum );
RtStatus_t ddi_nand_hal_spy_CountPageWrite( NandPhysicalMedia * pNANDDescriptor, uint32_t wSectorNum );
RtStatus_t ddi_nand_hal_spy_GetMaxReads(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Pages, uint64_t *pu64TotalReads, bool bDriveTypeSystem);
RtStatus_t ddi_nand_hal_spy_GetMaxErasures(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Blocks, uint64_t *pu64TotalErasures, bool bDriveTypeSystem);
static bool ddi_nand_hal_spy_isIndexinArr(uint32_t iPage_or_iBlock);

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Class to give the spy access to HAL calls.
 *
 * An instance of this class replaces the NandPhysicalMedia subclass for a chip enable.
 * When one of the methods is called, the spy gets a chance to do its work, then it
 * passes the call to the original NandPhysicalMedia subclass instance.
 */
class NandHalSpyInterposer : public NandPhysicalMedia
{
protected:
    //! Original instance being overridden.
    NandPhysicalMedia * m_original;
    
public:
    //! \brief Initialization.
    void init(NandPhysicalMedia * original);
    
    //! \brief Override the original nand object instance.
    void override();
    
    //! \brief Restore the original nand object.
    void restore();

    virtual RtStatus_t reset();
    
    virtual RtStatus_t readID(uint8_t * pReadIDDecode);

    virtual RtStatus_t readRawData(uint32_t wSectorNum, uint32_t columnOffset, uint32_t readByteCount, SECTOR_BUFFER * pBuf);
    
    virtual RtStatus_t readPage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t readMetadata(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t readPageWithEcc(const NandEccDescriptor_t * ecc, uint32_t pageNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);
    
    virtual RtStatus_t writeRawData(uint32_t pageNumber, uint32_t columnOffset, uint32_t writeByteCount, const SECTOR_BUFFER * data);

    virtual RtStatus_t writePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary);

    virtual RtStatus_t writeFirmwarePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary);
    
    virtual RtStatus_t readFirmwarePage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC);

    virtual RtStatus_t eraseBlock(uint32_t uBlockNumber);

//     virtual RtStatus_t eraseMultipleBlocks(uint32_t startBlockNumber, uint32_t requestedBlockCount, uint32_t * actualBlockCount);

    virtual RtStatus_t copyPages(NandPhysicalMedia * targetNand, uint32_t uSourceStartSectorNum, uint32_t uTargetStartSectorNum, uint32_t wNumSectors, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, NandCopyPagesFilter * filter, uint32_t * successfulCopies);

    virtual bool isBlockBad(uint32_t blockAddress, SECTOR_BUFFER * auxBuffer, bool checkFactoryMarkings=false, RtStatus_t * readStatus=NULL);
    
    virtual RtStatus_t markBlockBad(uint32_t blockAddress, SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer);

    virtual RtStatus_t enableSleep(bool isEnabled);
    
    virtual bool isSleepEnabled();

    virtual char * getDeviceName();

    virtual RtStatus_t readMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t readMultipleMetadata(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t writeMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount);
    virtual RtStatus_t eraseMultipleBlocks(MultiplaneParamBlock * blocks, unsigned blockCount);

};

void NandHalSpyInterposer::init(NandPhysicalMedia * original)
{
    // Save pointer to the instance we're overriding.
    m_original = original;
    
    // Fill in our fields from the original.
    pNANDParams = original->pNANDParams;
    wChipNumber = original->wChipNumber;
    totalPages = original->totalPages;
    wTotalBlocks = original->wTotalBlocks;
    wTotalInternalDice = original->wTotalInternalDice;
    wBlocksPerDie = original->wBlocksPerDie;
}

void NandHalSpyInterposer::override()
{
    g_nandHalContext.nands[wChipNumber] = this;
}

void NandHalSpyInterposer::restore()
{
    g_nandHalContext.nands[wChipNumber] = m_original;
}

RtStatus_t NandHalSpyInterposer::reset()
{
    return m_original->reset();
}

RtStatus_t NandHalSpyInterposer::readID(uint8_t * pReadIDDecode)
{
    return m_original->readID(pReadIDDecode);
}

RtStatus_t NandHalSpyInterposer::readRawData(uint32_t wSectorNum, uint32_t columnOffset, uint32_t readByteCount, SECTOR_BUFFER * pBuf)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a read even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageRead( m_original, wSectorNum );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->readRawData(wSectorNum, columnOffset, readByteCount, pBuf);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_readTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_readTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if (    ( RetVal != SUCCESS ) &&
            ( RetVal != ERROR_DDI_NAND_HAL_ECC_FIXED ) && // This error simply indicates that there were correctable bit errors.
            ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + wSectorNum;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_READ_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Read Sector Failed, offset=%d, error=x%x\r\n",
                            eventID, RetVal );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::readPage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a read even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageRead( m_original, uSectorNumber );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->readPage(uSectorNumber, pBuffer, pAuxiliary, pECC);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_readTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_readTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if (    ( RetVal != SUCCESS ) &&
            ( RetVal != ERROR_DDI_NAND_HAL_ECC_FIXED ) && // This error simply indicates that there were correctable bit errors.
            ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + uSectorNumber;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_READ_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Read Sector Failed, offset=%d, error=x%x\r\n",
                            eventID, RetVal );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::readMetadata(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, NandEccCorrectionInfo_t * pECC)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a read even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageRead( m_original, uSectorNumber );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->readMetadata(uSectorNumber, pBuffer, pECC);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_readTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_readTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if (( RetVal != SUCCESS ) &&
        ( RetVal != ERROR_DDI_NAND_HAL_ECC_FIXED ) && // This error simply indicates that there were correctable bit errors.
        ( RetVal != ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR ) && // This error simply indicates that there were correctable bit errors.
        ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + uSectorNumber;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_READ_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Read Sector Failed, offset=%d, error=x%x\r\n",
                            eventID, RetVal );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::readPageWithEcc(const NandEccDescriptor_t * ecc, uint32_t pageNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a read even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageRead( m_original, pageNumber );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->readPageWithEcc(ecc, pageNumber, pBuffer, pAuxiliary, pECC);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_readTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_readTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if (    ( RetVal != SUCCESS ) &&
            ( RetVal != ERROR_DDI_NAND_HAL_ECC_FIXED ) && // This error simply indicates that there were correctable bit errors.
            ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + pageNumber;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_READ_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Read Sector Failed, offset=%d, error=x%x\r\n",
                            eventID, RetVal );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::writeRawData(uint32_t pageNumber, uint32_t columnOffset, uint32_t writeByteCount, const SECTOR_BUFFER * data)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a write even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageWrite( m_original, pageNumber );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->writeRawData(pageNumber, columnOffset, writeByteCount, data);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_writeTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_writeTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if ( ( RetVal != SUCCESS ) && ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + pageNumber;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_WRITE_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Write Sector Failed = %d\r\n", eventID );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::writePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a write even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageWrite( m_original, uSectorNum );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->writePage(uSectorNum, pBuffer, pAuxiliary);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_writeTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_writeTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if ( ( RetVal != SUCCESS ) && ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + uSectorNum;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_WRITE_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Write Sector Failed = %d\r\n", eventID );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::writeFirmwarePage(uint32_t uSectorNum, const SECTOR_BUFFER * pBuffer, const SECTOR_BUFFER * pAuxiliary)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a write even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageWrite( m_original, uSectorNum );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->writeFirmwarePage(uSectorNum, pBuffer, pAuxiliary);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_writeTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_writeTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if ( ( RetVal != SUCCESS ) && ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + uSectorNum;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_WRITE_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Write Sector Failed = %d\r\n", eventID );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::readFirmwarePage(uint32_t uSectorNumber, SECTOR_BUFFER * pBuffer, SECTOR_BUFFER * pAuxiliary, NandEccCorrectionInfo_t * pECC)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count a read even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountPageRead( m_original, uSectorNumber );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->readFirmwarePage(uSectorNumber, pBuffer, pAuxiliary, pECC);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_readTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_readTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if (    ( RetVal != SUCCESS ) &&
            ( RetVal != ERROR_DDI_NAND_HAL_ECC_FIXED ) && // This error simply indicates that there were correctable bit errors.
            ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = stc_PageOriginPerNand[wChipNumber] + uSectorNumber;
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_READ_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Read Sector Failed, offset=%d, error=x%x\r\n",
                            eventID, RetVal );
    }
#endif

    return RetVal;
}

RtStatus_t NandHalSpyInterposer::eraseBlock(uint32_t uBlockNumber)
{
    RtStatus_t RetVal;
    uint64_t startTime, stopTime;
#ifdef CMP_PROFILE_ENABLE
    uint32_t eventID;
#endif
 /* *******************************************************************
        Gather statistics.
        We count an erase even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    ddi_nand_hal_spy_CountBlockErase( m_original, uBlockNumber );

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    startTime = hw_profile_GetMicroseconds();
    
    RetVal = m_original->eraseBlock(uBlockNumber);
    
    stopTime = hw_profile_GetMicroseconds();
    ddi_nand_hal_spy_eraseTime.u32_sumofIter += ( stopTime - startTime );
    ddi_nand_hal_spy_eraseTime.u32_numofIter++;

#ifdef CMP_PROFILE_ENABLE
    if ( ( RetVal != SUCCESS ) && ( stc_cmp_profile_log_buffer != NULL ) ) 
    {
        // Capture the event
        eventID = uBlockNumber + stc_BlockOriginPerNand[wChipNumber];
        cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_ERASE_FAILURE );
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Block Erase Failed = %d\r\n", eventID );
    }
#endif

    return RetVal;
}

// RtStatus_t NandHalSpyInterposer::eraseMultipleBlocks(uint32_t startBlockNumber, uint32_t requestedBlockCount, uint32_t * actualBlockCount)
// {
//     uint64_t startTime, stopTime;
// #ifdef CMP_PROFILE_ENABLE
//     uint32_t eventID;
// #endif
// 
// /* *******************************************************************
//         Do the operaton.
//     ******************************************************************* */
//     startTime = hw_profile_GetMicroseconds();
//     
//     uint32_t localActualCount;
//     RtStatus_t RetVal = m_original->eraseMultipleBlocks(startBlockNumber, requestedBlockCount, &localActualCount);
//     
//     if (actualBlockCount)
//     {
//         *actualBlockCount = localActualCount;
//     }
// 
//     stopTime = hw_profile_GetMicroseconds();
//     ddi_nand_hal_spy_eraseTime.u32_sumofIter += ( stopTime - startTime );
//     ddi_nand_hal_spy_eraseTime.u32_numofIter++;
//     
//  /* *******************************************************************
//         Gather statistics.
//         We count an erase even without checking the status of the
//         NAND operation, because even an attempt may imply stress
//         on the NAND.
//     ******************************************************************* */
//     unsigned i;
//     for (i = 0; i < localActualCount; ++i)
//     {
//         ddi_nand_hal_spy_CountBlockErase( m_original, startBlockNumber + i );
//     }
// 
// #ifdef CMP_PROFILE_ENABLE
//     if ( ( RetVal != SUCCESS ) && ( stc_cmp_profile_log_buffer != NULL ) ) 
//     {
//         // Capture the event
//         eventID = startBlockNumber + 1 + stc_BlockOriginPerNand[wChipNumber];
//         cmp_profile_capture(DDI_NAND_GROUP, eventID, NAND_HAL_SPY_PROFILE_ERASE_FAILURE );
//         tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY Multi Block Erase Failed = %d\n", eventID );
//     }
// #endif
// 
//     return RetVal;
// }

RtStatus_t NandHalSpyInterposer::copyPages(NandPhysicalMedia * targetNand, uint32_t uSourceStartSectorNum, uint32_t uTargetStartSectorNum, uint32_t wNumSectors, SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer, NandCopyPagesFilter * filter, uint32_t * successfulCopies)
{
    uint32_t iSector;

 /* *******************************************************************
        Gather statistics.
        We count a read and write even without checking the status of the
        NAND operation, because even an attempt may imply stress
        on the NAND.
    ******************************************************************* */
    for ( iSector=0 ; iSector<wNumSectors ; iSector++)
    {
        ddi_nand_hal_spy_CountPageRead  ( m_original, uSourceStartSectorNum+iSector );
        ddi_nand_hal_spy_CountPageWrite ( targetNand, uTargetStartSectorNum+iSector );
    }

 /* *******************************************************************
        Do the operaton.
    ******************************************************************* */
    return m_original->copyPages(targetNand, uSourceStartSectorNum, uTargetStartSectorNum, wNumSectors, sectorBuffer, auxBuffer, filter, successfulCopies);
}

bool NandHalSpyInterposer::isBlockBad(uint32_t blockAddress, SECTOR_BUFFER * auxBuffer, bool checkFactoryMarkings, RtStatus_t * readStatus)
{
    return m_original->isBlockBad(blockAddress, auxBuffer, checkFactoryMarkings, readStatus);
}

RtStatus_t NandHalSpyInterposer::markBlockBad(uint32_t blockAddress, SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer)
{
    return m_original->markBlockBad(blockAddress, pageBuffer, auxBuffer);
}

RtStatus_t NandHalSpyInterposer::enableSleep(bool isEnabled)
{
    return m_original->enableSleep(isEnabled);
}

bool NandHalSpyInterposer::isSleepEnabled()
{
    return m_original->isSleepEnabled();
}

char * NandHalSpyInterposer::getDeviceName()
{
    return m_original->getDeviceName();
}

RtStatus_t NandHalSpyInterposer::readMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount)
{
    return m_original->readMultiplePages(pages, pageCount);
}

RtStatus_t NandHalSpyInterposer::readMultipleMetadata(MultiplaneParamBlock * pages, unsigned pageCount)
{
    return m_original->readMultipleMetadata(pages, pageCount);
}

RtStatus_t NandHalSpyInterposer::writeMultiplePages(MultiplaneParamBlock * pages, unsigned pageCount)
{
    return m_original->writeMultiplePages(pages, pageCount);
}

RtStatus_t NandHalSpyInterposer::eraseMultipleBlocks(MultiplaneParamBlock * blocks, unsigned blockCount)
{
    return m_original->eraseMultipleBlocks(blocks, blockCount);
}


RtStatus_t ddi_nand_hal_spy_Init( NandPhysicalMedia * pNANDDescriptor,
    ddi_nand_hal_spy_ReadsPerPage_t     nReadWarningThreshold,
    ddi_nand_hal_spy_ErasuresPerBlock_t nEraseWarningThreshold )
{
    RtStatus_t  RetVal;
    uint32_t    iPage   = 0;
    uint32_t    iBlock  = 0;
    int         iNand   = 0;

    //  This init function gets called once for each chip-enable.
    //  The total quantity of blocks (and by implication, pages) that
    //  nand_hal_spy tracks is the lesser of the size of the
    //  spy memory, or the size of all NANDs present.

    // Remember the highest chip number we've seen.
    stc_MaxQuantityChips = 1 + std::max<uint32_t>(stc_MaxQuantityChips, pNANDDescriptor->wChipNumber);

    // Compute the quantities of pages and blocks of a system with that quantity of chips...
    stc_MaxPages    = pNANDDescriptor->totalPages * stc_MaxQuantityChips;
    stc_MaxBlocks   = pNANDDescriptor->wTotalBlocks  * stc_MaxQuantityChips;

    // ...but nand_hal_spy is limited by the quantities of pages and blocks
    // that it can track in its memory.
    stc_MaxPages    = std::min<uint32_t>( stc_MaxPages, DDI_NAND_HAL_SPY_MAX_PAGES );
    stc_MaxBlocks   = std::min<uint32_t>( stc_MaxBlocks, DDI_NAND_HAL_SPY_MAX_BLOCKS );

    // If already initialized, do nothing else.
    if (stc_bInitialized) return (SUCCESS);

    if ( NULL == pNANDDescriptor )              return (ERROR_DDI_NAND_GROUP_GENERAL);
    if ( NULL == pNANDDescriptor->pNANDParams ) return (ERROR_DDI_NAND_GROUP_GENERAL);

 /* *******************************************************************
        Modify the warning thresholds, if they were given as parameters.
    ******************************************************************* */

    if ( 0 != nReadWarningThreshold )
        n_ddi_nand_hal_spy_ReadWarningThreshold     = nReadWarningThreshold;

    if ( 0 != nEraseWarningThreshold )
        n_ddi_nand_hal_spy_EraseWarningThreshold    = nEraseWarningThreshold;

    
 /* *******************************************************************
        Compute values for stc_PageOriginPerNand for all NANDs.
        stc_PageOriginPerNand[] are offsets into the
        n_ddi_nand_hal_spy_ReadsPerPage[] counter array for each
        NAND, used to quickly access the counter array upon page
        reads.
    ******************************************************************* */
    for ( iNand=0 ; iNand<DDI_NAND_HAL_SPY_MAX_NANDS ; iNand++ )
    {
        if ( iPage < DDI_NAND_HAL_SPY_MAX_PAGES )
        {            
            // iPage fits within the range of SPY memory.
            // Memorize its origin.
            stc_PageOriginPerNand[iNand] = iPage;
            iPage += pNANDDescriptor->totalPages;
        }
        else
        {
            // iPage does not fit within the range of SPY memory.
            // Set the origin beyond the legal range as a flag.
            stc_PageOriginPerNand[iNand] = iPage;
        }
    }

 /* *******************************************************************
        Compute values for stc_BlockOriginPerNand for all NANDs.
        stc_BlockOriginPerNand[] are offsets into the
        n_ddi_nand_hal_spy_PageWriteIndexPerBlock[] and
        n_ddi_nand_hal_spy_ErasuresPerBlock counter arrays for each
        NAND, used to quickly access the counter arrays upon block
        operations.
    ******************************************************************* */
    for ( iNand=0 ; iNand<DDI_NAND_HAL_SPY_MAX_NANDS ; iNand++ )
    {
        if ( iBlock < DDI_NAND_HAL_SPY_MAX_BLOCKS )
        {            
            // iBlock fits within the range of SPY memory.
            // Memorize its origin.
            stc_BlockOriginPerNand[iNand] = iBlock;
            iBlock += pNANDDescriptor->wTotalBlocks;
        }
        else
        {
            // iBlock does not fit within the range of SPY memory.
            // Set the origin beyond the legal range as a flag.
            stc_BlockOriginPerNand[iNand] = iBlock;
        }
    }



 /* *******************************************************************
        Override the HAL API functions for this NAND with our own
        spy functions.
    ******************************************************************* */
    
    NandHalSpyInterposer * interposer = new NandHalSpyInterposer;
    interposer->init(pNANDDescriptor);
    interposer->override();
    stc_bAPIOverridden = TRUE;
    stc_pNANDDescriptor = interposer;

 /* *******************************************************************
        Clear the counters for all NAND blocks and pages.
    ******************************************************************* */
    RetVal = ddi_nand_hal_spy_Reset();

    if (SUCCESS == RetVal)
    {
        // Init successful.
        stc_bInitialized = TRUE;
    }
    else
    {
        // Init failed.  Clean up.
        ddi_nand_hal_spy_DeInitPrivate();
    }

    return (RetVal);
}


/**************************************************************************
 * Function:    ddi_nand_hal_spy_RestoreAPI
 *
 * Description: Reverses the actions of ddi_nand_hal_spy_OverrideAPI().
 *
 * Input:       
 *
 * Output:      
 *
 * Return:      
 *
 * See Also:    
 *************************************************************************/
RtStatus_t ddi_nand_hal_spy_RestoreAPI( NandPhysicalMedia * pNANDDescriptor )
{
    if ( !stc_bAPIOverridden ) return ( SUCCESS );

    if ( NULL == pNANDDescriptor ) return ( ERROR_DDI_NAND_GROUP_GENERAL );

 /* *******************************************************************
        Restore the pointer used for the NAND HAL API.
    ******************************************************************* */
    ((NandHalSpyInterposer *)pNANDDescriptor)->restore();
    delete pNANDDescriptor;

    stc_bAPIOverridden = FALSE;

    return ( SUCCESS );
}


/**************************************************************************
 * Function:    ddi_nand_hal_spy_Reset
 *
 * Description: Erases the SPY counters.
 *
 * Input:       
 *
 * Output:      
 *
 * Return:      
 *
 * See Also:    
 *************************************************************************/
RtStatus_t ddi_nand_hal_spy_Reset(void)
{
    memset(n_ddi_nand_hal_spy_ReadsPerPage, 0, sizeof(n_ddi_nand_hal_spy_ReadsPerPage) );

    memset(n_ddi_nand_hal_spy_PageWriteIndexPerBlock, 0, sizeof(n_ddi_nand_hal_spy_PageWriteIndexPerBlock) );

    memset(n_ddi_nand_hal_spy_ErasuresPerBlock, 0, sizeof(n_ddi_nand_hal_spy_ErasuresPerBlock) );
    n_ddi_nand_hal_spy_ErasuresPerBlockMaxQty   = 0;
    i_ddi_nand_hal_spy_ErasuresPerBlockMax      = DDI_NAND_HAL_SPY_MAX_BLOCKS;

    return (SUCCESS);
}


static RtStatus_t ddi_nand_hal_spy_DeInitPrivate(void)
{
    if ( NULL == stc_pNANDDescriptor )
    {
        return ( ERROR_DDI_NAND_GROUP_GENERAL );
    }

    stc_MaxPages        =
    stc_MaxBlocks       =
    stc_MaxQuantityChips   = 0;

 /* *******************************************************************
        Restore the HAL API functions for this NAND, if necessary.
    ******************************************************************* */
    return ( ddi_nand_hal_spy_RestoreAPI( stc_pNANDDescriptor ) );
}


/**************************************************************************
 * Function:    ddi_nand_hal_spy_DeInit
 *
 * Description: Reverses the actions of ddi_nand_hal_spy_Init().
 *
 * Input:       
 *
 * Output:      
 *
 * Return:      
 *
 * See Also:    
 *************************************************************************/
RtStatus_t ddi_nand_hal_spy_DeInit(void)
{
    // If we're not initialized, then do nothing.
    if ( !stc_bInitialized ) return (SUCCESS);

    return ( ddi_nand_hal_spy_DeInitPrivate( ) );
}

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
RtStatus_t ddi_nand_hal_spy_GetMaxReads(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Pages, uint64_t *pu64TotalReads, bool bDriveTypeSystem)
{
    int         iPage;
    int         iBufferCurrIndex =0;
    int maxValue = -1, newmaxValue = -1;
    uint64_t    uSumOfAllOperations = 0;
    nand::Region  *pRegion_Info;
    uint32_t    u32PhysicalBlockNum;
    uint32_t    iSectorOffsetWithinBlock;

    // Find the maximum value and its index
    for ( iPage = 0; iPage < stc_MaxPages; iPage++ )
    {
        stc_pNANDDescriptor->pageToBlockAndOffset(iPage, &u32PhysicalBlockNum, &iSectorOffsetWithinBlock);
//        if ( SUCCESS != ddi_nand_media_regions_GetRegionFromPhyBlk( &u32PhysicalBlockNum, &pRegion_Info, false /* bOnlyDataDriveRegions */ ) )
//        {
//            continue;
//        }
        pRegion_Info = g_nandMedia->getRegionForBlock(u32PhysicalBlockNum);

        if ( DDI_NAND_HAL_SPY_IS_SAME_DRIVE_TYPE( bDriveTypeSystem, pRegion_Info->m_eDriveType) )
        {
            if ( (n_ddi_nand_hal_spy_ReadsPerPage[iPage] > maxValue) )
            {
                maxValue = n_ddi_nand_hal_spy_ReadsPerPage[iPage];
            }
            uSumOfAllOperations += n_ddi_nand_hal_spy_ReadsPerPage[iPage];
        }
    }

    if ( -1 == maxValue )
    {
        return (ERROR_GENERIC);
    }

    // Find nElements-1 other maxima.
    // Note that this is a multi-pass process, since there could be
    // several instances of each maximum value, and we only want to
    // report a total of nElements-1 of them, by the caller's request.
    while ( iBufferCurrIndex < nElements )
        {
        for ( iPage = 0; iPage < stc_MaxPages; iPage++ )
        {
            stc_pNANDDescriptor->pageToBlockAndOffset(iPage, &u32PhysicalBlockNum, &iSectorOffsetWithinBlock);
//            if ( SUCCESS != ddi_nand_media_regions_GetRegionFromPhyBlk( &u32PhysicalBlockNum, &pRegion_Info, false /* bOnlyDataDriveRegions */ ) )
//            {
//                continue;
//            }
            pRegion_Info = g_nandMedia->getRegionForBlock(u32PhysicalBlockNum);

            // Copy all instances of maxValue to pBuffer
            if ( n_ddi_nand_hal_spy_ReadsPerPage[iPage] == maxValue &&
                DDI_NAND_HAL_SPY_IS_SAME_DRIVE_TYPE( bDriveTypeSystem, pRegion_Info->m_eDriveType) )
            {
                if ( iBufferCurrIndex < nElements )
                {
                    pBuffer[iBufferCurrIndex].value = n_ddi_nand_hal_spy_ReadsPerPage[iPage];
                    pBuffer[iBufferCurrIndex].index = iPage;
                    iBufferCurrIndex = iBufferCurrIndex + 1;
                }
                else
                {
                    break;
                }
            }
            else if ( (n_ddi_nand_hal_spy_ReadsPerPage[iPage] > newmaxValue) &&
                      (n_ddi_nand_hal_spy_ReadsPerPage[iPage] < maxValue) ) 
            {
                if ( DDI_NAND_HAL_SPY_IS_SAME_DRIVE_TYPE( bDriveTypeSystem, pRegion_Info->m_eDriveType) )
                {
                    // Record the next max value
                    newmaxValue = n_ddi_nand_hal_spy_ReadsPerPage[iPage];
            }
        }
        }
        // Copy newmaxValue to maxValue and reset newmaxValue 
        maxValue = newmaxValue;
        newmaxValue = -1;
    }

    // Zero any leftover counts in the buffer.
    while ( iBufferCurrIndex < nElements )
    {
        pBuffer[iBufferCurrIndex].value =
        pBuffer[iBufferCurrIndex].index = 0;
        iBufferCurrIndex = iBufferCurrIndex + 1;
    }

    *pu64TotalReads     = uSumOfAllOperations;
    *pu32Pages          = stc_MaxPages;

    return (SUCCESS);
}

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
RtStatus_t ddi_nand_hal_spy_GetMaxErasures(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Blocks, uint64_t *pu64TotalErasures, bool bDriveTypeSystem)
{
    int         iBlock;
    int         iBufferCurrIndex =0;
    int maxValue = -1, newmaxValue = -1;
    uint64_t    uSumOfAllOperations = 0;
    nand::Region  *pRegion_Info;
    uint32_t    u32PhysicalBlockNum;

    // Find the maximum value and its index
    for ( iBlock = 0; iBlock < stc_MaxBlocks; iBlock++ )
    {
        u32PhysicalBlockNum = iBlock;
//        if ( SUCCESS != ddi_nand_media_regions_GetRegionFromPhyBlk( &u32PhysicalBlockNum, &pRegion_Info, false /* bOnlyDataDriveRegions */ ) )
//        {
//            continue;
//        }
        pRegion_Info = g_nandMedia->getRegionForBlock(u32PhysicalBlockNum);

        if ( DDI_NAND_HAL_SPY_IS_SAME_DRIVE_TYPE( bDriveTypeSystem, pRegion_Info->m_eDriveType) )
        {
            // This is the kind of drive that we are looking for.
            // Track the counts.
            if ( (n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock] > maxValue) )
            {
                maxValue = n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock];
    }
            uSumOfAllOperations += n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock];
        }
    }

    if ( -1 == maxValue )
    {
        return (ERROR_GENERIC);
    }

    // Find nElements-1 other maxima.
    // Note that this is a multi-pass process, since there could be
    // several instances of each maximum value, and we only want to
    // report a total of nElements-1 of them, by the caller's request.
    while ( iBufferCurrIndex < nElements )
        {
        for ( iBlock = 0; iBlock < stc_MaxBlocks; iBlock++ )
        {
//            u32PhysicalBlockNum = iBlock;
//            if ( SUCCESS != ddi_nand_media_regions_GetRegionFromPhyBlk( &u32PhysicalBlockNum, &pRegion_Info, false /* bOnlyDataDriveRegions */ ) )
//            {
//                continue;
//            }
            pRegion_Info = g_nandMedia->getRegionForBlock(u32PhysicalBlockNum);

            // Copy all instances of maxValue to pBuffer
            if ( n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock] == maxValue &&
                DDI_NAND_HAL_SPY_IS_SAME_DRIVE_TYPE( bDriveTypeSystem, pRegion_Info->m_eDriveType) )
            {
                if ( iBufferCurrIndex < nElements )
                {
                    pBuffer[iBufferCurrIndex].value = n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock];
                    pBuffer[iBufferCurrIndex].index = iBlock;
                    iBufferCurrIndex = iBufferCurrIndex + 1;
                }
                else
                {
                    break;
                }
            }
            else if ( (n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock] > newmaxValue) &&
                      (n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock] < maxValue) ) 
            {
                if ( DDI_NAND_HAL_SPY_IS_SAME_DRIVE_TYPE( bDriveTypeSystem, pRegion_Info->m_eDriveType) )
                {
                    // Record the next max value
                    newmaxValue = n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock];
            }
        }
        }
        // Copy newmaxValue to maxValue and reset newmaxValue 
        maxValue = newmaxValue;
        newmaxValue = -1;
    }

    // Zero any leftover counts in the buffer.
    while ( iBufferCurrIndex < nElements )
    {
        pBuffer[iBufferCurrIndex].value =
        pBuffer[iBufferCurrIndex].index = 0;
        iBufferCurrIndex = iBufferCurrIndex + 1;
    }

    *pu64TotalErasures  = uSumOfAllOperations;
    *pu32Blocks         = stc_MaxBlocks;

    return (SUCCESS);
}

/**************************************************************************
 * Function:    ddi_nand_hal_spy_isIndexinArr
 *
 * Description: static function used to find if a particular page or block index
 *                  is in the array. This array will be filled using debugger and
 *                  is used to analyse NAND accesses
 *
 * Input:       Page or Block index
 *
 * Output:     bool - TRUE - if index value is in the array, FALSE - otherwise
 *
 * Return:      TRUE - if index value is in the array, FALSE - otherwise
 *
 * See Also:    
 *************************************************************************/
static bool ddi_nand_hal_spy_isIndexinArr(uint32_t iPage_or_iBlock)
{
    uint32_t arrayIndex;
    bool RetVal = FALSE;

    for ( arrayIndex = 0 ; arrayIndex < DDI_NAND_HAL_SPY_NAND_ANALYSIS_INDEX; arrayIndex++ )
    {
        if ( iPage_or_iBlock == i_ddi_nand_hal_spy_NandAnalysis[arrayIndex] )
        {
            RetVal = TRUE;
        }
    }
    return RetVal;
}

/**************************************************************************
 * Function:    ddi_nand_hal_spy_CountBlockErase
 *
 * Description: Increments the erase counter for wBlockNum in the NAND
 *              identified by pNANDDescriptor.
 *              Prints a message if the quantity of erasures reaches
 *              n_ddi_nand_hal_spy_EraseWarningThreshold.
 *
 * Input:       wSectorNum is the block relative to the origin of the NAND
 *              designated by pNANDDescriptor.
 *
 * Output:      
 *
 * Return:      
 *
 * See Also:    
 *************************************************************************/
RtStatus_t ddi_nand_hal_spy_CountBlockErase( NandPhysicalMedia * pNANDDescriptor, uint32_t wBlockNum )
{
    uint32_t    iPage;
    uint32_t    nPages;
    uint32_t    iBlock;

    if ( wBlockNum >= pNANDDescriptor->wTotalBlocks )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_ALL, "HAL SPY block %d beyond max blocks %d.  Chip = %d\r\n",
            wBlockNum,
            pNANDDescriptor->wTotalBlocks,
            pNANDDescriptor->wChipNumber );
        return ( ERROR_DDI_NAND_GROUP_GENERAL );
    }

    // Offset the NAND-based block number into the dimensions of n_ddi_nand_hal_spy_ErasuresPerBlock[].
    iBlock = wBlockNum + stc_BlockOriginPerNand[pNANDDescriptor->wChipNumber];

    if ( iBlock >= DDI_NAND_HAL_SPY_MAX_BLOCKS )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_ALL, "HAL SPY block %d out of log range.  chip=%d \r\n",
            iBlock,
            pNANDDescriptor->wChipNumber );
        return ( ERROR_DDI_NAND_GROUP_GENERAL );
    }

 /* *******************************************************************
        Record the statistic for erasures

        We only record the statistic up to the threshold.
        
        Do not change the "<=" logic.  It is needed in the subsequent
        logging code. The counter will reach the threshold, causing one
        message to be logged, and next time will pass to "+1" above
        threshold and cease incrementing.  No more messages will be
        logged, nor will the counter increase.
    ******************************************************************* */
    if ( n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock] <= n_ddi_nand_hal_spy_EraseWarningThreshold )
    {
        n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock]++;
    }

    if ( stc_bSpyNandAnalysis )
    {
        if ( ddi_nand_hal_spy_isIndexinArr(iBlock) )
        {
#pragma asm
            nop
#pragma endasm
        }
    }

 /* *******************************************************************
        Record the maximum.
    ******************************************************************* */
    if ( n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock] > n_ddi_nand_hal_spy_ErasuresPerBlockMaxQty )
    {
        n_ddi_nand_hal_spy_ErasuresPerBlockMaxQty   = n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock];
        i_ddi_nand_hal_spy_ErasuresPerBlockMax      = iBlock;
    }
    
    if ( n_ddi_nand_hal_spy_ErasuresPerBlock[iBlock] == n_ddi_nand_hal_spy_EraseWarningThreshold )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY: write/erase limit reached for chip=%d block=%d\r\n",
            pNANDDescriptor->wChipNumber,
            wBlockNum );
    }

 /* *******************************************************************
        Clean the statistics for reads from the affected pages.
    ******************************************************************* */
    iPage       = pNANDDescriptor->blockToPage(iBlock);
    nPages      = pNANDDescriptor->blockToPage(iBlock+1) - iPage;

    memset(&(n_ddi_nand_hal_spy_ReadsPerPage[iPage]), 0, (nPages * sizeof(ddi_nand_hal_spy_ReadsPerPage_t)) );

 /* *******************************************************************
        Zero the index of the last page written to this block.
    ******************************************************************* */
    n_ddi_nand_hal_spy_PageWriteIndexPerBlock[iBlock] = 0;

    return ( SUCCESS );
}


/**************************************************************************
 * Function:    ddi_nand_hal_spy_CountPageRead
 *
 * Description: Increments the read counter for wSectorNum (page number) in the NAND
 *              identified by pNANDDescriptor.
 *              Prints a message if the quantity of reads reaches
 *              n_ddi_nand_hal_spy_ReadWarningThreshold.
 *
 * Input:       wSectorNum is the page relative to the origin of the NAND
 *              designated by pNANDDescriptor.
 *
 * Output:      
 *
 * Return:      
 *
 * See Also:    
 *************************************************************************/
RtStatus_t ddi_nand_hal_spy_CountPageRead( NandPhysicalMedia * pNANDDescriptor, 
    uint32_t wSectorNum )
{
    uint32_t    iPage;

    if ( NULL == pNANDDescriptor ) return (ERROR_DDI_NAND_GROUP_GENERAL);

    if ( wSectorNum >= pNANDDescriptor->totalPages )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_ALL, "HAL SPY sector %d beyond max sectors %d.  Chip = %d\r\n",
            wSectorNum,
            pNANDDescriptor->totalPages,
            pNANDDescriptor->wChipNumber );
        return ( ERROR_DDI_NAND_GROUP_GENERAL );
    }

    iPage = stc_PageOriginPerNand[pNANDDescriptor->wChipNumber] + wSectorNum;

    if ( iPage >= DDI_NAND_HAL_SPY_MAX_PAGES )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_ALL, "HAL SPY page %d out of log range.  chip=%d sector=%d\r\n",
            iPage,
            pNANDDescriptor->wChipNumber,
            wSectorNum );
        return ( ERROR_DDI_NAND_GROUP_GENERAL );
    }

 /* *******************************************************************
        Record the statistic for reads
        
        We only record the statistic up to the threshold.
        
        Do not change the "<=" logic.  It is needed in the subsequent
        logging code. The counter will reach the threshold, causing one
        message to be logged, and next time will pass to "+1" above
        threshold and cease incrementing.  No more messages will be
        logged, nor will the counter increase.
    ******************************************************************* */
    if ( n_ddi_nand_hal_spy_ReadsPerPage[iPage] <= n_ddi_nand_hal_spy_ReadWarningThreshold )
    {
        n_ddi_nand_hal_spy_ReadsPerPage[iPage]++;
    }

    if ( stc_bSpyNandAnalysis )
    {
         if ( ddi_nand_hal_spy_isIndexinArr(iPage) )
         {
#pragma asm
            nop
#pragma endasm
         }
    }

    if ( n_ddi_nand_hal_spy_ReadsPerPage[iPage] == n_ddi_nand_hal_spy_ReadWarningThreshold )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY: read limit reached for chip=%d page=%d\r\n",
            pNANDDescriptor->wChipNumber,
            wSectorNum );
    }

    return ( SUCCESS );
}


/**************************************************************************
 * Function:    ddi_nand_hal_spy_CountPageWrite
 *
 * Description: Checks for an out-of-order write in the NAND designated by
 *              pNANDDescriptor.  Prints a message if such a write occurs.
 *
 * Input:       wSectorNum is the page relative to the origin of the NAND
 *              designated by pNANDDescriptor.
 *
 * Output:      
 *
 * Return:      
 *
 * See Also:    
 *************************************************************************/
RtStatus_t ddi_nand_hal_spy_CountPageWrite( NandPhysicalMedia * pNANDDescriptor, uint32_t wSectorNum )
{
    uint32_t    iBlockInChip;
    uint32_t    iBlock;
    uint32_t    iSectorOffsetWithinBlock;

    if ( NULL == pNANDDescriptor ) return (ERROR_DDI_NAND_GROUP_GENERAL);

    pNANDDescriptor->pageToBlockAndOffset(wSectorNum, &iBlockInChip, &iSectorOffsetWithinBlock);
    
    if ( iBlockInChip >= pNANDDescriptor->wTotalBlocks )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_ALL, "HAL SPY block %d beyond max blocks %d of chip = %d\r\n",
            iBlockInChip,
            pNANDDescriptor->wTotalBlocks,
            pNANDDescriptor->wChipNumber );
        return ( ERROR_DDI_NAND_GROUP_GENERAL );
    }

    iBlock = iBlockInChip + stc_BlockOriginPerNand[pNANDDescriptor->wChipNumber];

    if ( iBlock >= DDI_NAND_HAL_SPY_MAX_BLOCKS )
    {
        tss_logtext_Print( LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_ALL, "HAL SPY block %d out of log range.  chip=%d sector=%d\r\n",
            iBlock,
            pNANDDescriptor->wChipNumber,
            wSectorNum );
        return ( ERROR_DDI_NAND_GROUP_GENERAL );
    }

 /* *******************************************************************
        Check if the pages in this block are being written in order.
        MLC NANDs do not tolerate out-of-order writes.
        Note that Type8 NANDs (and possibly other NANDs) allow multiple
        writes to a single physical page as long as they are offset
        to different ranges of columns (bytes).  Therefore, a
        rewrite to the same page is legal for such NANDs, and we
        could have
        (iSectorOffsetWithinBlock == n_ddi_nand_hal_spy_PageWriteIndexPerBlock[ iBlock ]).
    ******************************************************************* */
    if ( iSectorOffsetWithinBlock < n_ddi_nand_hal_spy_PageWriteIndexPerBlock[ iBlock ] )
    {
        // We don't care about 0, because that's the initial value of the statistic.
        if ( 0 != n_ddi_nand_hal_spy_PageWriteIndexPerBlock[ iBlock ] )
        {
            // Out of order
            tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "\r\nHAL SPY: Out-of-order write for chip=%d block=%d sector=%d (in chip), sector-in-block=%d\r\n",
                pNANDDescriptor->wChipNumber,
                iBlockInChip,
                wSectorNum,
                iSectorOffsetWithinBlock );
            tss_logtext_Print( LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_ALL, "HAL SPY: Previous write sector=%d, sector-in-block=%d\r\n",
                pNANDDescriptor->blockToPage(iBlock) + n_ddi_nand_hal_spy_PageWriteIndexPerBlock[ iBlock ],
                n_ddi_nand_hal_spy_PageWriteIndexPerBlock[ iBlock ] );

            // Do not record the current page number.  Keep the bigger, previous one.
        }
    }
    else
    {
        // Record the current page number.
        n_ddi_nand_hal_spy_PageWriteIndexPerBlock[ iBlock ] = iSectorOffsetWithinBlock;
    }
    
    return ( SUCCESS );
}

#if __cplusplus
}
#endif // __cplusplus

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
