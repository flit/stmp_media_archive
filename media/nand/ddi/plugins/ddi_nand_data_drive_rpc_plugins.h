////////////////////////////////////////////////////////////////////////////////
//! \addtogroup rpc_agent
//! @{
//
//  Copyright (c) Freescale
//
//! \file ddi_nand_data_drive_rpc_plugins.h
//! \brief Defines values, variables, and functions used by RPC plug-in for the data drive component.
//!
//! Details_here
//!
//! \see cross_references_if_any
////////////////////////////////////////////////////////////////////////////////

#ifndef __DDI_NAND_DATA_DRIVE_RPC_PLUGINS_H__
#define __DDI_NAND_DATA_DRIVE_RPC_PLUGINS_H__

////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////

#include "drivers\ddi_subgroups.h"
#include "components\telemetry\tss_rpc.h"

#ifdef __cplusplus
extern "C"   {
#endif
////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \brief Command indices used by RPC.
//!
//! These command indices are also listed in an rpcnet.ini file for the datadrive.
#define DDI_NAND_DATA_DRIVE_RPC_CMD_BASE                    (DDI_NAND_GROUP|0x00000100)
#define DDI_NAND_DATA_DRIVE_RPC_CMD_GET_STATS               (DDI_NAND_DATA_DRIVE_RPC_CMD_BASE+0)
#define DDI_NAND_DATA_DRIVE_RPC_CMD_SET_NSSM_COUNT          (DDI_NAND_DATA_DRIVE_RPC_CMD_BASE+1)
#define DDI_NAND_DATA_DRIVE_RPC_CMD_CLEAR_NSSM_BUILDS       (DDI_NAND_DATA_DRIVE_RPC_CMD_BASE+2)
#define DDI_NAND_DATA_DRIVE_RPC_CMD_CLEAR_MERGEBLOCKS       (DDI_NAND_DATA_DRIVE_RPC_CMD_BASE+3)

//! \brief Brief_description_of_the_enum
//!
//! Optional_longer_description
typedef enum ddi_nand_data_drive_rpc_dt_stats_k
{
    DDI_NAND_DATA_DRIVE_RPC_DT_STATS = DDI_NAND_DATA_DRIVE_RPC_CMD_GET_STATS

} ddi_nand_data_drive_rpc_dt_stats_k;

//! \brief Struct returned to rpc bulk port for DDI_NAND_DATA_DRIVE_RPC_CMD_GET_STATS query.
typedef struct ddi_nand_data_drive_rpc_dt_stats_t
{
    //! \brief All structures to be sent over a bulk TSS port must start with the "type" index.
    ddi_nand_data_drive_rpc_dt_stats_k  type;

    //! \brief Quantity NSSM entries
    uint32_t                            u32nssmCount;

    //! \brief Number of NSSM entries built
    uint32_t                            u32nssmBuilds;

    //! \brief Number of times two blocks were merged.
    uint32_t                            u32MergeBlocksShortCircuit;
    //! \brief Number of times two blocks were merged.
    uint32_t                            u32MergeBlocksQuick;
    //! \brief Number of times two blocks were merged.
    uint32_t                            u32MergeBlocksCore;
    
} ddi_nand_data_drive_rpc_dt_stats_t;

extern const tss_rpc_command_t ddi_nand_data_drive_rpc_plugins[];

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

extern uint32_t ddi_nand_data_drive_rpc_cmd_get_stats( void * pData, uint32_t* pLength);
extern uint32_t ddi_nand_data_drive_rpc_cmd_set_nssm_count(void *pData, uint32_t *pLength);
extern uint32_t ddi_nand_data_drive_rpc_cmd_clear_nssm_builds(void *pData, uint32_t *pLength);
extern uint32_t ddi_nand_data_drive_rpc_cmd_clear_mergeblocks(void *pData, uint32_t *pLength);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __DDI_NAND_DATA_DRIVE_RPC_PLUGINS_H__ */
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
