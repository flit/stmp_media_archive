////////////////////////////////////////////////////////////////////////////////
//! \addtogroup tss_rpc_plugin
//! @{
//
//  Copyright (c) Freescale
//
//! \file      ddi_nand_data_drive_rpc_plugins.c
//! \brief     brief_description_here
//!
//! Details_here
//!
//! \see       cross_references_if_any
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
#include <error.h>
#include "components/telemetry/tss_rpc.h"          // RPC defines
#include "ddi_nand_data_drive_rpc_plugins.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_media.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

const tss_rpc_command_t ddi_nand_data_drive_rpc_plugins[] =
{
    { DDI_NAND_DATA_DRIVE_RPC_CMD_GET_STATS         , ddi_nand_data_drive_rpc_cmd_get_stats        },
    { DDI_NAND_DATA_DRIVE_RPC_CMD_SET_NSSM_COUNT    , ddi_nand_data_drive_rpc_cmd_set_nssm_count        },
    { DDI_NAND_DATA_DRIVE_RPC_CMD_CLEAR_NSSM_BUILDS , ddi_nand_data_drive_rpc_cmd_clear_nssm_builds     },
    { DDI_NAND_DATA_DRIVE_RPC_CMD_CLEAR_MERGEBLOCKS , ddi_nand_data_drive_rpc_cmd_clear_mergeblocks     },
    { NULL, NULL }

};


////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief Dumps the number of times an NSSM entry has been built.
//! \param[in] pData    Ptr to parameters from the command line.
//! \param[in] pLength  Ptr to length of contents at *pData, in units of u8's.
///////////////////////////////////////////////////////////////////////////////
uint32_t ddi_nand_data_drive_rpc_cmd_get_stats( void * pData, uint32_t* pLength)
{
    ddi_nand_data_drive_rpc_dt_stats_t  rStruct;
    
    *pLength = 0;                   // force response to Status-Code only.

    // Now back to work.

    rStruct.type                        = DDI_NAND_DATA_DRIVE_RPC_DT_STATS;
    rStruct.u32nssmCount                = 0; //NandMediaInfo.nssmCount;
    rStruct.u32nssmBuilds               = 0; //NandMediaInfo.u32nssmBuilds;
    rStruct.u32MergeBlocksShortCircuit  = 0; //NandMediaInfo.u32MergeBlocksShortCircuit;
    rStruct.u32MergeBlocksQuick         = 0; //NandMediaInfo.u32MergeBlocksQuick;
    rStruct.u32MergeBlocksCore          = 0; //NandMediaInfo.u32MergeBlocksCore;

    tss_rpc_Bulk( TSS_RPC_BULKSTREAM_ID, TSS_RPC_RAW_BULK_RESPONSE, &rStruct, sizeof(ddi_nand_data_drive_rpc_dt_stats_t));

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Clears the number of times an NSSM entry has been built.
//! \param[in] pData    Ptr to parameters from the command line.
//! \param[in] pLength  Ptr to length of contents at *pData, in units of u8's.
///////////////////////////////////////////////////////////////////////////////
uint32_t ddi_nand_data_drive_rpc_cmd_set_nssm_count(void *pData, uint32_t *pLength)
{

    RtStatus_t  eRetCode;
    uint32_t    nssmCount;

    //
    // The following continguous code is copied from the tuner component,
    // which also uses RPC bulk.
    //    
    if( (*pLength != sizeof(uint32_t)) || (pData == NULL) ) {
        // No parameter.  Bail.
        return SUCCESS;
    }
    *pLength = 0;                   // force response to Status-Code only.

    nssmCount = *(uint32_t*)pData;
    // Don't set the count to zero, ever.
    if ( 0 == nssmCount ) return SUCCESS;
    
    eRetCode = DriveSetInfo(DATA_DRIVE_ID_INTERNAL, kDriveInfoNSSMCount, &nssmCount);

    return eRetCode;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Clears the number of times an NSSM entry has been built.
//! \param[in] pData    Ptr to parameters from the command line.
//! \param[in] pLength  Ptr to length of contents at *pData, in units of u8's.
///////////////////////////////////////////////////////////////////////////////
uint32_t ddi_nand_data_drive_rpc_cmd_clear_nssm_builds(void *pData, uint32_t *pLength)
{
    *pLength = 0;                   // force response to Status-Code only.

//    NandMediaInfo.u32nssmBuilds = 0;

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Clears the number of times any paired blocks have been merged.
//! \param[in] pData    Ptr to parameters from the command line.
//! \param[in] pLength  Ptr to length of contents at *pData, in units of u8's.
///////////////////////////////////////////////////////////////////////////////
uint32_t ddi_nand_data_drive_rpc_cmd_clear_mergeblocks(void *pData, uint32_t *pLength)
{
    *pLength = 0;                   // force response to Status-Code only.

//    NandMediaInfo.u32MergeBlocksShortCircuit  =
//    NandMediaInfo.u32MergeBlocksQuick          =
//    NandMediaInfo.u32MergeBlocksCore            = 0;

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
