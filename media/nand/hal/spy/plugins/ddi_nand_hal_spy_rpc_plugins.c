////////////////////////////////////////////////////////////////////////////////
//! \addtogroup tss_rpc_plugin
//! @{
//
// Copyright(C) 2007 SigmaTel, Inc.
//
//! \file ddi_hal_spy_rpc_plugin.c
//! \brief Lookup table for NAND SPY RPC plugins.
//
////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
#include "components/telemetry/tss_logtext.h"
#include "components/telemetry/tss_rpc.h"          // RPC defines
#include "drivers/media/nand/hal/spy/ddi_nand_hal_spy_rpc_plugins.h"
#include "ddi_nand_hal_spy_rpc_local.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////
const tss_rpc_command_t NandHalSpyRpcPlugins[] =
{
    { NAND_HAL_SPY_CMD_GET_READS,           tss_rpc_nand_GetMaxReads },
    { NAND_HAL_SPY_CMD_GET_ERASURES,        tss_rpc_nand_GetMaxErasures },
    { NAND_HAL_SPY_CMD_GET_ACCESS_TIME,     tss_rpc_nand_GetNandAccessTimes },
    { NAND_HAL_SPY_CMD_CLEAR_ACCESS_TIME,   tss_rpc_nand_ClearNandAccessTimes },
    { NAND_HAL_SPY_CMD_CLEAR_NAND_COUNTS,   tss_rpc_nand_ClearNandCounts },
    { NULL, NULL }
};
static void tss_rpc_nand_GetMaxReads_LowPri(uint32_t memStruct);
static void tss_rpc_nand_GetMaxErasures_LowPri(uint32_t memStruct);

