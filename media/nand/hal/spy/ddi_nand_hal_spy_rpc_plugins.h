////////////////////////////////////////////////////////////////////////////////
//! \addtogroup rpc_agent
//! @{
//
// Copyright(C) 2007 SigmaTel, Inc.
//
//! \file ddi_nand_hal_spy_rpc_plugins.h
//! \brief Public header for the NAND HAL SPY RPC commands and structures
//!
//! WARNING: Changing or moving values will effect associated apps/scripts.
//!
////////////////////////////////////////////////////////////////////////////////
#ifndef __DDI_NAND_HAL_SPY_RPC_PLUGIN_H
#define __DDI_NAND_HAL_SPY_RPC_PLUGIN_H

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#include "drivers\ddi_subgroups.h"
#include "components\telemetry\tss_rpc.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#define DDI_NAND_HAL_SPY_GROUP              (DDI_NAND_GROUP|0x00000000)
#define NAND_HAL_SPY_CMD_GET_READS          (DDI_NAND_HAL_SPY_GROUP + 0)
#define NAND_HAL_SPY_CMD_GET_ERASURES       (DDI_NAND_HAL_SPY_GROUP + 1)
#define NAND_HAL_SPY_CMD_GET_ACCESS_TIME    (DDI_NAND_HAL_SPY_GROUP + 2)
#define NAND_HAL_SPY_CMD_CLEAR_ACCESS_TIME  (DDI_NAND_HAL_SPY_GROUP + 3)
#define NAND_HAL_SPY_CMD_CLEAR_NAND_COUNTS  (DDI_NAND_HAL_SPY_GROUP + 4)

// \brief NAND HAL SPY RPC plugins (see main plugin)
#pragma weak NandHalSpyRpcPlugins
extern const tss_rpc_command_t NandHalSpyRpcPlugins[];

#endif // __DDI_NAND_HAL_SPY_RPC_PLUGIN_H
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

