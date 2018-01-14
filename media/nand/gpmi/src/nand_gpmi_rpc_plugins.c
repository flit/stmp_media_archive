////////////////////////////////////////////////////////////////////////////////
//! \addtogroup tss_rpc_plugin
//!{
//
// Copyright(C) 2010 Freescale
//
//! \file nand_gpmi_rpc_plugins.c
//! \brief Lookup table for RPC plugins.
//
////////////////////////////////////////////////////////////////////////////////

// This file nominally belongs in the component/plugin subdirectory.

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
#include "components/telemetry/tss_logtext.h"
#include "components/telemetry/tss_rpc.h"          // RPC defines
#include "ddi_nand_gpmi.h"
#include "nand_gpmi_rpc_plugins.h"
#include "nand_gpmi_rpc_local.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

static void nand_gpmi_cmd_gtim_LowPri(uint32_t memStruct);

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////
const tss_rpc_command_t nand_gpmi_RpcPlugins[] =
{
    { NAND_GPMI_CMD_GTIM,        nand_gpmi_cmd_gtim },
    { NULL, NULL }
};

////////////////////////////////////////////////////////////////////////////////
//Code
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//! \brief Apply the given GPMI timing values to the GPMI timing function.
//! \param[in] pData    Ptr to any additional data in payload.  Cast it as needed.
//! \param[in] pLength  Ptr to length of additional data in payload, in bytes.
///////////////////////////////////////////////////////////////////////////////
static NAND_Timing2_struct_t       stc_NT2;
uint32_t nand_gpmi_cmd_gtim(void *pData, uint32_t *pLength)
{
    nand_gpmi_rpc_gtim_parms_t  *pParms = (nand_gpmi_rpc_gtim_parms_t *)pData;
    int                         nParm   = (*pLength)>>2;

    // No response to RPC.
    *pLength = 0;

    switch (nParm)
    {
        case 7:
            stc_NT2.u8RHOH              = pParms->u32RHOH        ;
            // fall through
        case 6:
            stc_NT2.u8RLOH              = pParms->u32RLOH        ;
            // fall through
        case 5:
            stc_NT2.u8REA               = pParms->u32REA         ;
            // fall through
        case 4:
            stc_NT2.u8DataHold          = pParms->u32DataHold    ;
            // fall through
        case 3:
            stc_NT2.u8DataSetup         = pParms->u32DataSetup   ;
            // fall through
        case 2:
            stc_NT2.u8DSAMPLE_TIME      = pParms->u32DSAMPLE_TIME;
            // fall through
        case 1:
            stc_NT2.u8AddressSetup      = pParms->u32AddressSetup;

            stc_NT2.eState              = e_NAND_Timing_State_DYNAMIC_DSAMPLE_TIME;

            // The command takes too long to execute.  RPC would time-out while
            // waiting for the command to finish.  Therefore, we give the
            // remaining processes to the deferred-procedure task.
            os_dpc_Send(OS_DPC_LOWEST_LEVEL_DPC, nand_gpmi_cmd_gtim_LowPri, NULL, TX_WAIT_FOREVER);

        default:
            break;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief DPC callback to run the ddi_nand_hal_GpmiSetNandTiming() function.
//!
//! \param[in] memStruct     not used.
///////////////////////////////////////////////////////////////////////////////
static void nand_gpmi_cmd_gtim_LowPri(uint32_t memStruct)
{
    ddi_nand_hal_GpmiSetNandTiming(&stc_NT2, 0/*u32GpmiPeriod_ns*/,
            g_u32GPMIPropDelayMin_ns, g_u32GPMIPropDelayMax_ns, false
            /*bWriteToTheDevice*/);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//!}
