////////////////////////////////////////////////////////////////////////////////
//! \addtogroup tss_rpc_plugin
//! @{
//
// Copyright(C) 2007 SigmaTel, Inc.
//
//! \file ddi_nand_system_drive_rpc_plugins.c
//! \brief Lookup table for NAND System Drive RPC plugins and associated functions
//
////////////////////////////////////////////////////////////////////////////////
//   Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
#include "components/telemetry/tss_logtext.h"
#include "components/telemetry/tss_rpc.h"          // RPC defines
#include "ddi_nand_system_drive_plugins.h"
#include "ddi_nand_system_drive_recover.h"
#include "ddi_nand.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

uint32_t tss_rpc_nand_system_drive_read_disturbance_stat_enable
(
    void     *pData,
    uint32_t *pLength
);

uint32_t tss_rpc_nand_system_drive_read_disturbance_stat_disable
(
    void     *pData,
    uint32_t *pLength
);

uint32_t tss_rpc_nand_system_drive_read_disturbance_print_stats
(
    void     *pData,
    uint32_t *pLength
);

uint32_t tss_rpc_nand_system_drive_read_disturbance_kick
(
    void     *pData,
    uint32_t *pLength
);

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! Look up table for RPC commands related to Nand System Drive
const tss_rpc_command_t NandSystemDriveRpcPlugins[] =
{
    { NAND_SYSTEM_DRIVE_CMD_STAT_ENABLE,            tss_rpc_nand_system_drive_read_disturbance_stat_enable},
    { NAND_SYSTEM_DRIVE_CMD_STAT_DISABLE,           tss_rpc_nand_system_drive_read_disturbance_stat_disable},
    { NAND_SYSTEM_DRIVE_CMD_PRINT_NUM_DISTURBANCES, tss_rpc_nand_system_drive_read_disturbance_print_stats},
    { NAND_SYSTEM_DRIVE_CMD_KICK_RECOVERY, tss_rpc_nand_system_drive_read_disturbance_kick},
    { NULL, NULL }
};

////////////////////////////////////////////////////////////////////////////////
//Code
////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
//! \brief Enable print statements during Nand System Drive Read Disturbance recovery
/////////////////////////////////////////////////////////////////////////////////////

uint32_t tss_rpc_nand_system_drive_read_disturbance_stat_enable
(
    void     *pData,
    uint32_t *pLength
)
{
//    g_recoveryContext.printStatistics = true;
    return SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////////
//! \brief Disable print statements during nand System Drive Read Disturbance recovery
//////////////////////////////////////////////////////////////////////////////////////

uint32_t tss_rpc_nand_system_drive_read_disturbance_stat_disable
(
    void     *pData,
    uint32_t *pLength
)
{
//    g_recoveryContext.printStatistics = false;
    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////
//! \brief Print statistics relating to Nand System Drive Read Disturbance recovery
///////////////////////////////////////////////////////////////////////////////////

uint32_t tss_rpc_nand_system_drive_read_disturbance_print_stats
(
    void     *pData,
    uint32_t *pLength
)
{
    g_nandMedia->getRecoveryManager()->printStatistics();

    return SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////
//! \brief Initiate system drive read disturbance recovery on primary drive.
///////////////////////////////////////////////////////////////////////////////////
uint32_t tss_rpc_nand_system_drive_read_disturbance_kick(void * pData, uint32_t * pLength)
{
//    RtStatus_t result;

    nand::SystemDriveRecoveryManager * manager = g_nandMedia->getRecoveryManager();
    manager->startRecovery(manager->getPrimaryDrive());
    
//     tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Initiating read disturbance recovery on primary drive\r\n");
//     
//     result = ddi_nand_refresh_firmware(kNandRefreshAsync, rpc_kick_recovery_callback, 0);
//     
//     if (result == SUCCESS)
//     {
//         tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "--> System drive read disturbance recovery started successfully\r\n");
//     }
//     else
//     {
//         tss_logtext_Print(LOGTEXT_VERBOSITY_2 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Failed to initiate recovery (error=%x)\r\n", result);
//     }

    return SUCCESS;
}

