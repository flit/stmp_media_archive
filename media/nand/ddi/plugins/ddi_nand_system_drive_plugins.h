////////////////////////////////////////////////////////////////////////////////
//! \addtogroup rpc_agent
//! @{
//
// Copyright(C) 2007 SigmaTel, Inc.
//
//! \file ddi_nand_system_drive_plugins.h
//! \brief Public header for the NAND HAL SPY RPC commands and structures
//!
//! WARNING: Changing or moving values will effect associated apps/scripts.
//!
////////////////////////////////////////////////////////////////////////////////
#ifndef __DDI_NAND_SYSTEM_DRIVE_RPC_PLUGIN_H
#define __DDI_NAND_SYSTEM_DRIVE_RPC_PLUGIN_H

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#include "drivers\ddi_subgroups.h"
#include "components\telemetry\tss_rpc.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#define DDI_NAND_SYSTEM_DRIVE_GROUP                  (DDI_NAND_GROUP|0x00001000)

#define NAND_SYSTEM_DRIVE_CMD_STAT_ENABLE            (DDI_NAND_SYSTEM_DRIVE_GROUP + 0)
#define NAND_SYSTEM_DRIVE_CMD_STAT_DISABLE           (DDI_NAND_SYSTEM_DRIVE_GROUP + 1)
#define NAND_SYSTEM_DRIVE_CMD_PRINT_NUM_DISTURBANCES (DDI_NAND_SYSTEM_DRIVE_GROUP + 2)
#define NAND_SYSTEM_DRIVE_CMD_KICK_RECOVERY          (DDI_NAND_SYSTEM_DRIVE_GROUP + 3)

extern const tss_rpc_command_t NandSystemDriveRpcPlugins[];

#endif // __DDI_NAND_SYSTEM_DRIVE_RPC_PLUGIN_H

// End of file

////////////////////////////////////////////////////////////////////////////////
//! @}
