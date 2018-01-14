////////////////////////////////////////////////////////////////////////////////
//! \addtogroup tss_rpc_plugin
//!{
//
// Copyright(C) 2010 Freescale
//
//! \file nand_gpmi_rpc_local.h
//! \brief Private header for the nand_gpmi RPC plugins.
//!
//!  Plugins are added to a table during rpc_Init().
//!
////////////////////////////////////////////////////////////////////////////////
#ifndef __NAND_GPMI_RPC_LOCAL_H
#define __NAND_GPMI_RPC_LOCAL_H

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
#include "components/telemetry/tss_logtext.h"
#include "components/telemetry/tss_rpc.h"          // RPC defines

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! \brief Order and structure of the timing parameters passed to the gtim RPC function.
//!
//! This structure mimic that of NAND_Timing2_struct_t, but with u32's.
typedef struct nand_gpmi_rpc_gtim_parms_t
{
    uint32_t            u32DataSetup;        //!< The data setup time (tDS), in nanoseconds.

    uint32_t            u32DataHold;         //!< The data hold time (tDH), in nanoseconds.

    uint32_t            u32AddressSetup;     //!< The address setup time (tSU), in nanoseconds.
                                            //! This value amalgamates the NAND parameters tCLS, tCS, and tALS.

    uint32_t            u32DSAMPLE_TIME;     //!< The data sample time, in nanoseconds.

    uint32_t            u32REA;              //!< From the NAND datasheet.

    uint32_t            u32RLOH;             //!< From the NAND datasheet.
                                            //! This is the amount of time that the last
                                            //! contents of the data lines will persist
                                            //! after the controller drives the -RE
                                            //! signal true.
                                            //! EDO Mode: This time is from the NAND spec, and the persistence of data
                                            //! is determined by (tRLOH + tDH).
                                            //! Non-EDO Mode: This time is ignored, because the persistence of data
                                            //! is determined by tRHOH.

    uint32_t            u32RHOH;             //!< From the NAND datasheet.
                                            //! This is the amount of time
                                            //! that the last contents of the data lines will persist after the
                                            //! controller drives the -RE signal false.
                                            //! EDO Mode: This time is ignored, because the persistence of data
                                            //! is determined by (tRLOH + tDH).
                                            //! Non-EDO Mode: This time is totally due to capacitive effects of the
                                            //! hardware.  For reliable behavior it should be set to zero, unless there is
                                            //! specific knowledge of the trace capacitance and the persistence of the
                                            //! data values.

} nand_gpmi_rpc_gtim_parms_t;

extern uint32_t     g_u32GPMIPropDelayMin_ns;
extern uint32_t     g_u32GPMIPropDelayMax_ns;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

#if __cplusplus
extern "C" {
#endif // __cplusplus

extern void ddi_nand_hal_GpmiSetNandTiming(const NAND_Timing2_struct_t * pNAND_Timing2_struct, 
                                    uint32_t u32GpmiPeriod_ns, 
                                    uint32_t u32PropDelayMin_ns, 
                                    uint32_t u32PropDelayMax_ns,
                                    bool bWriteToTheDevice);
extern uint32_t nand_gpmi_cmd_gtim(void *pData, uint32_t *pLength);

#if __cplusplus
}
#endif // __cplusplus

#endif // __NAND_GPMI_RPC_LOCAL_H
// End of file
////////////////////////////////////////////////////////////////////////////////
//!}