////////////////////////////////////////////////////////////////////////////////
//Code
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//! \brief Dumps Nand Max page read information
//! \param[in] pParam   Not used with current implementation.
///////////////////////////////////////////////////////////////////////////////
uint32_t tss_rpc_nand_GetMaxReads(void *pData, uint32_t *pLength)
{
    os_dpc_Send(OS_DPC_LOWEST_LEVEL_DPC, tss_rpc_nand_GetMaxReads_LowPri, NULL, TX_WAIT_FOREVER);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Dumps Nand Max page erase access information
//! \param[in] pParam   Not used with current implementation.
///////////////////////////////////////////////////////////////////////////////
uint32_t tss_rpc_nand_GetMaxErasures(void *pData, uint32_t *pLength)
{
    os_dpc_Send(OS_DPC_LOWEST_LEVEL_DPC, tss_rpc_nand_GetMaxErasures_LowPri, NULL, TX_WAIT_FOREVER);

    return SUCCESS;
}

static void tss_rpc_nand_GetMaxPrintHelper( int iPrints, ddi_nand_hal_spy_GetMax_t *pRead, uint64_t u64Total )
{
    int i;
    uint32_t    u32lo;

    for ( i = 0; i < iPrints; i++ )
    {
        tss_rpc_Print(TSS_RPC_STDLOG_ID, "%d - Loc=%d Val=%d\n", i, pRead[i].index, pRead[i].value);
        tx_thread_sleep(10);
    }

    u32lo = u64Total;
    if ( (u64Total>>32)!=0 )
    {
        uint32_t u32hi = u64Total>>32;

        // u64 value is too big for tss to print directly.  Print hex values instead.
        tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total operations (u64)= x%x x%x ", u32hi, u32lo);

    }
    else
    {
        // u32 value can be printed by tss directly.
        tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total operations =%d ", u32lo);
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Dumps Nand Max page read information using lowest priority thread
//! \param[in] pParam   Not used with current implementation.
///////////////////////////////////////////////////////////////////////////////
void tss_rpc_nand_GetMaxReads_LowPri(uint32_t memStruct)
{
    ddi_nand_hal_spy_GetMax_t pRead[DDI_NAND_HAL_SPY_GETMAX_NUM_READS];
    uint32_t    u32Pages;
    uint64_t    u64TotalReads;

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");

    if (NULL == ddi_nand_hal_spy_GetMaxReads)
    {
        tss_rpc_Print(TSS_RPC_STDLOG_ID, "        ddi_nand_hal_spy_GetMaxReads() is undefined.  Cannot proceed.\n");
        return;
    }
    
    ///////////////////////////////////////////////////////////////////////////
    // Get counts for data drives.
    ///////////////////////////////////////////////////////////////////////////
    ddi_nand_hal_spy_GetMaxReads(DDI_NAND_HAL_SPY_GETMAX_NUM_READS, pRead, &u32Pages, &u64TotalReads, false /* bDriveTypeSystem */);

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        Data Drive Reads\n");
    tss_rpc_nand_GetMaxPrintHelper( DDI_NAND_HAL_SPY_GETMAX_NUM_READS, pRead, u64TotalReads );
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "on %d pages\n", u32Pages);

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");

    ///////////////////////////////////////////////////////////////////////////
    // Get counts for system drives.
    ///////////////////////////////////////////////////////////////////////////
    ddi_nand_hal_spy_GetMaxReads(DDI_NAND_HAL_SPY_GETMAX_NUM_READS, pRead, &u32Pages, &u64TotalReads, true /* bDriveTypeSystem */);

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        System Drive Reads\n");
    tss_rpc_nand_GetMaxPrintHelper( DDI_NAND_HAL_SPY_GETMAX_NUM_READS, pRead, u64TotalReads );
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "on %d pages\n", u32Pages);

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");

    return;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Dumps Nand Max page erase access information using lowest priority thread
//! \param[in] pParam   Not used with current implementation.
///////////////////////////////////////////////////////////////////////////////
void tss_rpc_nand_GetMaxErasures_LowPri(uint32_t memStruct)
{
    ddi_nand_hal_spy_GetMax_t pErase[DDI_NAND_HAL_SPY_GETMAX_NUM_ERASURES];
    uint32_t    u32Blocks;
    uint64_t    u64TotalErasures;

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");

    if (NULL == ddi_nand_hal_spy_GetMaxErasures)
    {
        tss_rpc_Print(TSS_RPC_STDLOG_ID, "        ddi_nand_hal_spy_GetMaxErasures() is undefined.  Cannot proceed.\n");
        return;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Get counts for system drives.
    ///////////////////////////////////////////////////////////////////////////
    ddi_nand_hal_spy_GetMaxErasures(DDI_NAND_HAL_SPY_GETMAX_NUM_ERASURES, pErase, &u32Blocks, &u64TotalErasures, false /* bDriveTypeSystem */);

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        Data Drive Erasures\n");
    tss_rpc_nand_GetMaxPrintHelper( DDI_NAND_HAL_SPY_GETMAX_NUM_ERASURES, pErase, u64TotalErasures );
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "on %d blocks\n", u32Blocks);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");

    ///////////////////////////////////////////////////////////////////////////
    // Get counts for system drives.
    ///////////////////////////////////////////////////////////////////////////
    ddi_nand_hal_spy_GetMaxErasures(DDI_NAND_HAL_SPY_GETMAX_NUM_ERASURES, pErase, &u32Blocks, &u64TotalErasures, true /* bDriveTypeSystem */);

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        System Drive Erasures\n");
    tss_rpc_nand_GetMaxPrintHelper( DDI_NAND_HAL_SPY_GETMAX_NUM_ERASURES, pErase, u64TotalErasures );
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "on %d blocks\n", u32Blocks);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");

    return;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Dumps Nand Page read, write and block erase timings
//! \param[in] pParam   Not used with current implementation.
///////////////////////////////////////////////////////////////////////////////
uint32_t tss_rpc_nand_GetNandAccessTimes(void *pData, uint32_t *pLength)
{
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        Read Access Time \n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total time taken for all reads is %d \n", ddi_nand_hal_spy_readTime.u32_sumofIter);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total number of reads is          %d \n", ddi_nand_hal_spy_readTime.u32_numofIter);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "***********************************************\n");

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        Write Access Time \n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total time taken for all writes is %d \n", ddi_nand_hal_spy_writeTime.u32_sumofIter);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total number of writes is          %d \n", ddi_nand_hal_spy_writeTime.u32_numofIter);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "***********************************************\n");

    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        Block Erase Time \n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "        --------------------\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total time taken for block erases is %d \n", ddi_nand_hal_spy_eraseTime.u32_sumofIter);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Total number of erasures is          %d \n", ddi_nand_hal_spy_eraseTime.u32_numofIter);
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "***********************************************\n");

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Clears Nand Page read, write and block erase timings
//! \param[in] pParam   Not used with current implementation.
///////////////////////////////////////////////////////////////////////////////
uint32_t tss_rpc_nand_ClearNandAccessTimes(void *pData, uint32_t *pLength)
{
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "\n");
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Clearing Read Access Time........ \n");
    ddi_nand_hal_spy_readTime.u32_numofIter = 0;
    ddi_nand_hal_spy_readTime.u32_sumofIter = 0;
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Clearing Write Access Time........ \n");
    ddi_nand_hal_spy_writeTime.u32_numofIter = 0;
    ddi_nand_hal_spy_writeTime.u32_sumofIter = 0;
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Clearing Erase Access Time........ \n");
    ddi_nand_hal_spy_eraseTime.u32_numofIter = 0;
    ddi_nand_hal_spy_eraseTime.u32_sumofIter = 0;
    tss_rpc_Print(TSS_RPC_STDLOG_ID, "Done \n");

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Clears Nand Page read and block erase counts
//! \param[in] pParam   Not used with current implementation.
///////////////////////////////////////////////////////////////////////////////
uint32_t tss_rpc_nand_ClearNandCounts(void *pData, uint32_t *pLength)
{
    return ( ddi_nand_hal_spy_Reset() );
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
