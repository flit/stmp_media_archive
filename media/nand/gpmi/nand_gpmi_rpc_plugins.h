////////////////////////////////////////////////////////////////////////////////
//! \addtogroup rpc_agent
//!{
//
// Copyright(C) 2010, Freescale
//
//! \file nand_gpmi_rpc_plugins.h
//! \brief Public header for RPC commands and structures
//!
//! WARNING: Changing or moving values will effect associated apps/scripts.
//!
////////////////////////////////////////////////////////////////////////////////
#ifndef __NAND_GPMI_RPC_PLUGINS_H
#define __NAND_GPMI_RPC_PLUGINS_H

// This file nominally belongs in the component root directory.

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#include "components\telemetry\tss_rpc.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

// Replace this base number with a real base for your component.
//! \brief Base index of RPC commands for this component.
#define NAND_GPMI_CMD_BASE                   0x00220200
//
// Individual command indices.
//
//! \brief Brief_description_of_the_macro
#define NAND_GPMI_CMD_GTIM              (NAND_GPMI_CMD_BASE + 0)
// Add more as needed...

extern const tss_rpc_command_t nand_gpmi_RpcPlugins[];

#endif // __NAND_GPMI_RPC_PLUGINS_H
// End of file
////////////////////////////////////////////////////////////////////////////////
//!}
