//! \addtogroup tss_rpc_plugin
//! @{
//
// Copyright(C) 2007 SigmaTel, Inc.
//
//! \file ddi_nand_hal_spy_local.h
//! \brief Contains private header for the RPC plugins.
//!
//!  Plugins are added to a table during rpc_Init().
//!
////////////////////////////////////////////////////////////////////////////////
#ifndef __DDI_NAND_HAL_SPY_RPC_PLUGIN_LOCAL_H
#define __DDI_NAND_HAL_SPY_RPC_PLUGIN_LOCAL_H

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
#include "components/telemetry/tss_logtext.h"
#include "components/telemetry/tss_rpc.h"          // RPC defines
#include "drivers/media/nand/hal/spy/ddi_nand_hal_spy_rpc_plugins.h"

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////
typedef struct {
	uint32_t index;
	uint16_t value;
} ddi_nand_hal_spy_GetMax_t;

typedef struct {
    uint64_t u32_sumofIter;
    uint32_t u32_numofIter;
} ddi_nand_hal_spy_TimeAnalysis_t;

#define DDI_NAND_HAL_SPY_GETMAX_NUM_READS           20
#define DDI_NAND_HAL_SPY_GETMAX_NUM_ERASURES        20

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////
#pragma weak ddi_nand_hal_spy_readTime
#pragma weak ddi_nand_hal_spy_writeTime
#pragma weak ddi_nand_hal_spy_eraseTime
extern ddi_nand_hal_spy_TimeAnalysis_t ddi_nand_hal_spy_readTime;
extern ddi_nand_hal_spy_TimeAnalysis_t ddi_nand_hal_spy_writeTime;
extern ddi_nand_hal_spy_TimeAnalysis_t ddi_nand_hal_spy_eraseTime;

#if __cplusplus
extern "C" {
#endif // __cplusplus

#pragma weak ddi_nand_hal_spy_GetMaxReads
#pragma weak ddi_nand_hal_spy_GetMaxErasures
extern RtStatus_t ddi_nand_hal_spy_GetMaxReads(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Pages, uint64_t *pu64TotalReads, bool bDriveTypeSystem);
extern RtStatus_t ddi_nand_hal_spy_GetMaxErasures(uint32_t nElements, ddi_nand_hal_spy_GetMax_t *pBuffer, uint32_t *pu32Blocks, uint64_t *pu64TotalErasures, bool bDriveTypeSystem);
extern RtStatus_t ddi_nand_hal_spy_Reset(void);
uint32_t tss_rpc_nand_GetMaxReads(void *pData, uint32_t *pLength);
uint32_t tss_rpc_nand_GetMaxErasures(void *pData, uint32_t *pLength);
uint32_t tss_rpc_nand_GetNandAccessTimes(void *pData, uint32_t *pLength);
uint32_t tss_rpc_nand_ClearNandAccessTimes(void *pData, uint32_t *pLength);
uint32_t tss_rpc_nand_ClearNandCounts(void *pData, uint32_t *pLength);

#if __cplusplus
}
#endif // __cplusplus

#endif // __DDI_NAND_HAL_SPY_RPC_PLUGIN_LOCAL_H
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
